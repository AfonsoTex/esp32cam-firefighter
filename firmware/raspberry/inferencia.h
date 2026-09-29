#pragma once

#include <string>
#include <vector>
#include <opencv2/core.hpp>

// Stores the 5 values predicted for one cell of the 32x32 output grid.
struct Prediction {
    float presence;
    float xOffset;
    float yOffset;
    float width;
    float height;
};


// Load the ONNX model and keep it in memory for future predictions.
// Call once before prever(). Throws an exception if loading fails.
// caminho is the path to the model file, for example "firenet.onnx".
void carregarModelo(const std::string& caminho);

// Receive a frame, prepare it, run the model, and return detections.
// The input must be a non-empty, 8-bit BGR image with three channels.
// The frame is passed by reference and must not be modified.
// Return an empty vector when no flames are detected.
// Throws an exception if the model is not loaded or inference fails.
std::vector<float> prever(const cv::Mat& frame);