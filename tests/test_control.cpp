// Descriptors, enumeration and the probe/commit negotiation, checked through
// the class driver with UVCHostSimulator
#include "TestUtils.h"
#include "TinyUVC.h"

static void setupDevice(UVCDevice &uvc) {
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 320, 240, {30, 15}, 40000));
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 640, 480, 15));
  CHECK(uvc.addResolution(UVCFormat::YUY2, 160, 120, {10, 5}));
  CHECK(uvc.begin());
}

static void testDescriptors() {
  for (int cdc = 0; cdc < 2; cdc++) {
    for (uint16_t mps : {64, 512}) {
      desktopSettings().maxPacketSize = mps;
      UVCDevice uvc;
      setupDevice(uvc);
      UVCHostSimulator host(cdc == 1);
      bool ok = host.connect(uvc);
      if (!ok) std::fprintf(stderr, "  %s\n", host.error().c_str());
      CHECK(ok);
      CHECK_EQ(uvc.controlInterface(), cdc ? 2 : 0);
      CHECK_EQ(host.formats().size(), 2u);
      if (host.formats().size() != 2) continue;
      const auto &mjpeg = host.formats()[0];
      CHECK(mjpeg.format == UVCFormat::MJPEG);
      CHECK_EQ(mjpeg.frames.size(), 2u);
      CHECK_EQ(mjpeg.frames[0].width, 320);
      CHECK_EQ(mjpeg.frames[0].maxFrameSize, 40000u);
      CHECK_EQ(mjpeg.frames[0].intervals.size(), 2u);
      CHECK_EQ(mjpeg.frames[0].defaultInterval, fpsToInterval(30));
      CHECK_EQ(mjpeg.frames[1].maxFrameSize, 640u * 480 * 2);
      const auto &yuy2 = host.formats()[1];
      CHECK(yuy2.format == UVCFormat::YUY2);
      CHECK_EQ(yuy2.frames[0].maxFrameSize, 160u * 120 * 2);
      CHECK_EQ(uvc.payloadSize() % mps, 0u);
    }
  }
  desktopSettings().maxPacketSize = 64;
}

static void testConfigurationErrors() {
  UVCDevice uvc;
  CHECK(!uvc.begin());  // no resolution
  CHECK(!uvc.addResolution(UVCFormat::YUY2, 161, 120, 10));  // odd width
  CHECK(!uvc.addResolution(UVCFormat::MJPEG, 0, 120, 10));
  CHECK(!uvc.addResolution(UVCFormat::MJPEG, 160, 120, {0}));
  CHECK_EQ(uvc.formatCount(), 0);
  CHECK(uvc.addResolution(UVCFormat::MJPEG, 160, 120, 10));
  CHECK(uvc.begin());
  CHECK(!uvc.addResolution(UVCFormat::MJPEG, 320, 240, 10));  // after begin
  UVCDevice second;
  second.addResolution(UVCFormat::MJPEG, 160, 120, 10);
  CHECK(!second.begin());  // only one active device
  uvc.end();
  CHECK(second.begin());
}

