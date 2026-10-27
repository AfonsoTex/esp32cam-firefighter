# ESP32-CAM and Raspberry Pi Firefighting Robot

<div align="center">
  <img src="docs/photos/robot.jpg" width="720" alt="The complete firefighting robot"><br>
  <em>The complete robot: ESP32-CAM, Raspberry Pi 5, pan/tilt camera and water nozzle.</em>
</div>

<br>

A student robotics project combining an ESP32-CAM, a Raspberry Pi 5 and a PC. The car
follows a line on the floor (or is driven with an Xbox controller). When the Raspberry
Pi sees a flame with its own camera and a neural network trained for this project
(FireNet), it stops the car, aims a pan/tilt water nozzle at the flame and sprays short
water pulses. Then the car carries on.

The car itself (WiFi driving, video and line following) was built in the first phase
and is documented in [esp32cam-rc-car](https://github.com/AfonsoTex/esp32cam-rc-car).
This repository is the firefighting phase. The next phase, a robot that navigates the house on its own
and refills itself, continues in [autonomous-fire-robot](https://github.com/AfonsoTex/autonomous-fire-robot).

This is an experimental candle-scale prototype. Detection, aiming and water reach
depend on the scene and the mechanical assembly; it is not a fire-safety device.

## Technical report

### [Read the full technical report (PDF)](docs/Technical_Report.pdf)

The project's engineering report brings together the design, electronics,
software, flame detection and aiming system.

[Browse supporting documentation](docs/README.md) · [Next phase](https://github.com/AfonsoTex/autonomous-fire-robot)

## What it does

The car has two driving modes, toggled with button A on the gamepad:

- **Manual** — drive it with an Xbox controller over WiFi.
- **Autonomous line following** — the PC analyses the ESP32 camera stream, finds a line on the floor, and steers the car along it.

In both modes the Raspberry Pi can take over when it sees a flame: stop → confirm the
target → aim → spray → return the camera home → let the car move again.

## Architecture

| Board | Job | Code |
| --- | --- | --- |
| **ESP32-CAM** | Streams the front camera to the PC and drives the motors | [`firmware/esp32-cam/`](firmware/esp32-cam/README.md) |
| **PC** | Gamepad, line-following vision and steering | [`server/`](server/README.md) |
| **Raspberry Pi 5** | Flame detection, car stop, pan/tilt aiming, pump, browser video | [`firmware/raspberry/`](firmware/raspberry/README.md) |
| **PC (offline)** | Annotating data, training and exporting FireNet | [`model_training/`](model_training/README.md) |

<div align="center">
  <img src="docs/diagrams/architecture.svg" width="860" alt="How the PC, ESP32-CAM and Raspberry Pi work together">
</div>

The ESP32 sends its camera to the PC; the PC decides how to drive and sends the
commands back. The Raspberry Pi watches for flames with its own camera. While it sees
one, it holds a wire to the ESP32 at "stop", aims the servos and fires the pump.

**On the ESP32 (two tasks on two cores):**
- **Core 0** captures JPEG frames from the OV2640 and sends them to the PC over **UDP** (port 1884).
- **Core 1** receives `MOV:x,DIR:y` commands over **UDP** (port 1883) and drives the motors.

**On the PC (`server/unified_python_server - UDP.py`), three threads:**
- **Receive** — reassembles each JPEG frame from UDP packets, keeping only the newest one.
- **Processing** — runs the line-following vision pipeline and state machine.
- **Main** — reads the gamepad and sends commands to the car.

**On the Raspberry Pi (`firmware/raspberry/`):**
- Captures 640×480 images and feeds a 180×180 copy to FireNet through ONNX Runtime.
- Removes duplicate boxes (NMS), selects a target and moves hardware-PWM pan/tilt servos.
- Uses the flame's initial height in the image to choose a calibrated aim point (the nozzle is not where the camera is). This is not a distance measurement.
- Drives the pump through its driver on GPIO17, and the movement-permission wire on GPIO23 → ESP32 GPIO2 (shared ground).
- Serves annotated video at `http://<raspberry-hostname>:8080/`. No monitor is needed.

### Why UDP

The first version streamed over TCP. When a packet was lost, TCP waited to resend it
before delivering newer data, so the video lagged. For driving, the newest frame and the
current command matter more than every old one, so video and control now use UDP. UDP
can lose or reorder packets, so the application does its own checks: frames are found by
their JPEG start/end markers (`FF D8` / `FF D9`), and control has a heartbeat.

The PC sends a movement command when it changes and an `HB` heartbeat every 0.2 s
otherwise. If the ESP32 hears nothing for 1 s, it stops the motors and forgets the last
command. When contact returns it asks for a fresh one (`SEND_MOV`). After 5 s of silence
it broadcasts `HELLO` to find the PC again, and if WiFi is lost for 10 s it restarts.

### WiFi setup

On boot, the ESP32 tries the WiFi networks saved in flash (NVS). If none works, it
starts its own access point. Connect to it and send `WIFI:ssid,password` and then
`RESET:NOW` (each followed by a newline) over TCP port 1883. The AP name and password
are set in `config.h`.

## Line following

For each frame:
1. **Grayscale** — only dark vs light matters.
2. **ROI** — keep a strip near the bottom (the floor in front of the car).
3. **Threshold** — separate the line from the floor.
4. **Morphology** — clean up noise.
5. **Contours** — find the line and reject reflections and floor patches by size/shape.
6. **Error** — horizontal distance between the line's centre and the image centre.
7. **Proportional control** — steering is proportional to the error.

A small state machine handles losing the line: normal following, and a recovery state
that reverses until the line is back in view after a sharp curve.

## Flame detection and aiming

FireNet is a small convolutional network trained on images recorded with the robot's
own camera and annotated with a custom tool. It divides the image into a 45×45 grid
and predicts, for each cell, whether there is a flame and where its box is.

When a flame appears, the Raspberry Pi:
1. Pulls the permission wire LOW, so the car stops.
2. Waits for the chassis to settle and confirms the flame over several frames.
3. Moves the pan/tilt servos until the flame is at the calibrated aim point.
4. Sprays a short water pulse once the aim has been stable for a moment. It retries a limited number of times.
5. Returns the camera home and, once the scene is clear, lets the car move again.

After spraying, new detections near the sprayed spot are briefly ignored, because water
on the floor can reflect the flame. Details are in the
[Raspberry README](firmware/raspberry/README.md).

## Hardware

- AI-Thinker ESP32-CAM (ESP32-S + OV2640)
- 2× L293D H-bridges (four DC motors, two per side)
- 3D-printed chassis
- 7.4 V battery (2× 18650), 5 V step-down for the motor drivers, 5 V UBEC for the Raspberry Pi and servos
- Raspberry Pi 5 with an OV5647 camera
- Two SG90 servos for the camera/nozzle pan/tilt, powered from the UBEC 5 V output
- Water reservoir, pump and hose, switched by an IRLZ44 MOSFET

Wiring diagrams and the chassis files are in [`hardware/`](hardware/README.md).

## How to use

**ESP32 firmware**
1. Install the ESP32 board package in Arduino IDE (Boards Manager → "esp32" by Espressif). Board: **AI Thinker ESP32-CAM**.
2. Open `firmware/esp32-cam/main/main.ino`. In `config.h`, set `VIDEO_DESTINATION_IP` to your PC's local IP and set the AP credentials.
3. Upload to the ESP32 (disconnect the Raspberry permission wire on GPIO2 while uploading).

**PC server**
1. Run `python "server/unified_python_server - UDP.py"` (needs opencv-python, numpy, pygame).
2. Power the car. It connects automatically.
3. Press button A on the gamepad to switch between manual and line following.

**Raspberry Pi**
1. Install a C++17 toolchain, OpenCV with GStreamer, the libcamera GStreamer plugin, ONNX Runtime and the lgpio development files.
2. Copy `firmware/raspberry/` to the Pi, set `ORT` to the ONNX Runtime folder and build:

```bash
g++ -std=c++17 -O2 -pthread main.cpp camera.cpp inference.cpp video_server.cpp servos.cpp aim_controller.cpp pump.cpp detection_filter.cpp car_control.cpp \
  -I"$ORT/include" \
  -L"$ORT/lib" -Wl,-rpath,"$ORT/lib" \
  $(pkg-config --cflags --libs opencv4) \
  -lonnxruntime -llgpio -o firenet
```

3. Run `./firenet` for video only, or `./firenet --aim PAN_HOME_US TILT_HOME_US PAN_DIRECTION TILT_DIRECTION --pump` for aiming and water. The ESP32 only moves when the Raspberry allows it, so video-only mode keeps the car stopped.

**Settings**

All Raspberry and training settings are in one file:
[`firmware/raspberry/settings.h`](firmware/raspberry/settings.h). The C++ program
includes it and the Python training tools read it, so they always agree. Each value has
a comment explaining its effect. `FAST_TEST_MODE` switches to faster, less cautious
timing for testing; check it before comparing behaviour. Line-following settings are at
the top of the PC server script.

**Training your own model**

The workflow is **record → annotate → train → check → export → copy to the Pi**. See
the [training guide](model_training/README.md).

**Network**
- The PC, ESP32 and Raspberry Pi must be on the same local network.
- Allow UDP ports 1883/1884 through the PC firewall. The Raspberry video uses TCP port 8080.
- Run only one PC server at a time.

## Tuning (important — read this)

The line follower is **not plug-and-play**. The values at the top of
`unified_python_server - UDP.py` must be adjusted to **your** floor, tape, lighting and car:

- **Tape and floor contrast.** Dark matte tape on a light, non-reflective floor works best. Reflections look almost white and confuse detection.
- **Threshold.** Adjust until the processed view shows a clean, solid line with no floor patches.
- **GAIN, DEADBAND.** Raise GAIN if the car does not turn enough; lower it if it snakes.
- **ROI height.** How far ahead the car looks.

**Battery note:** the minimum PWM duty a motor needs to turn is **higher on a low
battery**. Speed values may need adjusting as the battery drains.

## Known limitations

- **Turning radius.** Both L293Ds share direction pins, so the car cannot pivot in place. Very sharp curves can exceed its turning ability; the reverse-recovery state helps but does not fully solve this.
- **WiFi range.** The ESP32-CAM's on-board antenna is weak; an external antenna helps a lot.
- **Power.** Camera + WiFi draw current spikes; an undersized regulator can brown out and reset the chip. A large capacitor on the 5 V input helps.
- **False or missed detections.** Wet floors, bright objects, distance and blur can still cause mistakes. NMS removes duplicate boxes, not reflections elsewhere in the image.
- **Aiming calibration.** Moving the nozzle or camera invalidates the calibration table. The servos have no position feedback.
- **Communication checks.** The heartbeat checks control traffic, not video freshness. Commands have no delivery acknowledgement, and the permission wire is a static level rather than a watchdog signal.

## Repository layout

```
firmware/esp32-cam/main/  ESP32 firmware (main.ino) + config.h
firmware/raspberry/       FireNet inference, browser video, aiming, pump, settings.h
model_training/           FireNet annotation, training, evaluation and export
server/                   PC server: video + gamepad control + line following
hardware/                 wiring diagrams and 3D chassis (STL to print, F3D source)
docs/                     technical report, datasheets, photos and diagrams
```

## Demo

**Firefighting: line following, flame detection, aiming and water**

<div align="center">
  <video src="https://github.com/user-attachments/assets/553fcfee-71d8-45a7-b270-38277c7fac2c" width="420" controls></video>
</div>

The earlier videos of manual driving and line following are in
[esp32cam-rc-car](https://github.com/AfonsoTex/esp32cam-rc-car).
