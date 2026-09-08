#pragma once
// ── Câmara OV2640 e streaming de vídeo por UDP ──────────────────────
#include "esp_camera.h"

void setup_camera();
void task_camara(void *parameter);
