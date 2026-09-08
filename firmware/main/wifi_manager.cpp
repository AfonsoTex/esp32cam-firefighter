#include "wifi_manager.h"
#include "network_state.h"
#include "camera_stream.h"
#include "config.h"

#include <Arduino.h>
#include <WiFi.h>
#include "nvs_flash.h"    // NVS partition init (non-volatile storage in flash)
#include "nvs.h"          // NVS read/write API for WiFi credentials

// Layout version of the credentials record saved in NVS. Bump by hand
// when the struct fields below change, so old records can be told apart.
// (Written on save; not yet checked on read.)
#define WIFI_STORAGE_VERSION 1

typedef struct {
    uint8_t version;
    char ssid[32];
    char password[64];
} my_wifi_credentials_t;

// TCP server, used ONLY in Access Point mode (initial WiFi setup).
// With no known network, the ESP32 becomes the AP "ESP32_CAM_AFONSO".
// You connect to it and send "WIFI:ssid,password\n" to this port to
// store a new network in NVS flash.
WiFiServer ESPserver(1883);

// Credentials of the config network the ESP32 creates when it has no
// known WiFi. Set in config.h.
const char *ap_ssid     = AP_SSID;
const char *ap_password = AP_PASSWORD;

void setup_flash_memory() {
    esp_err_t return_error_value = nvs_flash_init();
    if (return_error_value == ESP_ERR_NVS_NO_FREE_PAGES || return_error_value == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
}

// Debug only: prints every WiFi network stored in flash.
void dump_nvs_to_serial() {
    nvs_handle_t h;
    uint32_t count = 0;

    if (nvs_open("wifi_storage_id", NVS_READONLY, &h) != ESP_OK) {
        Serial.println("[NVS] Namespace empty or not found");
        return;
    }
    nvs_get_u32(h, "wifi_count", &count);
    Serial.printf("[NVS] Total networks: %lu\n", count);

    for (uint32_t i = 0; i < count; i++) {
        char key[16];
        sprintf(key, "wifi_%lu", i);
        my_wifi_credentials_t net;
        size_t size = sizeof(my_wifi_credentials_t);
        if (nvs_get_blob(h, key, &net, &size) == ESP_OK) {
            Serial.printf("[NVS] [%lu] SSID: %s | PASS: %s\n", i, net.ssid, net.password);
        }
    }
    nvs_close(h);
}

void enable_access_point() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    Serial.println("\n--- ACCESS POINT MODE ---");
    Serial.print("IP: "); Serial.println(WiFi.softAPIP());
    ESPserver.begin();
}

// Stores one WiFi network in NVS flash. Called from AP mode with the
// ssid and password parsed from the WIFI: command.
// Fills the struct, reads the current count, saves the record under key
// "wifi_<count>", then increments the count. Numbered keys let several
// networks be stored and read back later by dump / the scan on boot.
void Write_to_flash(char *ssid, char *password) {
    nvs_handle_t my_handle;
    my_wifi_credentials_t my_network;
    uint32_t count;

    memset(&my_network, 0, sizeof(my_wifi_credentials_t));
    my_network.version = WIFI_STORAGE_VERSION;

    strncpy(my_network.ssid, ssid, sizeof(my_network.ssid) - 1);
    strncpy(my_network.password, password, sizeof(my_network.password) - 1);

    esp_err_t err = nvs_open("wifi_storage_id", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        Serial.println("Error opening NVS handle!");
        return;
    }

    err = nvs_get_u32(my_handle, "wifi_count", &count);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        count = 0;
        nvs_set_u32(my_handle, "wifi_count", count);
    }

    char key[16];
    sprintf(key, "wifi_%lu", count);

    err = nvs_set_blob(my_handle, key, &my_network, sizeof(my_wifi_credentials_t));
    if (err == ESP_OK) {
        count++;
        nvs_set_u32(my_handle, "wifi_count", count);
        nvs_commit(my_handle);
    } else {
        Serial.printf("Error writing blob: %s\n", esp_err_to_name(err));
    }
    dump_nvs_to_serial();
    nvs_close(my_handle);
}

