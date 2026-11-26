/**
 * @file ESP32CameraWebcam.ino
 * @brief Turns an ESP32-S3 camera board into a USB webcam: the sensor's
 * JPEG frames (OV2640, OV3660, OV5640) are streamed as MJPEG in two
 * resolutions. Uses https://github.com/pschatzmann/TinyCamera.
 *
 * Board settings: Tools > USB Mode > "USB-OTG (TinyUSB)",
 * USB CDC On Boot > "Disabled", PSRAM > "OPI PSRAM" (if your board has it).
 * The sketch adds its own USB serial port next to the camera (logging, and
 * uploads after a 1200 baud reset).
 *
 * setPinsEsp32S3Eye() fits the ESP32-S3-EYE and most generic ESP32-S3 camera
 * boards; for others use the preset of your board from TinyCameraPins.h
 * (setPinsXiaoEsp32S3, setPinsM5StackCamS3Unit, setPinsEsp32S3CamLcd, ...).
 */
#include "TinyCameraPins.h"
#include "TinyUVC.h"  // first: lets Arduino find the library
#include "TinyUVC/TinyCamera.h"

using namespace tiny_camera;

USBCDC USBSerial;  // USB serial port in addition to the camera
UVCDevice uvc;
UVCCamera camera(uvc);

void setup() {
  USBSerial.begin(115200);
  UVCLog.begin(USBSerial, UVCLogLevel::Info);
  TinyCameraLogger.begin(USBSerial, TinyCameraLogLevel::Info);

  camera_config_t config = TinyCamera::defaultConfig();
  setPinsEsp32S3Eye(config);  // also generic ESP32-S3 WROOM CAM boards
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = FRAMESIZE_VGA;  // the largest size, sizes the buffers
  config.jpeg_quality = 12;
  config.fb_count = 1;
  config.fb_location = CAMERA_FB_IN_PSRAM;
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY;

  camera.addFrameSize(FRAMESIZE_QVGA);  // default for the host
  camera.addFrameSize(FRAMESIZE_VGA);
  camera.setFps({15, 10});
  camera.setMaxJpegSize(100 * 1024);
  if (!camera.begin(config)) {
    USBSerial.println("camera.begin() failed");
  }
}

void loop() { camera.copy(); }
