// ── ESP32-CAM RC Car ─────────────────────────────────────────────────
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

    // Connects to the Wi-Fi network and starts the video task.
    // This is a blocking function, it only proceeds when Wi-Fi is connected.
    wifi_setup_and_connect();

    // ==========================================
    // UDP CONTROL INITIALIZATION
    // ==========================================
    udpControl.begin(SERVER_PORT); // Listens for packets on port 1883
    Serial.printf("UDP Control listening on port %d\n", SERVER_PORT);

    // Sends the initial packet to the PC. 
    // This is what the Python script ("recvfrom") is waiting for to discover the ESP32's IP!
    udpControl.beginPacket(destino, SERVER_PORT);
    udpControl.print("HELLO");
    udpControl.endPacket();
    
    lastHeartbeat = millis(); 
}

void loop() {
    // Layer 1 — is WiFi connected?
    // If not: stop the motors and start timing the outage.
    if (WiFi.status() != WL_CONNECTED) {
        stop_motors();
        if (!trackingLostWifi) {
            wifiLostTimestamp = millis();
            trackingLostWifi = true;
        }
        if (millis() - wifiLostTimestamp > 10000) ESP.restart();
        return; // Do nothing else if there is no Wi-Fi
    }
    trackingLostWifi = false;


    // Layer 2 — Read incoming UDP packets
    // Process packets immediately to update lastHeartbeat before testing for timeout.
    int packetSize = udpControl.parsePacket();
    if (packetSize > 0) {
        // Reads the packet directly into the buffer
        int len = udpControl.read(buffer, sizeof(buffer) - 1);
        if (len > 0) {
            buffer[len] = '\0'; // Adds a null terminator to make it a valid string
        }
        
        lastHeartbeat = millis(); // Receiving ANY packet means the PC is alive

        // If it's a MOV command, process it for the motors
        if (strncmp(buffer, "MOV:", 4) == 0) {
            processar_comando(buffer);
        }
    }


    // Layer 3 — Watchdog (Safety Stop & PC Wake-up)
    // If 1000ms (1s) pass without packets (neither MOV nor HB), stop the car for safety.
    if (millis() - lastHeartbeat > 1000) {
        stop_motors();
    }

    // If 5000ms (5s) of silence pass, the Python script on the PC might have restarted 
    // and lost our IP. We resend "HELLO" every 2 seconds to wake it up.
    if (millis() - lastHeartbeat > 5000) {
        if (millis() - lastReconnectAttempt > 2000) {
            lastReconnectAttempt = millis();
            Serial.printf("[%lu] Prolonged silence. Sending HELLO to PC...\n", millis());
            
            udpControl.beginPacket(destino, SERVER_PORT);
            udpControl.print("HELLO");
            udpControl.endPacket();
        }
    }
}