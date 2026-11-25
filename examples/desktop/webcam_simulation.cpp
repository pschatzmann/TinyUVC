/**
 * @file webcam_simulation.cpp
 * @brief Runs the complete webcam on the desktop: TinyCamera (test pattern
 * or your real webcam) -> UVCCamera -> UVCDevice -> class driver ->
 * simulated USB host, and stores what the host receives in a file:
 *
 *   ./webcam_simulation --format mjpeg --size 320x240 --frames 60 --out cam.mjpeg
 *   ffplay -f mjpeg cam.mjpeg
 *
 *   ./webcam_simulation --format yuy2 --size 160x120 --out cam.yuv
 *   ffplay -f rawvideo -pixel_format yuyv422 -video_size 160x120 cam.yuv
 *
 * Options: --format mjpeg|yuy2, --size QQVGA|QVGA|VGA|<w>x<h> (one of
 * 160x120, 320x240, 640x480), --fps N, --frames N, --source pattern|webcam,
 * --hs (high speed, 512 byte packets), --out FILE
 */
#include <cstdio>
#include <cstring>
#include <string>

#include "TinyUVCCamera.h"

using namespace tiny_camera;

static framesize_t parseSize(const std::string &s, uint16_t &w, uint16_t &h) {
  if (s == "QQVGA" || s == "160x120") { w = 160; h = 120; return FRAMESIZE_QQVGA; }
  if (s == "VGA" || s == "640x480") { w = 640; h = 480; return FRAMESIZE_VGA; }
  w = 320;
  h = 240;
  return FRAMESIZE_QVGA;
}

int main(int argc, char **argv) {
  std::string format = "mjpeg", size = "QVGA", source = "pattern", out = "";
  int frames = 60, fps = 15;
  bool hs = false;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() { return i + 1 < argc ? std::string(argv[++i]) : std::string(); };
    if (a == "--format") format = next();
    else if (a == "--size") size = next();
    else if (a == "--frames") frames = std::stoi(next());
    else if (a == "--fps") fps = std::stoi(next());
    else if (a == "--source") source = next();
    else if (a == "--out") out = next();
    else if (a == "--hs") hs = true;
    else {
      std::printf("unknown option %s\n", a.c_str());
      return 1;
    }
  }
  const bool mjpeg = format == "mjpeg";
  if (out.empty()) out = mjpeg ? "webcam.mjpeg" : "webcam.yuv";
  uint16_t w, h;
  const framesize_t fs = parseSize(size, w, h);

  UVCLog.setLevel(UVCLogLevel::Info);
  TinyCameraLogger.begin(TinyCameraLogLevel::Warn);
  desktopSettings().maxPacketSize = hs ? 512 : 64;

  // --- the "microcontroller" side
  UVCDevice uvc;
  UVCCamera camera(uvc);
  camera.addFrameSize(fs);
  camera.setFps((uint8_t)fps);
  camera_config_t cfg = TinyCamera::defaultConfig();
  cfg.source = source == "webcam" ? CAMERA_SOURCE_AUTO : CAMERA_SOURCE_TEST_PATTERN;
  cfg.pixel_format = mjpeg ? PIXFORMAT_JPEG : PIXFORMAT_RGB565;
  cfg.frame_size = fs;
  if (!camera.begin(cfg)) {
    std::printf("camera start failed%s\n",
                mjpeg ? " (JPEG needs TinyJPEG on the desktop)" : "");
    return 1;
  }

  // --- the "PC" side
  UVCHostSimulator host;
  if (!host.connect(uvc)) {
    std::printf("enumeration failed: %s\n", host.error().c_str());
    return 1;
  }
  std::printf("device offers:\n");
  for (auto &f : host.formats())
    for (auto &fr : f.frames)
      std::printf("  %s %ux%u, %u fps\n", toString(f.format), fr.width, fr.height,
                  (unsigned)intervalToFps(fr.defaultInterval));

  FILE *file = std::fopen(out.c_str(), "wb");
  if (!file) {
    std::printf("cannot write %s\n", out.c_str());
    return 1;
  }
  size_t bytes = 0;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    if (f.error) return;
    std::fwrite(f.data, 1, f.size, file);
    bytes += f.size;
  });
  if (!host.startStream(mjpeg ? UVCFormat::MJPEG : UVCFormat::YUY2, w, h, fps)) {
    std::printf("stream start failed: %s\n", host.error().c_str());
    return 1;
  }

  const uint32_t start = platform::millis();
  while (host.framesReceived() < (uint32_t)frames) {
    camera.copy();  // what loop() does on the microcontroller
    host.poll();    // the host reads the bulk endpoint
  }
  const uint32_t ms = platform::millis() - start;
  host.stopStream();
  std::fclose(file);

  std::printf("%u frames (%u with errors) in %u ms = %.1f fps, %u USB transfers "
              "(%u zero length packets), %zu bytes -> %s\n",
              host.framesReceived(), host.framesWithError(), (unsigned)ms,
              ms ? host.framesReceived() * 1000.0 / ms : 0.0, host.transfers(),
              host.zeroLengthPackets(), bytes, out.c_str());
  if (mjpeg)
    std::printf("play: ffplay -f mjpeg %s\n", out.c_str());
  else
    std::printf("play: ffplay -f rawvideo -pixel_format yuyv422 -video_size %ux%u %s\n",
                w, h, out.c_str());
  return host.framesWithError() == 0 ? 0 : 2;
}
