/**
 * @file AudioToolsAviWebcam.ino
 * @brief Plays a Motion-JPEG AVI file from an SD card as webcam picture:
 * the DemuxerAVI of arduino-audio-tools extracts the JPEG frames and
 * UVCVideoOutput sends them unchanged (no decoding), at the file's frame
 * rate. Resolution and frame rate are taken from the AVI header. The video
 * loops.
 *
 * Create a matching file with e.g.
 *   ffmpeg -i input.mp4 -vf "scale=320:240,fps=15" -c:v mjpeg -q:v 5 -an video.avi
 *
 * Dependency: https://github.com/pschatzmann/arduino-audio-tools
 * Board settings: see TestPatternWebcam. ESP32: the SD card is read with
 * SD_MMC in 1-bit mode (pins of the ESP32-S3-EYE and most ESP32-S3 camera
 * boards), other boards use SPI.
 */
#include "AudioTools.h"
// the video classes of arduino-audio-tools need FreeRTOS: on RP2040 select
// Tools > Operating System > FreeRTOS, on STM32 install STM32FreeRTOS
#include "AudioTools/Concurrency/RTOS.h"
#include "AudioTools/AudioCodecs/ContainerAVI.h"
#include "TinyUVCAudioTools.h"

#if defined(ARDUINO_ARCH_ESP32)
#include <SD_MMC.h>
#define CARD SD_MMC
const int SD_CLK = 39, SD_CMD = 38, SD_D0 = 40;
USBCDC USBSerial;  // USB serial port in addition to the camera
#define LOG USBSerial
#else
#include <SD.h>
#include <SPI.h>
#define CARD SD
const int SD_CS = SS;
#define LOG Serial
#endif

const char *path = "/Videos/output176x144-mjpeg.avi";
const uint32_t MAX_JPEG = 64 * 1024;

UVCDevice uvc;
UVCVideoOutput out(uvc, PixelFormat::MJPEG);
DemuxerAVI demuxer;
File file;
uint8_t buffer[1024];

bool beginCard() {
#if defined(ARDUINO_ARCH_ESP32)
  CARD.setPins(SD_CLK, SD_CMD, SD_D0);
  return CARD.begin("/sdcard", true);  // 1-bit mode
#else
  return CARD.begin(SD_CS);
#endif
}

bool openFile() {
  file = CARD.open(path);
  if (!file) return false;
  demuxer.begin();
  return true;
}

/// Feeds data to the demuxer (which may take it in several steps)
void feed(const uint8_t *data, size_t n) {
  size_t done = 0;
  while (done < n) {
    size_t w = demuxer.write(data + done, n - done);
    if (w == 0) break;
    done += w;
  }
}

void setup() {
  LOG.begin(115200);
  UVCLog.begin(LOG, UVCLogLevel::Info);
  demuxer.setOutputVideo(out);
  demuxer.setMute(true);  // video only
  out.setMaxFrameSize(MAX_JPEG);
  out.setPacing(true);  // play at the frame rate, not as fast as possible

  if (!beginCard() || !openFile()) {
    LOG.println("Could not open the video file");
    return;
  }
  // read the AVI header to find out size, frame rate and codec
  VideoInfo info;
  for (int i = 0; i < 256 && !info; i++) {
    size_t n = file.read(buffer, sizeof(buffer));
    if (n == 0) break;
    feed(buffer, n);
    info = demuxer.getVideoInfo();
  }
  if (!info || info.format != VideoFormat::MJPEG) {
    LOG.println("Not a Motion-JPEG AVI");
    return;
  }
  LOG.printf("%s: %dx%d, %.1f fps\n", path, info.width, info.height, info.fps);
  addResolution(uvc, info, MAX_JPEG);
  uvc.begin();
  // continue from here: the demuxer is positioned after the header
}

void loop() {
  if (!file || !uvc.isStreaming()) return;
  size_t n = file.read(buffer, sizeof(buffer));
  if (n == 0) {  // end of file: start again
    file.close();
    openFile();
    return;
  }
  feed(buffer, n);
}
