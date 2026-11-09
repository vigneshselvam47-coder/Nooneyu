/*
  ESP32-CAM AI Image Classifier (AI-Thinker module)

  Captures a JPEG periodically and sends it to an AI inference server on your
  trusted local network. The server should accept POST image/jpeg and return
  JSON like: {"label":"person","confidence":0.94}

  Board: AI Thinker ESP32-CAM
  Arduino IDE: install the ESP32 board package and ArduinoJson library.
  Configure Wi-Fi and the inference endpoint below before uploading.
*/

#include "esp_camera.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Replace these with your Wi-Fi credentials.
const char *WIFI_SSID = "YOUR_WIFI_SSID";
const char *WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Use the address of your own inference server on the local network.
// Expected request: POST with Content-Type: image/jpeg and JPEG bytes as body.
// Expected response JSON: {"label":"person","confidence":0.94}
const char *INFERENCE_URL = "http://192.168.1.100:8000/classify";

const unsigned long CAPTURE_INTERVAL_MS = 5000;
const float ALERT_CONFIDENCE = 0.70f;
unsigned long lastCaptureMs = 0;

// AI-Thinker ESP32-CAM camera pin map.
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

bool initializeCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;
  config.jpeg_quality = 12;  // Lower value means higher quality/larger image.
  config.fb_count = psramFound() ? 2 : 1;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  const esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera initialization failed: 0x%x\n", err);
    return false;
  }
  return true;
}

void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
  }

  Serial.printf("\nConnected. Device IP: %s\n", WiFi.localIP().toString().c_str());
}

void classifyFrame() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fi disconnected; reconnecting...");
    WiFi.disconnect();
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    return;
  }

  camera_fb_t *frame = esp_camera_fb_get();
  if (frame == nullptr) {
    Serial.println("Failed to capture camera frame.");
    return;
  }

  HTTPClient http;
  http.setTimeout(15000);
  if (!http.begin(INFERENCE_URL)) {
    Serial.println("Could not initialize HTTP connection to inference server.");
    esp_camera_fb_return(frame);
    return;
  }

  http.addHeader("Content-Type", "image/jpeg");
  const int statusCode = http.POST(frame->buf, frame->len);
  esp_camera_fb_return(frame);

  if (statusCode <= 0) {
    Serial.printf("Inference request failed: %s\n", http.errorToString(statusCode).c_str());
    http.end();
    return;
  }

  const String response = http.getString();
  http.end();

  if (statusCode < 200 || statusCode >= 300) {
    Serial.printf("Inference server returned HTTP %d: %s\n", statusCode, response.c_str());
    return;
  }

  JsonDocument result;
  const DeserializationError parseError = deserializeJson(result, response);
  if (parseError) {
    Serial.printf("Invalid JSON from inference server: %s\n", parseError.c_str());
    return;
  }

  const char *label = result["label"] | "unknown";
  const float confidence = result["confidence"] | 0.0f;
  Serial.printf("AI result: %s (%.1f%% confidence)\n", label, confidence * 100.0f);

  if (confidence >= ALERT_CONFIDENCE) {
    Serial.printf("Detection alert: %s\n", label);
    // Add an alert action here, such as a status LED or a notification service.
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\nStarting ESP32-CAM AI classifier...");

  if (!initializeCamera()) {
    Serial.println("Restart the board after checking the camera wiring and board selection.");
    while (true) delay(1000);
  }

  connectToWiFi();
}

void loop() {
  const unsigned long now = millis();
  if (now - lastCaptureMs >= CAPTURE_INTERVAL_MS) {
    lastCaptureMs = now;
    classifyFrame();
  }
  delay(10);
}
