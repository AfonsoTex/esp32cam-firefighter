import os
import pickle

import cv2
import numpy as np
import torch
from torch.utils.data import Dataset, DataLoader


PROJECT_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

CACHE_DIR = os.path.join(PROJECT_DIR, "resultados", "cache")


def cache_paths(split):
    return (os.path.join(CACHE_DIR, split + "_images.npy"),
            os.path.join(CACHE_DIR, split + "_boxes.pkl"))


IMAGE_SIZE = 128

GRID_SIZE = IMAGE_SIZE // 4

BATCH_SIZE = 32


def read_split(images_dir, labels_dir, fire_class):
    names = []

    for n in os.listdir(images_dir):
        if n.lower().endswith((".jpg", ".jpeg", ".png")):
            names.append(n)

    names = sorted(names)

    samples = []

    for name in names:

        image_path = os.path.join(images_dir, name)

        base = os.path.splitext(name)[0]

        label_path = os.path.join(labels_dir, base + ".txt")

        boxes = []

        if os.path.exists(label_path):
            with open(label_path) as f:
                for line in f:
                    parts = line.split()

                    if len(parts) != 5:
                        continue

                    class_id = int(parts[0])
                    x = float(parts[1])
                    y = float(parts[2])
                    width = float(parts[3])
                    height = float(parts[4])

                    boxes.append((class_id, x, y, width, height))

        fire_boxes = []

        for class_id, x, y, width, height in boxes:
            if class_id == fire_class:
                fire_boxes.append((x, y, width, height))

        samples.append((image_path, fire_boxes))

    return samples


def build_split(split):
    images_dir = os.path.join(os.path.dirname(PROJECT_DIR), "Datasets", "Detecao_fogo", split, "images")
    labels_dir = os.path.join(os.path.dirname(PROJECT_DIR), "Datasets", "Detecao_fogo", split, "labels")

    samples = read_split(images_dir, labels_dir, 0)

    return samples


def report_split(name, samples):
    positives = 0

    for sample in samples:
        if len(sample[1]) > 0:
            positives += 1

    negatives = len(samples) - positives

    print(name)
    print("  total     ", len(samples))
    print("  with fire ", positives)
    print("  no fire   ", negatives)




def encode_boxes(boxes):
# Transforms the fire information from [(x, y, width, height), ...]
# into the same 5x40x40 format predicted by the model.
# Everything stays zero except occupied cells, e.g.:
# cell (10,20) -> [1, x, y, w, h]
# cell (39,39) -> [0, 0, 0, 0, 0]
# necessary for training so that the prediction is the same as the label
    
    if len(boxes) > GRID_SIZE * GRID_SIZE:
        raise ValueError("More boxes than grid cells")
    # A 40x40 grid has 1600 cells, so it can represent at most 1600 boxes.

    grid = torch.zeros(5, GRID_SIZE, GRID_SIZE)

    for x, y, width, height in boxes:
        #boxes only contain the information of image that contain fire

        column = min(int(x * GRID_SIZE), GRID_SIZE - 1)
        row = min(int(y * GRID_SIZE), GRID_SIZE - 1)
        # Finds which grid cell contains the center of the box.
        # Example: x=0.52, y=0.27 -> column=20, row=10.


        grid[0, row, column] = 1
        #boxes only contain the information of image that contain fire

        # Store x/y relative to this cell's center; sizes remain fractions of the image.
        grid[1, row, column] = x * GRID_SIZE - column - 0.5
        grid[2, row, column] = y * GRID_SIZE - row - 0.5
        # x * GRID_SIZE gives the exact position inside the grid.
        # Example: x * 40 = 20.8 -> the position is between 20 and 21,
        # so it belongs to column 20: int(20.8) = 20.
        # Column 20 goes from 20.0 to 21.0, so its center is 20.5.
        # Therefore, 20.8 - 20.5 = 0.3: x is 0.3 cells to the right of the cell center.
        grid[3, row, column] = width
        grid[4, row, column] = height

    return grid


class FireDataset(Dataset):

    def __init__(self, split):
        self.images_path, boxes_path = cache_paths(split)
        self.images = None
        with open(boxes_path, "rb") as f:
            self.boxes = pickle.load(f)

    def __len__(self):
        return len(self.boxes)

    def __getitem__(self, index):

        if self.images is None:
            self.images = np.load(self.images_path, mmap_mode="r")

        image = np.asarray(self.images[index])

        image = torch.from_numpy(image).float() / 255.0

        image = image.permute(2, 0, 1)

        label = encode_boxes(self.boxes[index])

        return image, label


def build_loader(split, shuffle):
    dataset = FireDataset(split)
    loader = DataLoader(dataset, batch_size=BATCH_SIZE, shuffle=shuffle, num_workers=0)
    return loader


if __name__ == "__main__":
    train_samples = build_split("train")
    valid_samples = build_split("valid")

    report_split("train", train_samples)
    report_split("valid", valid_samples)
