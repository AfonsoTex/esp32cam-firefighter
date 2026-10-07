#pragma once

#include <string>

// Must match IMAGE_SIZE in model_training/data.py and the exported ONNX model.
inline constexpr int MODEL_INPUT_SIZE = 180;
inline constexpr int GRID_SIZE = MODEL_INPUT_SIZE / 4;
#include <vector>
#include <opencv2/core.hpp>

// Stores the 5 values predicted for one cell of the 45x45 output grid.
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

// Receive a frame, prepare it, run the model, and return the raw grid predictions.
// The input must be a 180x180, 8-bit RGB image with three channels (prepared by main.cpp).
// The frame is passed by reference and must not be modified.
// Always returns GRID_SIZE * GRID_SIZE predictions, one per grid cell, in row-major order.
// The values are raw model outputs: presence, width and height still need a sigmoid.
// Throws an exception if the model is not loaded or inference fails.
std::vector<Prediction> prever(const cv::Mat& frame);