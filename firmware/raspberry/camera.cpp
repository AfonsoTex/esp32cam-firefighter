#include "camera.h"
#include <iostream>

Camera::Camera() {}

Camera::~Camera() {
    libertar();
}

bool Camera::abrir(int index) {
    cap.open(index);
    if (!cap.isOpened()) {
        std::cerr << "Erro ao abrir a câmara." << std::endl;
        return false;
    }
    return true;
}

bool Camera::lerFrame(cv::Mat& frame) {
    if (!cap.isOpened()) return false;
    
    cv::Mat frameOriginal;
    cap >> frameOriginal; // Captura no formato original (BGR)
    
    if (frameOriginal.empty()) return false;

    // 1. Redimensionar para 128x128
    cv::resize(frameOriginal, frame, cv::Size(128, 128));

    // 2. Converter de BGR para RGB
    cv::cvtColor(frame, frame, cv::COLOR_BGR2RGB);

    return true;
}

void Camera::libertar() {
    if (cap.isOpened()) {
        cap.release();
    }
}