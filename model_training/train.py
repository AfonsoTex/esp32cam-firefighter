# Trains FireNet and evaluates it on validation data.
# Saves the model whenever its validation score improves.
# Automatically updates the cache after new annotated sessions enter the dataset.

import os
import time

import torch

from data import GRID_SIZE, IMAGE_SIZE, build_loader, PROJECT_DIR
from model import FireNet


CHECKPOINT = os.path.join(PROJECT_DIR, "firenet_grid.pt")

POSITION_WEIGHT = 1

COUNT_WEIGHT = 3

FIRE_WEIGHT = 1

NO_FIRE_WEIGHT = 2

CONFIDENCE_THRESHOLD = 0.7

MAX_DISTANCE = 10

MEASURE_TRAIN = False

# Starting learning rate for each new run, including when loading saved weights.
LEARNING_RATE = 0.001

# Halve the current learning rate after this many epochs without a new best score.
LEARNING_RATE_PATIENCE = 5

# Stop reducing after five halvings; changing the base rate also changes this limit.
MIN_LEARNING_RATE = LEARNING_RATE / 32


def get_boxes(grid, prediction=True):

    presence = grid[0]

    if prediction:
        presence = torch.sigmoid(presence)

    rows, columns = torch.where(presence > CONFIDENCE_THRESHOLD)

    boxes = []

    for row, column in zip(rows.tolist(), columns.tolist()):

        # Add the cell position to its predicted offset to recover the image position.
        x = (column + 0.5 + grid[1, row, column].item()) / GRID_SIZE
        y = (row + 0.5 + grid[2, row, column].item()) / GRID_SIZE

        width = grid[3, row, column]
        height = grid[4, row, column]

        if prediction:
            width = torch.sigmoid(width)
            height = torch.sigmoid(height)

        width = width.item()
        height = height.item()

        if not (0 <= x <= 1 and 0 <= y <= 1 and 0 <= width <= 1 and 0 <= height <= 1):
            continue

        boxes.append((x, y, width, height))

    return boxes


def evaluate(model, loader):

    model.eval()

    correct = 0 
    total_predicted = 0
    total_expected = 0
    total_distance = 0.0

    # Same three counts as above, but for images instead of flames:
    # images that really have no fire, images where the model stayed quiet,
    # and images where it stayed quiet and was right.
    real_no_fire = 0
    predicted_no_fire = 0
    correct_no_fire = 0

    with torch.no_grad():
        for images, labels in loader:

            predictions = model(images)

            for prediction, label in zip(predictions, labels):

                predicted_boxes = get_boxes(prediction)
                expected_boxes = get_boxes(label, prediction=False)

                total_predicted += len(predicted_boxes)
                total_expected += len(expected_boxes)

                if len(expected_boxes) == 0:
                    real_no_fire += 1

                if len(predicted_boxes) == 0:
                    predicted_no_fire += 1

                    if len(expected_boxes) == 0:
                        correct_no_fire += 1

                for x, y, width, height in predicted_boxes:

                    closest = None
                    closest_distance = MAX_DISTANCE

                    for index, (target_x, target_y, _, _) in enumerate(expected_boxes):

                        dx = (x - target_x) * IMAGE_SIZE
                        dy = (y - target_y) * IMAGE_SIZE
                        distance = (dx * dx + dy * dy) ** 0.5

                        if distance < closest_distance:
                            closest = index
                            closest_distance = distance

                    if closest is not None:
                        correct += 1
                        total_distance += closest_distance
                        expected_boxes.pop(closest)

    average_distance = total_distance / max(correct, 1)

    #                 2 * correct
    # Score = --------------------------------- * 100
    #         2 * correct + extra_boxes + missed_flames
    #
    # Each correct match pairs two items: one predicted box and one real flame.
    # That is why each match counts twice.
    # The numerator counts only matched items.
    # The denominator counts all items: matched items plus extra boxes and missed flames.
    # Multiplying by 100 expresses the score as a percentage.
    score = 2 * (correct / max(total_predicted, 1)) * (correct / max(total_expected, 1))
    score /= max((correct / max(total_predicted, 1)) + (correct / max(total_expected, 1)), 1e-8)

    score *= 100

    return (correct, total_expected, total_predicted, average_distance, score,
            correct_no_fire, real_no_fire, predicted_no_fire)


def report(name, results):

    correct, real, predicted, distance, _, correct_no_fire, real_no_fire, predicted_no_fire = results

    print(name)
    print(f"It found {correct} of the {real} real flames, but it predicted there to be {predicted}.")
    print(f"It found {correct_no_fire} of the {real_no_fire} no-fire images, but it predicted there to be {predicted_no_fire}.")
    print(f"Average position error: {distance:.1f} px")


