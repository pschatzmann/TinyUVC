#pragma once
/**
 * @file TinyUVC/TinyGPU.h
 * @brief Integration with TinyGPU (https://github.com/pschatzmann/TinyGPU):
 * UVCDisplayDriver is a TinyGPU DisplayDriver whose "display" is the
 * webcam. Everything that is rendered into a TinyGPU surface (2D drawing,
 * text, sprites, WireFrame3D) and written with writeData() shows up as
 * video on the PC.
 *
 * @code
 * UVCDevice uvc;
 * UVCDisplayDriver<RGB565> display(uvc);
 * FrameBufferRGB565 framebuffer(320, 240, FontRGB565);
 *
 * void setup() {
 *   display.addResolution(UVCFormat::MJPEG, 320, 240, 15);
 *   display.addResolution(UVCFormat::YUY2, 160, 120, 15);
 *   display.begin();
 *   framebuffer.begin();
 * }
 * void loop() {
 *   if (display.sizeChanged(framebuffer.width(), framebuffer.height()))
 *     framebuffer.resize(display.width(), display.height());
 *   framebuffer.clear(RGB565(255, 255, 255));
 *   ...                                   // draw
 *   display.writeData(framebuffer);       // waits for the next frame slot
 * }
 * @endcode
 *
 * Uncompressed YUY2 needs 2 bytes per pixel: full speed USB (about 1 MB/s)
 * carries 160x120 at 15+ fps, but 320x240 only at about 5 fps. For bigger
 * pictures offer MJPEG, the frames are then compressed:
 * - ESP32 (RGB565): with the JPEG encoder of esp32-camera, which comes with
 *   the arduino-esp32 core (fmt2jpg_cb(), img_converters.h).
 * - other boards, and RGB888 surfaces: with TinyJPEG
 *   (https://github.com/pschatzmann/TinyJPEG). Include TinyJPEGEncoder.h
 *   before this header (in Arduino the library is only found if the sketch
 *   includes it). Its encoder supports frames up to JE_MAX_WIDTH pixels
 *   wide (default 320): define it before the include for bigger frames.
 * Define TINYUVC_NO_JPEG to switch MJPEG off.
 */
#if defined(ARDUINO) && !defined(HOST)
// TinyGPU's touch and display drivers use them: the Arduino build only
// finds these libraries if they are included
#include <SPI.h>
#include <Wire.h>
#endif
#include <new>

// angle brackets: "TinyGPU.h" would be this file
#include <TinyGPU.h>

#include "../TinyUVC.h"

#if !defined(TINYUVC_NO_JPEG) && defined(ARDUINO_ARCH_ESP32) && \
    __has_include("img_converters.h")
#include "img_converters.h"
#define TINYUVC_JPEG_ESP32 1
#else
#define TINYUVC_JPEG_ESP32 0
#endif

#if !defined(TINYUVC_NO_JPEG) && __has_include(<TinyJPEGEncoder.h>)
#include <TinyJPEGEncoder.h>
#define TINYUVC_JPEG_TINYJPEG 1
#else
#define TINYUVC_JPEG_TINYJPEG 0
#endif

/// 1 if MJPEG frames can be produced from TinyGPU surfaces
#define TINYUVC_HAS_JPEG (TINYUVC_JPEG_ESP32 || TINYUVC_JPEG_TINYJPEG)

namespace tinyuvc {

/// @brief Maps a TinyGPU color type to the matching PixelFormat
template <typename RGB_T>
struct UVCTinyGPUPixel;

/// TinyGPU stores RGB565 byte-swapped: the high byte comes first in memory
template <>
struct UVCTinyGPUPixel<tinygpu::RGB565> {
  static constexpr PixelFormat format = PixelFormat::RGB565_BE;
  static constexpr size_t bytes = 2;
};

/// TinyGPU stores RGB888 as the bytes r, g, b
template <>
struct UVCTinyGPUPixel<tinygpu::RGB888> {
  static constexpr PixelFormat format = PixelFormat::RGB888;
  static constexpr size_t bytes = 3;
};

/**
 * @brief TinyGPU DisplayDriver that sends the written surfaces as webcam
 * frames to the USB host: uncompressed (YUY2), or compressed with TinyJPEG
 * (MJPEG) - whichever format the host selected.
 *
 * The surface is copied (YUY2) or compressed (MJPEG) into a frame buffer,
 * from PSRAM where available (see UVCMemory), so the sketch can render the
 * next frame while the previous one is still being sent. writeData() waits
 * until the device can take a new frame and, with pacing (default), until
 * the frame interval the host selected has passed: the render loop then
 * runs at the frame rate of the webcam.
 *
 * width()/height() report the resolution the host selected; resize your
 * surface when it changes (see the TinyGPUWireframeWebcam example).
 * Supported color types: RGB565 and RGB888.
 */
template <typename RGB_T = tinygpu::RGB565>
class UVCDisplayDriver : public tinygpu::DisplayDriver<RGB_T> {
 public:
  explicit UVCDisplayDriver(UVCDevice &uvc) : uvc_(uvc) {}
  ~UVCDisplayDriver() override {
#if TINYUVC_JPEG_TINYJPEG
    if (encoder_ != nullptr) {
      encoder_->~TinyJPEGEncoder();
      ::free(encoder_);
    }
#endif
  }

