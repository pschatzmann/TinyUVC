#pragma once
/**
 * @file UVCDefs.h
 * @brief USB Video Class 1.1 constants and the basic TinyUVC data types.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "UVCConfig.h"

namespace tinyuvc {

/// Constants of the USB Video Class specification (UVC 1.1)
/// @brief Constants of the USB Video Class 1.1 specification
namespace uvc_spec {
// Interface class/subclass
constexpr uint8_t CC_VIDEO = 0x0E;
constexpr uint8_t SC_VIDEOCONTROL = 0x01;
constexpr uint8_t SC_VIDEOSTREAMING = 0x02;
constexpr uint8_t SC_VIDEO_INTERFACE_COLLECTION = 0x03;
// Descriptor types
constexpr uint8_t DESC_INTERFACE = 0x04;
constexpr uint8_t DESC_ENDPOINT = 0x05;
constexpr uint8_t DESC_IAD = 0x0B;
constexpr uint8_t CS_INTERFACE = 0x24;
// VideoControl descriptor subtypes
constexpr uint8_t VC_HEADER = 0x01;
constexpr uint8_t VC_INPUT_TERMINAL = 0x02;
constexpr uint8_t VC_OUTPUT_TERMINAL = 0x03;
// VideoStreaming descriptor subtypes
constexpr uint8_t VS_INPUT_HEADER = 0x01;
constexpr uint8_t VS_FORMAT_UNCOMPRESSED = 0x04;
constexpr uint8_t VS_FRAME_UNCOMPRESSED = 0x05;
constexpr uint8_t VS_FORMAT_MJPEG = 0x06;
constexpr uint8_t VS_FRAME_MJPEG = 0x07;
constexpr uint8_t VS_COLORFORMAT = 0x0D;
// Terminal types
constexpr uint16_t TT_STREAMING = 0x0101;
constexpr uint16_t ITT_CAMERA = 0x0201;
// Class specific requests
constexpr uint8_t SET_CUR = 0x01;
constexpr uint8_t GET_CUR = 0x81;
constexpr uint8_t GET_MIN = 0x82;
constexpr uint8_t GET_MAX = 0x83;
constexpr uint8_t GET_RES = 0x84;
constexpr uint8_t GET_LEN = 0x85;
constexpr uint8_t GET_INFO = 0x86;
constexpr uint8_t GET_DEF = 0x87;
// Control selectors
constexpr uint8_t VC_REQUEST_ERROR_CODE_CONTROL = 0x02;
constexpr uint8_t VS_PROBE_CONTROL = 0x01;
constexpr uint8_t VS_COMMIT_CONTROL = 0x02;
// Request error codes (VC_REQUEST_ERROR_CODE_CONTROL)
constexpr uint8_t ERR_NONE = 0x00;
constexpr uint8_t ERR_OUT_OF_RANGE = 0x04;
constexpr uint8_t ERR_INVALID_UNIT = 0x05;
constexpr uint8_t ERR_INVALID_CONTROL = 0x06;
constexpr uint8_t ERR_INVALID_REQUEST = 0x07;
// Payload header bits (bmHeaderInfo)
constexpr uint8_t HDR_FID = 0x01;
constexpr uint8_t HDR_EOF = 0x02;
constexpr uint8_t HDR_PTS = 0x04;
constexpr uint8_t HDR_SCR = 0x08;
constexpr uint8_t HDR_STI = 0x20;
constexpr uint8_t HDR_ERR = 0x40;
constexpr uint8_t HDR_EOH = 0x80;
// Entity IDs used by TinyUVC's descriptors
constexpr uint8_t ID_CAMERA_TERMINAL = 1;
constexpr uint8_t ID_OUTPUT_TERMINAL = 2;
/// Size of the probe/commit control structure for UVC 1.1
constexpr uint16_t PROBE_COMMIT_SIZE = 34;
/// bcdUVC reported in the VideoControl header
constexpr uint16_t BCD_UVC = 0x0110;
}  // namespace uvc_spec

/// Video format offered to the USB host
enum class UVCFormat : uint8_t {
  /// Motion JPEG: every frame is a complete JPEG image
  MJPEG = 0,
  /// Uncompressed YUV 4:2:2 (YUYV), 16 bits per pixel
  YUY2 = 1,
};

/**
 * @brief Pixel format of the frame data handed to UVCDevice::writeFrame().
 * MJPEG (a JPEG image) is sent as is. (Not named JPEG: STM32H7 headers
 * define a JPEG macro.) YUY2 is sent as is (YUY2). All other formats
 * are converted to YUY2 on the fly, while they are sent, so they need no
 * extra frame buffer.
 */
