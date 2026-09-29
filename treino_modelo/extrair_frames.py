# Extracts images from a video at regular frame intervals.
# Saves them for annotation and training.
import argparse
import os

import cv2


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("video", help="ficheiro de video, ex.: vela.mp4")
parser.add_argument("--cada", type=int, default=10,
                    help="guarda 1 frame em cada N (por omissao 10)")
args = parser.parse_args()

video = cv2.VideoCapture(args.video)

if not video.isOpened():
    raise SystemExit(f"Nao consegui abrir {args.video}")

base = os.path.splitext(os.path.basename(args.video))[0]

project_dir = os.path.dirname(os.path.abspath(__file__))

pasta = os.path.join(project_dir, "resultados", "frames", base + "_frames")

os.makedirs(pasta, exist_ok=True)

indice = 0
guardadas = 0

while True:
    ok, frame = video.read()

    if not ok:
        break

    if indice % args.cada == 0:
        nome = f"{base}_{indice:06d}.jpg"
        cv2.imwrite(os.path.join(pasta, nome), frame)
        guardadas += 1

    indice += 1

video.release()

print(f"{indice} frames lidos, {guardadas} guardados em {pasta}/")