  /// Offers a resolution to the host. Call before begin(). MJPEG needs a
  /// JPEG encoder (see TINYUVC_HAS_JPEG); maxJpegSize is the biggest JPEG
  /// the host has to expect (0: width * height bytes).
  bool addResolution(UVCFormat format, uint16_t width, uint16_t height,
                     std::initializer_list<uint8_t> fps,
                     uint32_t maxJpegSize = 0) {
    if (format == UVCFormat::MJPEG) {
#if TINYUVC_JPEG_TINYJPEG
      if (!useEsp32Jpeg() && width > JE_MAX_WIDTH) {
        TINYUVC_LOGE("MJPEG wider than JE_MAX_WIDTH (%d)", (int)JE_MAX_WIDTH);
        return false;
      }
#endif
      if (!useEsp32Jpeg() && !TINYUVC_JPEG_TINYJPEG) {
        TINYUVC_LOGE("MJPEG needs a JPEG encoder: include TinyJPEGEncoder.h");
        return false;
      }
      if (maxJpegSize == 0) maxJpegSize = (uint32_t)width * height;
    }
    return uvc_.addResolution(format, width, height, fps, maxJpegSize);
  }
  bool addResolution(UVCFormat format, uint16_t width, uint16_t height,
                     uint8_t fps, uint32_t maxJpegSize = 0) {
    return addResolution(format, width, height, {fps}, maxJpegSize);
  }
  /// Uncompressed (YUY2) resolution
  bool addResolution(uint16_t width, uint16_t height, uint8_t fps) {
    return addResolution(UVCFormat::YUY2, width, height, {fps});
  }
  bool addResolution(uint16_t width, uint16_t height,
                     std::initializer_list<uint8_t> fps) {
    return addResolution(UVCFormat::YUY2, width, height, fps);
  }

  /// Starts the UVC device (if it is not running yet)
  bool begin() override { return uvc_.begin(); }

  void end() override {
    waitSent(1000);
    for (auto &b : buffers_) b.release();
    work_.release();
  }

  /// Wait for the frame interval the host selected (default true)
  void setPacing(bool pacing) { pacing_ = pacing; }

  /// How long writeData() waits for the previous frame (default 1000 ms)
  void setTimeout(uint32_t ms) { timeoutMs_ = ms; }

  /// JPEG quality 1..100 (default 80)
  void setJpegQuality(uint8_t quality) { quality_ = quality; }

  /// Where the frame buffers are allocated (default: PSRAM if available)
  void setMemoryType(UVCMemoryType type) {
    end();
    for (auto &b : buffers_) b.setMemoryType(type);
    work_.setMemoryType(type);
  }

  /// Sends the surface as the next frame. Returns false if it was not
  /// sent: not streaming, the surface size does not match the host's
  /// selection, or the previous frame did not finish in time.
  bool writeData(tinygpu::ISurface<RGB_T> &surface) override {
    if (!uvc_.isStreaming()) return false;
    if (surface.width() != width() || surface.height() != height()) {
      mismatched_++;
      return false;
    }
    if (isJpeg()) return sendJpeg(surface.data());
    UVCFrameBuffer *buf = nextBuffer(rawBytes());
    if (buf == nullptr) return false;
    memcpy(buf->data(), surface.data(), rawBytes());
    return send(*buf, rawBytes(), UVCTinyGPUPixel<RGB_T>::format);
  }

  /// Copies the surface into the frame at x/y (e.g. for partial updates of
  /// GUI libraries) without sending it: call flush() when the frame is
  /// complete.
  bool writeData(tinygpu::ISurface<RGB_T> &surface, size_t x,
                 size_t y) override {
    if (!uvc_.isStreaming() || x >= width() || y >= height()) return false;
    if (!work_.reserve(rawBytes())) return false;
    const size_t bpp = UVCTinyGPUPixel<RGB_T>::bytes;
    const size_t w = std::min(surface.width(), width() - x);
    const size_t h = std::min(surface.height(), height() - y);
    const uint8_t *src = surface.data();
    for (size_t row = 0; row < h; row++) {
      memcpy(work_.data() + ((y + row) * width() + x) * bpp,
             src + row * surface.width() * bpp, w * bpp);
    }
    dirty_ = true;
    return true;
  }

