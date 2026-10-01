#include "camera.h"
#include <iostream>
#include <string>

Camera::Camera() {}

Camera::~Camera() {
    libertar();
}

bool Camera::abrir(int index) {
    // Capture through libcamera and deliver BGR frames to OpenCV.
    const std::string pipeline =
    "libcamerasrc ! "
    "video/x-raw,format=NV12,width=640,height=480,"
    "framerate=30/1,colorimetry=bt709 ! "
    "videoconvert ! "
    "video/x-raw,format=BGR ! "
    "appsink max-buffers=1 drop=true sync=false";

    cap.open(pipeline, cv::CAP_GSTREAMER);

    if (!cap.isOpened()) {
        std::cerr << "Could not open the camera." << std::endl;
        return false;
    }
    return true;
}

bool Camera::lerFrame(cv::Mat& frame) {
    if (!cap.isOpened()) return false;

    cv::Mat frameOriginal;
    cap >> frameOriginal; // Capture a BGR frame from GStreamer.

    if (frameOriginal.empty()) return false;

    // Resize the frame to the model input size.
    cv::resize(frameOriginal, frame, cv::Size(128, 128));

    // Correct the camera orientation before inference and display.
    cv::rotate(frame, frame, cv::ROTATE_180);

    // Convert BGR to RGB for the model.
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

    return true;
}

void Camera::libertar() {
    if (cap.isOpened()) {
        cap.release();
    }
}
