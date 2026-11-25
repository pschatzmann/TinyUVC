// Frame streaming end to end: UVCDevice -> class driver -> simulated bulk
// endpoint -> host side reassembly like Linux uvcvideo
#include <vector>

#include "TestUtils.h"
#include "TinyUVC.h"

/// A fake JPEG of the given size (SOI ... EOI) with a recognizable pattern
static std::vector<uint8_t> fakeJpeg(size_t size, uint8_t seed) {
  std::vector<uint8_t> j(size);
  for (size_t i = 0; i < size; i++) j[i] = (uint8_t)(i * 31 + seed);
  j[0] = 0xFF;
  j[1] = 0xD8;
  j[size - 2] = 0xFF;
  j[size - 1] = 0xD9;
  return j;
}

struct Received {
  std::vector<std::vector<uint8_t>> frames;
  std::vector<bool> errors;
  std::vector<uint32_t> pts;
  void attach(UVCHostSimulator &host) {
    host.onFrame([this](const UVCHostSimulator::Frame &f) {
      frames.emplace_back(f.data, f.data + f.size);
      errors.push_back(f.error);
      pts.push_back(f.hasPts ? f.pts : 0xFFFFFFFF);
    });
  }
};

/// Sends frames of many sizes, in particular sizes where a payload ends
/// exactly on a packet boundary (needs a zero length packet) or fills a
/// complete payload
static void testMjpegSizes(uint16_t mps, uint32_t payloadSize) {
  desktopSettings().maxPacketSize = mps;
  UVCDevice uvc;
  uvc.setPayloadSize(payloadSize);
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30, 100000));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  Received rx;
  rx.attach(host);
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));

  const uint32_t data = uvc.payloadSize() - 2;  // payload data per transfer
  std::vector<size_t> sizes = {4,         10,        mps - 2u,     mps - 1u, mps,
                               mps + 1u,  2u * mps - 2u, data - 1, data,     data + 1,
                               2 * data,  2 * data + mps - 2u, 3 * data + 7, 50000};
  std::vector<std::vector<uint8_t>> sent;
  for (size_t i = 0; i < sizes.size(); i++) {
    sent.push_back(fakeJpeg(sizes[i], (uint8_t)i));
    CHECK(uvc.waitReady(1000));
    CHECK(uvc.writeFrame(sent.back().data(), sent.back().size(), PixelFormat::MJPEG));
  }
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(rx.frames.size(), sent.size());
  for (size_t i = 0; i < sent.size() && i < rx.frames.size(); i++) {
    if (rx.frames[i] != sent[i])
      std::fprintf(stderr, "  frame %zu (%zu bytes) differs: got %zu bytes\n", i,
                   sent[i].size(), rx.frames[i].size());
    CHECK(rx.frames[i] == sent[i]);
    CHECK(!rx.errors[i]);
  }
  CHECK_EQ(uvc.framesSent(), (uint32_t)sent.size());
  CHECK_EQ(uvc.framesDropped(), 0u);
  CHECK_EQ(host.droppedPayloads(), 0u);
  CHECK(host.zeroLengthPackets() > 0);  // the boundary cases need them
  desktopSettings().maxPacketSize = 64;
}

static void testMjpegFullSpeed() { testMjpegSizes(64, 2048); }
static void testMjpegFullSpeedSmallPayload() { testMjpegSizes(64, 1000); }
static void testMjpegHighSpeed() { testMjpegSizes(512, 16384); }
static void testMjpegHighSpeedSmallPayload() { testMjpegSizes(512, 3000); }

/// The payload size is a multiple of the packet size and at most 32 packets
static void testPayloadSizeLimits() {
  struct Case {
    uint16_t mps;
    uint32_t requested, expected;
  } cases[] = {{64, 40000, 2048}, {64, 1000, 960},   {64, 10, 128},
               {512, 40000, 16384}, {512, 3000, 2560}, {512, 16384, 16384}};
  for (auto &c : cases) {
    desktopSettings().maxPacketSize = c.mps;
    UVCDevice uvc;
    uvc.setPayloadSize(c.requested);
    CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30));
    CHECK(uvc.begin());
    CHECK_EQ(uvc.payloadSize(), c.expected);
  }
  desktopSettings().maxPacketSize = 64;
}

