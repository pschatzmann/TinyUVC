#pragma once
/**
 * @file TinyUVCAudioTools.h
 * @brief Integration with the video classes of arduino-audio-tools
 * (https://github.com/pschatzmann/arduino-audio-tools):
 * - UVCVideoOutput: a VideoOutput, so everything that writes frames to a
 *   VideoOutput - VideoPlayer (AVI/MP4 files), MJPEGEncoder, the video
 *   decoders (H264, MPEG1, MJPEG -> RGB565/YUV422) - can feed the webcam.
 * - UVCVideoCopy: streams the frames of a VideoFrameSource (e.g.
 *   CameraFrameSourceTinyCamera) to the webcam.
 *
 * The video classes of arduino-audio-tools use FreeRTOS: on RP2040/RP2350
 * select Tools > Operating System > "FreeRTOS SMP" and include
 * "AudioTools/Concurrency/RTOS.h" after AudioTools.h, on STM32 install the
 * STM32FreeRTOS library.
 */
#include "AudioToolsConfig.h"
#include "AudioTools/Video/Video.h"
#include "TinyUVC.h"

namespace tinyuvc {

/// Maps an audio-tools VideoFormat to the matching PixelFormat
inline bool toPixelFormat(audio_tools::VideoFormat format, PixelFormat &out) {
  using audio_tools::VideoFormat;
  switch (format) {
    case VideoFormat::MJPEG:
      out = PixelFormat::MJPEG;
      return true;
    case VideoFormat::YUV422:
      out = PixelFormat::YUY2;
      return true;
    case VideoFormat::RGB565:
      out = PixelFormat::RGB565_LE;
      return true;
    case VideoFormat::RGB888:
      out = PixelFormat::RGB888;
      return true;
    case VideoFormat::RAW:  // 24 bit BGR
      out = PixelFormat::BGR888;
      return true;
    default:
      return false;  // H264/MPEG need a decoder first
  }
}

/// Adds the resolution of a VideoInfo to a UVCDevice: MJPEG for MJPEG
/// frames, YUY2 for raw frames (fps 0 = 15).
inline bool addResolution(UVCDevice &uvc, const audio_tools::VideoInfo &info,
                          uint32_t maxJpegSize = 0) {
  PixelFormat pf;
  if (!info || !toPixelFormat(info.format, pf)) return false;
  const uint8_t fps = info.fps <= 0 ? 15 : info.fps >= 255 ? 255 : (uint8_t)(info.fps + 0.5f);
  const bool jpeg = pf == PixelFormat::MJPEG;
  return uvc.addResolution(jpeg ? UVCFormat::MJPEG : UVCFormat::YUY2, info.width,
                           info.height, fps, jpeg ? maxJpegSize : 0);
}

/**
 * @brief VideoOutput that sends every frame (write() calls up to flush())
 * to a UVCDevice. Two frame buffers are used: one is sent while the next
 * one is written.
 *
 * The frame buffers grow to the size of the biggest frame and are taken
 * from PSRAM where the board has it (see setMemoryType(), reserve()).
 *
 * Writers that deliver one complete frame per write() without calling
 * flush() (e.g. MJPEGEncoder) are supported as well: a complete JPEG
 * written in one call is sent right away, for other formats use
 * setFlushOnWrite(true).
 *
 * @code
 * UVCDevice uvc;
 * UVCVideoOutput out(uvc, PixelFormat::MJPEG);
 * MJPEGEncoder encoder(out);   // RGB565 -> JPEG -> webcam
 * @endcode
 */
class UVCVideoOutput : public audio_tools::VideoOutput {
 public:
  explicit UVCVideoOutput(UVCDevice &uvc, PixelFormat format = PixelFormat::MJPEG)
      : uvc_(uvc), format_(format) {}

  /// Pixel format of the frames written to this output
  void setPixelFormat(PixelFormat format) { format_ = format; }
  PixelFormat pixelFormat() const { return format_; }

  /// Same with an audio-tools VideoFormat; false if not supported
  bool setVideoFormat(audio_tools::VideoFormat format) {
    return toPixelFormat(format, format_);
  }

  /// How long flush() waits for the previous frame to be sent. 0 (default):
  /// drop the frame if the device is busy, which keeps live sources live.
  void setTimeout(uint32_t ms) { timeoutMs_ = ms; }

  /// Max frame size: bigger frames are dropped (default 512 KB)
  void setMaxFrameSize(size_t size) { maxFrameSize_ = size; }

  /// Where the two frame buffers are allocated: UVCMemoryType::Auto
  /// (default: PSRAM if available, else RAM), PSRAM or RAM. Call it before
  /// the first frame or reserve().
  void setMemoryType(UVCMemoryType type) {
    release();
    for (auto &b : buffers_) b.setMemoryType(type);
  }

  /// Allocates both frame buffers for frames of up to size bytes now,
  /// instead of growing them with the first frames. False if there is not
  /// enough memory.
  bool reserve(size_t size) {
    return buffers_[0].reserve(size) && buffers_[1].reserve(size);
  }

  /// Frees the frame buffers; they are allocated again by the next frame.
  /// The frame that is still being sent keeps its buffer.
  void release() {
    buffers_[fill_].release();
    if (!uvc_.isSending()) buffers_[1 - fill_].release();
    overflow_ = false;
  }

  /// flush() waits until the frame interval selected by the host has passed
  /// since the previous frame: plays files (e.g. from DemuxerAVI) at their
  /// frame rate instead of as fast as USB allows. Implies waiting for the
  /// device (see setTimeout()).
  void setPacing(bool pacing) { pacing_ = pacing; }

