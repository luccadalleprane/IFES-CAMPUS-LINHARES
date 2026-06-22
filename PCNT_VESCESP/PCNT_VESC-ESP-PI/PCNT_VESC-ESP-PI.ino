/*
   Name:        Aeropendulo_Controle_Encoder.ino
   Description: Integração do controle do VESC via UART + Encoder PCNT.
                Permite alterar Kp, Ki, Setpoint e acionar PARADA DE EMERGÊNCIA (E).
*/

#include <Arduino.h>
#include <VescUart.h>
#include "driver/pulse_cnt.h"

// ==========================================
// CONFIGURAÇÕES DO VESC (UART)
// ==========================================
HardwareSerial VescSerial(2); 
VescUart UART;

#define RX_PIN 16
#define TX_PIN 17

float current = 0.0; 

// ==========================================
// CONFIGURAÇÕES DO ENCODER (PCNT)
// ==========================================
#define ENCODER_A 18
#define ENCODER_B 19

const float COUNTS_PER_REV = 1200.0;

pcnt_unit_handle_t pcnt_unit = NULL;
int16_t raw_count = 0;
int64_t acc_count = 0;
int64_t last_acc_count = 999999;

float graus = 0.0f;
float radianos = 0.0f;

// ==========================================
// VARIÁVEIS DO CONTROLADOR (DINÂMICAS)
// ==========================================
float kp = 1.5f;
float ki = 0.5f;             
float setpoint = 0.7880f;    

float integral_erro = 0.0f;  
unsigned long last_time = 0; 

const float SAT_MAX = 5.0f;  
const float SAT_MIN = -5.0f; 

// Flag de Segurança (Bloqueio do Motor)
bool parada_emergencia = false;

// ==========================================
// FUNÇÃO DE CONFIGURAÇÃO DO ENCODER
// ==========================================
pcnt_unit_handle_t configurarEncoder(int pinA, int pinB) {
    pcnt_unit_config_t unit_config = {
        .low_limit = -30000,
        .high_limit = 30000,
    };
    pcnt_unit_handle_t unit = NULL;
    pcnt_new_unit(&unit_config, &unit);

    pcnt_glitch_filter_config_t filter_config = { .max_glitch_ns = 1000 };
    pcnt_unit_set_glitch_filter(unit, &filter_config);

    pcnt_chan_config_t chan_a_config = { .edge_gpio_num = pinA, .level_gpio_num = pinB };
    pcnt_channel_handle_t chan_a = NULL;
    pcnt_new_channel(unit, &chan_a_config, &chan_a);

    pcnt_chan_config_t chan_b_config = { .edge_gpio_num = pinB, .level_gpio_num = pinA };
    pcnt_channel_handle_t chan_b = NULL;
    pcnt_new_channel(unit, &chan_b_config, &chan_b);

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
    
    acc_count += (int16_t)(val - raw_count); 
    raw_count = (int16_t)val;
}

// ==========================================
// SETUP
// ==========================================
void setup() {
    Serial.begin(115200); 
    
    VescSerial.begin(115200, SERIAL_8N1, RX_PIN, TX_PIN); 
    while (!VescSerial) { ; }
    UART.setSerialPort(&VescSerial);

    pcnt_unit = configurarEncoder(ENCODER_A, ENCODER_B);
    
    Serial.println("\n--- SISTEMA AEROPÊNDULO INICIALIZADO ---");
    Serial.println("Comandos aceitos no Monitor Serial:");
    Serial.println("  P[valor] -> Define Kp (Ex: P2.5)");
    Serial.println("  I[valor] -> Define Ki (Ex: I0.4)");
    Serial.println("  S[valor] -> Define Setpoint em radianos (Ex: S0.5)");
    Serial.println("  E        -> [EMERGÊNCIA] Desliga o motor imediatamente!");
    Serial.println("  R        -> [RESUME] Libera o motor e reinicia o controle.");
    Serial.println("----------------------------------------\n");
    
    last_time = millis(); 
}

