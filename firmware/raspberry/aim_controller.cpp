#include "aim_controller.h"
#include "servos.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float IMAGE_CENTER = 0.5f;
// Desired horizontal position of the flame: 0 = left, 0.5 = center, 1 = right.
// Move this point to compensate for the nozzle's offset from the camera.
// The existing deadband allows the flame to stay within 0.05 of this position.
constexpr float AIM_HORIZONTAL_POSITION = 0.51f;
constexpr float MIN_CONFIDENCE = 0.6f;
constexpr float CENTER_DEADBAND = 0.05f;
constexpr float MAX_TARGET_DISTANCE = 0.15f;
constexpr int CONFIRMATION_WINDOW = 6;
constexpr int REQUIRED_DETECTIONS = 4;
constexpr int MAX_MISSING_FRAMES = 5;
constexpr int REQUIRED_CENTERED_FRAMES = 5;
constexpr float PULSE_GAIN = 60.0f;
constexpr int MAX_PULSE_STEP_US = 12;
constexpr double MOVE_SETTLE_SECONDS = 0.25;
constexpr double RETURN_STEP_SECONDS = 0.15;
constexpr double HOME_SETTLE_SECONDS = 2.0;
constexpr double MAX_MISSING_SECONDS = 1.0;
// The same selected flame must stay inside the aiming deadband for this long.
constexpr double CENTERED_BEFORE_PUMP_SECONDS = 2.0;
constexpr int MAX_PUMP_ATTEMPTS = 3;
// Wait this long after the pump turns off before allowing another attempt.
constexpr double PUMP_RETRY_COOLDOWN_SECONDS = 10.0;
}

AimController::AimController(int homePan, int homeTilt,
                             int horizontalDirection, int verticalDirection)
{
    // pan and tilt will change as we aim at the flame.
    // At startup, we want them to equal the home pulses received as parameters.
    // These assignments only store values; they do not physically move the servos.
    pan = homePan;
    tilt = homeTilt;

    // Keep the home pulses unchanged so we know where to return after aiming.
    homePanPulse = homePan;
    homeTiltPulse = homeTilt;

    // Store the correction direction for each servo, according to its mounting.
    panDirection = horizontalDirection;
    tiltDirection = verticalDirection;
}

const char* AimController::status() const
{
    if (state == State::Search) {
        return "Confirming flame";
    }
    if (state == State::Track) {
        return "Aiming";
    }
    if (state == State::Hold) {
        if (pumpAttempts >= MAX_PUMP_ATTEMPTS) {
            return "Target centered - attempt limit reached";
        }
        if (pumpAttempts > 0) {
            return "Target centered - waiting to retry";
        }
        return "Target centered";
    }
    if (state == State::Return) {
        return "Returning to search position";
    }
    return "Settling";
}

void AimController::pumpPulseFinished(double currentTime)
{
    if (!waitingForPumpToFinish) {
        return;
    }

    waitingForPumpToFinish = false;
    nextPumpAttemptTime = currentTime + PUMP_RETRY_COOLDOWN_SECONDS;
}

bool AimController::update(const std::vector<Detection>& flames, double currentTime)
{
    // A request belongs only to this frame; never reuse an old request.
    pumpRequested = false;
    if (lastFrameTime >= 0 && currentTime - lastFrameTime > MAX_MISSING_SECONDS) {
        if (state == State::Track || state == State::Hold) {
            beginReturn(currentTime);
        }
        confirmedFrames = 0;
        recentDetections.clear();
    }
    lastFrameTime = currentTime;

    if (state == State::Return) {
        return moveHome(currentTime);
    }

    if (state == State::Cooldown) {
        if (nextObservationTime < 0) {
            nextObservationTime = currentTime + HOME_SETTLE_SECONDS;
        }
        if (currentTime < nextObservationTime) {
            return false;
        }
        state = State::Search;
        confirmedFrames = 0;
        recentDetections.clear();
    }

    if (currentTime < nextObservationTime) {
        return false;
    }

    Detection matchingFlame;
    bool flameFound = findMatchingFlame(flames, matchingFlame);
    if (!flameFound) {
        handleMissingFlame(currentTime);
        return false;
    }

    selectedFlame = matchingFlame;
    missingFrames = 0;
    lastDetectionTime = currentTime;

    if (state == State::Search) {
        rememberDetection(true);
        if (confirmedFrames < REQUIRED_DETECTIONS) {
            return false;
        }
        state = State::Track;
    }

    return aimAtSelectedFlame(currentTime);
}

