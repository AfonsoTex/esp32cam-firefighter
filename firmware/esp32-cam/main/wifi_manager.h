#pragma once
// Manages saved Wi-Fi networks and access point configuration.
void setup_flash_memory();
void dump_nvs_to_serial();
void Write_to_flash(char *ssid, char *password);
void enable_access_point();
void loop_config_mode();

// Connects to a saved network or stays in access point configuration mode.
// On connection, starts the camera streaming task on Core 0.
void wifi_setup_and_connect();