  /// Sends the frame composed with writeData(surface, x, y)
  bool flush() {
    if (!dirty_ || !uvc_.isStreaming()) return false;
    dirty_ = false;
    if (isJpeg()) return sendJpeg(work_.data());
    UVCFrameBuffer *buf = nextBuffer(rawBytes());
    if (buf == nullptr) return false;
    memcpy(buf->data(), work_.data(), rawBytes());
    return send(*buf, rawBytes(), UVCTinyGPUPixel<RGB_T>::format);
  }

  /// Width/height selected by the host (the first resolution while the
  /// host is not streaming)
  size_t width() const override { return selected().width; }
  size_t height() const override { return selected().height; }

  /// true when the host selected a different size than width x height
  bool sizeChanged(size_t width, size_t height) const {
    return width != this->width() || height != this->height();
  }

  /// true if the host selected MJPEG
  bool isJpeg() const {
    return uvc_.isStreaming() && uvc_.settings().format == UVCFormat::MJPEG;
  }

  /// Waits until the device finished reading the previous frame
  bool waitSent(uint32_t timeoutMs) {
    const uint32_t start = platform::millis();
    while (uvc_.isSending()) {
      if (platform::millis() - start >= timeoutMs) return false;
      platform::idle();
    }
    return true;
  }

  uint32_t frames() const { return frames_; }
  uint32_t dropped() const { return dropped_; }
  /// Surfaces that were rejected because of their size
  uint32_t mismatched() const { return mismatched_; }
  /// Size of the last JPEG
  size_t lastJpegSize() const { return lastJpegSize_; }

  UVCDevice &device() { return uvc_; }

 protected:
  UVCDevice &uvc_;
  /// frames handed to the device: one is sent while the next is prepared
  UVCFrameBuffer buffers_[2];
  int next_ = 0;
  /// RGB565 in TinyJPEG's byte order, or frames composed by partial writes
  UVCFrameBuffer work_;
#if TINYUVC_JPEG_TINYJPEG
  tinyjpeg::TinyJPEGEncoder *encoder_ = nullptr;  // allocated when needed
#endif

  /// esp32-camera's encoder: RGB565 only (in TinyGPU's byte order)
  static constexpr bool useEsp32Jpeg() {
    return TINYUVC_JPEG_ESP32 && UVCTinyGPUPixel<RGB_T>::bytes == 2;
  }

  /// Output of fmt2jpg_cb() into a frame buffer
  struct JpegSink {
    uint8_t *data;
    size_t capacity;
    size_t len;
    bool overflow;
  };

  static size_t jpegSinkWrite(void *arg, size_t index, const void *data,
                              size_t len) {
    JpegSink *sink = (JpegSink *)arg;
    if (data == nullptr) return 0;  // end of the image
    if (index + len > sink->capacity) {
      sink->overflow = true;
      return 0;  // stops the encoder
    }
    memcpy(sink->data + index, data, len);
    if (index + len > sink->len) sink->len = index + len;
    return len;
  }
  uint8_t quality_ = 80;
  bool pacing_ = true;
  bool dirty_ = false;
  bool sent_ = false;
  uint32_t timeoutMs_ = 1000;
  uint32_t lastUs_ = 0;
  uint32_t frames_ = 0;
  uint32_t dropped_ = 0;
  uint32_t mismatched_ = 0;
  size_t lastJpegSize_ = 0;

  struct Size {
    size_t width = 0;
    size_t height = 0;
    uint32_t maxFrameSize = 0;
  };

  /// The resolution the host selected, or the first one
  Size selected() const {
    Size s;
    const UVCFrameSpec *f = nullptr;
    if (uvc_.isStreaming()) {
      const UVCStreamSettings st = uvc_.settings();
      if (st.formatIndex >= 1 && st.formatIndex <= uvc_.formatCount()) {
        const UVCFormatSpec &fmt = uvc_.formats()[st.formatIndex - 1];
        if (st.frameIndex >= 1 && st.frameIndex <= fmt.frameCount)
          f = &fmt.frames[st.frameIndex - 1];
      }
    } else if (uvc_.formatCount() > 0 && uvc_.formats()[0].frameCount > 0) {
      f = &uvc_.formats()[0].frames[0];
    }
    if (f != nullptr) {
      s.width = f->width;
      s.height = f->height;
      s.maxFrameSize = f->maxFrameSize;
    }
    return s;
  }

