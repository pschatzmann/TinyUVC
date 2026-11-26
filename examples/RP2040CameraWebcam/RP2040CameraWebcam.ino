/**
 * @file RP2040CameraWebcam.ino
 * @brief USB webcam with a Raspberry Pi Pico (RP2040/RP2350) and an OV7670
 * or OV2640 camera module (PicoCamera, bundled with arduino-pico, via
 * https://github.com/pschatzmann/TinyCamera). The sensor's RGB565 frames
 * are converted to YUY2 while they are sent.
 *
 * Board settings: Tools > USB Stack > "Adafruit TinyUSB".
 *
 * RP2040 camera modules have no standard pinout: set the pins below.
 */
#include "TinyUVC/TinyCamera.h"

using namespace tiny_camera;

UVCDevice uvc;
UVCCamera camera(uvc);

void setup() {
  Serial.begin(115200);

  camera_config_t config = TinyCamera::defaultConfig();
  config.pixel_format = PIXFORMAT_RGB565;
  config.frame_size = FRAMESIZE_QVGA;
  // camera pins: adjust to your wiring
  config.pin_d0 = 0;  // D0..D7 must be consecutive GPIOs (PIO)
  config.pin_d1 = 1;
  config.pin_d2 = 2;
  config.pin_d3 = 3;
  config.pin_d4 = 4;
  config.pin_d5 = 5;
  config.pin_d6 = 6;
  config.pin_d7 = 7;
  config.pin_vsync = 8;
  config.pin_href = 9;
  config.pin_pclk = 10;
  config.pin_xclk = 11;
  config.pin_sccb_sda = 12;
  config.pin_sccb_scl = 13;

  camera.setFps(10);
  if (!camera.begin(config)) {
    Serial.println("camera.begin() failed");
  }
}

void loop() { camera.copy(); }