// ==========================================
// LOOP PRINCIPAL
// ==========================================
void loop() {
    // 1. LEITURA DO ENCODER
    lerEncoderHardware();

    if (acc_count != last_acc_count) {
        graus = (180.0f * (float)acc_count) / COUNTS_PER_REV;
        radianos = (PI * (float)acc_count) / COUNTS_PER_REV;

        Serial.print("Angulo: ");
        Serial.print(graus, 2);
        Serial.print("° | Rad: ");
        Serial.print(radianos, 4);
        Serial.print(" rad | Kp: ");
        Serial.print(kp, 2);
        Serial.print(" | Ki: ");
        Serial.print(ki, 2);
        
        if (parada_emergencia) {
            Serial.println(" | [EMERGÊNCIA ATIVA - MOTOR BLOQUEADO]");
        } else {
            Serial.print(" | Setpoint: ");
            Serial.print(setpoint, 4);
            Serial.print(" | I_Current: ");
            Serial.print(current);
            Serial.println(" A");
        }

        last_acc_count = acc_count;
    }

    // 2. PARSER DO MONITOR SERIAL (Sintonização Dinâmica e Segurança)
    if (Serial.available() > 0) {
        String inputString = Serial.readStringUntil('\n');
        inputString.trim(); 
        
        if (inputString.length() > 0) {
            char comando = inputString.charAt(0);          
            String valorString = inputString.substring(1); 
            float valor = valorString.toFloat();
            
            // COMANDO DE EMERGÊNCIA
            if (comando == 'E' || comando == 'e') {
                parada_emergencia = true;
                current = 0.0f;
                integral_erro = 0.0f; // Zera o integrador para evitar trancos ao voltar
                Serial.println("\n################################################");
                Serial.println("[PERIGO] PARADA DE EMERGÊNCIA SOLICITADA!");
                Serial.println("Motor cortado para 0A. Controle desativado.");
                Serial.println("Para liberar o sistema, digite 'R' e pressione Enter.");
                Serial.println("################################################\n");
            } 
            // COMANDO PARA REASSUMIR O SISTEMA
            else if (comando == 'R' || comando == 'r') {
                parada_emergencia = false;
                last_time = millis(); // Reseta o delta do tempo para o integrador não dar salto
                Serial.println("\n>>> [SISTEMA LIBERADO] Reiniciando controle PI... <<<\n");
            }
            // ALTERAÇÃO DE PARÂMETROS (Só aceita se não estiver em emergência)
            else if (!parada_emergencia) {
                if (comando == 'P' || comando == 'p') {
                    kp = valor;
                    Serial.print("\n>>> [CONFIG] Novo Kp definido: ");
                    Serial.println(kp, 4);
                } 
                else if (comando == 'I' || comando == 'i') {
                    ki = valor;
                    integral_erro = 0.0f; 
                    Serial.print("\n>>> [CONFIG] Novo Ki definido: ");
                    Serial.println(ki, 4);
                } 
                else if (comando == 'S' || comando == 's') {
                    setpoint = valor;
                    Serial.print("\n>>> [CONFIG] Novo Setpoint definido: ");
                    Serial.print(setpoint, 4);
                    Serial.println(" rad\n");
                } 
                else {
                    Serial.println("\n[ERRO] Comando desconhecido. Use P, I, S, E ou R.");
                }
            } else {
                Serial.println("\n[AVISO] Sistema em modo de EMERGÊNCIA. Comandos P, I e S bloqueados. Digite 'R' para liberar.");
            }
        }
    }

    // ==========================================
    // CONTROLADOR PI + ANTI-WINDUP
    // ==========================================
    unsigned long current_time = millis();
    float dt = (current_time - last_time) / 1000.0f;
    last_time = current_time;

    if (dt <= 0.0f || dt > 0.5f) dt = 0.01f; 

    // Se a parada de emergência estiver ativa, ignora o cálculo do PID e força zero
    if (parada_emergencia) {
        current = 0.0f;
        integral_erro = 0.0f;
    } 
    else {
        // Fluxo normal do controlador
        float erro = setpoint - radianos;
        float P = kp * erro;
        float u_ideal = P + (ki * (integral_erro + erro * dt));

        // Saturação física
        if (u_ideal > SAT_MAX) {
            current = SAT_MAX;
        } else if (u_ideal < SAT_MIN) {
            current = SAT_MIN;
        } else {
            current = u_ideal;
        }

        // Anti-Windup Clamping
        bool sat_alta = (u_ideal > SAT_MAX && erro > 0);
        bool sat_baixa = (u_ideal < SAT_MIN && erro < 0);

        if (!sat_alta && !sat_baixa) {
            integral_erro += erro * dt; 
        }
    }

    // ==========================================
    // 3. ENVIO DE COMANDO PARA O VESC
    // ==========================================
    UART.setCurrent(current);
    
    delay(10); 
}