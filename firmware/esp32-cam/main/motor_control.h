#pragma once
// ── Controlo dos motores (ponte H L293D) ────────────────────────────

void pinos_setup();
void pwm_channel_setup();
void stop_motors();
void motor_logic(float speed, float steering);
void processar_comando(char* data);