/// Raw frames in every pixel format arrive as correctly converted YUY2
static void testYuy2Conversion() {
  const uint16_t w = 64, h = 48;
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::YUY2, w, h, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  Received rx;
  rx.attach(host);
  CHECK(host.startStream(UVCFormat::YUY2, w, h));

  const PixelFormat formats[] = {PixelFormat::YUY2,   PixelFormat::RGB565_LE,
                                 PixelFormat::RGB565_BE, PixelFormat::RGB888,
                                 PixelFormat::BGR888, PixelFormat::GRAY8};
  std::vector<std::vector<uint8_t>> expected;
  std::vector<std::vector<uint8_t>> sources;
  for (PixelFormat fmt : formats) {
    std::vector<uint8_t> src((size_t)w * h * bytesPerPixel(fmt));
    for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i * 13 + 5);
    std::vector<uint8_t> exp((size_t)w * h * 2);
    UVCConvert::toYUY2(fmt, src.data(), exp.data(), (size_t)w * h / 2);
    expected.push_back(exp);
    sources.push_back(src);
  }
  for (size_t i = 0; i < sources.size(); i++) {
    CHECK(uvc.waitReady(1000));
    CHECK(uvc.writeFrame(sources[i].data(), sources[i].size(), formats[i]));
  }
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(rx.frames.size(), expected.size());
  for (size_t i = 0; i < rx.frames.size() && i < expected.size(); i++) {
    CHECK(rx.frames[i] == expected[i]);
    CHECK(!rx.errors[i]);
  }
}

/// Frames that do not match the host's selection are rejected
static void testRejectedFrames() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 160, 120, 30, 2000));
  CHECK(uvc.addResolution(UVCFormat::YUY2, 160, 120, 30));
  CHECK(uvc.begin());
  auto jpeg = fakeJpeg(1000, 1);
  CHECK(!uvc.isReady());
  CHECK(!uvc.writeFrame(jpeg.data(), jpeg.size(), PixelFormat::MJPEG));  // not streaming
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.startStream(UVCFormat::MJPEG, 160, 120));
  std::vector<uint8_t> raw(160 * 120 * 2);
  CHECK(!uvc.writeFrame(raw.data(), raw.size(), PixelFormat::RGB565_LE));
  auto big = fakeJpeg(2001, 2);
  CHECK(!uvc.writeFrame(big.data(), big.size(), PixelFormat::MJPEG));
  CHECK(!uvc.writeFrame(nullptr, 10, PixelFormat::MJPEG));
  // fits into one payload: copied into the transfer buffer at once
  CHECK(uvc.writeFrame(jpeg.data(), jpeg.size(), PixelFormat::MJPEG));
  CHECK(uvc.isReady());
  // waits for the transfer of the first one
  CHECK(uvc.writeFrame(jpeg.data(), jpeg.size(), PixelFormat::MJPEG));
  CHECK(!uvc.isReady());
  CHECK(!uvc.writeFrame(jpeg.data(), jpeg.size(), PixelFormat::MJPEG));  // busy
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK(host.stopStream());
  CHECK(host.startStream(UVCFormat::YUY2, 160, 120));
  CHECK(!uvc.writeFrame(jpeg.data(), jpeg.size(), PixelFormat::MJPEG));
  CHECK(!uvc.writeFrame(raw.data(), raw.size() - 2, PixelFormat::RGB565_LE));
  CHECK(uvc.writeFrame(raw.data(), raw.size(), PixelFormat::RGB565_LE));
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(host.framesReceived(), 3u);
  CHECK_EQ(host.framesWithError(), 0u);
}

