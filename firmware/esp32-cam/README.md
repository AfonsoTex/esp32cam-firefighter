# ESP32-CAM firmware

This folder contains the Arduino firmware for the AI-Thinker ESP32-CAM board.
It captures video from its OV2640 camera, sends JPEG data over Wi-Fi, and receives
commands to drive the motors through an L293D motor driver.

The sketch and its supporting files are inside `main/`. This firmware does not
run FireNet and does not serve the Raspberry Pi's browser page. The Raspberry Pi
program currently captures its own camera; the two video paths are separate.

## How to read the files

An `.ino` file is an Arduino sketch. Its `setup()` function runs at startup, and
its `loop()` function runs repeatedly. A `.h` file declares what a module exposes;
a `.cpp` file implements the actual behavior.

## File guide

| File | Purpose |
| --- | --- |
| `main/main.ino` | Entry point. Initializes serial output, motor pins, PWM, flash storage, the camera, and Wi-Fi. Starts UDP control and broadcasts `HELLO`. Its loop receives movement commands, tracks communication timeouts, stops motors when contact is lost, and sends discovery messages again after prolonged silence. |
| `main/config.h` | Network settings: `DESTINO_IP` is the video receiver's IP address; `CONTROL_DISCOVERY_IP` is the broadcast address used for discovery; `AP_SSID` and `AP_PASSWORD` configure the ESP32's setup Wi-Fi network. The checked-in destination and access-point settings are placeholders. |
| `main/network_state.h` | Declares shared network state: UDP control/video sockets, the destination address, a packet buffer, and timestamps used to detect lost communication. Defines control port 1883 and video port 1884. `extern` declarations let several files refer to the same variables. |
| `main/network_state.cpp` | Creates the shared variables declared in the header, sets `destino` from `DESTINO_IP`, and initializes the timestamps and Wi-Fi-loss flag. This avoids creating separate copies of the state in each module. |
| `main/wifi_manager.h` | Declares functions for flash initialization, storing Wi-Fi credentials, configuration access-point mode, and connection to a saved network. |
| `main/wifi_manager.cpp` | Stores Wi-Fi credentials in NVS (non-volatile flash storage, which survives a restart). Scans for saved networks and attempts a connection. If none works, starts an access point and a TCP configuration server. After a successful connection, starts the camera task on core 0. Its debug function prints stored network names and passwords to serial output. |
| `main/camera_stream.h` | Declares `setup_camera()` and `task_camara()`, the camera initialization function and the background video task. |
| `main/camera_stream.cpp` | Defines the board's OV2640 pin mapping and configures JPEG capture at VGA size, with two frame buffers in PSRAM (additional RAM on the board). The active task captures frames, writes their JPEG bytes through the UDP video socket, returns the buffers to the driver, and waits 33 ms between iterations. An older TCP streaming implementation remains commented out and does not run. |
| `main/motor_control.h` | Declares motor pin/PWM setup, stopping, speed/steering control, and movement-command parsing. |
| `main/motor_control.cpp` | Parses `MOV:x,DIR:y` and converts speed and steering into motor direction and PWM output. GPIOs 12/13 select a shared direction; GPIOs 14/15 control left/right power. PWM rapidly switches an output on and off; its duty cycle sets the power level. The code uses LEDC motor channels 0/1, while the camera clock uses channel 2. |
| `README.md` | This file: explains the sketch, supporting modules, and current communication flow. |

## Startup and operation

1. `setup()` initializes hardware and reads saved Wi-Fi settings.
2. `wifi_manager.cpp` tries available saved networks. If none connects, it stays
   in configuration mode until restarted.
3. Once connected, a separate camera task runs on core 0 and sends JPEG data to
   `DESTINO_IP` on UDP port 1884. The 33 ms wait is not a guarantee of 30 fps:
   capture and transmission also take time.
4. The main loop listens for UDP control on port 1883 and passes movement
   commands to `motor_control.cpp`.

## Network messages

A port identifies a service on a device. TCP and UDP are different transports,
so TCP port 1883 for Wi-Fi setup and UDP port 1883 for motor control are distinct.

| Transport | Message or data | Purpose |
| --- | --- | --- |
| TCP 1883, configuration mode | `WIFI:ssid,password` followed by a newline | Stores a Wi-Fi network in flash. Saving it does not leave the configuration loop. |
| TCP 1883, configuration mode | `RESET:NOW` followed by a newline | Restarts the board so it can try the saved network. |
| UDP 1883, normal operation | `HELLO` sent by the ESP32 to the broadcast address | Lets a controller discover the ESP32's address and control port. |
| UDP 1883, normal operation | `MOV:x,DIR:y` sent to the ESP32 | Requests speed and steering. The motor logic expects values from -1 to 1; the current parser does not clamp them. |
| UDP 1883, normal operation | Any received packet | Refreshes the communication timer. A heartbeat packet keeps contact alive without changing movement. |
| UDP 1884, normal operation | JPEG bytes sent to `DESTINO_IP` | Supplies camera data to a receiver. This is not an HTTP video stream that a browser can open directly. |

The main loop stops the motors after more than one second without a control
packet. After five seconds of silence it broadcasts `HELLO` again, at intervals
of more than two seconds. If Wi-Fi disconnects, it stops the motors immediately
and restarts the board after more than ten seconds of continued disconnection.

## Editing and building

Start with `main/main.ino` to understand how the modules connect. Change network
settings in `main/config.h`, camera settings in `main/camera_stream.cpp`, and
motor pin assignments or movement logic in `main/motor_control.cpp`.

Open `main/main.ino` as an Arduino sketch, keeping its `.h` and `.cpp` files in
the same `main/` folder. The firmware uses Arduino-ESP32 facilities for Wi-Fi,
UDP, NVS, FreeRTOS tasks, and the ESP32 camera driver. Its motor setup uses the
`ledcAttachChannel()` API, so the installed board package must support that API.
Select the board configuration matching the AI-Thinker ESP32-CAM and its PSRAM.
