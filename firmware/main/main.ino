// ── ESP32-CAM RC Car ─────────────────────────────────────────────────
// Ponto de entrada do sketch. A lógica está dividida em módulos:
//   config.h         -> credenciais/definições que mudam com a tua rede
//   network_state.*  -> estado partilhado da ligação (buffer, sockets, timers)
//   motor_control.*  -> ponte H (L293D): direção e PWM dos motores
//   wifi_manager.*   -> guardar redes em NVS, modo Access Point, ligação
//   camera_stream.*  -> inicialização do OV2640 e streaming de vídeo por UDP
#include "config.h"
#include "network_state.h"
#include "motor_control.h"
#include "wifi_manager.h"
#include "camera_stream.h"

void setup() {
    Serial.begin(115200);
    pinos_setup();          // motor direction pins
    pwm_channel_setup();    // motor PWM on LEDC channels 0/1
    setup_flash_memory();   // init NVS partition
    dump_nvs_to_serial();   // debug: print stored networks

    setup_camera();         // configure and initialize the OV2640 sensor

    // Connects to a saved WiFi network, or falls back to AP config mode
    // (in which case this call never returns). On success it also starts
    // the camera streaming task pinned to Core 0.
    wifi_setup_and_connect();
}

void loop() {
    // Layer 1 — is WiFi connected?
    // If not: stop the motors and start timing the outage (trackingLostWifi
    // makes sure the instant is recorded only once). After 10 s down,
    // ESP.restart(). The return blocks everything else — no WiFi, nothing to
    // do. If connected, clear the flag so the next outage is timed fresh.
    if (WiFi.status() != WL_CONNECTED) {
        stop_motors();
        if (!trackingLostWifi) {
            wifiLostTimestamp = millis();
            trackingLostWifi = true;
        }
        if (millis() - wifiLostTimestamp > 10000) ESP.restart();
        return;
    }
    trackingLostWifi = false;

    // Layer 2 — is the TCP link to the PC up?
    // If down: stop the motors and retry connect once every 5 s (connect
    // blocks while waiting, so calling it every loop would trap us here).
    // Reset lastHeartbeat either way so the fresh link isn't seen as silent.
    if (!client.connected()) {
        stop_motors();
        if (millis() - lastReconnectAttempt > 5000) {
            lastReconnectAttempt = millis();
            Serial.printf("[%lu] Trying to reach the PC...\n", millis());
            if (client.connect(destino, SERVER_PORT)) {
                Serial.printf("[%lu] Connected successfully!\n", millis());
                lastHeartbeat = millis();
            } else {
                lastHeartbeat = millis();
                Serial.printf("[%lu] CONNECTION FAILED!\n", millis());
            }
        }
        return;
    }

    // Layer 3 — silence watchdog. PC sends HB every 0.2 s, so silence = trouble.
    if (millis() - lastHeartbeat > 1000) {
        stop_motors();                    // 1 s: safety stop
    }

    if (millis() - lastHeartbeat > 5000) {
        Serial.printf("[%lu] HEARTBEAT TIMEOUT - closing!\n", millis());
        client.stop();                    // 5 s: assume dead, drop the socket
        lastReconnectAttempt = millis();
    }

    // Layer 4 — read all pending lines; only MOV: lines drive the motors.
    if (client.available() > 0) {
        while (client.available() > 0) {
            memset(buffer, 0, sizeof(buffer));
            client.readBytesUntil('\n', buffer, sizeof(buffer) - 1);
            lastHeartbeat = millis();
        }

        if (strncmp(buffer, "MOV:", 4) == 0) {
            processar_comando(buffer);
        }
    }
}