/// The host stops in the middle of a frame and restarts: the next frames
/// must be received intact (the stale payload is dropped by the host's FID
/// synchronization or arrives as a separate error frame at most)
static void testStopInTheMiddle() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  Received rx;
  rx.attach(host);
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  auto a = fakeJpeg(20000, 1);
  CHECK(uvc.writeFrame(a.data(), a.size(), PixelFormat::MJPEG));
  host.poll();
  host.poll();  // two payloads of the frame
  CHECK(host.stopStream());
  CHECK(!uvc.isStreaming());
  CHECK(!uvc.isReady());
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  // the transfer queued before the stop is read first, then the frame is
  // dropped on the device
  host.poll();
  CHECK(uvc.isReady());
  CHECK_EQ(uvc.framesDropped(), 1u);
  auto b = fakeJpeg(9000, 2);
  auto c = fakeJpeg(7000, 3);
  CHECK(uvc.writeFrame(b.data(), b.size(), PixelFormat::MJPEG));
  CHECK(uvc.waitReady(1000));
  CHECK(uvc.writeFrame(c.data(), c.size(), PixelFormat::MJPEG));
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  // b and c must be the last two good frames
  std::vector<std::vector<uint8_t>> good;
  for (size_t i = 0; i < rx.frames.size(); i++)
    if (!rx.errors[i]) good.push_back(rx.frames[i]);
  CHECK(good.size() >= 2);
  if (good.size() >= 2) {
    CHECK(good[good.size() - 2] == b);
    CHECK(good[good.size() - 1] == c);
  }
}

/// A failing transfer drops the frame, streaming continues
static void testTransferFailure() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  Received rx;
  rx.attach(host);
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  auto a = fakeJpeg(10000, 1);
  CHECK(uvc.writeFrame(a.data(), a.size(), PixelFormat::MJPEG));
  simBus().failNextXfer = true;
  host.poll();  // first payload done, starting the second one fails
  CHECK(uvc.isReady());
  CHECK_EQ(uvc.framesDropped(), 1u);
  auto b = fakeJpeg(5000, 2);
  CHECK(uvc.writeFrame(b.data(), b.size(), PixelFormat::MJPEG));
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK(!rx.frames.empty());
  if (!rx.frames.empty()) CHECK(rx.frames.back() == b);
}

/// Presentation time stamps
static void testTimestamps() {
  UVCDevice uvc;
  uvc.setTimestamps(true);
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  Received rx;
  rx.attach(host);
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  auto a = fakeJpeg(9000, 1);
  CHECK(uvc.writeFrame(a.data(), a.size(), PixelFormat::MJPEG));
  CHECK(uvc.waitReady(1000));
  CHECK(uvc.writeFrame(a.data(), a.size(), PixelFormat::MJPEG));
  CHECK(uvc.waitReady(1000));
  host.pollAll();
  CHECK_EQ(rx.frames.size(), 2u);
  if (rx.frames.size() == 2) {
    CHECK(rx.frames[0] == a);
    CHECK(rx.pts[0] != 0xFFFFFFFF);
    CHECK(rx.pts[1] >= rx.pts[0]);
  }
}

/// Stream callback and generation counter
static void testCallbacks() {
  UVCDevice uvc;
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, 30));
  struct State {
    int starts = 0, stops = 0;
    UVCStreamSettings last;
  } state;
  uvc.setStreamCallback(
      [](bool on, const UVCStreamSettings &s, void *ref) {
        State *st = (State *)ref;
        if (on) {
          st->starts++;
          st->last = s;
        } else {
          st->stops++;
        }
      },
      &state);
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  uint32_t gen = uvc.streamGeneration();
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  CHECK(uvc.streamGeneration() != gen);
  CHECK_EQ(state.starts, 1);
  CHECK_EQ(state.last.width, 320);
  CHECK(host.stopStream());
  CHECK_EQ(state.stops, 1);
  host.disconnect();  // bus reset while not streaming: no extra stop
  CHECK_EQ(state.stops, 1);
}

int main() {
  RUN(testMjpegFullSpeed);
  RUN(testMjpegFullSpeedSmallPayload);
  RUN(testMjpegHighSpeed);
  RUN(testMjpegHighSpeedSmallPayload);
  RUN(testPayloadSizeLimits);
  RUN(testYuy2Conversion);
  RUN(testRejectedFrames);
  RUN(testStopInTheMiddle);
  RUN(testTransferFailure);
  RUN(testTimestamps);
  RUN(testCallbacks);
  return testMain();
}
