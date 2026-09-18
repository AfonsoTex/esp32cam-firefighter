#include "servos.h"

#include <csignal>
#include <iostream>
#include <sstream>
#include <string>

// Signal handlers only request a stop; GPIO cleanup happens in main.
static volatile std::sig_atomic_t parar = 0;

static void pedirParagem(int)
{
    parar = 1;
}

int main()
{
#ifdef __linux__
    // On the Pi, interrupt terminal reads so Ctrl+C can end the program.
    struct sigaction acao {};
    acao.sa_handler = pedirParagem;
    sigemptyset(&acao.sa_mask);
    acao.sa_flags = 0;
    if (sigaction(SIGINT, &acao, nullptr) != 0 or
        sigaction(SIGTERM, &acao, nullptr) != 0 or
        sigaction(SIGHUP, &acao, nullptr) != 0) {
        std::cerr << "Erro ao configurar a paragem do programa.\n";
        return 1;
    }
#else
    // Allows local syntax checks and simulated tests on other platforms.
    std::signal(SIGINT, pedirParagem);
    std::signal(SIGTERM, pedirParagem);
#endif

    if (!inicializarServos()) {
        std::cerr << "Erro ao abrir gpiochip0 ou reservar os GPIO.\n";
        return 1;
    }

    // Initialization prepares the pins but does not request a position.
    std::cout << "Pan: GPIO" << GPIO_PAN << " | Tilt: GPIO" << GPIO_TILT << '\n'
              << "Comandos: pan <us>, tilt <us>, sair\n"
              << "Intervalo: " << PULSO_MIN_US << " a " << PULSO_MAX_US
              << " us. Comeca por " << PULSO_CENTRO_US << " us.\n"
              << "Para parar os impulsos e terminar: sair ou Ctrl+C.\n";

    std::string linha;
    int codigoSaida = 0;
    while (!parar) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, linha) or parar) {
            break;
        }

        std::istringstream entrada(linha);
        std::string comando;
        std::string extra;
        if (!(entrada >> comando)) {
            continue;
        }

        if (comando == "sair" and !(entrada >> extra)) {
            break;
        }

        int pulso_us;
        if ((comando != "pan" and comando != "tilt") or
            !(entrada >> pulso_us) or (entrada >> extra)) {
            std::cerr << "Comando invalido. Exemplo: pan 1500\n";
            continue;
        }

        if (pulso_us < PULSO_MIN_US or pulso_us > PULSO_MAX_US) {
            std::cerr << "Pulso rejeitado: usa " << PULSO_MIN_US
                      << " a " << PULSO_MAX_US << " us.\n";
            continue;
        }

        int gpio;
        if (comando == "pan") {
            gpio = GPIO_PAN;
        } else {
            gpio = GPIO_TILT;
        }
        if (parar) {
            break;
        }
        if (!definirPulso(gpio, pulso_us)) {
            std::cerr << "Falha ao gerar o sinal. A terminar o teste.\n";
            codigoSaida = 1;
            break;
        }

        std::cout << comando << ": " << pulso_us << " us\n";
    }

    // Stops pulses on exit, end of input, or a handled termination signal.
    terminarServos();
    std::cout << "\nControlo dos servos terminado.\n";
    return codigoSaida;
}
