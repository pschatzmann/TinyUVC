#pragma once
/**
 * @file TinyUVCCamera.h
 * @brief Streams a camera of the TinyCamera library
 * (https://github.com/pschatzmann/TinyCamera) as USB webcam: esp32-camera
 * sensors (OV2640, OV3660, OV5640, ...), ESP32-P4 MIPI-CSI, RP2040
 * (PicoCamera), STM32 DCMI and the desktop (webcam or test pattern).
 */
#include "TinyCamera.h"
#include "TinyUVC.h"

namespace tinyuvc {

/**
 * @brief Connects a TinyCamera to a UVCDevice.
 *
 * Offers MJPEG if the camera config uses PIXFORMAT_JPEG (sensors with a
 * JPEG encoder), otherwise YUY2, which is converted on the fly from the
 * camera's RGB565 (or YUV422 / grayscale) frames. When the host selects a
 * different resolution or format, the camera is reconfigured.
 *
 * @code
 * UVCDevice uvc;
 * UVCCamera camera(uvc);
 * void setup() {
 *   camera_config_t cfg = TinyCamera::defaultConfig();
 *   setPinsAIThinker(cfg);
 *   camera.addFrameSize(FRAMESIZE_QVGA);
 *   camera.addFrameSize(FRAMESIZE_VGA);
 *   camera.begin(cfg);   // also starts uvc
 * }
 * void loop() { camera.copy(); }
 * @endcode
 */
class UVCCamera {
 public:
  explicit UVCCamera(UVCDevice &uvc) : uvc_(uvc) {}

  /// Offers an additional frame size (call before begin()). Without any
  /// call, the frame size of the camera config is offered.
  bool addFrameSize(framesize_t size) {
    if (sizeCount_ >= TINYUVC_MAX_FRAMES) return false;
    sizes_[sizeCount_++].size = size;
    return true;
  }

  /// Frame rates offered for every size (call before begin()); the first is
  /// the default. Default: 15 fps.
  void setFps(std::initializer_list<uint8_t> fps) {
    fpsCount_ = 0;
    for (uint8_t f : fps)
      if (f > 0 && fpsCount_ < TINYUVC_MAX_INTERVALS) fps_[fpsCount_++] = f;
  }
  void setFps(uint8_t fps) { setFps({fps}); }

  /// Selects the formats offered to the host. Default: MJPEG if the camera
  /// config uses PIXFORMAT_JPEG, YUY2 otherwise. With both, the camera is
  /// restarted when the host switches between them.
  void setFormats(bool mjpeg, bool yuy2) {
    mjpeg_ = mjpeg;
    yuy2_ = yuy2;
    formatsSet_ = true;
  }

  /// Camera pixel format used for YUY2 streams: PIXFORMAT_RGB565 (default),
  /// PIXFORMAT_YUV422 or PIXFORMAT_GRAYSCALE (not on STM32)
  void setRawPixelFormat(pixformat_t fmt) { raw_ = fmt; }

  /// Byte order of RGB565 frames. Default: big endian on ESP32 with
  /// esp32-camera, little endian everywhere else.
  void setRgb565BigEndian(bool bigEndian) { rgb565BE_ = bigEndian; }

  /// Max JPEG size reported to the host (0 = width * height * 2)
  void setMaxJpegSize(uint32_t size) { maxJpegSize_ = size; }

  /// Starts the camera, determines the offered resolutions (one probe
  /// capture per frame size) and starts the UVC device (unless startDevice
  /// is false: then call uvc.begin() yourself)
  bool begin(const camera_config_t &config, bool startDevice = true) {
    config_ = config;
    if (!formatsSet_) {
      mjpeg_ = config.pixel_format == PIXFORMAT_JPEG;
      yuy2_ = !mjpeg_;
    }
    if (!mjpeg_ && !yuy2_) return false;
    if (sizeCount_ == 0) addFrameSize(config.frame_size);
    if (fpsCount_ == 0) setFps(15);
    // probe every frame size
    const pixformat_t probeFmt = mjpeg_ ? PIXFORMAT_JPEG : raw_;
    for (int i = 0; i < sizeCount_; i++) {
      if (!configure(sizes_[i].size, probeFmt)) return false;
      tiny_camera::TinyCameraFrame frame = camera_.captureFrame();
      if (!frame) {
        TINYUVC_LOGE("UVCCamera: probe capture failed");
        return false;
      }
      sizes_[i].width = (uint16_t)frame.width();
      sizes_[i].height = (uint16_t)frame.height();
      TINYUVC_LOGI("UVCCamera: frame size %d is %ux%u", (int)sizes_[i].size,
                   sizes_[i].width, sizes_[i].height);
    }
    if (!uvc_.isActive()) {
      uvc_.clearResolutions();
      for (int f = 0; f < 2; f++) {
        const bool isMjpeg = f == 0;
        if ((isMjpeg && !mjpeg_) || (!isMjpeg && !yuy2_)) continue;
        for (int i = 0; i < sizeCount_; i++) {
          if (!uvc_.addResolution(isMjpeg ? UVCFormat::MJPEG : UVCFormat::YUY2,
                                  sizes_[i].width, sizes_[i].height, fps_,
                                  fpsCount_, isMjpeg ? maxJpegSize_ : 0))
            return false;
        }
      }
      if (startDevice && !uvc_.begin()) return false;
    }
    generation_ = uvc_.streamGeneration();
    return true;
  }

