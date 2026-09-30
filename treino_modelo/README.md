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

## Criar um dataset com vídeos próprios

Processo da gravação na Raspberry até ao treino no PC. Os caminhos das ferramentas e do dataset são relativos à pasta do projeto. Os comandos Python pressupõem o ambiente `.venv` já preparado, conforme a configuração acima.

### 1 Gravar vídeo na Raspberry

- Gravar situações reais, com e sem chama, sem caixas de deteção.

- Não precisamos de alterar o programa: podemos gravar com rpicam-vid.

- No terminal da Raspberry, para o FireNet com Ctrl+C para libertar a câmara. Depois executa:

```bash
rpicam-vid -n -t 60000 --width 640 --height 480 \
  --framerate 30 --rotation 180 --codec libav -o ~/sessao01.mp4
```

- Grava 60 segundos em /home/afonso/sessao01.mp4. A rotação de 180° corrige a orientação atual da câmara. Usa outro nome nas gravações seguintes para não substituir a anterior.

### 2 Passar o vídeo para o PC

- O vídeo pode ficar em qualquer pasta do PC; não precisa de estar dentro do projeto.

- Exemplo para o Desktop, executado no PowerShell do PC:

```powershell
scp afonso@BaraoForrester.local:~/sessao01.mp4 "C:\Users\afons\Desktop\"
```

- A cópia fica em C:\Users\afons\Desktop\sessao01.mp4. O original continua na Raspberry.

### 3 Partir o vídeo em frames

- No PowerShell do PC, entra na pasta do projeto e abre a ferramenta:

```powershell
cd "C:\Users\afons\Desktop\Project-esp32cam\esp32cam-firefighter"
.\.venv\Scripts\python.exe treino_modelo/anotar_imagens.py
```

- Escolhe “Abrir video novo”: abre uma janela para selecionares o vídeo, onde quer que esteja.

- A ferramenta usa automaticamente extrair_frames.py para extrair frames; não precisas de o executar diretamente.

- Escolhe o intervalo: por exemplo, uma imagem a cada 60 frames, aproximadamente dois segundos num vídeo de 30 fps.

- Os frames vão para treino_modelo/resultados/frames/<sessão>/, independentemente da localização do vídeo.

### 4 Anotar as imagens

- Na mesma ferramenta, desenha caixas nas chamas e guarda com Enter; quando não houver chama, escolhe “Sem chama”.

- As imagens continuam na pasta da sessão. Cada imagem recebe um .txt com as anotações na subpasta labels/. Um .txt vazio significa que confirmaste que não há chama.

- Podes parar e usar “Continuar imagens ja extraidas”, selecionando a pasta da sessão.

### 5 Enviar para o dataset

- Carrega em “Finalizar” e confirma a divisão, por defeito 80% para treino e 20% para validação.

- A ferramenta copia automaticamente para estas pastas, dentro do projeto:

```text
Datasets/Detecao_fogo/
  train/images/   imagens de treino
  train/labels/   respetivas anotações
  valid/images/   imagens de validação
  valid/labels/   respetivas anotações
```

- **Depois da confirmação e de verificar as cópias, a ferramenta apaga os temporários da sessão e o vídeo selecionado no PC. O vídeo na Raspberry mantém-se. Se quiseres conservar também o vídeo no PC, anota uma cópia.**

### 6 Preparar cache e treinar

- No PowerShell do PC, dentro da pasta do projeto, executa:

```powershell
.\.venv\Scripts\python.exe treino_modelo/treinar.py
```

- O treino prepara a cache automaticamente; não precisas de executar criar_cache.py separadamente.

- A cache guarda os dados já preparados para acelerar o treino, em treino_modelo/resultados/cache/.

- O modelo resultante fica em treino_modelo/firenet_grid.pt.

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
