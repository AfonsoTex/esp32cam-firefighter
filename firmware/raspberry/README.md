# Raspberry Pi — FireNet

This folder contains the Linux C++ program that captures the Raspberry Pi camera,
runs the FireNet model, draws detections, and streams the result to a web browser.
It can run from an SSH terminal without a monitor or graphical desktop.

The current program uses a camera connected to the Raspberry Pi through libcamera.
It does not receive the ESP32 camera's UDP stream. Servo aiming is optional and requires calibration. Pump control is enabled only when `--pump` is supplied with `--aim`.

## How to read the files

A `.h` file is a header: it declares the functions, classes, or constants that
other files can use. A `.cpp` file contains their implementation: the instructions
that actually do the work. `main.cpp` connects these parts into one program.

## File guide

| File | Purpose |
| --- | --- |
| `main.cpp` | Program entry point. Opens the camera, loads the model, starts the HTTP server, and repeatedly captures and processes frames. Converts raw predictions into boxes, draws confidence labels, encodes JPEG images, and publishes them. Defines the confidence threshold, model path and HTTP port. Handles Ctrl+C and shutdown. |
| `camera.h` | Declares the `Camera` class and its `abrir()` (open), `lerFrame()` (read frame), and `libertar()` (release) methods. Holds the OpenCV capture object. |
| `camera.cpp` | Implements camera capture. Requests NV12 at 640×480 and 30 fps through libcamera/GStreamer, converts it to BGR for OpenCV, rotates the capture by 180 degrees, and converts it to full-resolution RGB. `main.cpp` resizes a separate copy to 180×180 for the model. Releases the camera when finished. The current pipeline selects the default libcamera camera; the `index` argument is not used. |
| `inference.h` | Declares `carregarModelo()` (load model) and `prever()` (predict). Defines `Prediction`, which stores five raw output values for each grid cell: presence, horizontal and vertical offsets, width, and height. |
| `inference.cpp` | Loads the model with ONNX Runtime. Scales RGB values from 0–255 to 0–1 and places them into a `[1, 3, 180, 180]` tensor: one image, three channels, height, and width. Runs inference using the names `image` and `predictions`, then returns the 45×45 grid of raw predictions. `main.cpp` converts these values into displayed detections. |
| `firenet.onnx` | The exported neural network, including its trained weights. This is a model file, not C++ source. It is loaded at runtime and must match the input/output layout expected by `inference.cpp`. |
| `video_server.h` | Declares `VideoServer`: `start()` opens the server, `publish()` supplies the latest JPEG, and `stop()` shuts it down. Declares the shared frame storage and the synchronization objects used by background threads. |
| `video_server.cpp` | Implements the Linux HTTP server. Serves the HTML page at `/` and an MJPEG stream at `/stream.mjpg`. MJPEG is a sequence of JPEG images sent over one connection. The `PAGE` constant contains the HTML and CSS. Four worker threads handle browser connections, while a mutex protects shared image data and a condition variable wakes workers when a new frame arrives. |
| `pump.h` | Declares pump initialization, on/off control, and shutdown. Defines `GPIO_BOMBA` as GPIO 17. |
| `pump.cpp` | Implements the pump output using `lgpio`. Opens GPIO chip 0, claims GPIO 17 with an initial LOW level, and generates one timed HIGH pulse followed by LOW. The library times the pulse without blocking video capture. Cancels active pulses and turns the pump off before closing the controller. Enabled by `--pump`. |
| `aim_controller.h` | Declares `Detection`, the aiming states, and the controller interface. Lists the stored target, frame counters, and timers. |
| `aim_controller.cpp` | Implements target matching, 4-of-6 confirmation, proportional corrections, the center deadband, and the return to the search position. Tuning constants are grouped at the top. |
| `servos.h` | Declares servo initialization, pulse-width control, and shutdown. Defines pan/tilt GPIOs 18 and 19, a 20 ms period, and a 1000–2000 microsecond pulse range with a 1500 microsecond center. GPIO numbers are BCM numbers, not connector pin positions. |
| `servos.cpp` | Implements servo control through Linux PWM files under `/sys/class/pwm/pwmchip1`. Exports channels 2 and 3, sets their period and configured initial pulse, validates requested pulse widths, and disables pulses on shutdown. Assumes the matching PWM hardware configuration is already enabled. Called by `main.cpp` only when calibrated aiming is explicitly enabled. |
| `README.md` | This file: explains the modules and how to build and run the current program. |

