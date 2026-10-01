#pragma once
// Controls the motors through the L293D H-bridge.

void pinos_setup();
void pwm_channel_setup();
void stop_motors();
void motor_logic(float speed, float steering);
void processar_comando(char* data);
