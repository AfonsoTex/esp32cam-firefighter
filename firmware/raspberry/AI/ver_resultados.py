import os
import cv2
import torch

from dados import IMAGE_SIZE, FireDataset, PROJECT_DIR
from modelo import FireNet
from treinar import CHECKPOINT, get_boxes, MAX_DISTANCE


OUTPUT_DIR = os.path.join(PROJECT_DIR, "resultados", "previsoes_valid")


def tensor_to_image(image_tensor):

    image = image_tensor.permute(1, 2, 0)

    image = (image * 255.0).byte().numpy()

    image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

    return image


def draw_box(image, x, y, width, height, color):

    x_min = max(0, int((x - width / 2) * IMAGE_SIZE))
    y_min = max(0, int((y - height / 2) * IMAGE_SIZE))
    x_max = min(IMAGE_SIZE - 1, int((x + width / 2) * IMAGE_SIZE))
    y_max = min(IMAGE_SIZE - 1, int((y + height / 2) * IMAGE_SIZE))

    cv2.rectangle(image, (x_min, y_min), (x_max, y_max), color, 2)

    return image


if __name__ == "__main__":

    if not os.path.exists(CHECKPOINT):
        raise FileNotFoundError("Run treinar.py first to create firenet_grid.pt.")

    valid_dataset = FireDataset("valid")

    if not len(valid_dataset):
        raise ValueError("Empty validation split")

    model = FireNet()

    checkpoint = torch.load(CHECKPOINT, map_location="cpu", weights_only=True)
    model.load_state_dict(checkpoint["weights"])
    model.eval()

    os.makedirs(OUTPUT_DIR, exist_ok=True)

    true_positive_images = 0
    false_positive_images = 0
    true_negative_images = 0
    false_negative_images = 0

    matched_flames = 0
    total_distance = 0.0

    with torch.no_grad():

        for index in range(len(valid_dataset)):

            image_tensor, label = valid_dataset[index]

            batch = image_tensor.unsqueeze(0)
            prediction = model(batch)[0]

            predicted_boxes = get_boxes(prediction)
            expected_boxes = get_boxes(label, prediction=False)

            has_real_fire = len(expected_boxes) > 0
            has_predicted_fire = len(predicted_boxes) > 0

            if has_real_fire and has_predicted_fire:
                true_positive_images += 1
                kind = "fire"
            elif not has_real_fire and has_predicted_fire:
                false_positive_images += 1
                kind = "false_positive_fire"
            elif not has_real_fire and not has_predicted_fire:
                true_negative_images += 1
                kind = "no_fire"
            else:
                false_negative_images += 1
                kind = "false_negative_no_fire"

            remaining_expected_boxes = expected_boxes.copy()

            for x, y, width, height in predicted_boxes:

                closest = None
                closest_distance = MAX_DISTANCE

                for box_index, (target_x, target_y, _, _) in enumerate(remaining_expected_boxes):

                    dx = (x - target_x) * IMAGE_SIZE
                    dy = (y - target_y) * IMAGE_SIZE
                    distance = (dx * dx + dy * dy) ** 0.5

                    if distance < closest_distance:
                        closest = box_index
                        closest_distance = distance

                if closest is not None:
                    matched_flames += 1
                    total_distance += closest_distance
                    remaining_expected_boxes.pop(closest)

            image = tensor_to_image(image_tensor)

            for x, y, width, height in expected_boxes:
                draw_box(image, x, y, width, height, (0, 0, 255))

            for x, y, width, height in predicted_boxes:
                draw_box(image, x, y, width, height, (0, 255, 0))

            file_name = kind + "_" + str(index) + ".jpg"

            if not cv2.imwrite(os.path.join(OUTPUT_DIR, file_name), image):
                raise RuntimeError("Could not save " + file_name)

    total_images = len(valid_dataset)
    correct_images = true_positive_images + true_negative_images
    wrong_images = false_positive_images + false_negative_images
    average_distance = total_distance / max(matched_flames, 1)

    print("Total images:", total_images)
    print("Correct images:", correct_images)
    print("Wrong images:", wrong_images)
    print("Images with fire correctly detected:", true_positive_images)
    print("Images without fire falsely detected as fire:", false_positive_images)
    print("Images without fire correctly rejected:", true_negative_images)
    print("Images with fire missed:", false_negative_images)
    print(f"Average position error: {average_distance:.1f} px")
    print("Green: predictions. Red: annotations.")
    print("Images saved in", OUTPUT_DIR)