  /// Stops the camera
  void end() {
    frame_.release();
    camera_.end();
  }

  /**
   * Call in loop(): reconfigures the camera when the host (re)starts a
   * stream, and captures and sends a frame when the device is ready and
   * the frame interval selected by the host has passed. Returns true if a
   * frame was sent.
   */
  bool copy() {
    if (!uvc_.isStreaming()) {
      if (uvc_.isActive() && frame_) frame_.release();
      return false;
    }
    const uint32_t gen = uvc_.streamGeneration();
    if (gen != generation_) {
      generation_ = gen;
      frame_.release();
      if (!applySettings(uvc_.settings())) return false;
    }
    const uint32_t intervalUs = uvc_.settings().interval / 10;
    const uint32_t now = platform::micros();
    if (sentOnce_ && now - lastFrameUs_ < intervalUs) return false;
    if (!uvc_.isReady()) return false;
    frame_.release();  // the previous frame was completely sent
    frame_ = camera_.captureFrame();
    if (!frame_) return false;
    TINYUVC_LOGD("UVCCamera: frame %u bytes, %ux%u, t=%ld.%06ld", (unsigned)frame_.size(),
                 (unsigned)frame_.width(), (unsigned)frame_.height(),
                 (long)frame_.timestamp().tv_sec, (long)frame_.timestamp().tv_usec);
    PixelFormat fmt;
    if (!toPixelFormat(frame_.format(), fmt)) {
      TINYUVC_LOGW("UVCCamera: unsupported camera pixel format %d",
                   (int)frame_.format());
      return false;
    }
    if (!uvc_.writeFrame(frame_.data(), frame_.size(), fmt)) return false;
    lastFrameUs_ = now;
    sentOnce_ = true;
    frames_++;
    return true;
  }

  /// The camera (e.g. for sensor settings: camera().sensor())
  tiny_camera::TinyCamera &camera() { return camera_; }

  /// Number of frames sent
  uint32_t frames() const { return frames_; }

 protected:
  struct Size {
    framesize_t size;
    uint16_t width = 0;
    uint16_t height = 0;
  };
  UVCDevice &uvc_;
  tiny_camera::TinyCamera camera_;
  tiny_camera::TinyCameraFrame frame_;
  camera_config_t config_{};
  Size sizes_[TINYUVC_MAX_FRAMES];
  int sizeCount_ = 0;
  uint8_t fps_[TINYUVC_MAX_INTERVALS] = {};
  int fpsCount_ = 0;
  bool mjpeg_ = false;
  bool yuy2_ = true;
  bool formatsSet_ = false;
  pixformat_t raw_ = PIXFORMAT_RGB565;
#if defined(ESP32) && !defined(TINY_CAMERA_ESP_VIDEO)
  bool rgb565BE_ = true;
#else
  bool rgb565BE_ = false;
#endif
  uint32_t maxJpegSize_ = 0;
  uint32_t generation_ = 0;
  uint32_t lastFrameUs_ = 0;
  bool sentOnce_ = false;
  uint32_t frames_ = 0;
  bool configured_ = false;
  framesize_t curSize_{};
  pixformat_t curFormat_{};

  /// (Re)starts the camera with the frame size and pixel format if needed
  bool configure(framesize_t size, pixformat_t fmt) {
    if (configured_ && camera_.isActive() && size == curSize_ && fmt == curFormat_)
      return true;
    camera_config_t cfg = config_;
    cfg.frame_size = size;
    cfg.pixel_format = fmt;
    camera_.end();
    if (!camera_.begin(cfg)) {
      configured_ = false;
      TINYUVC_LOGE("UVCCamera: camera start failed");
      return false;
    }
    curSize_ = size;
    curFormat_ = fmt;
    configured_ = true;
    return true;
  }

  bool applySettings(const UVCStreamSettings &s) {
    for (int i = 0; i < sizeCount_; i++) {
      if (sizes_[i].width == s.width && sizes_[i].height == s.height) {
        const pixformat_t fmt = s.format == UVCFormat::MJPEG ? PIXFORMAT_JPEG : raw_;
        return configure(sizes_[i].size, fmt);
      }
    }
    TINYUVC_LOGE("UVCCamera: no frame size for %ux%u", s.width, s.height);
    return false;
  }

  bool toPixelFormat(pixformat_t in, PixelFormat &out) const {
    switch (in) {
      case PIXFORMAT_JPEG:
        out = PixelFormat::MJPEG;
        return true;
      case PIXFORMAT_RGB565:
        out = rgb565BE_ ? PixelFormat::RGB565_BE : PixelFormat::RGB565_LE;
        return true;
#if !defined(ARDUINO_ARCH_STM32)  // the STM32 backend only has RGB565/JPEG
      case PIXFORMAT_YUV422:
        out = PixelFormat::YUY2;
        return true;
      case PIXFORMAT_GRAYSCALE:
        out = PixelFormat::GRAY8;
        return true;
#endif
#if (defined(ESP32) || defined(TINY_CAMERA_DESKTOP))
      case PIXFORMAT_RGB888:
        out = PixelFormat::RGB888;
        return true;
#endif
      default:
        return false;
    }
  }
};

}  // namespace tinyuvc
