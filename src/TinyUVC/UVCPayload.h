#pragma once
/**
 * @file UVCPayload.h
 * @brief Splits one video frame into UVC payloads (payload header + data).
 */
#include "UVCConvert.h"
#include "UVCDefs.h"

namespace tinyuvc {

/**
 * @brief Produces the UVC payloads of one frame. Each payload starts with a
 * 2 byte header (6 bytes with a presentation time stamp) that carries the
 * frame id bit (FID, toggled per frame) and, in the last payload of the
 * frame, the end of frame bit (EOF).
 *
 * The source data is only read while fill() runs: once done() is true the
 * caller may reuse the frame buffer. Raw pixel formats are converted to
 * YUY2 on the fly.
 */
class UVCPayloadEncoder {
 public:
  /// Starts a new frame. For JPEG/YUY2 sources the data is sent as is,
  /// other pixel formats are converted to YUY2 (len must then be
  /// pixels * bytesPerPixel(fmt), with an even number of pixels).
  void begin(const uint8_t *data, size_t len, PixelFormat fmt, bool fid,
             bool withPts = false, uint32_t pts = 0) {
    data_ = data;
    fmt_ = fmt;
    fid_ = fid;
    withPts_ = withPts;
    pts_ = pts;
    pos_ = 0;
    if (fmt == PixelFormat::MJPEG || fmt == PixelFormat::YUY2) {
      outLen_ = len;
      srcPairBytes_ = 0;
    } else {
      srcPairBytes_ = (uint8_t)(2 * bytesPerPixel(fmt));
      outLen_ = (len / srcPairBytes_) * 4;
    }
    active_ = true;
  }

  /// Header length in bytes
  uint8_t headerLength() const { return withPts_ ? 6 : 2; }

  /// Writes the next payload (max maxLen bytes) and returns its length.
  /// Returns 0 if the frame is complete.
  size_t fill(uint8_t *buf, size_t maxLen) {
    if (!active_) return 0;
    const uint8_t hdr = headerLength();
    if (maxLen <= (size_t)hdr + 4) return 0;
    size_t space = maxLen - hdr;
    size_t remaining = outLen_ - pos_;
    size_t n;
    if (srcPairBytes_ == 0) {
      n = remaining < space ? remaining : space;
      memcpy(buf + hdr, data_ + pos_, n);
    } else {
      space &= ~(size_t)3;  // whole pixel pairs only
      n = remaining < space ? remaining : space;
      const uint8_t *src = data_ + (pos_ / 4) * srcPairBytes_;
      UVCConvert::toYUY2(fmt_, src, buf + hdr, n / 4);
    }
    pos_ += n;
    const bool last = pos_ >= outLen_;
    buf[0] = hdr;
    buf[1] = (uint8_t)(uvc_spec::HDR_EOH | (fid_ ? uvc_spec::HDR_FID : 0) |
                       (last ? uvc_spec::HDR_EOF : 0) | (withPts_ ? uvc_spec::HDR_PTS : 0));
    if (withPts_) le::put32(buf + 2, pts_);
    if (last) active_ = false;
    return hdr + n;
  }

  /// true while the frame still has data to send
  bool active() const { return active_; }
  /// Aborts the current frame
  void abort() { active_ = false; }
  /// Size of the frame data as it is sent (after any conversion)
  size_t outputLength() const { return outLen_; }

 private:
  const uint8_t *data_ = nullptr;
  size_t outLen_ = 0;
  size_t pos_ = 0;
  PixelFormat fmt_ = PixelFormat::MJPEG;
  uint8_t srcPairBytes_ = 0;
  bool fid_ = false;
  bool withPts_ = false;
  bool active_ = false;
  uint32_t pts_ = 0;
};

}  // namespace tinyuvc