if __name__ == "__main__":

    from build_cache import ensure_cache
    ensure_cache()

    train_loader = build_loader("train", True, augment=True)
    # Measure training performance on unchanged images, just like validation.
    train_evaluation_loader = build_loader("train", False)
    valid_loader = build_loader("valid", False)

    model = FireNet()

    optimizer = torch.optim.Adam(model.parameters(), lr=LEARNING_RATE)

    presence_loss = torch.nn.BCEWithLogitsLoss(reduction="none")
    position_loss = torch.nn.MSELoss(reduction="none")

    best_score = -1.0

    if os.path.exists(CHECKPOINT):
        checkpoint = torch.load(CHECKPOINT, map_location="cpu", weights_only=True)
        model.load_state_dict(checkpoint["weights"])
        print("Checking saved model on validation data...")
        best_score = evaluate(model, valid_loader)[4]

        print(f"Resuming best model. Score: {best_score:.2f}%")

    total_steps = len(train_loader)

    learning_rate = LEARNING_RATE
    epochs_without_improvement = 0

    for epoch in range(30):

        # Python counts from 0; use 1..30 to match the epoch numbers shown below.
        epoch_number = epoch + 1

        print(f"epoch {epoch_number:2d}  learning rate: {learning_rate:g}")

        model.train()

        train_start = time.time()

        for step, (images, labels) in enumerate(train_loader):

            predictions = model(images)

            has_fire = labels[:, 0] == 1
            no_fire = ~has_fire

            presence_errors = presence_loss(predictions[:, 0], labels[:, 0])

            # Average occupied and empty cells separately, then weight the empty-cell error.
            fire_error = (presence_errors * has_fire).sum() / has_fire.sum().clamp_min(1)
            no_fire_error = (presence_errors * no_fire).sum() / no_fire.sum().clamp_min(1)
            presence_error = FIRE_WEIGHT * fire_error + NO_FIRE_WEIGHT * no_fire_error

            offsets = predictions[:, 1:3]
            sizes = torch.sigmoid(predictions[:, 3:5])
            positions = torch.cat((offsets, sizes), dim=1)

            position_errors = position_loss(positions, labels[:, 1:5])
            position_errors = position_errors * labels[:, 0:1]

            position_error = position_errors.sum() / (has_fire.sum() * 4).clamp_min(1)

            # [:, 0] selects all images (:) and only the presence map (0) from each one.
            # sigmoid converts those values into probabilities between 0 and 1.
            presence_probabilities = torch.sigmoid(predictions[:, 0])

            # sum(dim=(1, 2)) adds the values across rows (1) and columns (2).
            # This gives one estimated fire count per image, without adding different images together.
            predicted_count = presence_probabilities.sum(dim=(1, 2))
            real_count = labels[:, 0].sum(dim=(1, 2))

            count_error = (predicted_count - real_count).abs().mean()

            total_error = presence_error + POSITION_WEIGHT * position_error
            total_error = total_error + COUNT_WEIGHT * count_error

            optimizer.zero_grad()

            total_error.backward()

            optimizer.step()

            if (step + 1) % 50 == 0:
                elapsed = time.time() - train_start
                print(f"epoch {epoch + 1:2d}  {step + 1:4d}/{total_steps}   {elapsed:.0f}s", flush=True)

        train_seconds = time.time() - train_start

        eval_train_seconds = 0.0

        if MEASURE_TRAIN:
            start = time.time()
            train_results = evaluate(model, train_evaluation_loader)
            eval_train_seconds = time.time() - start
            report(f"epoch {epoch + 1:2d}  train", train_results)

        start = time.time()
        results = evaluate(model, valid_loader)
        eval_valid_seconds = time.time() - start

        report(f"epoch {epoch + 1:2d}  valid", results)

        print(f"epoch {epoch + 1:2d}   train {train_seconds:.0f}s"
              f"   eval train {eval_train_seconds:.0f}s"
              f"   eval valid {eval_valid_seconds:.0f}s")

        score = results[4]

        print(f"Score: {score:.2f}% | Best: {best_score:.2f}%")

        if score > best_score:
            best_score = score
            # A new best score restarts the wait; keep the current learning rate.
            epochs_without_improvement = 0

            torch.save({"score": score, "weights": model.state_dict()}, CHECKPOINT)

            print("Saved", CHECKPOINT)
        else:
            # Equal or lower scores do not beat the best saved model.
            epochs_without_improvement += 1

        if epochs_without_improvement >= LEARNING_RATE_PATIENCE:
            # Smaller updates may help refine the model after progress stalls.
            # This rate takes effect in the next epoch, not the one just evaluated.
            next_learning_rate = learning_rate / 2
            if next_learning_rate < MIN_LEARNING_RATE:
                next_learning_rate = MIN_LEARNING_RATE

            if next_learning_rate < learning_rate:
                learning_rate = next_learning_rate

                # Change Adam's rate without discarding its accumulated history.
                for parameter_group in optimizer.param_groups:
                    parameter_group["lr"] = learning_rate

                print(f"No improvement for {LEARNING_RATE_PATIENCE} epochs. "
                      f"Learning rate for the next epoch: {learning_rate:g}")

            # Allow another full waiting period before considering another reduction.
            epochs_without_improvement = 0
