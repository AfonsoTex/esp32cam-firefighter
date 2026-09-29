from pathlib import Path

import torch
from modelo import FireNet

# Resolve model files relative to this script, regardless of the working directory.
model_directory = Path(__file__).resolve().parent
output_path = model_directory / "firenet.onnx"

# Create the network using the architecture defined in modelo.py.
model = FireNet()

# Load the saved checkpoint onto the CPU.
checkpoint = torch.load(
    model_directory / "firenet_grid.pt",
    map_location="cpu",
    weights_only=True,
)

# Replace the initial parameters with the trained weights and biases.
model.load_state_dict(checkpoint["weights"])

# Set the network to evaluation mode.
model.eval()

# Create an example input: 1 RGB image, 128 pixels high and wide.
# This defines the input shape; it does not train the network.
example = torch.zeros(1, 3, 128, 128)

# Export the network operations and trained parameters into one ONNX file.
torch.onnx.export(
    model,
    example,
    output_path,
    input_names=["image"],           # Name of the model input.
    output_names=["predictions"],    # Name of the model output.
    opset_version=17,                # ONNX operator set version.
    dynamo=False,                    # Use the classic PyTorch exporter.
)

print(f"ONNX model saved to: {output_path}")
