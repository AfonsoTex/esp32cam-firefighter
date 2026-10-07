# Creates small random variations when training reads an image.
# Original images, annotations and cached files are never changed.

import random

import cv2
import numpy as np


ENABLE_AUGMENTATION = True

# Half of the images are mirrored; each flame box is mirrored with its image.
FLIP_PROBABILITY = 0.5

# Small rotations simulate a tilted camera; avoid unrealistic upside-down flames.
ROTATION_PROBABILITY = 0.3
MAX_ROTATION_DEGREES = 5.0

# A small crop changes position and zoom. It must keep every flame box intact.
CROP_PROBABILITY = 0.5
MIN_CROP_SCALE = 0.90

# Keep lighting changes small so flames still resemble the camera recordings.
MIN_BRIGHTNESS = 0.90
MAX_BRIGHTNESS = 1.10
MIN_CONTRAST = 0.90
MAX_CONTRAST = 1.10


def rotate_without_cutting_flames(image, boxes, angle):
    image_height, image_width = image.shape[:2]
    center = (image_width / 2, image_height / 2)
    matrix = cv2.getRotationMatrix2D(center, angle, 1.0)

    rotated_boxes = []
    for x, y, width, height in boxes:
        left = (x - width / 2) * image_width
        right = (x + width / 2) * image_width
        top = (y - height / 2) * image_height
        bottom = (y + height / 2) * image_height
        corners = [(left, top), (right, top), (right, bottom), (left, bottom)]

        rotated_x_positions = []
        rotated_y_positions = []
        for corner_x, corner_y in corners:
            # Apply the same rotation to each corner as to the image pixels.
            rotated_x = matrix[0, 0] * corner_x + matrix[0, 1] * corner_y + matrix[0, 2]
            rotated_y = matrix[1, 0] * corner_x + matrix[1, 1] * corner_y + matrix[1, 2]

            # One corner outside means a flame could be cut. Reject the whole
            # rotation, keeping the image and every box as they were before it.
            if rotated_x < 0 or rotated_x > image_width:
                return image, boxes
            if rotated_y < 0 or rotated_y > image_height:
                return image, boxes

            rotated_x_positions.append(rotated_x)
            rotated_y_positions.append(rotated_y)

        # Boxes stay upright and enclose all four rotated corners.
        new_left = min(rotated_x_positions)
        new_right = max(rotated_x_positions)
        new_top = min(rotated_y_positions)
        new_bottom = max(rotated_y_positions)
        new_x = (new_left + new_right) / 2 / image_width
        new_y = (new_top + new_bottom) / 2 / image_height
        new_width = (new_right - new_left) / image_width
        new_height = (new_bottom - new_top) / image_height
        rotated_boxes.append((new_x, new_y, new_width, new_height))

    # Empty corners use a constant fill. Reflecting pixels could duplicate a
    # nearby flame without creating a corresponding annotation.
    rotated_image = cv2.warpAffine(
        image, matrix, (image_width, image_height),
        flags=cv2.INTER_LINEAR,
        borderMode=cv2.BORDER_CONSTANT,
        borderValue=(0, 0, 0),
    )
    return rotated_image, rotated_boxes


def crop_without_cutting_flames(image, boxes, left, top, crop_width, crop_height):
    image_height, image_width = image.shape[:2]
    right = left + crop_width
    bottom = top + crop_height

    # Check every full box, not just its center. If even one flame would be
    # partly cut or entirely removed, reject this crop and keep the whole image.
    for x, y, width, height in boxes:
        flame_left = (x - width / 2) * image_width
        flame_right = (x + width / 2) * image_width
        flame_top = (y - height / 2) * image_height
        flame_bottom = (y + height / 2) * image_height

        if flame_left < left or flame_right > right:
            return image, boxes
        if flame_top < top or flame_bottom > bottom:
            return image, boxes

    # All flames fit. Crop the image and resize it back to the model input size.
    cropped_image = image[top:bottom, left:right]
    cropped_image = cv2.resize(cropped_image, (image_width, image_height))

    # Coordinates are relative to the new crop, so update position and size.
    cropped_boxes = []
    for x, y, width, height in boxes:
        new_x = (x * image_width - left) / crop_width
        new_y = (y * image_height - top) / crop_height
        new_width = width * image_width / crop_width
        new_height = height * image_height / crop_height
        cropped_boxes.append((new_x, new_y, new_width, new_height))

    return cropped_image, cropped_boxes


def augment_image(image, boxes):
    if not ENABLE_AUGMENTATION:
        return image, boxes

    if random.random() < FLIP_PROBABILITY:
        image = cv2.flip(image, 1)
        flipped_boxes = []
        for x, y, width, height in boxes:
            flipped_boxes.append((1.0 - x, y, width, height))
        boxes = flipped_boxes

    if random.random() < ROTATION_PROBABILITY:
        angle = random.uniform(-MAX_ROTATION_DEGREES, MAX_ROTATION_DEGREES)
        image, boxes = rotate_without_cutting_flames(image, boxes, angle)

    if random.random() < CROP_PROBABILITY:
        image_height, image_width = image.shape[:2]
        scale = random.uniform(MIN_CROP_SCALE, 1.0)
        crop_width = max(1, round(image_width * scale))
        crop_height = max(1, round(image_height * scale))
        left = random.randint(0, image_width - crop_width)
        top = random.randint(0, image_height - crop_height)
        image, boxes = crop_without_cutting_flames(
            image, boxes, left, top, crop_width, crop_height
        )

    # Lighting changes affect pixels only; flame boxes stay in the same place.
    brightness = random.uniform(MIN_BRIGHTNESS, MAX_BRIGHTNESS)
    contrast = random.uniform(MIN_CONTRAST, MAX_CONTRAST)
    image = image.astype(np.float32)
    image = (image - 127.5) * contrast + 127.5
    image = image * brightness
    image = np.clip(image, 0, 255).astype(np.uint8)

    return image, boxes
