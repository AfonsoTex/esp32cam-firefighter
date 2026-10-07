#pragma once

#include <deque>
#include <vector>

struct Detection {
    float x;
    float y;
    float width;
    float height;
    float confidence;
};

class AimController {
// Groups the logic and memory needed to aim at a flame.
// For example, stores the selected flame and how many frames confirmed it.
public:
    enum class State {
        Search,   // Find and confirm a flame across several frames.
        Track,    // Adjust the servo positions to center the flame.
        Hold,     // Keep the aim steady while the centered flame remains detected.
        Return,   // Return to the initial position.
        Cooldown  // Wait for the camera to settle before searching again.
};

    AimController(int homePan, int homeTilt, int horizontalDirection, int verticalDirection);
    // homePan: initial horizontal servo position, in pulse microseconds.
    // homeTilt: initial vertical servo position, in pulse microseconds.
    // horizontalDirection: 1 increases the pulse when the flame is to the right;
    // -1 decreases it. Choose the value that turns the camera towards the flame,
    // depending on the servo's physical mounting and rotation direction.





    // Initial target confirmation:
    // update() coordinates the initial target confirmation and aiming:
    // 1. findMatchingFlame() chooses a flame and stores it in matchingFlame.
    // 2. Copy matchingFlame into selectedFlame to remember it for the next frame.
    // 3. rememberDetection(true) records that we found it in this frame.
    // 4. If fewer than 4 recent frames confirmed it, return without aiming.
    //    On the next frame, look for the same flame and repeat the confirmation.
    // 5. Once confirmed, enter Track and call aimAtSelectedFlame()
    //    to calculate the servo corrections.
    bool update(const std::vector<Detection>& flames, double currentTime);

    // True for one update only, after the selected flame stayed centered for 2 seconds.
    // main.cpp uses this request to start one pump pulse when --pump is enabled.
    bool pumpRequested = false;

    // main.cpp calls this when the timed pulse ends or is cancelled.
    // The 10-second retry cooldown starts here, after the pump is off.
    void pumpPulseFinished(double currentTime);

    const char* status() const;

    int pan;
    int tilt;
    State state = State::Cooldown;

private:
    // Stores the horizontal home pulse, so we know where to return after pan changes.
    int homePanPulse;

    // Stores the vertical home pulse, so we know where to return after tilt changes.
    int homeTiltPulse;

    // 1 increases the pulse when the flame is to the right; -1 decreases it.
    // Choose the value that turns the camera towards the flame.
    int panDirection;

    // 1 increases the pulse when the flame is below the image center; -1 decreases it.
    // Choose the value that turns the camera towards the flame.
    int tiltDirection;

    // Stores the flame we choose to follow.
    // Initialize all fields to known values (zero).
    Detection selectedFlame{};

    // After selecting a flame, stores whether we keep finding it in the following frames.
    // When selecting another flame, clear this history and reset confirmedFrames to zero,
    // so detections of the previous flame do not count towards confirming the new one.
    std::deque<bool> recentDetections;
    //
    int confirmedFrames = 0;
    int missingFrames = 0;
    int centeredFrames = 0;

    // Start timing on the first centered observation; reset if it moves or disappears.
    double centeredSince = -1;

    // Count water pulses for this selected target, including cancelled pulses.
    // Brief detection losses do not reset the count. Returning home does.
    int pumpAttempts = 0;

    // Do not request another pulse until main.cpp confirms the pump is off.
    bool waitingForPumpToFinish = false;

    // Earliest time another attempt is allowed, measured after the previous pulse ends.
    double nextPumpAttemptTime = 0;

    // Stores the time until which we must wait before continuing.
    // Example: after moving a servo at 10.0 seconds, waiting 0.25 seconds
    // means setting this value to 10.25, giving the camera time to settle.
    // -1 means no waiting deadline has been set yet.
    double nextObservationTime = -1;
    double lastDetectionTime = 0;
    double lastFrameTime = -1;

    // Selects the flame to track:
    // - If no flame is selected, chooses the one nearest the image center.
    // - Otherwise, tries to find the same flame near its last known position.
    // Position matching can confuse flames that are very close together.
    // Stores the chosen detection in match and returns true if one was found.
    bool findMatchingFlame(const std::vector<Detection>& flames, Detection& match) const;

    void rememberDetection(bool detected);
    void handleMissingFlame(double currentTime);
    bool aimAtSelectedFlame(double currentTime);
    void holdCenteredFlame(double currentTime);
    int calculatePulseChange(float positionError, int direction) const;
    void beginReturn(double currentTime);
    bool moveHome(double currentTime);
};
