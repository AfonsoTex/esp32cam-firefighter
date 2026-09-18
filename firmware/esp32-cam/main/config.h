#pragma once
#define DESTINO_IP  "IP_DESTINY"
// Always broadcast HELLO on the local network so the Python server can
// discover the ESP32 even if the PC's IP changes.
// Our PC has a reserved IP, so sending HELLO directly to DESTINO_IP
// would also work. Video still uses DESTINO_IP.
#define CONTROL_DISCOVERY_IP "255.255.255.255"
#define AP_SSID     "YOUR_AP_SSID"
#define AP_PASSWORD "YOUR_AP_PASSWORD"
