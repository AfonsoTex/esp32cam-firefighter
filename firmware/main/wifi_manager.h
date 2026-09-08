#pragma once
// ── Gestão de WiFi: guarda redes em NVS, modo Access Point de configuração,
// e a ligação normal a uma rede já conhecida ────────────────────────
void setup_flash_memory();
void dump_nvs_to_serial();
void Write_to_flash(char *ssid, char *password);
void enable_access_point();
void loop_config_mode();

// Tenta ligar a uma rede guardada em flash; se nenhuma resultar, entra em
// modo Access Point (nunca regressa). Se ligar com sucesso, arranca também
// a task de streaming de vídeo (task_camara) pinada ao Core 0.
void wifi_setup_and_connect();