## Image flow

```text
Camera → 640×480 RGB (orientation corrected)
           ├── resize copy to 180×180 → FireNet → detections → optional aiming
           └── convert to BGR → draw detections and aiming status → JPEG → browser
```

The browser retains the captured image detail. The model receives a separate reduced copy.

Closing the browser does not stop inference. The server keeps only the newest
JPEG instead of building a queue for slow clients. It supports four simultaneous
connections; each open video stream occupies one worker.

## Build on the Raspberry Pi

Requirements: a C++17 compiler, OpenCV with GStreamer support, libcamera's
GStreamer source, ONNX Runtime, and the lgpio library/development headers.
Set `ORT` to the ONNX Runtime installation
folder, containing `include/` and `lib/`. From this folder:

```bash
g++ -std=c++17 -O2 -pthread main.cpp camera.cpp inference.cpp video_server.cpp servos.cpp aim_controller.cpp pump.cpp \
  -I"$ORT/include" \
  -L"$ORT/lib" -Wl,-rpath,"$ORT/lib" \
  $(pkg-config --cflags --libs opencv4) \
  -lonnxruntime -llgpio -o firenet
```

`-pthread` enables thread support. This command builds the video/inference
program with optional servo and pump support. The generated
`firenet` file is the executable, while `firenet.onnx` is the model it loads.

## View the video

Keep `firenet.onnx` in the current working directory and run:

```bash
./firenet
```

From a PC on the same network, open `http://BaraoForrester.local:8080/`, or replace
the hostname with the Raspberry Pi's IP address. `HTTP_PORT` in `main.cpp`
selects the port. The server provides local-network HTTP access without
authentication and does not require an external hosting service.

Press Ctrl+C in the Raspberry terminal to stop. If the stream stops, check that
terminal for errors and reload the page after restarting the program. An occupied
HTTP port is reported as a startup error.

## Validation status

The HTTP server has been compiled and tested on Linux with synthetic frame data,
including concurrent streams, reconnects, error responses, and shutdown. Complete
camera capture, JPEG encoding, and ONNX inference together still need validation
on the Raspberry Pi with the real camera.

## Video resolution

The browser shows the original 640×480 capture, corrected for camera orientation, with detection boxes. Only a separate RGB copy is resized to 180×180 for the current model. Increasing the model input resolution requires coordinated training, export, and inference changes.

## Optional pan/tilt aiming

Without arguments, `./firenet` displays video without enabling PWM outputs.
To enable aiming, supply calibrated values:

```text
./firenet --aim PAN_HOME_US TILT_HOME_US PAN_DIRECTION TILT_DIRECTION
```

- Home pulses must be within 1000–2000 microseconds and point the camera towards the ground/search area. These values depend on the assembly; 1500 is not automatically the ground position.
- Directions are `1` or `-1`. Choose pan direction so a flame to the right moves towards the image center, and tilt direction so a flame below center moves towards the center. Calibrate after the camera's 180-degree image rotation.
- The existing PWM backend uses GPIO 18/19 and `/sys/class/pwm/pwmchip1/pwm2` and `pwm3`. Its hardware configuration and permissions must already work. Mechanical limits must permit the configured pulse range.
- Keep the car stationary for this first aiming test. This program does not send stop/resume commands to the ESP32 line-following server.

Adjust `AIM_HORIZONTAL_POSITION` in `aim_controller.cpp` to compensate for the nozzle's horizontal offset: 0 is the left edge, 0.5 is the center, and 1 is the right edge. Its current value is 0.51, with a tolerance of ±0.05. The vertical aiming position remains 0.5. Recompile after changing this value. Initial flame selection still uses the image center; servo corrections use the configured aiming point.

