#pragma once
/**
 * @file UVCConvert.h
 * @brief Conversion of raw pixel formats to YUY2 (YUV 4:2:2, Y0 U Y1 V),
 * pixel pair by pixel pair, so it can run while a frame is being sent.
 * Uses BT.601 limited range integer math.
 */
#include "UVCDefs.h"

namespace tinyuvc {

class UVCConvert {
 public:
  /// Converts `pairs` pixel pairs from `src` (in format fmt) to YUY2 in
  /// `dst` (4 bytes per pair). JPEG is not supported (nothing is written).
  static void toYUY2(PixelFormat fmt, const uint8_t *src, uint8_t *dst,
                     size_t pairs) {
    switch (fmt) {
      case PixelFormat::YUY2:
        memcpy(dst, src, pairs * 4);
        break;
      case PixelFormat::RGB565_LE:
        for (size_t i = 0; i < pairs; i++, src += 4, dst += 4)
          pair(rgb565(src[0] | (src[1] << 8)), rgb565(src[2] | (src[3] << 8)), dst);
        break;
      case PixelFormat::RGB565_BE:
        for (size_t i = 0; i < pairs; i++, src += 4, dst += 4)
          pair(rgb565((src[0] << 8) | src[1]), rgb565((src[2] << 8) | src[3]), dst);
        break;
      case PixelFormat::RGB888:
        for (size_t i = 0; i < pairs; i++, src += 6, dst += 4)
          pair(Rgb{src[0], src[1], src[2]}, Rgb{src[3], src[4], src[5]}, dst);
        break;
      case PixelFormat::BGR888:
        for (size_t i = 0; i < pairs; i++, src += 6, dst += 4)
          pair(Rgb{src[2], src[1], src[0]}, Rgb{src[5], src[4], src[3]}, dst);
        break;
      case PixelFormat::GRAY8:
        for (size_t i = 0; i < pairs; i++, src += 2, dst += 4) {
          dst[0] = grayToY(src[0]);
          dst[1] = 128;
          dst[2] = grayToY(src[1]);
          dst[3] = 128;
        }
        break;
      case PixelFormat::MJPEG:
        break;
    }
  }

  struct Rgb {
    uint8_t r, g, b;
  };

  static Rgb rgb565(uint16_t v) {
    uint8_t r = (uint8_t)((v >> 11) & 0x1F);
    uint8_t g = (uint8_t)((v >> 5) & 0x3F);
    uint8_t b = (uint8_t)(v & 0x1F);
    return Rgb{(uint8_t)((r << 3) | (r >> 2)), (uint8_t)((g << 2) | (g >> 4)),
               (uint8_t)((b << 3) | (b >> 2))};
  }

  static uint8_t y(const Rgb &c) {
    return (uint8_t)(((66 * c.r + 129 * c.g + 25 * c.b + 128) >> 8) + 16);
  }
  static uint8_t u(int r, int g, int b) {
    return clamp(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
  }
  static uint8_t v(int r, int g, int b) {
    return clamp(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
  }

 private:
  static uint8_t clamp(int v) { return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v)); }
  static uint8_t grayToY(uint8_t g) { return (uint8_t)(16 + (g * 219 + 127) / 255); }

  static void pair(const Rgb &a, const Rgb &b, uint8_t *dst) {
    int r = (a.r + b.r + 1) >> 1, g = (a.g + b.g + 1) >> 1,
        bl = (a.b + b.b + 1) >> 1;
    dst[0] = y(a);
    dst[1] = u(r, g, bl);
    dst[2] = y(b);
    dst[3] = v(r, g, bl);
  }
};

}  // namespace tinyuvc
