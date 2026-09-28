# FireNet — Flame Detection

A small PyTorch neural network that detects and locates flames in images and live webcam video.

## Files

| File | Purpose |
| --- | --- |
| `scripts/extrair_frames.py` | Extract images from a video (one every 10 frames by default). |
| `scripts/anotar_imagens.py` | Draw flame boxes and save YOLO-format labels (class `0`). |
| `scripts/criar_cache.py` | Resize images to 128 × 128 and cache images and labels for faster training. |
| `scripts/dados.py` | Load data, encode labels, and create training batches. |
| `scripts/modelo.py` | Define the FireNet neural network. |
| `scripts/treinar.py` | Train for 30 epochs and save the best validation model; reuse saved weights if available. |
| `scripts/ver_resultados.py` | Save validation images with predictions in green and annotations in red. |
| `scripts/detetar_camera.py` | Detect flames using a webcam, with optional video recording. |
| `resultados/modelos/firenet_grid.pt` | Saved model weights and validation score. |
| `resultados/cache/*_images.npy` | Prepared training and validation images. |
| `resultados/cache/*_boxes.pkl` | Matching flame annotations. |
| `venv-treino/` | Local Python environment; do not commit it to GitHub. |
| `Claude outputs/PWM_Raspberry_Pi_5.docx` | Supporting document; not required by the Python scripts. |

## Setup

Use Python 3.12. Run these commands in PowerShell from the project root (the folder containing `scripts/`):

```powershell
python -m venv venv-treino
.\venv-treino\Scripts\python.exe -m pip install torch numpy opencv-python pillow
New-Item -ItemType Directory -Force resultados/modelos | Out-Null
```

The commands below use the environment's Python directly, so activation is not required. Make sure all eight Python scripts listed above are present in `scripts/`.

## Use the webcam

Place the trained `firenet_grid.pt` file in `resultados/modelos/`, then run:

```powershell
.\venv-treino\Scripts\python.exe scripts/detetar_camera.py
```

Press **Q** or **Esc** to close. No training dataset or cache is needed for webcam detection when saved weights are available.

Optional settings:

```powershell
.\venv-treino\Scripts\python.exe scripts/detetar_camera.py --camera 0 --threshold 0.4 --gravar recording.mp4
```

Recording saves clean video by default. Add `--com-caixas` to include detection boxes and text.

## Train and evaluate

The code expects the dataset next to the project folder, in this layout:

```text
IA/
├── code/                  # Project root
│   ├── scripts/
│   └── resultados/
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
.\venv-treino\Scripts\python.exe scripts/criar_cache.py
.\venv-treino\Scripts\python.exe scripts/treinar.py
.\venv-treino\Scripts\python.exe scripts/ver_resultados.py
```

Rebuild the cache whenever images, labels, or `IMAGE_SIZE` change. Training saves the best model to `resultados/modelos/firenet_grid.pt`. Visual validation results are saved to `resultados/previsoes_valid/`.

## Prepare new images from a video

```powershell
.\venv-treino\Scripts\python.exe scripts/extrair_frames.py recording.mp4 --cada 10
.\venv-treino\Scripts\python.exe scripts/anotar_imagens.py resultados/frames/recording_frames
```

Draw a box around each flame, or select **Sem chama** for an image without flames. Move the images and generated labels into the appropriate dataset folders, then rebuild the cache before training.

## GitHub

Commit the scripts and this README. Include the trained `.pt` file if others should be able to try webcam detection immediately. Exclude `venv-treino/`, `__pycache__/`, caches, and generated images/videos.
