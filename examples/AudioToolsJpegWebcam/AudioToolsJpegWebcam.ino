/**
 * @file AudioToolsJpegWebcam.ino
 * @brief MJPEG webcam on a board without a JPEG encoder (e.g. RP2040): an
 * RGB565 picture is compressed by the MJPEGEncoder of arduino-audio-tools
 * and sent via UVCVideoOutput. Compressing frames lets bigger resolutions
 * and frame rates through full speed USB than uncompressed YUY2.
 *
 * Dependencies: https://github.com/pschatzmann/arduino-audio-tools and
 * https://github.com/pschatzmann/TinyJPEG
 *
 * Board settings: see TestPatternWebcam.
 */
#include "AudioTools.h"
// the video classes of arduino-audio-tools need FreeRTOS: on RP2040 select
// Tools > Operating System > FreeRTOS, on STM32 install STM32FreeRTOS
#include "AudioTools/Concurrency/RTOS.h"
#include "AudioTools/Video/CodecJPEG.h"
#include "TinyUVC.h"  // first: lets Arduino find the library
#include "TinyUVC/AudioTools.h"

#if defined(ARDUINO_ARCH_ESP32)
USBCDC USBSerial;  // USB serial port in addition to the camera
#define LOG USBSerial
#else
#define LOG Serial
#endif

const uint16_t W = 320, H = 240;
UVCDevice uvc;
UVCVideoOutput out(uvc, PixelFormat::MJPEG);
MJPEGEncoder encoder(out);
uint16_t *picture = nullptr;  // RGB565
uint32_t frameCount = 0;

void drawPicture(uint32_t n) {
  static const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                   0xF81F, 0xF800, 0x001F, 0x0000};
  for (uint16_t y = 0; y < H; y++)
    for (uint16_t x = 0; x < W; x++)
      picture[y * W + x] = bars[((x + 4 * n) / (W / 8)) % 8];
}

void setup() {
  LOG.begin(115200);
  UVCLog.begin(LOG, UVCLogLevel::Info);
  picture = new uint16_t[W * H];

  // the JPEG size depends on the content: tell the host the maximum
  uvc.addResolution(UVCFormat::MJPEG, W, H, 10, 40 * 1024);
  uvc.begin();

  out.setMaxFrameSize(40 * 1024);
  encoder.setSize(W, H);
  encoder.setVideoFormat(VideoFormat::RGB565);
  encoder.setQuality(80);
  encoder.begin();
}

void loop() {
  if (uvc.isReady()) {
    drawPicture(frameCount++);
    encoder.write((const uint8_t *)picture, W * H * 2);  // -> out -> uvc
    delay(uvc.settings().interval / 10000);
  }
}
