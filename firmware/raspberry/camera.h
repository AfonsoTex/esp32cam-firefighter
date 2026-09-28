#indef CAMERA_H
#define CAMERA_H

#include <opencv2/opencv.hpp>

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