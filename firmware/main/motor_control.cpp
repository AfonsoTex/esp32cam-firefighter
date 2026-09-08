#include "motor_control.h"
#include <Arduino.h>
#include <stdio.h>   // atof

// ── H-bridge (L293D) ──────────────────────────────────────────────
// EN pins carry PWM and set how much power each side gets.
// IN pins are digital only and set the direction, shared by both motors:
// both go forward or both go backward, never one each way.
// Steering comes from feeding the two sides different PWM duty.
const int EN1_pin = 14;   // PWM, left side
const int EN2_pin = 15;   // PWM, right side
const int IN1_pin = 12;   // direction pair: HIGH/LOW = forward
const int IN2_pin = 13;   //                 LOW/HIGH = backward

// LEDC hardware channels for the two PWM outputs.
const int PWM_CHANNEL_1 = 0;
const int PWM_CHANNEL_2 = 1;

void stop_motors() {
    ledcWrite(EN1_pin, 0);
    ledcWrite(EN2_pin, 0);
}

void pinos_setup() {
    pinMode(IN1_pin, OUTPUT);
    pinMode(IN2_pin, OUTPUT);

    digitalWrite(IN1_pin, LOW);
    digitalWrite(IN2_pin, LOW);
}

void pwm_channel_setup() {
    // ledcAttachChannel also sets the pin as output, so no pinMode needed.
    ledcAttachChannel(EN1_pin, 1000, 8, PWM_CHANNEL_1);
    ledcAttachChannel(EN2_pin, 1000, 8, PWM_CHANNEL_2);
}

// Turns speed and steering (both -1..1, from the PC) into direction and
// per-side power. IN pins set direction for both motors; the EN duty of
// each side is cut by the steering amount
void motor_logic(float speed, float steering) {
    Serial.printf("MOTOR: speed=%.2f steering=%.2f\n", speed, steering);

    // Direction
    if(speed == 0){
        digitalWrite(IN1_pin, LOW);
        digitalWrite(IN2_pin, LOW);
    }
    else if(speed > 0){
        digitalWrite(IN1_pin, HIGH);
        digitalWrite(IN2_pin, LOW);
    }
    else if(speed < 0){
        digitalWrite(IN1_pin, LOW);
        digitalWrite(IN2_pin, HIGH);
    }

    // Velocity
    int en_esquerda = 0;
    int en_direita = 0;

    if(steering >= 0){
        en_esquerda = (int)(speed * 255);
        en_direita  = (int)(speed * 255 * (1 - steering));
    }
    else{
        en_direita  = (int)(speed * 255);
        en_esquerda = (int)(speed * 255 * (1 + steering));
    }

    ledcWrite(EN1_pin, abs(en_esquerda));
    ledcWrite(EN2_pin, abs(en_direita));
    Serial.printf("EN_LEFT=%d EN_RIGHT=%d\n", abs(en_esquerda), abs(en_direita));
}

void processar_comando(char* data) {
    Serial.print("Command received to process: ");
    Serial.println(data);

    // Parses a "MOV:x,DIR:y" line and drives the motors with x and y.
    // data+4 skips "MOV:"; strchr finds the comma; writing '\0' over the
    // comma splits the string so mov holds only x. virgula+5 skips ",DIR:".
    // atof turns each number text into a float.

    char *mov = data + 4;
    char *virgula = strchr(mov, ',');

    float move = 0.0;
    float direc = 0.0;

    if (virgula != NULL) {
        *virgula = '\0';
        char *dir_str = virgula + 5;   // assumes fixed ",DIR:" prefix

        move  = atof(mov);
        direc = atof(dir_str);
        motor_logic(move, direc);
    }
}