  /// Every write() is a complete frame (for writers that never call flush())
  void setFlushOnWrite(bool flushOnWrite) { flushOnWrite_ = flushOnWrite; }

  size_t write(const uint8_t *data, size_t len) override {
    UVCFrameBuffer &buf = buffers_[fill_];
    const bool complete = flushOnWrite_ || (buf.empty() && isCompleteJpeg(data, len));
    if (overflow_ || buf.size() + len > maxFrameSize_) {
      overflow_ = true;
    } else if (!buf.append(data, len)) {
      TINYUVC_LOGW("UVCVideoOutput: out of memory (frame of %u bytes)",
                   (unsigned)(buf.size() + len));
      overflow_ = true;
    }
    if (complete) flush();
    return len;
  }

  /// Completes the frame and hands it to the UVC device
  void flush() override {
    UVCFrameBuffer &buf = buffers_[fill_];
    if (buf.empty() && !overflow_) return;  // e.g. flush() after a complete JPEG
    hadOutput_ = false;
    if (buf.empty()) {
      overflow_ = false;
      dropped_++;
      return;
    }
    bool ok = !overflow_ && uvc_.isStreaming();
    if (ok && pacing_ && sent_) {
      const uint32_t intervalUs = uvc_.settings().interval / 10;
      while (uvc_.isStreaming() && platform::micros() - lastUs_ < intervalUs)
        platform::idle();
    }
    if (ok && !uvc_.isReady()) {
      const uint32_t timeout = timeoutMs_ > 0 ? timeoutMs_ : (pacing_ ? 1000 : 0);
      ok = timeout > 0 && uvc_.waitReady(timeout);
    }
    if (ok && uvc_.writeFrame(buf.data(), buf.size(), format_)) {
      // keep this buffer until it is sent, write the next frame to the other
      fill_ = 1 - fill_;
      hadOutput_ = true;
      frames_++;
      sent_ = true;
      lastUs_ = platform::micros();
    } else {
      dropped_++;
    }
    buffers_[fill_].clear();
    overflow_ = false;
  }

  bool hadOutput() const override { return hadOutput_; }

  /// true if data is a whole JPEG image (SOI ... EOI)
  bool isCompleteJpeg(const uint8_t *data, size_t len) const {
    return format_ == PixelFormat::MJPEG && len >= 4 && data[0] == 0xFF &&
           data[1] == 0xD8 && data[len - 2] == 0xFF && data[len - 1] == 0xD9;
  }

  /// Frames handed to the device / dropped
  uint32_t frames() const { return frames_; }
  uint32_t dropped() const { return dropped_; }

 protected:
  UVCDevice &uvc_;
  PixelFormat format_;
  UVCFrameBuffer buffers_[2];
  int fill_ = 0;
  size_t maxFrameSize_ = 512 * 1024;
  uint32_t timeoutMs_ = 0;
  bool pacing_ = false;
  bool flushOnWrite_ = false;
  bool sent_ = false;
  uint32_t lastUs_ = 0;
  bool overflow_ = false;
  bool hadOutput_ = false;
  uint32_t frames_ = 0;
  uint32_t dropped_ = 0;
};

/**
 * @brief Streams the frames of an audio-tools VideoFrameSource (e.g.
 * CameraFrameSourceTinyCamera) to a UVCDevice.
 *
 * @code
 * CameraFrameSourceTinyCamera source;
 * UVCDevice uvc;
 * UVCVideoCopy copier(uvc, source);
 * void setup() {
 *   source.begin(config, 15);
 *   copier.begin();          // offers the source's resolution, starts uvc
 * }
 * void loop() { copier.copy(); }
 * @endcode
 */
class UVCVideoCopy {
 public:
  UVCVideoCopy(UVCDevice &uvc, audio_tools::VideoFrameSource &source)
      : uvc_(uvc), source_(source) {}

  /// Offers the resolution of the source (if the device is not started
  /// yet) and starts the device
  bool begin(uint32_t maxJpegSize = 0) {
    const audio_tools::VideoInfo info = source_.videoInfo();
    if (!toPixelFormat(info.format, format_)) {
      TINYUVC_LOGE("UVCVideoCopy: unsupported video format %d", (int)info.format);
      return false;
    }
    if (uvc_.isActive()) return true;
    if (!addResolution(uvc_, info, maxJpegSize)) return false;
    return uvc_.begin();
  }

  /// Pixel format of the source frames (set by begin())
  void setPixelFormat(PixelFormat format) { format_ = format; }

  /// Sends the next frame of the source when the device is ready and the
  /// frame interval selected by the host has passed. Returns true if a frame
  /// was sent.
  bool copy() {
    if (!uvc_.isReady()) return false;
    const uint32_t now = platform::micros();
    if (sent_ && now - lastUs_ < uvc_.settings().interval / 10) return false;
    size_t len = 0;
    // the previous frame stays valid until this call: it was sent completely
    const uint8_t *data = source_.nextFrame(len);
    if (data == nullptr || len == 0) return false;
    if (!uvc_.writeFrame(data, len, format_)) return false;
    lastUs_ = now;
    sent_ = true;
    return true;
  }

 protected:
  UVCDevice &uvc_;
  audio_tools::VideoFrameSource &source_;
  PixelFormat format_ = PixelFormat::MJPEG;
  uint32_t lastUs_ = 0;
  bool sent_ = false;
};

}  // namespace tinyuvc
