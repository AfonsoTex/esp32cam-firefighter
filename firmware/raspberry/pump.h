#pragma once

constexpr int GPIO_BOMBA = 17;

// GPIO 17 controls the existing pump driver: HIGH = on, LOW = off.
constexpr int PUMP_ON_MICROSECONDS = 1000000;

// Opens the GPIO controller and reserves the pump pin as an output, LOW.
// Returns true on success or false on failure.
bool inicializarBomba();

// true turns the pump on, false turns it off.
// Returns true if accepted or false on failure.
bool definirBomba(bool ligada);

// Starts one 1-second HIGH pulse, followed by LOW. Does not block the video loop.
// lgpio times the pulse independently, even if frame processing is delayed.
bool startPumpPulse();

// Reports whether the timed pulse is still running. Throws if the GPIO check fails.
bool pumpIsRunning();

// Turns the pump off and closes the controller.
void terminarBomba();
