#include "inference.h"
#include <onnxruntime_cxx_api.h>
#include <array>
#include <stdexcept>

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
    const auto inputShape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    const auto outputShape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    if (inputShape != std::vector<int64_t>{1, 3, MODEL_INPUT_SIZE, MODEL_INPUT_SIZE} ||
        outputShape != std::vector<int64_t>{1, 5, GRID_SIZE, GRID_SIZE}) {
        throw std::runtime_error("Model dimensions do not match. Export and copy the 180x180 ONNX model.");
    }
}


std::vector<Prediction> prever(const cv::Mat& frame)
{
    if (frame.rows != MODEL_INPUT_SIZE || frame.cols != MODEL_INPUT_SIZE || frame.type() != CV_8UC3) {
        throw std::runtime_error("Expected a 180x180 RGB image.");
    }
    cv::Mat normalized;

    // Convert all channel values to float and scale from 0..255 to 0..1.
    frame.convertTo(normalized, CV_32F, 1.0 / 255.0);

    // Allocate space for 3 channels of MODEL_INPUT_SIZE x MODEL_INPUT_SIZE float values.
    std::vector<float> input(3 * MODEL_INPUT_SIZE * MODEL_INPUT_SIZE);

    for (int y = 0; y < MODEL_INPUT_SIZE; y++) {
        for (int x = 0; x < MODEL_INPUT_SIZE; x++) {

            // Read the RGB values of the current pixel.
            cv::Vec3f pixel = normalized.at<cv::Vec3f>(y, x);

            // Convert the 2D pixel position (y = row, x = column) into a 1D index.
            // y * MODEL_INPUT_SIZE skips all complete rows before the current row, and + x moves to the current column.
            // 0 * MODEL_INPUT_SIZE * MODEL_INPUT_SIZE means we are storing the value in channel 0, the R channel.
            input[0 * MODEL_INPUT_SIZE * MODEL_INPUT_SIZE + y * MODEL_INPUT_SIZE + x] = pixel[0];

            // Store the G value in the second channel block.
            input[1 * MODEL_INPUT_SIZE * MODEL_INPUT_SIZE + y * MODEL_INPUT_SIZE + x] = pixel[1];

            // Store the B value in the third channel block.
            input[2 * MODEL_INPUT_SIZE * MODEL_INPUT_SIZE + y * MODEL_INPUT_SIZE + x] = pixel[2];
        }
    }

    // ONNX Runtime expects tensor dimensions as int64_t,
    // and the 4 values represent batch, channels, height and width.
    // input contains the image values, while inputShape describes how those values are organized.
    std::array<int64_t, 4> inputShape = {1, 3, MODEL_INPUT_SIZE, MODEL_INPUT_SIZE};

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

    // Run the model with the input tensor and get the output tensor back.
    // Ort::RunOptions{nullptr} means default run options.
    // The arguments are: input names, input tensors, number of inputs,
    // output names and number of outputs we want to receive.
    // The result is one Ort::Value for each requested output.
    std::vector<Ort::Value> outputs =
        session.Run(
            Ort::RunOptions{nullptr},
            inputNames,
            &inputTensor,
            1,
            outputNames,
            1
        );

    // Get direct access to the float values inside the output tensor.
    float* outputData =
        outputs[0].GetTensorMutableData<float>();

    // Create one Prediction for each cell of the GRID_SIZE x GRID_SIZE output grid.
    std::vector<Prediction> predictions(GRID_SIZE * GRID_SIZE);

    for (int row = 0; row < GRID_SIZE; row++) {
        for (int column = 0; column < GRID_SIZE; column++) {

            // Convert the 2D grid position into a 1D index.
            int index = row * GRID_SIZE + column;

            // Group the 5 values belonging to this grid cell.
            predictions[index].presence =
                outputData[0 * GRID_SIZE * GRID_SIZE + index];

            predictions[index].xOffset =
                outputData[1 * GRID_SIZE * GRID_SIZE + index];

            predictions[index].yOffset =
                outputData[2 * GRID_SIZE * GRID_SIZE + index];

            predictions[index].width =
                outputData[3 * GRID_SIZE * GRID_SIZE + index];

            predictions[index].height =
                outputData[4 * GRID_SIZE * GRID_SIZE + index];
        }
    }

    return predictions;
}