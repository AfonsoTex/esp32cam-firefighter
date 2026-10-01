#include "camera.h"
#include "inferencia.h"
#include "video_server.h"

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

// The model output is a 32x32 grid.
constexpr int GRID_SIZE = 32;

// Minimum confidence (0..1) needed to draw a flame box.
constexpr float CONFIDENCE_THRESHOLD = 0.6;

// The browser shows the 128x128 frame enlarged to this size.
constexpr int DISPLAY_SIZE = 512;
constexpr unsigned short HTTP_PORT = 8080;

// Signal handlers only set a flag; normal code releases the resources.
volatile std::sig_atomic_t stopRequested = 0;

void requestStop(int)
{
    stopRequested = 1;
}

// Path to the trained model. Run the program from the folder that contains it.
const std::string MODEL_PATH = "firenet.onnx";

// One detected flame. All values are normalized (0..1) relative to the image.
struct Detection {
    float x;          // Box center, horizontal.
    float y;          // Box center, vertical.
    float width;
    float height;
    float confidence;
};


// Convert a raw model value into a probability between 0 and 1.
float sigmoid(float value)
{
    return 1.0f / (1.0f + std::exp(-value));
}


// Turn the 32x32 grid of raw predictions into a list of flame boxes.
// This follows the same rules as get_boxes() in treinar.py.
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


int main()
{
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

    cv::Mat frame;
    int exitCode = 0;

    // 3. Capture frames continuously and run the model on each one.
    try {
        while (!stopRequested) {

            // The frame is already 128x128 and RGB, as the model expects.
            if (!camera.lerFrame(frame)) {
                std::cerr << "Could not read another camera frame." << std::endl;
                exitCode = 1;
                break;
            }

            std::vector<Prediction> predictions;

            try {
                predictions = prever(frame);
            } catch (const std::exception& error) {
                std::cerr << "Inference error: " << error.what() << std::endl;
                exitCode = 1;
                break;
            }

            std::vector<Detection> detections = getDetections(predictions);

            // 4. OpenCV's JPEG encoder expects BGR, while the model receives RGB.
            cv::Mat display;
            cv::cvtColor(frame, display, cv::COLOR_RGB2BGR);
            cv::resize(display, display, cv::Size(DISPLAY_SIZE, DISPLAY_SIZE));

            drawDetections(display, detections);

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
    server.stop();
    camera.libertar();

    return exitCode;
}
