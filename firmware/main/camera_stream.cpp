#include "camera_stream.h"
#include "network_state.h"
#include <Arduino.h>

// ── OV2640 camera pin map (AI-Thinker ESP32-CAM)
// The ESP32-CAM board carries the ESP32-S module and the OV2640 sensor
// wired together on the same PCB.
// These pin numbers describe how the OV2640 sensor is physically wired
// to the ESP32 on this board. The wiring is fixed in the PCB and cannot
// be changed. The camera driver cannot detect it, so it must be told.
// Wrong number here = driver talks to the wrong pin = camera never responds.
// Source: ESP32_CAM_V1.6 schematic (docs/ESP32_CAM_V1_6.pdf).
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27

#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5

#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// clock frequency, pixel format, frame size, number of frame buffers.
// Declared empty here, filled field by field in setup_camera(), then handed
// to esp_camera_init(&config).
camera_config_t config;

void setup_camera() {
    // ── Camera configuration: fill the config struct field by field ───
    // Pin map (source: ESP32_CAM_V1.6 schematic, docs/ESP32_CAM_V1_6.pdf)
    config.pin_pwdn     = PWDN_GPIO_NUM;
    config.pin_reset    = RESET_GPIO_NUM;
    config.pin_xclk     = XCLK_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM;
    config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href  = HREF_GPIO_NUM;
    config.pin_pclk  = PCLK_GPIO_NUM;

    config.xclk_freq_hz = 20000000;      // 20 MHz clock the ESP32 generates and
                                         // feeds to the sensor (it has no
                                         // oscillator of its own). 20 MHz is the
                                         // OV2640 datasheet recommended value.
    config.ledc_timer   = LEDC_TIMER_1;  // LEDC timer+channel used to produce that
    config.ledc_channel = LEDC_CHANNEL_2;// XCLK. Channel 2, so it does not clash
                                         // with channels 0/1 driving the motors.
    config.pixel_format = PIXFORMAT_JPEG; // sensor outputs JPEG, already compressed
    config.frame_size = FRAMESIZE_VGA;
    config.jpeg_quality = 30;           // 0=best/largest, 63=worst; higher = smaller frame
    config.fb_count     = 2;              // two frame buffers: capture one while sending the other
    config.fb_location  = CAMERA_FB_IN_PSRAM; // frames go in PSRAM; too big for internal RAM
    config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY; // fetch a new frame only when a buffer is free

    esp_err_t err_cam = esp_camera_init(&config);
    if (err_cam != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err_cam);
    } else {
        Serial.println("Camera OK");
    }
}

/*void task_camara(void *parameter) {
    Serial.printf("Camera task running on Core %d\n", xPortGetCoreID());

    while (true) {
        if (!clienteVideo.connected()) {
            Serial.println("[VIDEO] Trying to connect to PC...");
            if (clienteVideo.connect(destino, VIDEO_PORT)) {
                Serial.println("[VIDEO] Connected!");
            } else {
                Serial.println("[VIDEO] Connection failed");
                vTaskDelay(2000 / portTICK_PERIOD_MS);
                continue;
            }
        }

        camera_fb_t *fb = esp_camera_fb_get();
        if (fb == NULL) {
            Serial.println("[VIDEO] Frame capture failed");
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        uint32_t tamanho = fb->len;
        clienteVideo.write((uint8_t*)&tamanho, 4);
        clienteVideo.write(fb->buf, fb->len);

        esp_camera_fb_return(fb);

        vTaskDelay(1 / portTICK_PERIOD_MS);
    }
}
*/

void task_camara(void *parameter) {
    // Initializes the UDP socket on the specified port to start sending camera frames
    udpVideo.begin(VIDEO_PORT);

    while (true) {
        // GOLDEN RULE: Only stream video if the control TCP connection is active.
        // This prevents UDP from flooding the network and blocking the TCP handshake.
        if (!client.connected()) {
            vTaskDelay(100 / portTICK_PERIOD_MS);
            continue;
        }

        // Capture a frame from the OV2640 camera sensor
        camera_fb_t *fb = esp_camera_fb_get();

        // Check if the frame was successfully captured
        if (fb != NULL) {
            // Begin building a UDP packet targeted at the destination IP and video port
            udpVideo.beginPacket(destino, VIDEO_PORT);

            // Write the raw JPEG buffer data into the UDP packet stream
            udpVideo.write(fb->buf, fb->len);

            // Send the completed UDP packet across the network
            udpVideo.endPacket();

            // Return the frame buffer to the driver pool so memory can be reused
            esp_camera_fb_return(fb);
        }

        // Delay task execution to maintain a target rate of approximately 30 frames per second
        vTaskDelay(33 / portTICK_PERIOD_MS);
    }
}
