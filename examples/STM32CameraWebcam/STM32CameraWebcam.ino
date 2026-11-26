/**
 * @file STM32CameraWebcam.ino
 * @brief USB webcam with a WeAct STM32H750 board and an OV7725 camera on
 * its DCMI connector (https://github.com/pschatzmann/TinyCamera). The
 * RGB565 frames are converted to YUY2 while they are sent over the board's
 * USB OTG port (full speed).
 *
 * Needs the Adafruit TinyUSB Library. Board settings: Tools > USB support >
 * "None" (TinyUSB drives the USB port).
 */
#include "TinyCameraPins.h"
#include "TinyUVC.h"  // first: lets Arduino find the library
#include "TinyUVC/TinyCamera.h"

using namespace tiny_camera;

UVCDevice uvc;
UVCCamera camera(uvc);

void setup() {
  camera_config_t config = TinyCamera::defaultConfig();
  setPinsWeActStm32H750(config);
  config.pixel_format = PIXFORMAT_RGB565;  // OV7725 has no JPEG encoder
  config.frame_size = FRAMESIZE_QVGA;
  camera.addFrameSize(FRAMESIZE_QQVGA);
  camera.addFrameSize(FRAMESIZE_QVGA);
  camera.setFps({10, 5});
  camera.begin(config);
}

void loop() { camera.copy(); }
