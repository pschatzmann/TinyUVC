/**
 * @file TestPatternWebcam.ino
 * @brief A USB webcam without a camera: sends moving color bars (generated
 * as RGB565 and converted to YUY2 on the fly). The quickest way to check
 * that TinyUVC works on your board: open the camera on the PC with e.g.
 * the Camera app (Windows), Photo Booth / QuickTime (macOS), or
 * `ffplay /dev/video0` / guvcview (Linux).
 *
 * Board settings:
 * - ESP32-S2/S3/P4: Tools > USB Mode > "USB-OTG (TinyUSB)",
 *   USB CDC On Boot > "Disabled". The sketch adds its own USB serial port
 *   next to the camera (logging, and uploads without pressing BOOT).
 * - RP2040/RP2350: Tools > USB Stack > "Pico SDK" (default) or
 *   "Adafruit TinyUSB"
 * - STM32: install the Adafruit TinyUSB Library, Tools > USB support >
 *   "None"
 */
#include "TinyUVC.h"

#if defined(ARDUINO_ARCH_ESP32)
USBCDC USBSerial;  // USB serial port in addition to the camera
#define LOG USBSerial
#else
#define LOG Serial
#endif

const uint16_t W = 160, H = 120;
UVCDevice uvc;
uint16_t frame[W * H];  // RGB565 (little endian on all supported MCUs)
uint32_t frameCount = 0;

void drawFrame(uint32_t n) {
  static const uint16_t bars[8] = {0xFFFF, 0xFFE0, 0x07FF, 0x07E0,
                                   0xF81F, 0xF800, 0x001F, 0x0000};
  for (uint16_t y = 0; y < H; y++) {
    for (uint16_t x = 0; x < W; x++) {
      uint16_t bar = ((x + n) / (W / 8)) % 8;
      frame[y * W + x] = y < H * 3 / 4 ? bars[bar] : (uint16_t)(((x * 31) / W) * 0x0841);
    }
  }
}

void setup() {
  LOG.begin(115200);
  UVCLog.begin(LOG, UVCLogLevel::Info);
  uvc.addResolution(UVCFormat::YUY2, W, H, 15);
  if (!uvc.begin()) {  // on ESP32 this also starts USB
    LOG.println("uvc.begin() failed");
  }
}

void loop() {
  if (uvc.isReady()) {
    drawFrame(frameCount++);
    uvc.writeFrame((const uint8_t *)frame, sizeof(frame), PixelFormat::RGB565_LE);
    // the host selected the frame rate: wait for the next frame
    delay(uvc.settings().interval / 10000);
  }
}
