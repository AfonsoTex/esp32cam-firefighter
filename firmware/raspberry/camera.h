#ifndef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp> 
// Includes OpenCV image processing and video capture interfaces.

class Camera{
    private:
        cv::VideoCapture cap;

    public:
        Camera();
        ~Camera();

        bool abrir(int index = 0);
        bool lerFrame(cv::Mat& frame);
        void libertar();
};
#endif