enum class PixelFormat : uint8_t {
  MJPEG,      ///< one complete JPEG image (for UVCFormat::MJPEG)
  YUY2,       ///< packed YUV 4:2:2, byte order Y0 U Y1 V
  RGB565_LE,  ///< 16 bit RGB565, little endian (low byte first)
  RGB565_BE,  ///< 16 bit RGB565, big endian (high byte first, esp32-camera)
  RGB888,     ///< 24 bit, byte order R G B
  BGR888,     ///< 24 bit, byte order B G R
  GRAY8,      ///< 8 bit luminance
};

/// Bytes per pixel of a raw pixel format (0 for JPEG)
inline constexpr uint8_t bytesPerPixel(PixelFormat fmt) {
  return fmt == PixelFormat::MJPEG    ? 0
         : fmt == PixelFormat::RGB888 ? 3
         : fmt == PixelFormat::BGR888 ? 3
         : fmt == PixelFormat::GRAY8  ? 1
                                      : 2;
}

/// Name of a pixel format, for logging
inline const char *toString(PixelFormat fmt) {
  switch (fmt) {
    case PixelFormat::MJPEG:
      return "JPEG";
    case PixelFormat::YUY2:
      return "YUY2";
    case PixelFormat::RGB565_LE:
      return "RGB565_LE";
    case PixelFormat::RGB565_BE:
      return "RGB565_BE";
    case PixelFormat::RGB888:
      return "RGB888";
    case PixelFormat::BGR888:
      return "BGR888";
    case PixelFormat::GRAY8:
      return "GRAY8";
  }
  return "?";
}

/// Name of a UVC format, for logging
inline const char *toString(UVCFormat fmt) {
  return fmt == UVCFormat::MJPEG ? "MJPEG" : "YUY2";
}

/// Frame interval in 100ns units for a frame rate
inline constexpr uint32_t fpsToInterval(uint32_t fps) {
  return fps == 0 ? 0 : 10000000UL / fps;
}

/// Frame rate for a frame interval in 100ns units
inline constexpr uint32_t intervalToFps(uint32_t interval) {
  return interval == 0 ? 0 : (10000000UL + interval / 2) / interval;
}

/// One resolution (UVC frame descriptor) with its supported frame rates
struct UVCFrameSpec {
  uint16_t width = 0;
  uint16_t height = 0;
  /// Max size of one frame in bytes, reported to the host for buffer sizing
  uint32_t maxFrameSize = 0;
  /// Number of valid entries in intervals[]
  uint8_t intervalCount = 0;
  /// Supported frame intervals in 100ns units; intervals[0] is the default
  uint32_t intervals[TINYUVC_MAX_INTERVALS] = {};

  /// true if the interval is listed
  bool hasInterval(uint32_t interval) const {
    for (int j = 0; j < intervalCount; j++)
      if (intervals[j] == interval) return true;
    return false;
  }
  /// Supported interval that is closest to the requested one
  uint32_t closestInterval(uint32_t interval) const {
    if (intervalCount == 0) return 0;
    uint32_t best = intervals[0];
    uint32_t bestDiff = 0xFFFFFFFF;
    for (int j = 0; j < intervalCount; j++) {
      uint32_t d = intervals[j] > interval ? intervals[j] - interval
                                           : interval - intervals[j];
      if (d < bestDiff) {
        bestDiff = d;
        best = intervals[j];
      }
    }
    return best;
  }
  uint32_t minInterval() const {
    uint32_t r = intervals[0];
    for (int j = 1; j < intervalCount; j++)
      if (intervals[j] < r) r = intervals[j];
    return r;
  }
  uint32_t maxInterval() const {
    uint32_t r = intervals[0];
    for (int j = 1; j < intervalCount; j++)
      if (intervals[j] > r) r = intervals[j];
    return r;
  }
};

