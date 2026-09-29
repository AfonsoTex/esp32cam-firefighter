# FireNet — training tools

This folder contains the tools to prepare images, annotate flames, train FireNet, and export the model for the Raspberry Pi.

## Tools

| File | Purpose |
| --- | --- |
| `anotar_imagens.py` | Opens a video, extracts images, lets you annotate them, and sends images and labels to the dataset when you finalize the session. Also lets you resume a session. |
| `treinar.py` | Updates the cache automatically, trains for 30 epochs, and saves the best model to `firenet_grid.pt`. Reuses existing weights when available. |
| `ver_resultados.py` | Reports validation performance and saves images with predictions in green and annotations in red to `resultados/previsoes_valid/`. |
| `detetar_camera.py` | Shows live webcam predictions using the PyTorch model. Can record video with `--gravar`. |
| `model_converter.py` | Converts `firenet_grid.pt` to `firenet.onnx` for inference on the Raspberry Pi. |
| `extrair_frames.py` | Extracts images from a video separately. Optional: the annotator already calls this tool. |
| `criar_cache.py` | Prepares 128×128 images and labels to speed up training. Optional: training updates the cache automatically. |
| `modelo.py` | Defines the FireNet architecture; used by the other scripts. |
| `dados.py` | Reads the dataset and prepares training batches; used by the other scripts. |
| `requirements-converter.txt` | Lists the pinned conversion dependencies. |

## Initial setup on Windows

Use Python 3.12. Run these commands **from the root of the `esp32cam-firefighter` repository**:

```powershell
python -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r treino_modelo/requirements-converter.txt
.\.venv\Scripts\python.exe -m pip install opencv-python==4.10.0.84 pillow
```

All commands below also run from the repository root. They use the environment's Python directly, so activation is not required.

## Normal workflow: annotate → train → export

**1. Copy the video from the Raspberry Pi to your PC and open the annotator:**

```powershell
.\.venv\Scripts\python.exe treino_modelo/anotar_imagens.py
```

The interface currently uses Portuguese button labels, shown below with their English meanings.

- Choose **Abrir video novo** (Open new video) and select the video.
- Choose the interval: 60 saves one image every 60 frames, not every 60 seconds.
- Draw a box around each flame and press **Enter**. If there is no flame, choose **Sem chama** (No flame).
- To continue later, choose **Continuar imagens ja extraidas** (Resume extracted images) and open the session folder. The program starts at the first image without a label and skips annotated images after saving.
- Once every image is annotated, click **Finalizar** (Finalize). The default split is 80% training and 20% validation; you can adjust the percentage.

**After confirmation**, the program copies the images and labels to the dataset, verifies the copies, and deletes the session originals and source video. It removes the session folder only if empty. If the video has been replaced, it preserves it. Older folders without video metadata can be imported, but their video is not deleted automatically. If the export fails, reopen the same folder to resume.

You do not need to copy images or labels manually. Each session uses unique names, so videos with the same name do not overwrite dataset images. Do not replace the video of a session still in progress.

**2. Run training:**

```powershell
.\.venv\Scripts\python.exe treino_modelo/treinar.py
```

The cache is updated automatically. The best model is saved to `treino_modelo/firenet_grid.pt`.

**3. Export the model:**

```powershell
.\.venv\Scripts\python.exe treino_modelo/model_converter.py
```

The converter produces `treino_modelo/firenet.onnx`. Copy it to `firmware/raspberry/firenet.onnx` and then to the Raspberry Pi; this copy is not automatic.

## Where files are stored

```text
esp32cam-firefighter/
├── treino_modelo/
│   ├── firenet_grid.pt
│   └── resultados/
│       ├── frames/<unique_session>/ ← images and labels during annotation
│       ├── cache/                   ← prepared training data
│       └── previsoes_valid/         ← visual validation results
└── Datasets/Detecao_fogo/
    ├── train/images/                ← finalized training images
    ├── train/labels/                ← matching .txt files
    ├── valid/images/                ← finalized validation images
    ├── valid/labels/                ← matching .txt files
    └── sessions/                    ← imported session records
```

During annotation, JPG files remain in the session folder and `.txt` files go in its `labels/` subfolder. An empty `.txt` means you confirmed that the image contains no flame.

The split uses earlier frames for training and later frames for validation. Images from the same video can still be similar; use independent recordings for a reliable final evaluation.

## Optional tools

```powershell
# Inspect results on the validation set
.\.venv\Scripts\python.exe treino_modelo/ver_resultados.py

# Show webcam predictions; press Q or Esc to exit
.\.venv\Scripts\python.exe treino_modelo/detetar_camera.py

# Record clean video while displaying webcam predictions
.\.venv\Scripts\python.exe treino_modelo/detetar_camera.py --gravar sessao.mp4

# Extract images without opening the annotator; prints the created folder
.\.venv\Scripts\python.exe treino_modelo/extrair_frames.py sessao.mp4 --cada 60

# Force a cache rebuild
.\.venv\Scripts\python.exe treino_modelo/criar_cache.py
```

Webcam detection requires `firenet_grid.pt`, but does not require the dataset. Recordings do not include boxes by default; add `--com-caixas` to include them. Use video without boxes when collecting training data.

Keep videos, datasets, caches, and the `.venv` environment out of Git commits.
