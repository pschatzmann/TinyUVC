// UVCDisplayDriver: TinyGPU surfaces sent as webcam frames
#include <vector>

#include "TestUtils.h"
#if __has_include(<TinyJPEGEncoder.h>)
#include <TinyJPEGEncoder.h>
#endif
#include "TinyUVC/TinyGPU.h"

using namespace tinygpu;

/// YUY2: the frame matches the surface, converted from TinyGPU's RGB565
static void testYUY2() {
  UVCDevice uvc;
  UVCDisplayDriver<RGB565> display(uvc);
  CHECK(display.addResolution(UVCFormat::YUY2, 32, 24, 30));
  CHECK(display.addResolution(UVCFormat::YUY2, 16, 12, 30));
  CHECK(display.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  std::vector<std::vector<uint8_t>> received;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    received.emplace_back(f.data, f.data + f.size);
  });

  // not streaming: the first resolution, nothing is sent
  CHECK_EQ(display.width(), 32u);
  CHECK_EQ(display.height(), 24u);
  FrameBufferRGB565 fb(32, 24, FontRGB565);
  CHECK(fb.begin());
  CHECK(!display.writeData(fb));

  CHECK(host.startStream(UVCFormat::YUY2, 16, 12));
  CHECK(display.sizeChanged(fb.width(), fb.height()));
  CHECK(!display.writeData(fb));  // wrong size
  CHECK_EQ(display.mismatched(), 1u);
  CHECK(fb.resize(display.width(), display.height()));
  CHECK(!display.sizeChanged(fb.width(), fb.height()));

  display.setPacing(false);
  for (int n = 0; n < 3; n++) {
    fb.clear(RGB565(255, 255, 255));
    fb.drawLine(0, n, 15, 11, RGB565(255, 0, 0));
    fb.fillRect(2, 2, 4, 4, RGB565(0, 0, 255));
    std::vector<uint8_t> pixels(fb.data(), fb.data() + fb.size());
    CHECK(display.writeData(fb));
    // the next frame can be drawn while this one is sent: it is a copy
    fb.clear(RGB565(0, 0, 0));
    CHECK(uvc.waitReady(1000));
    host.pollAll();
    CHECK_EQ(received.size(), (size_t)n + 1);
    std::vector<uint8_t> expected(16 * 12 * 2);
    UVCConvert::toYUY2(PixelFormat::RGB565_BE, pixels.data(), expected.data(),
                       16 * 12 / 2);
    CHECK(received.back() == expected);
  }
  CHECK_EQ(display.frames(), 3u);

  // partial writes compose a frame, flush() sends it
  Sprite<RGB565> tile(8, 6, FontRGB565);
  CHECK(tile.begin());
  tile.clear(RGB565(0, 255, 0));
  for (size_t y = 0; y < 12; y += 6)
    for (size_t x = 0; x < 16; x += 8) CHECK(display.writeData(tile, x, y));
  CHECK(display.flush());
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.size(), 4u);
  std::vector<uint8_t> green(16 * 12 * 2);
  for (size_t i = 0; i < 16 * 12; i++) {
    green[i * 2] = tile.data()[0];
    green[i * 2 + 1] = tile.data()[1];
  }
  std::vector<uint8_t> expected(16 * 12 * 2);
  UVCConvert::toYUY2(PixelFormat::RGB565_BE, green.data(), expected.data(),
                     16 * 12 / 2);
  CHECK(received.back() == expected);
  CHECK(!display.flush());  // nothing new
}

/// Pacing: writeData() follows the frame rate the host selected
static void testPacing() {
  UVCDevice uvc;
  UVCDisplayDriver<RGB565> display(uvc);
  CHECK(display.addResolution(16, 12, 30));
  CHECK(display.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.startStream(UVCFormat::YUY2, 16, 12));
  FrameBufferRGB565 fb(16, 12, FontRGB565);
  CHECK(fb.begin());
  const uint32_t start = platform::millis();
  int sent = 0;
  while (platform::millis() - start < 500) {
    if (display.writeData(fb)) sent++;
    host.pollAll();
  }
  // 30 fps: about 15 frames in 500 ms
  CHECK(sent >= 10 && sent <= 17);
}

#if TINYUVC_HAS_JPEG
/// MJPEG: every frame is a complete JPEG of the selected size
static void testMJPEG() {
  UVCDevice uvc;
  UVCDisplayDriver<RGB565> display(uvc);
  CHECK(display.addResolution(UVCFormat::MJPEG, 64, 48, 30));
  CHECK(display.addResolution(UVCFormat::YUY2, 16, 12, 30));
  CHECK(display.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  std::vector<std::vector<uint8_t>> received;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    received.emplace_back(f.data, f.data + f.size);
  });
  CHECK(host.startStream(UVCFormat::MJPEG, 64, 48));
  CHECK(display.isJpeg());
  FrameBufferRGB565 fb(64, 48, FontRGB565);
  CHECK(fb.begin());
  display.setPacing(false);
  for (int n = 0; n < 3; n++) {
    fb.clear(RGB565(255, 255, 255));
    fb.drawCircle(32, 24, 10 + n, RGB565(255, 0, 0));
    CHECK(display.writeData(fb));
  }
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.size(), 3u);
  for (auto &f : received) {
    CHECK(f.size() > 100);
    CHECK(f[0] == 0xFF && f[1] == 0xD8);
    CHECK(f[f.size() - 2] == 0xFF && f[f.size() - 1] == 0xD9);
  }
  CHECK_EQ(display.lastJpegSize(), received.back().size());

  // the host switches to YUY2: the driver follows
  host.stopStream();
  CHECK(host.startStream(UVCFormat::YUY2, 16, 12));
  CHECK(!display.isJpeg());
  CHECK(fb.resize(16, 12));
  CHECK(display.writeData(fb));
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.back().size(), 16u * 12 * 2);
}
#endif

int main() {
  RUN(testYUY2);
  RUN(testPacing);
#if TINYUVC_HAS_JPEG
  RUN(testMJPEG);
#else
  std::printf("TinyJPEG not available: MJPEG test skipped\n");
#endif
  return testMain();
}
