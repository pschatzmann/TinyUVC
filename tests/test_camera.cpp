// UVCCamera with TinyCamera's desktop backend (synthetic test pattern)
#include <vector>

#include "TestUtils.h"
#include "TinyUVC/TinyCamera.h"

using namespace tiny_camera;

static camera_config_t testConfig(pixformat_t fmt) {
  camera_config_t cfg = TinyCamera::defaultConfig();
  cfg.source = CAMERA_SOURCE_TEST_PATTERN;
  cfg.test_pattern_fps = 0;  // unthrottled
  cfg.pixel_format = fmt;
  cfg.frame_size = FRAMESIZE_QQVGA;
  return cfg;
}

/// Streams until the host received `frames` more frames (max 3 seconds):
/// lets the camera copy and the host read
static void run(UVCCamera &camera, UVCHostSimulator &host, int frames) {
  const uint32_t target = host.framesReceived() + frames;
  const uint32_t start = platform::millis();
  while (host.framesReceived() < target && platform::millis() - start < 3000) {
    camera.copy();
    host.poll();
  }
}

/// RGB565 camera -> YUY2 webcam, the host switches resolution
static void testYuy2() {
  UVCDevice uvc;
  UVCCamera camera(uvc);
  camera.addFrameSize(FRAMESIZE_QQVGA);
  camera.addFrameSize(FRAMESIZE_QVGA);
  camera.setFps({60, 30});
  CHECK(camera.begin(testConfig(PIXFORMAT_RGB565)));
  CHECK(uvc.isActive());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK_EQ(host.formats().size(), 1u);
  CHECK(host.formats()[0].format == UVCFormat::YUY2);
  CHECK_EQ(host.formats()[0].frames.size(), 2u);

  size_t lastSize = 0;
  uint32_t errors = 0;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    lastSize = f.size;
    errors += f.error;
  });
  CHECK(host.startStream(UVCFormat::YUY2, 160, 120, 60));
  run(camera, host, 3);
  CHECK(host.framesReceived() >= 3);
  CHECK_EQ(lastSize, 160u * 120 * 2);

  CHECK(host.stopStream());
  CHECK(host.startStream(UVCFormat::YUY2, 320, 240, 60));
  uint32_t before = host.framesReceived();
  run(camera, host, 3);
  CHECK(host.framesReceived() >= before + 3);
  CHECK_EQ(lastSize, 320u * 240 * 2);
  CHECK_EQ(errors, 0u);
  CHECK_EQ(uvc.framesDropped(), 0u);
}

/// The frame rate selected by the host limits how often frames are sent
static void testPacing() {
  UVCDevice uvc;
  UVCCamera camera(uvc);
  camera.setFps(10);
  CHECK(camera.begin(testConfig(PIXFORMAT_RGB565)));
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.startStream(UVCFormat::YUY2, 160, 120));
  uint32_t start = platform::millis();
  while (platform::millis() - start < 1000) {
    camera.copy();
    host.poll();
  }
  // 10 fps for 1 s: 10 frames (at 0, 100, ... 900 ms). Without pacing it
  // would be hundreds; a busy machine (CI) may deliver fewer.
  CHECK(host.framesReceived() >= 3);
  CHECK(host.framesReceived() <= 11);
}

#if defined(TINY_CAMERA_DESKTOP_HAS_JPEG)
/// JPEG camera -> MJPEG webcam
static void testMjpeg() {
  UVCDevice uvc;
  UVCCamera camera(uvc);
  camera.setFps(60);
  CHECK(camera.begin(testConfig(PIXFORMAT_JPEG)));
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.formats()[0].format == UVCFormat::MJPEG);
  int good = 0;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    if (!f.error && f.size > 4 && f.data[0] == 0xFF && f.data[1] == 0xD8 &&
        f.data[f.size - 2] == 0xFF && f.data[f.size - 1] == 0xD9)
      good++;
  });
  CHECK(host.startStream(UVCFormat::MJPEG, 160, 120));
  run(camera, host, 3);
  CHECK(good >= 3);
  CHECK_EQ(good, (int)host.framesReceived());
}

/// Both formats offered: switching restarts the camera with the other
/// pixel format
static void testBothFormats() {
  UVCDevice uvc;
  UVCCamera camera(uvc);
  camera.setFormats(true, true);
  camera.setFps(60);
  CHECK(camera.begin(testConfig(PIXFORMAT_JPEG)));
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK_EQ(host.formats().size(), 2u);
  size_t lastSize = 0;
  bool lastJpeg = false;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    lastSize = f.size;
    lastJpeg = f.size > 2 && f.data[0] == 0xFF && f.data[1] == 0xD8;
  });
  CHECK(host.startStream(UVCFormat::YUY2, 160, 120));
  run(camera, host, 2);
  CHECK_EQ(lastSize, 160u * 120 * 2);
  CHECK(host.stopStream());
  CHECK(host.startStream(UVCFormat::MJPEG, 160, 120));
  run(camera, host, 2);
  CHECK(lastJpeg);
}
#endif

int main() {
  TinyCameraLogger.begin(TinyCameraLogLevel::Warn);
  RUN(testYuy2);
  RUN(testPacing);
#if defined(TINY_CAMERA_DESKTOP_HAS_JPEG)
  RUN(testMjpeg);
  RUN(testBothFormats);
#else
  std::printf("TinyJPEG not available: MJPEG camera tests skipped\n");
#endif
  return testMain();
}
