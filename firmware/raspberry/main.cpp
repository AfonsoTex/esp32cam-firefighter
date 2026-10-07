#include "camera.h"
#include "inference.h"
#include "video_server.h"
#include "aim_controller.h"
#include "servos.h"
#include "pump.h"
#include <chrono>
#include <stdexcept>

#include <opencv2/opencv.hpp>

#include <algorithm>
#include <cmath>
#include <csignal>
#include <exception>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

// Minimum confidence (0..1) needed to draw a flame box.
constexpr float CONFIDENCE_THRESHOLD = 0.60f;

constexpr unsigned short HTTP_PORT = 8080;

// Signal handlers only set a flag; normal code releases the resources.
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int)
{
    stopRequested = 1;
}

// Use one monotonic clock for aiming and cooldowns; system clock changes cannot affect it.
double currentTimeSeconds()
{
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

// Path to the trained model. Run the program from the folder that contains it.
const std::string MODEL_PATH = "firenet.onnx";

// Convert a raw model value into a probability between 0 and 1.
float sigmoid(float value)
{
    return 1.0f / (1.0f + std::exp(-value));
}


// Turn the 45x45 grid of raw predictions into a list of flame boxes.
// This follows the same rules as get_boxes() in train.py.
std::vector<Detection> getDetections(const std::vector<Prediction>& predictions)
{
    std::vector<Detection> detections;

    for (int row = 0; row < GRID_SIZE; row++) {
        for (int column = 0; column < GRID_SIZE; column++) {

            const Prediction& prediction = predictions[row * GRID_SIZE + column];

            // The presence value is a logit, so it needs a sigmoid to become a confidence.
            float confidence = sigmoid(prediction.presence);

            if (confidence <= CONFIDENCE_THRESHOLD) {
                continue;
            }

            // Add the cell position to its predicted offset to recover the image position.
            float cellColumn = static_cast<float>(column);
            float cellRow = static_cast<float>(row);
            float gridSize = static_cast<float>(GRID_SIZE);

            float x = (cellColumn + 0.5f + prediction.xOffset) / gridSize;
            float y = (cellRow + 0.5f + prediction.yOffset) / gridSize;

            // Width and height are also logits, so they need a sigmoid too.
            float width = sigmoid(prediction.width);
            float height = sigmoid(prediction.height);

            // Ignore boxes whose center falls outside the image.
            if (x < 0.0f || x > 1.0f || y < 0.0f || y > 1.0f) {
                continue;
            }

            Detection detection;
            detection.x = x;
            detection.y = y;
            detection.width = width;
            detection.height = height;
            detection.confidence = confidence;

            detections.push_back(detection);
        }
    }

    return detections;
}


// Draw one rectangle and one "fire 0.87" label for each detection.
void drawDetections(cv::Mat& image, const std::vector<Detection>& detections)
{
    int imageWidth = image.cols;
    int imageHeight = image.rows;

    float imageWidthFloat = static_cast<float>(imageWidth);
    float imageHeightFloat = static_cast<float>(imageHeight);

    for (const Detection& detection : detections) {

        // Boxes are normalized, so multiply by the real size of the image being drawn.
        int xMin = static_cast<int>((detection.x - detection.width / 2.0f) * imageWidthFloat);
        int yMin = static_cast<int>((detection.y - detection.height / 2.0f) * imageHeightFloat);
        int xMax = static_cast<int>((detection.x + detection.width / 2.0f) * imageWidthFloat);
        int yMax = static_cast<int>((detection.y + detection.height / 2.0f) * imageHeightFloat);

        // Keep the box inside the image.
        xMin = std::max(0, xMin);
        yMin = std::max(0, yMin);
        xMax = std::min(imageWidth - 1, xMax);
        yMax = std::min(imageHeight - 1, yMax);

        cv::rectangle(image, cv::Point(xMin, yMin), cv::Point(xMax, yMax),
                      cv::Scalar(0, 255, 0), 2);

        // Build the label text with two decimal places, for example "fire 0.87".
        std::ostringstream label;
        label << "fire " << std::fixed << std::setprecision(2) << detection.confidence;

        // Put the label above the box, but never above the top of the image.
        int labelY = std::max(15, yMin - 8);

        cv::putText(image, label.str(), cv::Point(xMin, labelY),
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 0), 2);
    }
}


