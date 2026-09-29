#include "inferencia.h"
#include <onnxruntime_cxx_api.h>
#include <array>

// Create the ONNX Runtime environment and default session options.
Ort::Env env(ORT_LOGGING_LEVEL_WARNING, "FireNet");
Ort::SessionOptions options;

// Keep the session available between function calls.
// Initially, no model is loaded.
Ort::Session session{nullptr};


void carregarModelo(const std::string& caminho)
{
    // Load the network structure and trained weights from the ONNX file.
    session = Ort::Session(env, caminho.c_str(), options);
}


std::vector<Prediction> prever(const cv::Mat& frame)
{
    cv::Mat normalized;

    // Convert all channel values to float and scale from 0..255 to 0..1.
    frame.convertTo(normalized, CV_32F, 1.0 / 255.0);

    // Allocate space for 3 channels of 128x128 float values.
    std::vector<float> input(3 * 128 * 128);

    for (int y = 0; y < 128; y++) {
        for (int x = 0; x < 128; x++) {

            // Read the RGB values of the current pixel.
            cv::Vec3f pixel = normalized.at<cv::Vec3f>(y, x);

            // Convert the 2D pixel position (y = row, x = column) into a 1D index.
            // y * 128 skips all complete rows before the current row, and + x moves to the current column.
            // 0 * 128 * 128 means we are storing the value in channel 0, the R channel.
            input[0 * 128 * 128 + y * 128 + x] = pixel[0];

            // Store the G value in the second channel block.
            input[1 * 128 * 128 + y * 128 + x] = pixel[1];

            // Store the B value in the third channel block.
            input[2 * 128 * 128 + y * 128 + x] = pixel[2];
        }
    }

    // ONNX Runtime expects tensor dimensions as int64_t,
    // and the 4 values represent batch, channels, height and width.
    // input contains the image values, while inputShape describes how those values are organized.
    std::array<int64_t, 4> inputShape = {1, 3, 128, 128};

    // Describe the memory type that will be used for the tensor data.
    // The actual input data address will be provided later with input.data().
    Ort::MemoryInfo memoryInfo =
        Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator,
            OrtMemTypeDefault
        );

    // Create the ONNX tensor using the input values and their shape.
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(
            memoryInfo,
            input.data(),
            input.size(),
            inputShape.data(),
            inputShape.size()
        );

    // The model may have multiple inputs, so the input name tells ONNX Runtime
    // which model input should receive inputTensor.
    // The same applies to outputs: the output name selects which model output we want to read.
    const char* inputNames[] = {"image"};
    const char* outputNames[] = {"predictions"};

    // Create the ONNX tensor using:
    // - memoryInfo: describes the type of memory used by the tensor.
    // - input.data(): gives the address of the first input float in memory.
    // - input.size(): gives the total number of input floats.
    // - inputShape.data(): gives the address of the first shape value.
    //   CreateTensor expects the shape values as one continuous block of memory,
    //   so it starts at this address and reads the dimensions from there.
    // - inputShape.size(): tells CreateTensor how many shape values to read.
    Ort::Value inputTensor =
        Ort::Value::CreateTensor<float>(
            memoryInfo,
            input.data(),
            input.size(),
            inputShape.data(),
            inputShape.size()
        );

    // Get direct access to the float values inside the output tensor.
    float* outputData =
        outputs[0].GetTensorMutableData<float>();

    // Create one Prediction for each cell of the 32x32 output grid.
    std::vector<Prediction> predictions(32 * 32);

    for (int row = 0; row < 32; row++) {
        for (int column = 0; column < 32; column++) {

            // Convert the 2D grid position into a 1D index.
            int index = row * 32 + column;

            // Group the 5 values belonging to this grid cell.
            predictions[index].presence =
                outputData[0 * 32 * 32 + index];

            predictions[index].xOffset =
                outputData[1 * 32 * 32 + index];

            predictions[index].yOffset =
                outputData[2 * 32 * 32 + index];

            predictions[index].width =
                outputData[3 * 32 * 32 + index];

            predictions[index].height =
                outputData[4 * 32 * 32 + index];
        }
    }

    return predictions;
}