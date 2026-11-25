// Pixel conversion to YUY2 and the payload encoder
#include <vector>

#include "TestUtils.h"
#include "TinyUVC.h"

// sketches name their device "uvc": that must not clash with library names
UVCDevice uvc;

static void testKnownColors() {
  uint8_t out[4];
  // white / black pairs in RGB888
  const uint8_t white[6] = {255, 255, 255, 255, 255, 255};
  UVCConvert::toYUY2(PixelFormat::RGB888, white, out, 1);
  CHECK_EQ(out[0], 235);
  CHECK_EQ(out[1], 128);
  CHECK_EQ(out[2], 235);
  CHECK_EQ(out[3], 128);
  const uint8_t black[6] = {0, 0, 0, 0, 0, 0};
  UVCConvert::toYUY2(PixelFormat::RGB888, black, out, 1);
  CHECK_EQ(out[0], 16);
  CHECK_EQ(out[1], 128);
  CHECK_EQ(out[3], 128);
  // pure red: BT.601 Y=82 U=90 V=240
  const uint8_t red[6] = {255, 0, 0, 255, 0, 0};
  UVCConvert::toYUY2(PixelFormat::RGB888, red, out, 1);
  CHECK_EQ(out[0], 82);
  CHECK_EQ(out[1], 90);
  CHECK_EQ(out[3], 240);
  // the same red as BGR888
  const uint8_t redBgr[6] = {0, 0, 255, 0, 0, 255};
  UVCConvert::toYUY2(PixelFormat::BGR888, redBgr, out, 1);
  CHECK_EQ(out[0], 82);
  CHECK_EQ(out[3], 240);
}

static void testRgb565ByteOrder() {
  uint8_t le[4], be[4];
  const uint16_t red = 0xF800;
  const uint8_t srcLe[4] = {red & 0xFF, red >> 8, red & 0xFF, red >> 8};
  const uint8_t srcBe[4] = {red >> 8, red & 0xFF, red >> 8, red & 0xFF};
  UVCConvert::toYUY2(PixelFormat::RGB565_LE, srcLe, le, 1);
  UVCConvert::toYUY2(PixelFormat::RGB565_BE, srcBe, be, 1);
  for (int i = 0; i < 4; i++) CHECK_EQ(le[i], be[i]);
  CHECK_EQ(le[0], 82);  // red
  CHECK_EQ(le[3], 240);
}

static void testGray() {
  const uint8_t g[2] = {0, 255};
  uint8_t out[4];
  UVCConvert::toYUY2(PixelFormat::GRAY8, g, out, 1);
  CHECK_EQ(out[0], 16);
  CHECK_EQ(out[2], 235);
  CHECK_EQ(out[1], 128);
}

// The encoder must produce payloads that reassemble to the converted frame
static void testEncoderSplitsAndConverts() {
  const int pixels = 1000;  // 2000 bytes RGB565 -> 2000 bytes YUY2
  std::vector<uint8_t> src(pixels * 2);
  for (size_t i = 0; i < src.size(); i++) src[i] = (uint8_t)(i * 7);
  std::vector<uint8_t> expected(pixels * 2);
  UVCConvert::toYUY2(PixelFormat::RGB565_LE, src.data(), expected.data(), pixels / 2);

  UVCPayloadEncoder enc;
  enc.begin(src.data(), src.size(), PixelFormat::RGB565_LE, true);
  CHECK_EQ(enc.outputLength(), expected.size());
  std::vector<uint8_t> got;
  uint8_t buf[301];
  int payloads = 0;
  bool sawEof = false;
  while (enc.active()) {
    size_t n = enc.fill(buf, sizeof(buf));
    CHECK(n > 2);
    CHECK_EQ(buf[0], 2);
    CHECK(buf[1] & uvc_spec::HDR_FID);
    CHECK(((n - 2) % 4) == 0);  // whole pixel pairs
    sawEof = (buf[1] & uvc_spec::HDR_EOF) != 0;
    if (enc.active()) CHECK(!sawEof);
    got.insert(got.end(), buf + 2, buf + n);
    payloads++;
  }
  CHECK(sawEof);
  CHECK(got == expected);
  CHECK_EQ(payloads, 7);  // 2000 / 296
}

static void testEncoderPts() {
  const uint8_t jpeg[10] = {0xFF, 0xD8, 1, 2, 3, 4, 5, 6, 0xFF, 0xD9};
  UVCPayloadEncoder enc;
  enc.begin(jpeg, sizeof(jpeg), PixelFormat::MJPEG, false, true, 0x12345678);
  uint8_t buf[64];
  size_t n = enc.fill(buf, sizeof(buf));
  CHECK_EQ(n, 16u);
  CHECK_EQ(buf[0], 6);
  CHECK(buf[1] & uvc_spec::HDR_PTS);
  CHECK(buf[1] & uvc_spec::HDR_EOF);
  CHECK(!(buf[1] & uvc_spec::HDR_FID));
  CHECK_EQ(le::get32(buf + 2), 0x12345678u);
}

int main() {
  CHECK(!uvc.isActive());
  RUN(testKnownColors);
  RUN(testRgb565ByteOrder);
  RUN(testGray);
  RUN(testEncoderSplitsAndConverts);
  RUN(testEncoderPts);
  return testMain();
}
