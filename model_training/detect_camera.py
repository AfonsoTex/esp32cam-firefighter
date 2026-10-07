# Runs FireNet on webcam frames using the best saved validation checkpoint.
# Optionally records clean video for training. Press Q or Esc to exit.
import argparse
import time
from collections import deque

import cv2
import torch

import train
from data import IMAGE_SIZE
from model import FireNet
from train import CHECKPOINT, get_boxes


# Temporally smooths the alert: require detections in MINIMO of the last HISTORICO frames.
HISTORICO = 5

MINIMO = 3


def preprocess(frame):
    # Converts BGR to RGB, resizes to the training size, and normalizes to 0..1.
    # Returns a tensor with shape 1x3x180x180.
    image = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)
    image = cv2.resize(image, (IMAGE_SIZE, IMAGE_SIZE))

    tensor = torch.from_numpy(image).float() / 255.0
    tensor = tensor.permute(2, 0, 1)
    tensor = tensor.unsqueeze(0)

    return tensor


def draw_detections(frame, boxes):
    height, width = frame.shape[:2]

    for x, y, box_width, box_height in boxes:
        # Converts normalized box coordinates to pixels in the original webcam image.
        x_min = int((x - box_width / 2) * width)
        y_min = int((y - box_height / 2) * height)
        x_max = int((x + box_width / 2) * width)
        y_max = int((y + box_height / 2) * height)

        x_min, x_max = max(0, x_min), min(width - 1, x_max)
        y_min, y_max = max(0, y_min), min(height - 1, y_max)

        cv2.rectangle(frame, (x_min, y_min), (x_max, y_max), (0, 255, 0), 2)
        cv2.putText(frame, "fire", (x_min, max(0, y_min - 8)),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.6, (0, 255, 0), 2)

    return frame


def open_camera(index):
    # Tries Windows camera backends in order. Each gets up to 30 attempts to read a frame.
    backends = [
        ("MSMF", cv2.CAP_MSMF),
        ("DSHOW", cv2.CAP_DSHOW),
        ("default", cv2.CAP_ANY),
    ]

    for name, backend in backends:
        print(f"Trying camera {index} with the {name} backend...")
        capture = cv2.VideoCapture(index, backend)

        if not capture.isOpened():
            capture.release()
            continue

        for _ in range(30):
            ok, frame = capture.read()
            if ok and frame is not None:
                print(f"Camera opened with the {name} backend.")
                return capture
            time.sleep(0.1)

        capture.release()

    return None


def main():
    parser = argparse.ArgumentParser(description="Run FireNet on the PC webcam and optionally record video.")
    parser.add_argument("--camera", type=int, default=0,
                         help="camera index (0 = first/default camera)")
    parser.add_argument("--threshold", type=float, default=0.4,
                         help="minimum confidence required to draw a detection")
    parser.add_argument("--record", type=str, default=None,
                         help="record video to this file, e.g. room.mp4")
    parser.add_argument("--with-boxes", action="store_true",
                         help="record the image with boxes and text overlays "
                              "(otherwise record clean camera frames, which are "
                              "suitable for training)")
    args = parser.parse_args()

    # get_boxes reads the threshold from train, so update that module before inference.
    train.CONFIDENCE_THRESHOLD = args.threshold

    model = FireNet()
    checkpoint = torch.load(CHECKPOINT, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["weights"])
    model.eval()

    print(f"Model loaded from {CHECKPOINT} (saved score: {checkpoint['score']:.2f}%)")

    capture = open_camera(args.camera)

    if capture is None:
        raise RuntimeError(
            f"Could not open camera {args.camera} with any backend. "
            "In Settings > Privacy & security > Camera, enable camera access "
            "for desktop apps. Close other apps using the camera "
            "(Teams, Camera, or browser tabs), and try another index with --camera 1."
        )

    print("Running. Press 'q' or Esc in the video window to exit.")

    # Keeps the latest results; deque automatically discards the oldest when full.
    historico = deque(maxlen=HISTORICO)

    # Creates the video writer after the first frame, when the image dimensions are known.
    writer = None

    try:
        while True:
            ok, frame = capture.read()

            if not ok:
                print("Could not read another camera frame.")
                break

            # Keeps a clean copy for training recordings before drawing detections.
            limpo = frame.copy()

            with torch.no_grad():
                prediction = model(preprocess(frame))[0]

            # Converts grid logits to confidence scores and displays the highest score.
            melhor = torch.sigmoid(prediction[0]).max().item()

            boxes = get_boxes(prediction)

            historico.append(len(boxes) > 0)

            # Requires detections in at least MINIMO recent frames to show a stable alert.
            estavel = sum(historico) >= MINIMO

            frame = draw_detections(frame, boxes)

            status = f"FIRE DETECTED ({len(boxes)})" if estavel else "no fire"
            color = (0, 0, 255) if estavel else (0, 200, 0)
            cv2.putText(frame, status, (10, 30), cv2.FONT_HERSHEY_SIMPLEX,
                        0.9, color, 2)

            # Displays the highest confidence and the detection threshold.
            cv2.putText(frame, f"max {melhor:.2f}  threshold {args.threshold:.2f}"
                        f"  {sum(historico)}/{len(historico)}",
                        (10, 60), cv2.FONT_HERSHEY_SIMPLEX, 0.6,
                        (255, 255, 0), 2)

            if args.record:

                if writer is None:
                    altura, largura = frame.shape[:2]

                    # Uses 20 fps if the camera driver does not report a usable frame rate.
                    fps = capture.get(cv2.CAP_PROP_FPS)
                    if not fps or fps <= 1:
                        fps = 20.0

                    # The mp4v codec is used for MP4 output.
                    codec = cv2.VideoWriter_fourcc(*"mp4v")

                    writer = cv2.VideoWriter(args.record, codec, fps,
                                             (largura, altura))

                    print(f"Recording to {args.record} "
                          f"({largura}x{altura}, {fps:.0f} fps)")

                writer.write(frame if args.with_boxes else limpo)

            cv2.imshow("FireNet - live detection", frame)

            key = cv2.waitKey(1) & 0xFF
            if key in (ord("q"), 27):  # 27 = ESC
                break
    finally:
        if writer is not None:
            writer.release()
            print(f"Video saved: {args.record}")

        capture.release()
        cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