// Loop that runs in AP mode. Reads and handles the commands you send
// from Packet Sender. Infinite: only the ESP.restart() below leaves it.
// WIFI:ssid,password -> Write_to_flash() stores the network
// RESET:NOW          -> reboots the chip
void loop_config_mode() {
    Serial.println("Entering infinite config loop...");

    while (true) {
        WiFiClient configClient = ESPserver.available();

        if (configClient) {
            Serial.println("Config client detected.");

            while (configClient.connected() || configClient.available() > 0) {
                if (configClient.available() > 0) {
                    memset(buffer, 0, sizeof(buffer));
                    int n = configClient.readBytesUntil('\n', buffer, sizeof(buffer) - 1);

                    if (n > 0) {
                        buffer[n] = '\0';
                        if (buffer[n - 1] == '\r') buffer[n - 1] = '\0';

                        if (strncmp(buffer, "WIFI:", 5) == 0) {
                            char *ssid = buffer + 5;
                            char *virgula = strchr(ssid, ',');
                            if (virgula != NULL) {
                                *virgula = '\0';
                                char *password = virgula + 1;
                                Write_to_flash(ssid, password);
                                configClient.println("OK: Network saved.");
                            }
                        } else if (strncmp(buffer, "RESET:NOW", 9) == 0) {
                            configClient.println("OK: Restarting...");
                            delay(500);
                            ESP.restart();
                        }
                    }
                }
            }
            Serial.println("Client disconnected. Waiting...");
        }
        delay(10);
    }
}

void wifi_setup_and_connect() {
    // ── WiFi: try saved networks, or fall back to AP mode ─────────────
    nvs_handle_t my_handle_read;
    uint32_t count = 0;
    esp_err_t err = nvs_open("wifi_storage_id", NVS_READONLY, &my_handle_read);

    // No stored networks: either the namespace was never created (no network
    // ever saved) or the wifi_count key is missing. Both mean zero networks,
    // so fall into AP mode and stay in the config loop until you send one.
    // enable_access_point(): ESP32 creates its own network (ESP32_CAM_AFONSO),
    //   takes IP 192.168.4.1, opens the TCP server on port 1883.
    // loop_config_mode(): infinite loop reading your commands, saves the
    //   network you send to flash. Never returns — only leaves on chip reset
    //   (the RESET:NOW you send from Packet Sender).
    if (err != ESP_OK || nvs_get_u32(my_handle_read, "wifi_count", &count) != ESP_OK) {
        nvs_close(my_handle_read);
        enable_access_point();
        loop_config_mode();
    } else {
        int numero_redes_ar = WiFi.scanNetworks();
        bool conectou = false;
        my_wifi_credentials_t net;                    // buffer for each network read from flash
        size_t size = sizeof(my_wifi_credentials_t);

        // Match saved networks against what's on the air.
        // Outer loop: networks currently in range (i). Inner loop: networks
        // saved in flash (j, count of them). For each network in range, look
        // for a saved one with the same SSID; on a match, try to connect.
        for (int i = 0; i < numero_redes_ar && !conectou; i++) {
            for (uint32_t j = 0; j < count; j++) {

                // Build the NVS key for saved network j: "wifi_0", "wifi_1", ...
                char key[16];
                sprintf(key, "wifi_%lu", j);

                // match found: this saved network is in range. try to connect.
                if (nvs_get_blob(my_handle_read, key, &net, &size) == ESP_OK
                    && strcmp(net.ssid, WiFi.SSID(i).c_str()) == 0) {

                    Serial.printf("Trying: %s\n", net.ssid);
                    WiFi.begin(net.ssid, net.password);

                    // wait up to 10 s for the connection
                    unsigned long t0 = millis();
                    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 10000) {
                        delay(500);
                        Serial.print(".");
                    }

                    if (WiFi.status() == WL_CONNECTED) {
                        Serial.printf("\nConnected to: %s\n", net.ssid);
                        conectou = true;
                    } else {
                        Serial.printf("\nFailed: %s\n", net.ssid);
                        WiFi.disconnect();
                        delay(200);
                    }
                    break;   // SSID matched; stop scanning flash for this network
                }
            }
        }

        nvs_close(my_handle_read);

        // none of the saved networks was in range or accepted the connection:
        // fall back to AP mode, same as the no-networks case above.
        if (!conectou) {
            Serial.println("All networks failed. Fallback mode.");
            enable_access_point();
            loop_config_mode();
        }

        // WiFi is up: launch the camera streaming task pinned to Core 0,
        // so heavy JPEG sending never blocks the motor commands on Core 1.
        xTaskCreatePinnedToCore(
            task_camara,    // function the task runs
            "task_camara",  // name, debug only
            10000,          // stack size in bytes
            NULL,           // no argument
            1,              // priority
            NULL,           // task handle not kept
            0               // Core 0
        );
    }
}
