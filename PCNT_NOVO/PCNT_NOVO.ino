#include <Arduino.h>
#include "driver/pulse_cnt.h"

#define ENCODER_A 18
#define ENCODER_B 19

// Configuração do seu encoder
const float COUNTS_PER_REV = 1200.0;

// Variáveis para o acumulo de contagens (Evita estouro de limite)
pcnt_unit_handle_t pcnt_unit = NULL;
int16_t raw_count = 0;
int64_t acc_count = 0;
int64_t last_acc_count = 999999;

// Função moderna para configurar o hardware do ESP32 com Filtro de Ruído
pcnt_unit_handle_t configurarEncoder(int pinA, int pinB) {
    pcnt_unit_config_t unit_config = {
        .low_limit = -30000,
        .high_limit = 30000,
    };
    pcnt_unit_handle_t unit = NULL;
    pcnt_new_unit(&unit_config, &unit);

    // Filtro de hardware contra ruídos elétricos do motor
    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 1000 };
    pcnt_unit_set_glitch_filter(unit, &filter_config);

    // Configura Canal A
    pcnt_chan_config_t chan_a_config = { .edge_gpio_num = pinA, .level_gpio_num = pinB };
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_new_channel(unit, &chan_a_config, &chan_a);

    // Configura Canal B
    pcnt_chan_config_t chan_b_config = { .edge_gpio_num = pinB, .level_gpio_num = pinA };
    pcnt_channel_handle_t chan_b = NULL;
    pcnt_new_channel(unit, &chan_b_config, &chan_b);

    // Mapeamento de quadratura pura (Conta subidas e descidas)
    pcnt_channel_set_edge_action(chan_a, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_a, PCNT_CHANNEL_LEVEL_ACTION_KEEP, PCNT_CHANNEL_LEVEL_ACTION_INVERSE);
    pcnt_channel_set_edge_action(chan_b, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_DECREASE);
    pcnt_channel_set_level_action(chan_b, PCNT_CHANNEL_LEVEL_ACTION_INVERSE, PCNT_CHANNEL_LEVEL_ACTION_KEEP);

    pcnt_unit_enable(unit);
    pcnt_unit_clear_count(unit);
    pcnt_unit_start(unit);

    return unit;
}

void lerEncoderHardware() {
    int val = 0;
    pcnt_unit_get_count(pcnt_unit, &val);
    
    // Acumula de forma segura em 64 bits
    acc_count += (int16_t)(val - raw_count); 
    raw_count = (int16_t)val;
}

void setup() {
    Serial.begin(115200);
    
    // Inicializa o encoder usando a API atualizada
    pcnt_unit = configurarEncoder(ENCODER_A, ENCODER_B);
    
    Serial.println("Encoder Inicializado com Filtro de Ruido (API Nova).");
}

void loop() {
    // Atualiza as variáveis de contagem do hardware
    lerEncoderHardware();

    // Se houve movimento, calcula e imprime
    if (acc_count != last_acc_count) {
        
        // 1. Cálculo em Graus
        float graus = (360.0f * (float)acc_count) / COUNTS_PER_REV;

        // 2. Cálculo em Radianos (2 * PI * contagem / resolução)
        float radianos = (2.0f * PI * (float)acc_count) / COUNTS_PER_REV;

        Serial.print("Contagem Total: ");
        Serial.print(acc_count);
        
        Serial.print(" | Graus: ");
        Serial.print(graus, 2);
        Serial.print("°");

        Serial.print(" | Radianos: ");
        Serial.print(radianos, 4); // Exibe com 4 casas decimais para maior precisão
        Serial.println(" rad");

        last_acc_count = acc_count;
    }
    
    delay(5); 
}