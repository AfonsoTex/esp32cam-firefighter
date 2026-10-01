#pragma once
// Configures the OV2640 camera and streams video over UDP.
#include "esp_camera.h"

void setup_camera();
void task_camara(void *parameter);