bool AimController::findMatchingFlame(const std::vector<Detection>& flames,
                                      Detection& match) const
{
    float referenceX = IMAGE_CENTER;
    float referenceY = IMAGE_CENTER;
    bool hasSelectedFlame = false;

    // If we have left Search, a flame has already been selected.
    if (state != State::Search) {
        hasSelectedFlame = true;
    }

    // While still in Search, one or more confirmations mean we already
    // have a candidate flame to look for again in the next frame.
    if (confirmedFrames > 0) {
        hasSelectedFlame = true;
    }

    if (hasSelectedFlame) {
        referenceX = selectedFlame.x;
        referenceY = selectedFlame.y;
        //Detection selectedFlame{};
        // Stores the flame we choose to follow
    }

    bool found = false;
    float closestDistance = 0;
    for (std::size_t i = 0; i < flames.size(); i++) {
        const Detection& flame = flames[i];
        // Check that the coordinates and confidence are finite numbers.
        // If any value is invalid, skip this detection and examine the next one in flames.
        if (!std::isfinite(flame.x) ||
            !std::isfinite(flame.y) ||
            !std::isfinite(flame.confidence)) {
            continue;
        }

        // Ignore detections with insufficient confidence or a center outside the image.
        // Coordinates must be between 0 and 1. Continue with the next detection.
        if (flame.confidence < MIN_CONFIDENCE ||
            flame.x < 0 || flame.x > 1 ||
            flame.y < 0 || flame.y > 1) {
            continue;
        }

        float horizontalDistance = flame.x - referenceX;
        float verticalDistance = flame.y - referenceY;

        float distance = std::hypot(horizontalDistance, verticalDistance);
        // If we already selected a flame, ignore detections too far from its last position.
        // They may belong to another flame. Continue with the next detection.
        if (hasSelectedFlame && distance > MAX_TARGET_DISTANCE) {
            continue;
        }

        // Compare each flame with the best candidate found so far in this image.
        // Keep the first candidate, then replace it if another is closer to the reference.
        // If both are equally close, prefer the one with higher confidence.
        bool chooseThisFlame = false;

        if (!found) {
            chooseThisFlame = true;
        } else if (distance < closestDistance) {
            chooseThisFlame = true;
        } else if (distance == closestDistance && flame.confidence > match.confidence) {
            chooseThisFlame = true;
        }

        // Store this flame as the best candidate and remember its distance,
        // so the following flames can be compared with it.
        if (chooseThisFlame) {
            match = flame;
            closestDistance = distance;
            found = true;
        }
    }
    return found;
}

void AimController::rememberDetection(bool detected)
{
    recentDetections.push_back(detected);
    if (recentDetections.size() > CONFIRMATION_WINDOW) {
        recentDetections.pop_front();
    }

    confirmedFrames = 0;
    for (bool frameHasFlame : recentDetections) {
        if (frameHasFlame) {
            confirmedFrames++;
        }
    }
}

void AimController::handleMissingFlame(double currentTime)
{
    if (state == State::Search) {
        rememberDetection(false);
    } else {
        missingFrames++;
        if (missingFrames >= MAX_MISSING_FRAMES ||
            currentTime - lastDetectionTime > MAX_MISSING_SECONDS) {
            beginReturn(currentTime);
        }
    }

    centeredFrames = 0;
    centeredSince = -1;
    if (state == State::Hold) {
        state = State::Track;
    }
}

