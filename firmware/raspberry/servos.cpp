#include "servos.h"

#include <filesystem>
#include <fstream>
#include <string>

// static: internal linkage. The name PWMCHIP only exists inside servos.cpp.
// const: does not change after it is created.
static const std::string PWMCHIP = "/sys/class/pwm/pwmchip0";

bool inicializarServos()
{
    if (!std::filesystem::exists(PWMCHIP + "/pwm2")) {
        std::ofstream f(PWMCHIP + "/export");
        if (!f) {
            return false;
        }
        f << 2;
        f.flush();
        if (!f) {
            return false;
        }
    }
    if (!std::filesystem::exists(PWMCHIP + "/pwm3")) {
        // ainda nao exportado
        std::ofstream f(PWMCHIP + "/export");
        if (!f) {
            return false;
        }
        f << 3;
        f.flush();
        if (!f) {
            return false;
        }
    }

    {
        std::ofstream f(PWMCHIP + "/pwm2/period");
        if (!f) {
            return false;
        }
        f << 20000000;
        f.flush();
        if (!f) {
            return false;
        }
    }
    {
        std::ofstream f(PWMCHIP + "/pwm2/duty_cycle");
        if (!f) {
            return false;
        }
        f << 1500000;
        f.flush();
        if (!f) {
            return false;
        }
    }

    {
        std::ofstream f(PWMCHIP + "/pwm2/enable");
        if (!f) {
            return false;
        }
        f << 1;
        f.flush();
        if (!f) {
            return false;
        }
    }
    {
        std::ofstream f(PWMCHIP + "/pwm3/period");
        if (!f) {
            return false;
        }
        f << 20000000;
        f.flush();
        if (!f) {
            return false;
        }
    }
    {
        std::ofstream f(PWMCHIP + "/pwm3/duty_cycle");
        if (!f) {
            return false;
        }
        f << 1500000;
        f.flush();
        if (!f) {
            return false;
        }
    }
    {
        std::ofstream f(PWMCHIP + "/pwm3/enable");
        if (!f) {
            return false;
        }
        f << 1;
        f.flush();
        if (!f) {
            return false;
        }
    }

    return true;
}

bool definirPulso(int gpio, int pulso_us)
{
    // Accepts only the pins selected in servos.h.
    if (gpio != GPIO_PAN and gpio != GPIO_TILT) {
        return false;
    }

    // Invalid requests leave any existing pulse signal unchanged.
    if (pulso_us < PULSO_MIN_US or pulso_us > PULSO_MAX_US) {
        return false;
    }

    // Each GPIO pin is wired by the hardware to one PWM channel.
    std::string canal;
    if (gpio == GPIO_PAN) {
        canal = "/pwm2";
    } else if (gpio == GPIO_TILT) {
        canal = "/pwm3";
    }

    std::ofstream f(PWMCHIP + canal + "/duty_cycle");
    if (!f) {
        return false;
    }

    // Converts microseconds to nanoseconds, the unit sysfs accepts.
    f << pulso_us * 1000;
    f.flush();
    if (!f) {
        return false;
    }

    return true;
}

void terminarServos()
{
    // Stops the pulses on both channels.
    {
        std::ofstream f(PWMCHIP + "/pwm2/enable");
        if (!f) {
            return;
        }
        f << 0;
        f.flush();
        if (!f) {
            return;
        }
    }
    {
        std::ofstream f(PWMCHIP + "/pwm3/enable");
        if (!f) {
            return;
        }
        f << 0;
        f.flush();
        if (!f) {
            return;
        }
    }
}
