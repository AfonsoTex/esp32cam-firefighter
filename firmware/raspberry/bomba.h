#pragma once

constexpr int GPIO_BOMBA = 17;

// Opens the GPIO controller and reserves the pump pin as an output, LOW.
// Returns true on success or false on failure.
bool inicializarBomba();

// true turns the pump on, false turns it off.
// Returns true if accepted or false on failure.
bool definirBomba(bool ligada);

// Turns the pump off and closes the controller.
void terminarBomba();