// Reject incomplete values such as "1500abc" before enabling movement.
int readInteger(const char* text)
{
    std::size_t end;
    int value = std::stoi(text, &end);
    if (text[end] != '\0') {
        throw std::invalid_argument("Invalid integer");
    }
    return value;
}


int main(int argc, char* argv[])
{
    // Movement requires explicit calibrated home pulses and axis directions.
    bool aimEnabled = false;
    bool pumpEnabled = false;
    int homePan = 1500, homeTilt = 1500, panDirection = 1, tiltDirection = 1;
    if (argc != 1) {
        if ((argc != 6 && argc != 7) || std::string(argv[1]) != "--aim") {
            std::cerr << "Usage: ./firenet [--aim PAN_HOME_US TILT_HOME_US PAN_DIRECTION TILT_DIRECTION [--pump]]\n"
                      << "Directions must be +1 or -1. Calibrate the ground-facing home position first.\n";
            return 1;
        }
        try {
            if (argc == 7) {
                if (std::string(argv[6]) != "--pump") {
                    throw std::invalid_argument("The optional final argument must be --pump.");
                }
                pumpEnabled = true;
            }
            homePan = readInteger(argv[2]);
            homeTilt = readInteger(argv[3]);
            panDirection = readInteger(argv[4]);
            tiltDirection = readInteger(argv[5]);
            if (homePan < PULSO_MIN_US || homePan > PULSO_MAX_US ||
                homeTilt < PULSO_MIN_US || homeTilt > PULSO_MAX_US ||
                std::abs(panDirection) != 1 || std::abs(tiltDirection) != 1)
                throw std::invalid_argument("Home pulses must be 1000..2000 us; directions must be +1 or -1.");
            aimEnabled = true;
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            return 1;
        }
    }
    AimController aim(homePan, homeTilt, panDirection, tiltDirection);
    std::signal(SIGINT, requestStop);
    std::signal(SIGTERM, requestStop);

    // 1. Open the camera.
    Camera camera;

    if (!camera.abrir()) {
        return 1;
    }

    // 2. Load the model.
    try {
        carregarModelo(MODEL_PATH);
    } catch (const std::exception& error) {
        std::cerr << "Could not load model " << MODEL_PATH << ": "
                  << error.what() << std::endl;
        camera.libertar();
        return 1;
    }

    VideoServer server;
    if (!server.start(HTTP_PORT)) {
        std::cerr << "Could not start the HTTP server on port " << HTTP_PORT << std::endl;
        return 1;
    }

    std::cout << "Video available at http://BaraoForrester.local:" << HTTP_PORT
              << "/ (or use the Raspberry Pi IP address). Press Ctrl+C to exit." << std::endl;

    if (aimEnabled && (!inicializarServos(homePan, homeTilt))) {
        std::cerr << "Could not initialize servos. Check PWM configuration and permissions.\n";
        terminarServos();
        return 1;
    }
    if (aimEnabled) {
        std::cout << "Aiming enabled: keep the car stationary during this test.\n";
    } else {
        std::cout << "Video only. Use --aim with calibrated values to enable servo movement.\n";
    }

    // Claim the pump output as LOW before processing any target.
    if (pumpEnabled) {
        if (!inicializarBomba()) {
            std::cerr << "Could not initialize the pump on GPIO 17.\n";
            terminarBomba();
            terminarServos();
            return 1;
        }
        std::cout << "Pump enabled: 1-second pulses after centering, with limited retries and a cooldown.\n";
    }
    // Remember an outstanding pulse until lgpio reports that it has finished.
    bool pumpPulsePending = false;
    cv::Mat frame;
    int exitCode = 0;

    // 3. Capture frames continuously and run the model on each one.
    try {
        while (!stopRequested) {

            // Capture a full-resolution RGB frame for inference and display.
            if (!camera.lerFrame(frame)) {
                std::cerr << "Could not read another camera frame." << std::endl;
                exitCode = 1;
                break;
            }

            std::vector<Prediction> predictions;

            try {
                cv::Mat modelFrame;
                cv::resize(frame, modelFrame, cv::Size(MODEL_INPUT_SIZE, MODEL_INPUT_SIZE));
                predictions = prever(modelFrame);
            } catch (const std::exception& error) {
                std::cerr << "Inference error: " << error.what() << std::endl;
                exitCode = 1;
                break;
            }

            std::vector<Detection> detections = getDetections(predictions);

            if (aimEnabled) {
                const double now = currentTimeSeconds();

                // Start the retry cooldown after the real pulse ends, not when it starts.
                if (pumpEnabled && pumpPulsePending && !pumpIsRunning()) {
                    aim.pumpPulseFinished(currentTimeSeconds());
                    pumpPulsePending = false;
                }

                bool positionChanged = aim.update(detections, now);

                // Stop spraying before moving the servos, or if the target disappears.
                if (pumpEnabled && aim.state != AimController::State::Hold) {
                    if (!definirBomba(false)) {
                        throw std::runtime_error("Could not stop the pump.");
                    }
                    if (pumpPulsePending) {
                        aim.pumpPulseFinished(currentTimeSeconds());
                        pumpPulsePending = false;
                    }
                }
                if (positionChanged) {
                    bool panMoved = definirPulso(GPIO_PAN, aim.pan);
                    bool tiltMoved = definirPulso(GPIO_TILT, aim.tilt);
                    if (!panMoved || !tiltMoved) {
                        throw std::runtime_error("Servo command failed; stopping aiming.");
                    }
                }

                // The controller enforces the attempt limit and retry cooldown.
                // lgpio turns the pump off after one second without pausing capture.
                if (pumpEnabled && aim.pumpRequested) {
                    if (!startPumpPulse()) {
                        throw std::runtime_error("Could not start the pump pulse.");
                    }
                    pumpPulsePending = true;
                    std::cout << "Water pulse started: 1 second.\n";
                }
            }

            // 4. OpenCV's JPEG encoder expects BGR, while the model receives RGB.
            cv::Mat display;
            cv::cvtColor(frame, display, cv::COLOR_RGB2BGR);

            drawDetections(display, detections);
            std::string aimingStatus = "Aiming disabled";
            if (aimEnabled) {
                aimingStatus = aim.status();
            }
            if (pumpEnabled && pumpIsRunning()) {
                aimingStatus = "Spraying (1 second)";
            }
            cv::putText(display, aimingStatus, cv::Point(10, 25),
                        cv::FONT_HERSHEY_SIMPLEX, 0.6, cv::Scalar(0, 255, 255), 2);

            // 5. Publish a JPEG for the browser, without opening a desktop window.
            std::vector<unsigned char> jpeg;
            if (!cv::imencode(".jpg", display, jpeg, {cv::IMWRITE_JPEG_QUALITY, 80})) {
                std::cerr << "Could not encode the JPEG image." << std::endl;
                exitCode = 1;
                break;
            }
            server.publish(jpeg);
        }
    } catch (const std::exception& error) {
        std::cerr << "Video processing error: " << error.what() << std::endl;
        exitCode = 1;
    }

    // Stop browser connections and release the camera.
    if (pumpEnabled) terminarBomba();
    if (aimEnabled) terminarServos();
    server.stop();
    camera.libertar();

    return exitCode;
}
