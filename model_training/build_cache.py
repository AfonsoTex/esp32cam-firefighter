# Resizes and caches images and annotations to speed up training.
# Avoids reopening and resizing each image in every training epoch.
# Run again whenever the dataset or image size changes.

import os
import hashlib
import json
from pathlib import Path
import pickle
import time

import cv2
import numpy as np

from data import IMAGE_SIZE, build_split, CACHE_DIR


# Decompresses every JPEG once, resizes it, and stores the resulting pixels.
# Training then reads pixels that are already decompressed instead of running
# the JPEG decoder again in every epoch.
#
# Run once, from this folder:   python build_cache.py
# Run it again only if the dataset changes or IMAGE_SIZE changes.




def cache_paths(split):
    images_path = os.path.join(CACHE_DIR, split + "_images.npy")
    boxes_path = os.path.join(CACHE_DIR, split + "_boxes.pkl")
    return images_path, boxes_path


def build_cache(split):

    # Same function training uses, so the order of the images is identical.
    # That matters: the cache is addressed by position, not by file name.
    samples = build_split(split)

    total = len(samples)

    if total == 0:
        print("  empty, skipping")
        return

    images_path, boxes_path = cache_paths(split)

    # Creates the .npy on disk already at its final size and hands back
    # something that behaves like an array but whose writes go to the file.
    # This way the whole dataset never has to fit in RAM.
    images = np.lib.format.open_memmap(
        images_path,
        mode="w+",
        dtype=np.uint8,
        shape=(total, IMAGE_SIZE, IMAGE_SIZE, 3),
    )

    boxes = []

    start = time.time()

    for index, (image_path, sample_boxes) in enumerate(samples):

        # These three lines are the ones currently in FireDataset.__getitem__.
        # Here they run once per image instead of once per image per epoch.
        image = cv2.imread(image_path)

        if image is None:
            raise RuntimeError("could not read " + image_path)

        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)

        image = cv2.resize(image, (IMAGE_SIZE, IMAGE_SIZE))

        # Writes IMAGE_SIZE * IMAGE_SIZE * 3 bytes at offset index * that size.
        images[index] = image

        boxes.append(sample_boxes)

        if (index + 1) % 500 == 0 or index + 1 == total:
            elapsed = time.time() - start
            remaining = elapsed / (index + 1) * (total - index - 1)
            print(f"  {index + 1}/{total}   {remaining:5.0f}s left")

    # Pushes to disk whatever the operating system still had pending.
    images.flush()

    del images

    with open(boxes_path, "wb") as f:
        pickle.dump(boxes, f)

    size_gb = os.path.getsize(images_path) / (1024 ** 3)

    print(f"  {total} images, {size_gb:.2f} GB -> {images_path}")


def ensure_cache(force=False):
    # Rebuild only the splits whose source images, labels, or image size changed.
    os.makedirs(CACHE_DIR, exist_ok=True)
    for split in ("train", "valid"):
        samples = build_split(split)
        if not samples:
            raise ValueError(f"The {split} dataset is empty. Finalize a session in the annotator first.")
        digest = hashlib.sha256(str(IMAGE_SIZE).encode())
        for image_path, boxes in samples:
            image = Path(image_path)
            info = image.stat()
            digest.update(json.dumps([str(image.resolve()), info.st_size, info.st_mtime_ns, boxes]).encode())
        signature = digest.hexdigest()
        marker = Path(CACHE_DIR) / (split + "_signature.txt")
        images_path, boxes_path = cache_paths(split)
        if (not force and marker.exists() and marker.read_text() == signature
                and Path(images_path).is_file() and Path(boxes_path).is_file()):
            continue
        print(f"Updating {split} cache...")
        # An interrupted build must never look up to date on the next attempt.
        if marker.exists():
            marker.unlink()
        build_cache(split)
        marker.write_text(signature)


if __name__ == "__main__":
    ensure_cache(force=True)
    print("Done.")
