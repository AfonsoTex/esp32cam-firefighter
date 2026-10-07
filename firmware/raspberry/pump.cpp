#include "pump.h"

#include <lgpio.h>
#include <stdexcept>

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

    // Cancel the timed pulse before writing LOW, so it cannot turn the output back on.
    int stopResult = 0;
    if (!ligada) {
        // lgpio rejects a zero-length pulse when there is no active pulse to cancel.
        int busy = lgTxBusy(gpioHandle, GPIO_BOMBA, LG_TX_PWM);
        if (busy > 0) {
            stopResult = lgTxPulse(gpioHandle, GPIO_BOMBA, 0, 0, 0, 0);
            // The pulse may finish between the busy check and the cancel request.
            if (stopResult == LG_BAD_PWM_MICROS &&
                lgTxBusy(gpioHandle, GPIO_BOMBA, LG_TX_PWM) == 0) {
                stopResult = 0;
            }
        } else if (busy < 0) {
            stopResult = busy;
        }
    }

    int nivel;
    if (ligada) {
        nivel = 1;
    } else {
        nivel = 0;
    }

    int resultado = lgGpioWrite(gpioHandle, GPIO_BOMBA, nivel);
    if (resultado < 0 || stopResult < 0) {
        return false;
    }

    return true;
}

bool startPumpPulse()
{
    if (gpioHandle < 0) {
        return false;
    }

    // Arguments: controller, GPIO, HIGH duration, LOW duration, offset, cycle count.
    // One cycle means one second on, then off; 0 cycles would repeat indefinitely.
    int result = lgTxPulse(gpioHandle, GPIO_BOMBA, PUMP_ON_MICROSECONDS, 1, 0, 1);
    if (result < 0) {
        definirBomba(false);
        return false;
    }
    return true;
}

bool pumpIsRunning()
{
    int result = lgTxBusy(gpioHandle, GPIO_BOMBA, LG_TX_PWM);
    if (result < 0) {
        throw std::runtime_error("Could not check the pump pulse.");
    }
    return result > 0;
}

void terminarBomba()
{
    if (gpioHandle < 0) {
        return;
    }

    definirBomba(false);
    lgGpiochipClose(gpioHandle);
    gpioHandle = -1;
}
