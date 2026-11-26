// UVCVideoOutput / UVCVideoCopy with arduino-audio-tools on the desktop
// (built with the Arduino Emulator)
#include <vector>

#include "TestUtils.h"
#include "TinyUVC/AudioTools.h"
#if __has_include(<TinyJPEGEncoder.h>)
#include "AudioTools/Video/CodecJPEG.h"
#define HAS_MJPEG_ENCODER 1
#endif

using namespace audio_tools;

/// RGB565 frames written in chunks via write()/flush() arrive as YUY2
static void testVideoOutputRaw() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::YUY2, 32, 24, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  std::vector<uint8_t> received;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    received.assign(f.data, f.data + f.size);
  });
  UVCVideoOutput out(uvc);
  CHECK(out.setVideoFormat(VideoFormat::RGB565));
  CHECK(!out.setVideoFormat(VideoFormat::H264));
  CHECK(out.pixelFormat() == PixelFormat::RGB565_LE);

  std::vector<uint8_t> frame(32 * 24 * 2);
  for (size_t i = 0; i < frame.size(); i++) frame[i] = (uint8_t)(i * 3);
  // not streaming: dropped
  out.write(frame.data(), frame.size());
  out.flush();
  CHECK_EQ(out.dropped(), 1u);

  CHECK(host.startStream(UVCFormat::YUY2, 32, 24));
  out.setTimeout(1000);
  for (int n = 0; n < 3; n++) {
    for (size_t off = 0; off < frame.size(); off += 100)
      out.write(frame.data() + off, std::min<size_t>(100, frame.size() - off));
    out.flush();
    CHECK(out.hadOutput());
  }
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(out.frames(), 3u);
  CHECK_EQ(host.framesReceived(), 3u);
  std::vector<uint8_t> expected(frame.size());
  UVCConvert::toYUY2(PixelFormat::RGB565_LE, frame.data(), expected.data(),
                     32 * 24 / 2);
  CHECK(received == expected);
}

/// Frame buffers: reserve(), release() while a frame is sent, memory
/// failures and too big frames
static void testVideoOutputMemory() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 64, 48, 30, 20000));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  std::vector<std::vector<uint8_t>> received;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    received.emplace_back(f.data, f.data + f.size);
  });
  CHECK(host.startStream(UVCFormat::MJPEG, 64, 48));

  auto jpeg = [](size_t len, uint8_t fill) {
    std::vector<uint8_t> f(len, fill);
    f[0] = 0xFF; f[1] = 0xD8; f[len - 2] = 0xFF; f[len - 1] = 0xD9;
    return f;
  };
  UVCVideoOutput out(uvc);
  CHECK(out.reserve(10000));
  // a frame in chunks, then flush(): sent from the reserved buffer
  std::vector<uint8_t> a = jpeg(9000, 1);
  out.write(a.data(), 4000);
  out.write(a.data() + 4000, a.size() - 4000);
  out.flush();
  CHECK(uvc.isSending());
  // release() while the frame is being sent: the frame stays intact
  out.release();
  host.pollAll();
  CHECK(!uvc.isSending());
  CHECK_EQ(received.size(), 1u);
  CHECK(received.size() == 1 && received[0] == a);

  // buffers grow again after release()
  std::vector<uint8_t> b = jpeg(15000, 2);
  out.write(b.data(), b.size());
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.size(), 2u);
  CHECK(received.size() == 2 && received[1] == b);

  // too big: dropped, the next frame works
  out.setMaxFrameSize(12000);
  const uint32_t dropped = out.dropped();
  for (int i = 0; i < 3; i++) out.write(b.data() + i * 5000, 5000);
  out.flush();
  CHECK_EQ(out.dropped(), dropped + 1);
  out.write(a.data(), a.size());
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.size(), 3u);

  // PSRAM only: no PSRAM on the desktop, so frames are dropped
  out.setMemoryType(UVCMemoryType::PSRAMOnly);
  CHECK(!out.reserve(1000));
  out.write(a.data(), a.size());
  CHECK_EQ(out.dropped(), dropped + 2);
  out.setMemoryType(UVCMemoryType::RAMOnly);
  out.write(a.data(), a.size());
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(received.size(), 4u);
}

/// A VideoFrameSource of fake JPEG frames
class FakeJpegSource : public VideoFrameSource {
 public:
  const uint8_t *nextFrame(size_t &len) override {
    frame_.assign(500 + 37 * (count_ % 10), (uint8_t)count_);
    frame_[0] = 0xFF;
    frame_[1] = 0xD8;
    frame_[frame_.size() - 2] = 0xFF;
    frame_[frame_.size() - 1] = 0xD9;
    count_++;
    len = frame_.size();
    return frame_.data();
  }
  VideoInfo videoInfo() override {
    VideoInfo info;
    info.width = 64;
    info.height = 48;
    info.fps = 200;  // fast: no real pacing in the test
    info.format = VideoFormat::MJPEG;
    return info;
  }
  std::vector<uint8_t> frame_;
  int count_ = 0;
};

static void testVideoCopy() {
  UVCDevice uvc;
  FakeJpegSource source;
  UVCVideoCopy copier(uvc, source);
  CHECK(copier.begin(8000));
  CHECK(uvc.isActive());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.formats()[0].format == UVCFormat::MJPEG);
  CHECK_EQ(host.formats()[0].frames[0].maxFrameSize, 8000u);
  std::vector<size_t> sizes;
  host.onFrame([&](const UVCHostSimulator::Frame &f) { sizes.push_back(f.size); });
  CHECK(host.startStream(UVCFormat::MJPEG, 64, 48));
  const uint32_t start = platform::millis();
  while (sizes.size() < 5 && platform::millis() - start < 3000) {
    copier.copy();
    host.poll();
  }
  CHECK(sizes.size() >= 5);
  for (size_t i = 0; i < sizes.size(); i++) CHECK_EQ(sizes[i], 500 + 37 * (i % 10));
}

#if defined(HAS_MJPEG_ENCODER)
/// RGB565 -> MJPEGEncoder -> UVCVideoOutput: compressing raw frames
static void testEncoderChain() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 64, 48, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  int jpegs = 0;
  host.onFrame([&](const UVCHostSimulator::Frame &f) {
    if (f.size > 4 && f.data[0] == 0xFF && f.data[1] == 0xD8) jpegs++;
  });
  CHECK(host.startStream(UVCFormat::MJPEG, 64, 48));
  UVCVideoOutput out(uvc, PixelFormat::MJPEG);
  out.setTimeout(1000);
  MJPEGEncoder encoder(out);
  encoder.setSize(64, 48);
  encoder.setVideoFormat(VideoFormat::RGB565);
  CHECK(encoder.begin());
  std::vector<uint8_t> rgb(64 * 48 * 2);
  for (int n = 0; n < 3; n++) {
    for (size_t i = 0; i < rgb.size(); i++) rgb[i] = (uint8_t)(i + n * 50);
    CHECK(encoder.write(rgb.data(), rgb.size()) > 0);
  }
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(jpegs, 3);
}
#endif

int main() {
  RUN(testVideoOutputRaw);
  RUN(testVideoOutputMemory);
  RUN(testVideoCopy);
#if defined(HAS_MJPEG_ENCODER)
  RUN(testEncoderChain);
#else
  std::printf("TinyJPEG not available: MJPEGEncoder test skipped\n");
#endif
  return testMain();
}
