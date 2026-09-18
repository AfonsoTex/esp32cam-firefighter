#pragma once
// ── Estado de rede partilhado ───────────────────────────────────────
// Variáveis e definições usadas tanto pelo main.ino (loop principal)
// como pelo wifi_manager e camera_stream. Ficam aqui para não haver
// cópias diferentes da mesma variável em vários ficheiros.

#include <WiFi.h>
#include <WiFiUdp.h>

// Port where comandos.py listens. Video uses 1884 (see camara.py).
// IP picks the machine, port picks which program on it.
#define SERVER_PORT 1883
#define VIDEO_PORT 1884

// Fixed size buffer: String/malloc would fragment the heap over thousands of
// iterations, and the camera needs large contiguous blocks per JPEG frame.
extern char buffer[128];

// IP of the PC running the Python servers. The ESP32 is the client:
// on boot it connects out to this address, so it must know it up front.
// Set this in config.h — it changes with your network.
extern const char *destino;

// UDP sockets for control commands and video streaming.
extern WiFiUDP udpControl;
extern WiFiUDP udpVideo;

// Last time any data arrived from the PC. Silence for >1 s stops the
// motors; >5 s triggers another UDP discovery packet.
extern unsigned long lastHeartbeat;
extern unsigned long wifiLostTimestamp;
extern bool trackingLostWifi;
extern unsigned long lastReconnectAttempt;
