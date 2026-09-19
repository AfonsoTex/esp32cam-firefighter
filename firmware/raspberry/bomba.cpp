#include "bomba.h"

#include <lgpio.h>

// Stores the GPIO controller handle. -1 means it is not open.
static int gpioHandle = -1;

bool inicializarBomba()
{
    gpioHandle = lgGpiochipOpen(0);
    if (gpioHandle < 0) {
        return false;
    }

    // The last argument is the initial level: 0 keeps the pump off.
    int resultado = lgGpioClaimOutput(gpioHandle, 0, GPIO_BOMBA, 0);
    if (resultado < 0) {
        lgGpiochipClose(gpioHandle);
        gpioHandle = -1;
        return false;
    }

    return true;
}

bool definirBomba(bool ligada)
{
    if (gpioHandle < 0) {
        return false;
    }

    int nivel;
    if (ligada) {
        nivel = 1;
    } else {
        nivel = 0;
    }

    int resultado = lgGpioWrite(gpioHandle, GPIO_BOMBA, nivel);
    if (resultado < 0) {
        return false;
    }

    return true;
}

void terminarBomba()
{
    if (gpioHandle < 0) {
        return;
    }

    lgGpioWrite(gpioHandle, GPIO_BOMBA, 0);
    lgGpiochipClose(gpioHandle);
    gpioHandle = -1;
}