static void testProbeCommit() {
  UVCDevice uvc;
  setupDevice(uvc);
  UVCHostSimulator host;
  CHECK(host.connect(uvc));

  UVCProbeCommit pc;
  CHECK(host.getProbe(uvc_spec::GET_DEF, false, pc));
  CHECK_EQ(pc.formatIndex(), 1);
  CHECK_EQ(pc.frameIndex(), 1);
  CHECK_EQ(pc.frameInterval(), fpsToInterval(30));
  CHECK_EQ(pc.maxVideoFrameSize(), 40000u);
  CHECK_EQ(pc.maxPayloadTransferSize(), uvc.payloadSize());
  CHECK_EQ(pc.framingInfo(), 3);

  CHECK(host.getProbe(uvc_spec::GET_MIN, false, pc));
  CHECK_EQ(pc.frameInterval(), fpsToInterval(30));
  CHECK(host.getProbe(uvc_spec::GET_MAX, false, pc));
  CHECK_EQ(pc.formatIndex(), 2);
  CHECK_EQ(pc.frameIndex(), 1);
  CHECK_EQ(pc.frameInterval(), fpsToInterval(5));

  // GET_LEN / GET_INFO
  std::vector<uint8_t> data;
  UVCRequest r;
  r.bmRequestType = 0xA1;
  r.bRequest = uvc_spec::GET_LEN;
  r.wValue = uvc_spec::VS_PROBE_CONTROL << 8;
  r.wIndex = uvc.streamingInterface();
  r.wLength = 2;
  CHECK(host.control(r, nullptr, 0, &data));
  CHECK_EQ(data.size(), 2u);
  CHECK_EQ(le::get16(data.data()), uvc_spec::PROBE_COMMIT_SIZE);
  r.bRequest = uvc_spec::GET_INFO;
  r.wLength = 1;
  CHECK(host.control(r, nullptr, 0, &data));
  CHECK_EQ(data[0], 3);

  // invalid indexes are replaced by valid ones, the interval is rounded to
  // the closest supported one
  UVCProbeCommit set;
  set.setFormatIndex(2);
  set.setFrameIndex(9);
  set.setFrameInterval(fpsToInterval(6));  // closer to 5 than to 10 fps
  CHECK(host.setProbe(false, set));
  CHECK(host.getProbe(uvc_spec::GET_CUR, false, pc));
  CHECK_EQ(pc.formatIndex(), 2);
  CHECK_EQ(pc.frameIndex(), 1);
  CHECK_EQ(pc.frameInterval(), fpsToInterval(5));
  set.setFormatIndex(7);
  CHECK(host.setProbe(false, set));
  CHECK(host.getProbe(uvc_spec::GET_CUR, false, pc));
  CHECK_EQ(pc.formatIndex(), 1);

  // a short (UVC 1.0, 26 byte) probe is accepted
  UVCRequest s;
  s.bmRequestType = 0x21;
  s.bRequest = uvc_spec::SET_CUR;
  s.wValue = uvc_spec::VS_PROBE_CONTROL << 8;
  s.wIndex = uvc.streamingInterface();
  set.setFormatIndex(1);
  set.setFrameIndex(2);
  set.setFrameInterval(0);
  CHECK(host.control(s, set.raw, 26, nullptr));
  CHECK(host.getProbe(uvc_spec::GET_CUR, false, pc));
  CHECK_EQ(pc.frameIndex(), 2);
  CHECK_EQ(pc.frameInterval(), fpsToInterval(15));

  // commit starts the stream with the selected settings
  CHECK(!uvc.isStreaming());
  CHECK(host.startStream(UVCFormat::MJPEG, 640, 480));
  CHECK(uvc.isStreaming());
  CHECK(uvc.settings().format == UVCFormat::MJPEG);
  CHECK_EQ(uvc.settings().width, 640);
  CHECK_EQ(uvc.settings().fps(), 15u);
  CHECK(host.getProbe(uvc_spec::GET_CUR, true, pc));
  CHECK_EQ(pc.frameIndex(), 2);

  // both ways of stopping
  CHECK(host.stopStream());
  CHECK(!uvc.isStreaming());
  CHECK(host.startStream(UVCFormat::YUY2, 160, 120, 5));
  CHECK_EQ(uvc.settings().fps(), 5u);
  CHECK(host.stopStream(true));
  CHECK(!uvc.isStreaming());
}

static void testUnsupportedRequests() {
  UVCDevice uvc;
  setupDevice(uvc);
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  std::vector<uint8_t> data;
  // camera terminal control (exposure) is not supported: stall + error code
  UVCRequest r;
  r.bmRequestType = 0xA1;
  r.bRequest = uvc_spec::GET_CUR;
  r.wValue = 0x04 << 8;
  r.wIndex = (uint16_t)((uvc_spec::ID_CAMERA_TERMINAL << 8) | uvc.controlInterface());
  r.wLength = 4;
  CHECK(!host.control(r, nullptr, 0, &data));
  CHECK_EQ(host.requestErrorCode(), uvc_spec::ERR_INVALID_CONTROL);
  // unknown unit
  r.wIndex = (uint16_t)((9 << 8) | uvc.controlInterface());
  CHECK(!host.control(r, nullptr, 0, &data));
  CHECK_EQ(host.requestErrorCode(), uvc_spec::ERR_INVALID_UNIT);
  // unsupported request on the probe control
  r.wIndex = uvc.streamingInterface();
  r.wValue = uvc_spec::VS_PROBE_CONTROL << 8;
  r.bRequest = 0x88;
  CHECK(!host.control(r, nullptr, 0, &data));
  CHECK_EQ(host.requestErrorCode(), uvc_spec::ERR_INVALID_REQUEST);
  // a successful request resets the error code
  UVCProbeCommit pc;
  CHECK(host.getProbe(uvc_spec::GET_CUR, false, pc));
  CHECK_EQ(host.requestErrorCode(), uvc_spec::ERR_NONE);
}

int main() {
  RUN(testDescriptors);
  RUN(testConfigurationErrors);
  RUN(testProbeCommit);
  RUN(testUnsupportedRequests);
  return testMain();
}
