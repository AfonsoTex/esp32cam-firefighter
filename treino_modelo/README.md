# FireNet — Flame Detection

A small PyTorch neural network that detects and locates flames in images and live webcam video.

## Files

| File | Purpose |
| --- | --- |
| `extrair_frames.py` | Extract images from a video (one every 10 frames by default). |
| `anotar_imagens.py` | Draw flame boxes and save YOLO-format labels (class `0`). |
| `criar_cache.py` | Resize images to 128 × 128 and cache images and labels for faster training. |
| `dados.py` | Load data, encode labels, and create training batches. |
| `modelo.py` | Define the FireNet neural network. |
| `treinar.py` | Train for 30 epochs and save the best validation model; reuse saved weights if available. |
| `ver_resultados.py` | Save validation images with predictions in green and annotations in red. |
| `detetar_camera.py` | Detect flames using a webcam, with optional video recording. |
| `firenet_grid.pt` | Saved model weights and validation score. |
| `resultados/cache/*_images.npy` | Prepared training and validation images. |
| `resultados/cache/*_boxes.pkl` | Matching flame annotations. |
| `../.venv/` | Local Python environment; do not commit it to GitHub. |
| `Claude outputs/PWM_Raspberry_Pi_5.docx` | Supporting document; not required by the Python scripts. |

## Setup

Use Python 3.12. Run these commands in PowerShell from `treino_modelo/`:

```powershell
python -m venv ../.venv
..\.venv\Scripts\python.exe -m pip install torch==2.6.0 --index-url https://download.pytorch.org/whl/cpu
..\.venv\Scripts\python.exe -m pip install -r requirements-converter.txt
..\.venv\Scripts\python.exe -m pip install opencv-python==4.10.0.84 pillow
```

The commands below use the environment's Python directly, so activation is not required. Make sure all eight Python scripts listed above are present in this folder.

## Use the webcam

Place the trained `firenet_grid.pt` file in this folder, then run:

```powershell
..\.venv\Scripts\python.exe detetar_camera.py
```

Press **Q** or **Esc** to close. No training dataset or cache is needed for webcam detection when saved weights are available.

Optional settings:

```powershell
..\.venv\Scripts\python.exe detetar_camera.py --camera 0 --threshold 0.4 --gravar recording.mp4
```

Recording saves clean video by default. Add `--com-caixas` to include detection boxes and text.

## Train and evaluate

The code expects the dataset next to the project folder, in this layout:

```text
esp32cam-rc-car/
├── treino_modelo/         # Training scripts and firenet_grid.pt
├── .venv/                # Local Python environment
└── Datasets/
    └── Detecao_fogo/
        ├── train/
        │   ├── images/
        │   └── labels/
        └── valid/
            ├── images/
            └── labels/
```

Use `.jpg`, `.jpeg`, or `.png` images. Each label file must have the same base name as its image, with a `.txt` extension. Each flame uses one line:

```text
0 center_x center_y width height
```

Coordinates and dimensions are normalized to the image size (0 to 1). Use an empty label file for an image without flames. Keep training and validation data separate; avoid splitting near-identical frames from the same recording between them.

Build the cache, train, and inspect predictions:

```powershell
..\.venv\Scripts\python.exe criar_cache.py
..\.venv\Scripts\python.exe treinar.py
..\.venv\Scripts\python.exe ver_resultados.py
```

Rebuild the cache whenever images, labels, or `IMAGE_SIZE` change. Training saves the best model to `firenet_grid.pt`. Visual validation results are saved to `resultados/previsoes_valid/`.

## Prepare new images from a video

```powershell
..\.venv\Scripts\python.exe extrair_frames.py recording.mp4 --cada 10
..\.venv\Scripts\python.exe anotar_imagens.py resultados/frames/recording_frames
```

Draw a box around each flame, or select **Sem chama** for an image without flames. Move the images and generated labels into the appropriate dataset folders, then rebuild the cache before training.

## GitHub

Commit the scripts and this README. Include the trained `.pt` file if others should be able to try webcam detection immediately. Exclude `.venv/`, `__pycache__/`, caches, and generated images/videos.

## Export the model

From the repository root, run:

```powershell
.\.venv\Scripts\python.exe treino_modelo/model_converter.py
```

The converter reads `treino_modelo/firenet_grid.pt` and writes `treino_modelo/firenet.onnx`, independently of the current working directory. Copy the exported ONNX file to the Raspberry when deploying the inference application.

Training tools live here; Raspberry inference code stays in `firmware/raspberry/`, and the PC control server stays in `server/`.