bool AimController::aimAtSelectedFlame(double currentTime)
{
    float horizontalError = selectedFlame.x - AIM_HORIZONTAL_POSITION;
    float verticalError = selectedFlame.y - IMAGE_CENTER;
    bool centeredHorizontally = std::abs(horizontalError) <= CENTER_DEADBAND;
    bool centeredVertically = std::abs(verticalError) <= CENTER_DEADBAND;

    if (centeredHorizontally && centeredVertically) {
        holdCenteredFlame(currentTime);
        return false;
    }

    state = State::Track;
    centeredFrames = 0;
    centeredSince = -1;
    int panChange = calculatePulseChange(horizontalError, panDirection);
    int tiltChange = calculatePulseChange(verticalError, tiltDirection);
    int nextPan = std::clamp(pan + panChange, PULSO_MIN_US, PULSO_MAX_US);
    int nextTilt = std::clamp(tilt + tiltChange, PULSO_MIN_US, PULSO_MAX_US);

    if (nextPan == pan && nextTilt == tilt) {
        beginReturn(currentTime);
        return false;
    }

    pan = nextPan;
    tilt = nextTilt;
    nextObservationTime = currentTime + MOVE_SETTLE_SECONDS;
    return true;
}

int AimController::calculatePulseChange(float positionError, int direction) const
{
    if (std::abs(positionError) <= CENTER_DEADBAND) {
        return 0;
    }
    int pulseChange = static_cast<int>(std::round(positionError * PULSE_GAIN));
    pulseChange = std::clamp(pulseChange, -MAX_PULSE_STEP_US, MAX_PULSE_STEP_US);
    return direction * pulseChange;
}

void AimController::holdCenteredFlame(double currentTime)
{
    // The first centered observation starts the 2-second waiting period.
    if (centeredSince < 0) {
        centeredSince = currentTime;
    }

    // Keep the existing frame confirmation before declaring the aim stable.
    if (state != State::Hold) {
        centeredFrames++;
        if (centeredFrames >= REQUIRED_CENTERED_FRAMES) {
            state = State::Hold;
        }
    }

    // Every attempt still requires a confirmed, continuously centered target.
    if (state != State::Hold) {
        return;
    }
    if (currentTime - centeredSince < CENTERED_BEFORE_PUMP_SECONDS) {
        return;
    }
    if (pumpAttempts >= MAX_PUMP_ATTEMPTS) {
        return;
    }
    if (waitingForPumpToFinish) {
        return;
    }
    if (currentTime < nextPumpAttemptTime) {
        return;
    }

    // Request one pulse now, then wait for main.cpp to report that it ended.
    pumpRequested = true;
    pumpAttempts++;
    waitingForPumpToFinish = true;
}

void AimController::beginReturn(double currentTime)
{
    state = State::Return;
    confirmedFrames = 0;
    missingFrames = 0;
    centeredFrames = 0;
    centeredSince = -1;
    pumpAttempts = 0;
    waitingForPumpToFinish = false;
    nextPumpAttemptTime = 0;
    pumpRequested = false;
    recentDetections.clear();
    nextObservationTime = currentTime;
}

bool AimController::moveHome(double currentTime)
{
    if (currentTime < nextObservationTime) {
        return false;
    }

    int oldPan = pan;
    int oldTilt = tilt;
    int panChange = std::clamp(homePanPulse - pan, -MAX_PULSE_STEP_US, MAX_PULSE_STEP_US);
    int tiltChange = std::clamp(homeTiltPulse - tilt, -MAX_PULSE_STEP_US, MAX_PULSE_STEP_US);
    pan += panChange;
    tilt += tiltChange;
    nextObservationTime = currentTime + RETURN_STEP_SECONDS;

    if (pan == homePanPulse && tilt == homeTiltPulse) {
        state = State::Cooldown;
        nextObservationTime = currentTime + HOME_SETTLE_SECONDS;
    }
    return pan != oldPan || tilt != oldTilt;
}