  size_t rawBytes() const {
    return width() * height() * UVCTinyGPUPixel<RGB_T>::bytes;
  }

  /// The buffer for the next frame. The device takes a frame only when the
  /// previous one is done, so at most the other buffer is still being sent.
  UVCFrameBuffer *nextBuffer(size_t bytes) {
    UVCFrameBuffer &buf = buffers_[next_];
    if (buf.capacity() < bytes) {
      buf.release();  // no copy of the old content needed
      if (!buf.reserve(bytes)) {
        TINYUVC_LOGE("UVCDisplayDriver: no memory for %u bytes",
                     (unsigned)bytes);
        dropped_++;
        return nullptr;
      }
    }
    return &buf;
  }

  bool sendJpeg(const uint8_t *pixels) {
    const uint16_t w = (uint16_t)width(), h = (uint16_t)height();
#if TINYUVC_JPEG_ESP32
    if (useEsp32Jpeg()) {
      const size_t maxJpeg = selected().maxFrameSize;
      UVCFrameBuffer *buf = nextBuffer(maxJpeg);
      if (buf == nullptr) return false;
      JpegSink sink{buf->data(), maxJpeg, 0, false};
      const bool ok = fmt2jpg_cb((uint8_t *)pixels, (size_t)w * h * 2, w, h,
                                 PIXFORMAT_RGB565, quality_, jpegSinkWrite,
                                 &sink);
      if (!ok || sink.overflow) {
        TINYUVC_LOGW("JPEG encoding failed%s",
                     sink.overflow ? ": increase maxJpegSize" : "");
        dropped_++;
        return false;
      }
      lastJpegSize_ = sink.len;
      return send(*buf, sink.len, PixelFormat::MJPEG);
    }
#endif
#if TINYUVC_JPEG_TINYJPEG
    JEPixelFormat format = JE_FMT_RGB888;
    if (UVCTinyGPUPixel<RGB_T>::bytes == 2) {
      // TinyGPU's RGB565 is byte-swapped, TinyJPEG reads native order
      const size_t bytes = (size_t)w * h * 2;
      if (pixels != work_.data()) {
        if (!work_.reserve(bytes)) {
          dropped_++;
          return false;
        }
      }
      uint8_t *dst = work_.data();
      for (size_t i = 0; i < bytes; i += 2) {
        const uint8_t hi = pixels[i];
        dst[i] = pixels[i + 1];
        dst[i + 1] = hi;
      }
      pixels = dst;
      format = JE_FMT_RGB565;
    }
    const size_t maxJpeg = selected().maxFrameSize;
    UVCFrameBuffer *buf = nextBuffer(maxJpeg);
    if (buf == nullptr) return false;
    if (encoder_ == nullptr) {
      // the encoder works on its workspace all the time: internal RAM is
      // much faster than PSRAM (where new would put it on an ESP32)
      void *mem = UVCMemory::alloc(sizeof(tinyjpeg::TinyJPEGEncoder),
                                   UVCMemoryType::RAMOnly);
      if (mem == nullptr) {
        dropped_++;
        return false;
      }
      encoder_ = new (mem) tinyjpeg::TinyJPEGEncoder();
    }
    encoder_->setQuality(quality_);
    size_t len = 0;
    const JERESULT r =
        encoder_->encodeJpg(pixels, w, h, format, buf->data(), maxJpeg, len);
    if (r != JER_OK) {
      TINYUVC_LOGW("JPEG encoding failed (%d): increase maxJpegSize?", (int)r);
      dropped_++;
      return false;
    }
    lastJpegSize_ = len;
    return send(*buf, len, PixelFormat::MJPEG);
#else
    (void)pixels;
    (void)w;
    (void)h;
    dropped_++;
    return false;
#endif
  }

  bool send(UVCFrameBuffer &buf, size_t len, PixelFormat format) {
    if (pacing_ && sent_) {
      const uint32_t intervalUs = uvc_.settings().interval / 10;
      while (uvc_.isStreaming() && platform::micros() - lastUs_ < intervalUs)
        platform::idle();
    }
    if (!uvc_.isReady() && !uvc_.waitReady(timeoutMs_)) {
      dropped_++;
      return false;
    }
    if (!uvc_.writeFrame(buf.data(), len, format)) {
      dropped_++;
      return false;
    }
    next_ = 1 - next_;  // keep this buffer until it is sent
    lastUs_ = platform::micros();
    sent_ = true;
    frames_++;
    return true;
  }

  bool setAddressWindow(size_t, size_t, size_t, size_t) override {
    return true;
  }
};

}  // namespace tinyuvc