/// One video format (UVC format descriptor) with its resolutions
struct UVCFormatSpec {
  UVCFormat format = UVCFormat::MJPEG;
  uint8_t frameCount = 0;
  UVCFrameSpec frames[TINYUVC_MAX_FRAMES];
};

/// The format, resolution and frame rate the host selected
struct UVCStreamSettings {
  UVCFormat format = UVCFormat::MJPEG;
  uint16_t width = 0;
  uint16_t height = 0;
  /// Frame interval in 100ns units
  uint32_t interval = 0;
  /// 1-based UVC indexes as used in the probe/commit control
  uint8_t formatIndex = 0;
  uint8_t frameIndex = 0;
  /// Frame rate (frames per second, rounded)
  uint32_t fps() const { return intervalToFps(interval); }
  /// Size of an uncompressed YUY2 frame (0 for MJPEG)
  uint32_t rawFrameSize() const {
    return format == UVCFormat::YUY2 ? (uint32_t)width * height * 2 : 0;
  }
  bool operator==(const UVCStreamSettings &o) const {
    return format == o.format && width == o.width && height == o.height &&
           interval == o.interval;
  }
  bool operator!=(const UVCStreamSettings &o) const { return !(*this == o); }
};

/// Little endian helpers
namespace le {
inline void put16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
}
inline void put32(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}
inline uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t get32(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}
}  // namespace le

/**
 * @brief Video probe and commit control (UVC 1.1, 34 bytes). Kept as a byte
 * array in USB (little endian) layout with typed accessors, so there are no
 * packing or alignment issues.
 */
struct UVCProbeCommit {
  uint8_t raw[uvc_spec::PROBE_COMMIT_SIZE] = {};

  uint16_t bmHint() const { return le::get16(raw + 0); }
  uint8_t formatIndex() const { return raw[2]; }
  uint8_t frameIndex() const { return raw[3]; }
  uint32_t frameInterval() const { return le::get32(raw + 4); }
  uint32_t maxVideoFrameSize() const { return le::get32(raw + 18); }
  uint32_t maxPayloadTransferSize() const { return le::get32(raw + 22); }
  uint32_t clockFrequency() const { return le::get32(raw + 26); }
  uint8_t framingInfo() const { return raw[30]; }

  void setHint(uint16_t v) { le::put16(raw + 0, v); }
  void setFormatIndex(uint8_t v) { raw[2] = v; }
  void setFrameIndex(uint8_t v) { raw[3] = v; }
  void setFrameInterval(uint32_t v) { le::put32(raw + 4, v); }
  void setKeyFrameRate(uint16_t v) { le::put16(raw + 8, v); }
  void setPFrameRate(uint16_t v) { le::put16(raw + 10, v); }
  void setCompQuality(uint16_t v) { le::put16(raw + 12, v); }
  void setCompWindowSize(uint16_t v) { le::put16(raw + 14, v); }
  void setDelay(uint16_t v) { le::put16(raw + 16, v); }
  void setMaxVideoFrameSize(uint32_t v) { le::put32(raw + 18, v); }
  void setMaxPayloadTransferSize(uint32_t v) { le::put32(raw + 22, v); }
  void setClockFrequency(uint32_t v) { le::put32(raw + 26, v); }
  void setFramingInfo(uint8_t v) { raw[30] = v; }
  void setVersions(uint8_t preferred, uint8_t min, uint8_t max) {
    raw[31] = preferred;
    raw[32] = min;
    raw[33] = max;
  }
};

}  // namespace tinyuvc
