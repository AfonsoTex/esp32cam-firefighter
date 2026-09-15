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

// UDP sockets for control commands and video streaming.
WiFiUDP udpControl;
WiFiUDP udpVideo;

// Last time any data arrived from the PC. Silence for >1 s stops the
// motors; >5 s triggers another UDP discovery packet.
unsigned long lastHeartbeat = 0;
unsigned long wifiLostTimestamp = 0;
bool trackingLostWifi = false;
unsigned long lastReconnectAttempt = 0;
