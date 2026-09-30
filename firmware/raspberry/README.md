# Raspberry Pi — FireNet

This folder contains the Linux C++ program that captures the Raspberry Pi camera,
runs the FireNet model, draws detections, and streams the result to a web browser.
It can run from an SSH terminal without a monitor or graphical desktop.

The current program uses a camera connected to the Raspberry Pi through libcamera.
It does not receive the ESP32 camera's UDP stream. Pump and servo modules exist
in this folder, but the current `main.cpp` does not call them.

## How to read the files

A `.h` file is a header: it declares the functions, classes, or constants that
other files can use. A `.cpp` file contains their implementation: the instructions
that actually do the work. `main.cpp` connects these parts into one program.

## File guide

| File | Purpose |
| --- | --- |
| `main.cpp` | Program entry point. Opens the camera, loads the model, starts the HTTP server, and repeatedly captures and processes frames. Converts raw predictions into boxes, draws confidence labels, encodes JPEG images, and publishes them. Defines the confidence threshold, model path, display size, and HTTP port. Handles Ctrl+C and shutdown. |
| `camera.h` | Declares the `Camera` class and its `abrir()` (open), `lerFrame()` (read frame), and `libertar()` (release) methods. Holds the OpenCV capture object. |
| `camera.cpp` | Implements camera capture. Requests NV12 at 640×480 and 30 fps through libcamera/GStreamer, converts it to BGR for OpenCV, resizes each frame to 128×128, and converts it to RGB for the model. Releases the camera when finished. The current pipeline selects the default libcamera camera; the `index` argument is not used. |
| `inferencia.h` | Declares `carregarModelo()` (load model) and `prever()` (predict). Defines `Prediction`, which stores five raw output values for each grid cell: presence, horizontal and vertical offsets, width, and height. |
| `inferencia.cpp` | Loads the model with ONNX Runtime. Scales RGB values from 0–255 to 0–1 and places them into a `[1, 3, 128, 128]` tensor: one image, three channels, height, and width. Runs inference using the names `image` and `predictions`, then returns the 32×32 grid of raw predictions. `main.cpp` converts these values into displayed detections. |
| `firenet.onnx` | The exported neural network, including its trained weights. This is a model file, not C++ source. It is loaded at runtime and must match the input/output layout expected by `inferencia.cpp`. |
| `video_server.h` | Declares `VideoServer`: `start()` opens the server, `publish()` supplies the latest JPEG, and `stop()` shuts it down. Declares the shared frame storage and the synchronization objects used by background threads. |
| `video_server.cpp` | Implements the Linux HTTP server. Serves the HTML page at `/` and an MJPEG stream at `/stream.mjpg`. MJPEG is a sequence of JPEG images sent over one connection. The `PAGE` constant contains the HTML and CSS. Four worker threads handle browser connections, while a mutex protects shared image data and a condition variable wakes workers when a new frame arrives. |
| `bomba.h` | Declares pump initialization, on/off control, and shutdown. Defines `GPIO_BOMBA` as GPIO 17. `bomba` means pump. |
| `bomba.cpp` | Implements the pump output using `lgpio`. Opens GPIO chip 0, claims the pump pin with an initial LOW level, writes HIGH or LOW for on/off, and turns it off before closing the controller. Not used by the current video/inference program. |
| `servos.h` | Declares servo initialization, pulse-width control, and shutdown. Defines pan/tilt GPIOs 18 and 19, a 20 ms period, and a 1000–2000 microsecond pulse range with a 1500 microsecond center. GPIO numbers are BCM numbers, not connector pin positions. |
| `servos.cpp` | Implements servo control through Linux PWM files under `/sys/class/pwm/pwmchip0`. Exports channels 2 and 3, sets their period and initial center pulse, validates requested pulse widths, and disables pulses on shutdown. Assumes the matching PWM hardware configuration is already enabled. Not called by the current `main.cpp`. |
| `README.md` | This file: explains the modules and how to build and run the current program. |

## Image flow

```text
Raspberry camera → NV12 → BGR → resize to 128×128 → RGB
                                                    ↓
                                                 FireNet
                                                    ↓
RGB frame → BGR → enlarge to 512×512 → draw detections → JPEG → browser
```

RGB and BGR contain the same three color channels in different orders. The model
receives RGB; OpenCV's JPEG encoder expects BGR. Enlarging the image to 512×512
makes it easier to see but does not recover detail lost at 128×128.

Closing the browser does not stop inference. The server keeps only the newest
JPEG instead of building a queue for slow clients. It supports four simultaneous
connections; each open video stream occupies one worker.

## Build on the Raspberry Pi

Requirements: a C++17 compiler, OpenCV with GStreamer support, libcamera's
GStreamer source, and ONNX Runtime. Set `ORT` to the ONNX Runtime installation
folder, containing `include/` and `lib/`. From this folder:

```bash
g++ -std=c++17 -O2 -pthread main.cpp camera.cpp inferencia.cpp video_server.cpp \
  -I"$ORT/include" \
  -L"$ORT/lib" -Wl,-rpath,"$ORT/lib" \
  $(pkg-config --cflags --libs opencv4) \
  -lonnxruntime -o firenet
```

`-pthread` enables thread support. This command builds the video/inference
program only; it does not compile the pump or servo modules. The generated
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
