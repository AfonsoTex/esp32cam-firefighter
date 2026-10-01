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

The Python tool commands below run from the repository root; recording runs on the Raspberry Pi and file transfer runs on the PC. They use the environment's Python directly, so activation is not required.

## Create a dataset from your own videos

Record on the Raspberry Pi, then annotate and train on the PC. Tool and dataset paths are relative to the repository root. Prepare the `.venv` environment as described above first.

### 1. Record video on the Raspberry Pi

- Record real situations with and without flames, without detection boxes.
- Use `rpicam-vid`. Stop FireNet with Ctrl+C first to release the camera.
- Run in the Raspberry Pi terminal:

```bash
rpicam-vid -n -t 60000 --width 640 --height 480 \
  --framerate 30 --rotation 180 --codec libav -o ~/sessao01.mp4
```

- Records 60 seconds to `/home/afonso/sessao01.mp4`. The 180° rotation corrects the current camera orientation. Use a different filename for each recording to avoid overwriting it.

### 2. Copy the video to the PC

- The video can be in any PC folder; it does not need to be inside the project.
- To copy it to the Desktop, run in PowerShell on the PC:

```powershell
scp afonso@BaraoForrester.local:~/sessao01.mp4 "C:\Users\afons\Desktop\"
```

- The copy is saved to `C:\Users\afons\Desktop\sessao01.mp4`. The original remains on the Raspberry Pi.

### 3. Extract video frames

- In PowerShell on the PC, open the project folder and start the annotator:

```powershell
cd "C:\Users\afons\Desktop\Project-esp32cam\esp32cam-firefighter"
.\.venv\Scripts\python.exe treino_modelo/anotar_imagens.py
```

- Choose **Open new video** and select the video from any folder.
- The annotator uses `extrair_frames.py` automatically; you do not need to run it directly.
- Choose an interval: every 60 frames means approximately two seconds for a 30 fps video.
- Frames go to `treino_modelo/resultados/frames/<session>/`, regardless of the video's location.

### 4. Annotate the images

- In the same tool, draw a box around each flame and save with Enter. Choose **No flame** for images without flames.
- Images stay in the session folder. Each image gets a `.txt` annotation file in the `labels/` subfolder. An empty `.txt` confirms that there is no flame.
- To continue later, choose **Resume extracted images** and select the session folder.

### 5. Send the session to the dataset

- Click **Finalize** and confirm the split: by default, 80% training and 20% validation.
- The tool automatically copies files into these project folders:

```text
Datasets/Detecao_fogo/
  train/images/   training images
  train/labels/   matching annotations
  valid/images/   validation images
  valid/labels/   matching annotations
```

- **After confirmation and copy verification, the tool deletes the session files and the selected video on the PC. The Raspberry Pi video remains. To keep the PC video too, annotate a copy.**

### 6. Prepare the cache and train

- In PowerShell on the PC, from the repository root, run:

```powershell
.\.venv\Scripts\python.exe treino_modelo/treinar.py
```

- Training updates the cache automatically using `criar_cache.py`; you do not need to run it separately.
- The cache stores prepared data in `treino_modelo/resultados/cache/` to speed up training.
- The best model is saved to `treino_modelo/firenet_grid.pt`.

## Normal workflow: annotate → train → export

**1. Copy the video from the Raspberry Pi to your PC and open the annotator:**

```powershell
.\.venv\Scripts\python.exe treino_modelo/anotar_imagens.py
```

- Choose **Open new video** and select the video.
- Choose the interval: 60 saves one image every 60 frames, not every 60 seconds.
- Draw a box around each flame and press **Enter**. If there is no flame, choose **No flame**.
- To continue later, choose **Resume extracted images** and open the session folder. The program starts at the first image without a label and skips annotated images after saving.
- Once every image is annotated, click **Finalize**. The default split is 80% training and 20% validation; you can adjust the percentage.

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

## Common model problems and what to do

### 1. The model misses some flames

- Record more examples of these situations, varying distance, lighting, and position.
- Annotate the flames, add the images to the dataset, and retrain.
- Simply running a video through the model does not teach it.

### 2. The model detects flames where there are none

- Record the locations and objects that trigger false detections, without flames.
- Mark these images as **No flame**, add them to the dataset, and retrain.

### 3. The model detects more flames than there are

- Boxes on objects without flames: follow point 2.
- Multiple boxes on the same flame: a duplicate detection filter may be needed. More training data alone may not solve this.

### 4. High training score, low validation score

A student scores 99% on exercises they studied but 72% on a new test. They handle familiar exercises well but struggle when the examples change. Similarly, the model may have learned the training images too specifically.

- Collect more varied situations.
- Use image variations during training (data augmentation).
- Check that training and validation cover comparable situations, using separate examples.
- A training score of 100% alone does not prove memorization; compare it with validation.

### 5. Around 80% on both training and validation

A student scores 80% on familiar exercises and 80% on a new test. There is little gap between familiar and new material, but some material is still being missed.

- Review the examples and annotations for mistakes or missing situations.
- Check whether more training improves the results.
- If performance stops improving, review training settings and model capacity. These scores alone do not prove that a larger network is needed.

These are performance scores, not the dataset split of 80% training images and 20% validation images. In this project, evaluation uses a detection score based on matching predicted and annotated centers, rather than simple image accuracy. Training-set evaluation is disabled by default (`MEASURE_TRAIN = False`).
