#include "network_state.h"
#include "config.h"

// Fixed size buffer: String/mal
loc would fragment the heap over thousands of
// iterations, and the camera needs large contiguous blocks per JPEG frame.
char buffer[128];

// IP of the PC running the Python servers. The ESP32 is the client:
// on boot it connects out to this address, so it must know it up front.
// Set this in config.h — it changes with your network.
const char *destino = DESTINO_IP;

// Normal mode: the ESP32 is the client on both connections.
WiFiClient client;         // commands from comandos.py, port 1883
//WiFiClient clienteVideo;   // JPEG frames to camara.py, port 1884

// Declares the UDP object responsible for managing the video streaming channel
WiFiUDP udpVideo;

// Last time any data arrived from the PC. Silence for >1 s stops the
// motors; >5 s drops the TCP connection.
unsigned long lastHeartbeat = 0;
unsigned long wifiLostTimestamp = 0;
bool trackingLostWifi = false;
unsigned long lastReconnectAttempt = 0;