Behavior: select the flame nearest the image center, confirm it in 4 of the last 6 observations, then follow nearby detections of that target. Do not switch immediately to another flame when it disappears. Move at most 12 microseconds per axis and allow 250 ms to settle after each adjustment. Once centered within 5% of each image axis for five observations, keep holding the aim while the target remains detected; correct the aim again if it moves outside the deadband. There is no elapsed-time limit that ends tracking or holding a visible target. Five consecutive missing observations or more than one second without a match triggers a return to the calibrated home position. A processing gap longer than one second also triggers a return, as does reaching the servo limits when a correction is still needed. Wait two seconds at home before searching again. Missing detections do not prove that a flame has been extinguished.

Servo and pump outputs stop on normal exit or a reported capture/inference/control error. The controller and servo source were compiled and the controller tested with synthetic detections; physical direction, settling time, PWM setup, and complete camera/servo behavior still require testing on the Raspberry Pi. Nearby flames can be confused by position-only matching.

## Optional water pulse

Append `--pump` to the calibrated aiming command to enable GPIO 17 pump control:

```bash
./firenet --aim 1450 1400 -1 -1 --pump
```

The same selected flame must remain within the configured aiming deadband for
two seconds, with at least five centered observations. A missing or off-center
observation restarts that wait. The program then requests a one-second HIGH
pulse followed by LOW. `CENTERED_BEFORE_PUMP_SECONDS` in `aim_controller.cpp`
sets the waiting time; `PUMP_ON_MICROSECONDS` in `pump.h` sets the pulse duration.
The existing driver uses HIGH to turn the pump on and LOW to turn it off.

The lgpio library handles the timed pulse independently of frame processing.
If an observation loses the target or requires another servo correction, the
program cancels the pulse and writes LOW before moving. After the pulse ends
(or is cancelled), wait ten seconds before allowing another attempt. The target
must still be confirmed and continuously centered for two seconds when retrying.
The two-second centering wait can run during the ten-second cooldown; it does
not automatically add another two seconds afterwards. Video and tracking
continue throughout the cooldown.

Allow at most three pulses per target-selection cycle. Cancelled pulses also
count as attempts. Brief detection losses do not reset that count or bypass
the cooldown. After the third attempt, continue aiming but stop requesting
water for that cycle. `MAX_PUMP_ATTEMPTS` and `PUMP_RETRY_COOLDOWN_SECONDS` at the
top of `aim_controller.cpp` control the limit and cooldown. `main.cpp` reports
when the actual pulse finishes, so a delayed start or slow frame processing
cannot shorten the cooldown.

Returning home starts a new target-selection cycle and resets the attempt
count. This is a limit per tracking cycle, not guaranteed recognition of a
particular physical candle across separate cycles.

Without `--pump`, the program only aims and does not initialize the pump output.
The configured output is BCM GPIO 17 on GPIO chip 0, controlling the existing
pump driver. Physical GPIO selection, permissions, and pulse timing still need
verification on the Raspberry Pi with that driver. Synthetic tests verify the
controller timing and the lgpio calls; they do not exercise the physical pump.

### Reading the aiming code

Read `update()` in `aim_controller.cpp` first. It follows the cycle in order: finish returning home, wait for settling, find the target, confirm repeated detections, and calculate the next correction.

- `findMatchingFlame()` chooses near the image center initially, then near the last target position. Missing observations do not erase that position. Two nearby flames can still be confused; this is not guaranteed identity tracking.
- `rememberDetection()` stores the last six yes/no observations. Four matching detections confirm the target.
- `aimAtSelectedFlame()` subtracts the configured horizontal aiming point and vertical image center from the flame position. `calculatePulseChange()` converts each error into a servo correction. Inside `CENTER_DEADBAND`, it returns zero. Outside it, the correction grows with the error but is limited by `MAX_PULSE_STEP_US`.
- `handleMissingFlame()` tolerates brief detection gaps before returning home.
- `moveHome()` returns in small steps. The home tilt must be calibrated to face the ground.

The deadband is ±5% of image width and height around the configured aiming point (±9 pixels at 180×180). This avoids repeated corrections for small flame movements. Movement pauses do not contribute observations to the confirmation window.

The verified current setup exposes the GPIO PWM controller `1f00098000.pwm` as `pwmchip1`; `pwmchip0` is the fan controller. `sudo dtoverlay pwm-2chan` enabled GPIO 18/19 for the current boot only. Recheck the controller path after reboot or configuration changes before running the servos.
