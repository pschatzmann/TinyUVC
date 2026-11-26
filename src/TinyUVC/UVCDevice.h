#pragma once
/**
 * @file UVCDevice.h
 * @brief The USB Video Class device: descriptors, probe/commit negotiation
 * and streaming of frames. Independent of the USB stack: the TinyUSB glue
 * (UVCTinyUSB.h) and the platform layer (platform/UVCPlatform*.h headers) connect it to the
 * hardware.
 */
#include <atomic>
#include <initializer_list>

#include "UVCDescriptor.h"
#include "UVCLogger.h"
#include "UVCPayload.h"

namespace tinyuvc {

class UVCDevice;

/// @brief Functions every platform layer (platform/UVCPlatform*.h) implements
namespace platform {
/// Registers the UVC function with the USB stack (and starts it if needed)
bool begin(UVCDevice &dev);
/// Unregisters the UVC function, where the USB stack supports that
void end(UVCDevice &dev);
/// Max packet size of the bulk endpoint (64 full speed, 512 high speed)
uint16_t maxPacketSize();
/// Called while waiting: lets the USB stack make progress
void idle();
uint32_t millis();
uint32_t micros();
/// Allocates the transfer buffer (DMA capable, aligned)
uint8_t *allocBuffer(size_t size);
void freeBuffer(uint8_t *buf);
/// @brief Serializes app context calls into the USB stack against the USB
/// task, for USB stacks that need that (a TINYUVC_PLATFORM_CUSTOM layer);
/// a no-op on the supported platforms.
struct AppLock {
  AppLock();
  ~AppLock();
  AppLock(const AppLock &) = delete;
  AppLock &operator=(const AppLock &) = delete;
  /// storage for the platform's lock object
  alignas(8) uint8_t state[24];
};
}  // namespace platform

/// Setup request as seen by UVCDevice (independent of the USB stack)
struct UVCRequest {
  uint8_t bmRequestType = 0;
  uint8_t bRequest = 0;
  uint16_t wValue = 0;
  uint16_t wIndex = 0;
  uint16_t wLength = 0;

  enum Type : uint8_t { Standard = 0, Class = 1, Vendor = 2 };
  enum Recipient : uint8_t { Device = 0, Interface = 1, Endpoint = 2 };
  uint8_t type() const { return (bmRequestType >> 5) & 0x03; }
  uint8_t recipient() const { return bmRequestType & 0x1F; }
  bool isIn() const { return (bmRequestType & 0x80) != 0; }
};

/// What the USB glue has to do with a setup request
enum class UVCControlResult : uint8_t {
  Stall,    ///< reject (stall)
  DataIn,   ///< send data/len to the host
  DataOut,  ///< receive wLength bytes into data, then call handleControlData()
  Status,   ///< no data stage: just acknowledge
  Handled,  ///< nothing to do (the USB stack acknowledges itself)
};

/**
 * @brief USB Video Class (UVC 1.1) device - a USB webcam.
 *
 * Declare the formats/resolutions/frame rates you can deliver, call
 * begin(), then hand over frames with writeFrame() whenever isReady():
 * @code
 * UVCDevice uvc;
 * void setup() {
 *   uvc.addResolution(UVCFormat::MJPEG, 320, 240, 15);
 *   uvc.begin();
 * }
 * void loop() {
 *   if (uvc.isReady()) uvc.writeFrame(jpeg, jpegLen, PixelFormat::MJPEG);
 * }
 * @endcode
 *
 * The frame data must stay valid until isReady() is true again (it is
 * copied payload by payload into the transfer buffer while it is sent).
 * Frames are sent over a bulk endpoint, which all major operating systems
 * support (Linux, Windows, macOS).
 *
 * Only one UVCDevice can be active.
 */
class UVCDevice {
 public:
  UVCDevice() = default;
  ~UVCDevice() { end(); }
  UVCDevice(const UVCDevice &) = delete;
  UVCDevice &operator=(const UVCDevice &) = delete;

  // ------------------------------------------------------------ setup

  /// Adds a resolution with one frame rate. The format is created on first
  /// use; the first resolution of a format is the host's default.
  /// maxFrameSize: max bytes of one frame (0 = width * height * 2).
  bool addResolution(UVCFormat format, uint16_t width, uint16_t height,
                     uint8_t fps, uint32_t maxFrameSize = 0) {
    return addResolution(format, width, height, {fps}, maxFrameSize);
  }

  /// Adds a resolution with several frame rates; the first is the default.
  bool addResolution(UVCFormat format, uint16_t width, uint16_t height,
                     std::initializer_list<uint8_t> fpsList,
                     uint32_t maxFrameSize = 0) {
    return addResolution(format, width, height, fpsList.begin(),
                         fpsList.size(), maxFrameSize);
  }

  /// Adds a resolution with fpsCount frame rates; the first is the default.
  bool addResolution(UVCFormat format, uint16_t width, uint16_t height,
                     const uint8_t *fpsList, size_t fpsCount,
                     uint32_t maxFrameSize = 0) {
    if (active_) {
      TINYUVC_LOGE("addResolution() must be called before begin()");
      return false;
    }
    bool anyFps = false;
    for (size_t i = 0; i < fpsCount; i++) anyFps |= fpsList[i] != 0;
    if (width == 0 || height == 0 || !anyFps) {
      TINYUVC_LOGE("addResolution(): invalid size or no frame rate");
      return false;
    }
    if (format == UVCFormat::YUY2 && (width & 1)) {
      TINYUVC_LOGE("addResolution(): YUY2 needs an even width");
      return false;
    }
    UVCFormatSpec *f = findFormat(format);
    if (f == nullptr) {
      if (formatCount_ >= TINYUVC_MAX_FORMATS) {
        TINYUVC_LOGE("too many formats (TINYUVC_MAX_FORMATS)");
        return false;
      }
      f = &formats_[formatCount_++];
      *f = UVCFormatSpec();
      f->format = format;
    }
    if (f->frameCount >= TINYUVC_MAX_FRAMES) {
      TINYUVC_LOGE("too many resolutions (TINYUVC_MAX_FRAMES)");
      return false;
    }
    UVCFrameSpec &fr = f->frames[f->frameCount];
    fr = UVCFrameSpec();
    fr.width = width;
    fr.height = height;
    const uint32_t raw = (uint32_t)width * height * 2;
    fr.maxFrameSize = format == UVCFormat::YUY2 ? raw
                      : maxFrameSize != 0       ? maxFrameSize
                                                : raw;
    for (size_t i = 0; i < fpsCount; i++) {
      if (fpsList[i] == 0 || fr.intervalCount >= TINYUVC_MAX_INTERVALS) continue;
      fr.intervals[fr.intervalCount++] = fpsToInterval(fpsList[i]);
    }
    f->frameCount++;
    return true;
  }

  /// Removes all formats/resolutions (before begin())
  void clearResolutions() {
    if (!active_) formatCount_ = 0;
  }

  /// Name shown by the host (function and interface string); the string
  /// must stay valid
  void setName(const char *name) { name_ = name; }
  const char *name() const { return name_; }

  /// Size of one payload transfer (see TINYUVC_PAYLOAD_SIZE); call before
  /// begin()
  void setPayloadSize(uint32_t size) { payloadSizeCfg_ = size; }
  uint32_t payloadSize() const { return payloadSize_; }

  /// Adds a presentation time stamp (PTS, microseconds) to every payload
  void setTimestamps(bool enabled) { withPts_ = enabled; }

  /// Callback invoked from the USB task when the host starts (streaming =
  /// true) or stops a stream. Keep it short.
  void setStreamCallback(void (*cb)(bool streaming,
                                    const UVCStreamSettings &settings,
                                    void *ref),
                         void *ref = nullptr) {
    streamCb_ = cb;
    streamCbRef_ = ref;
  }

  /// Registers the camera with the USB stack. See the README for the
  /// platform specific requirements (USB stack / USB mode menu settings).
  bool begin() {
    if (active_) return true;
    if (formatCount_ == 0) {
      TINYUVC_LOGE("begin(): no resolution defined - call addResolution()");
      return false;
    }
    UVCDevice *&inst = instance();
    if (inst != nullptr && inst != this) {
      TINYUVC_LOGE("begin(): only one UVCDevice can be active");
      return false;
    }
    maxPacketSize_ = platform::maxPacketSize();
    payloadSize_ = effectivePayloadSize(payloadSizeCfg_, maxPacketSize_);
    // end() keeps the buffer if a transfer was still in flight: reuse it
    if (buffer_ == nullptr || bufferSize_ < payloadSize_) {
      if (buffer_ != nullptr && !inFlight_.load()) platform::freeBuffer(buffer_);
      buffer_ = platform::allocBuffer(payloadSize_);
      bufferSize_ = buffer_ != nullptr ? payloadSize_ : 0;
    }
    if (buffer_ == nullptr) {
      TINYUVC_LOGE("begin(): could not allocate %u bytes", (unsigned)payloadSize_);
      return false;
    }
    probe_ = negotiate(UVCProbeCommit());
    commit_ = probe_;
    inst = this;
    active_ = true;
    if (!platform::begin(*this)) {
      TINYUVC_LOGE("begin(): USB registration failed");
      active_ = false;
      inst = nullptr;
      platform::freeBuffer(buffer_);
      buffer_ = nullptr;
      bufferSize_ = 0;
      return false;
    }
    TINYUVC_LOGI("UVC device started: %d format(s), payload %u, packet %u",
                 formatCount_, (unsigned)payloadSize_, (unsigned)maxPacketSize_);
    return true;
  }

  /// Stops the camera function
  void end() {
    if (!active_) return;
    stopStream();
    platform::end(*this);
    active_ = false;
    if (instance() == this) instance() = nullptr;
    // a transfer in flight still reads the buffer: then it is kept and
    // reused by the next begin()
    if (!inFlight_.load()) {
      platform::freeBuffer(buffer_);
      buffer_ = nullptr;
      bufferSize_ = 0;
    }
  }

  // ------------------------------------------------------------ streaming

  /// true if begin() succeeded
  bool isActive() const { return active_; }

  /// true while the host is streaming (between commit and stop)
  bool isStreaming() const { return streaming_.load(); }

  /// true if a new frame can be written: streaming and the previous frame
  /// was completely handed to the USB stack
  bool isReady() const { return streaming_.load() && !framePending_.load(); }

  /// true while the device still reads the data passed to writeFrame():
  /// the data must not be changed or freed before this is false
  bool isSending() const { return framePending_.load(); }

  /// Format, resolution and frame rate the host selected
  UVCStreamSettings settings() const { return settings_; }

  /// Incremented whenever the host starts or stops a stream
  uint32_t streamGeneration() const { return generation_.load(); }

  /**
   * @brief Sends a frame. Returns false (and the frame is not sent) if the
   * device is not ready or the frame does not fit the format the host
   * selected:
   * - MJPEG: fmt must be PixelFormat::MJPEG
   * - YUY2: any raw format with exactly width * height pixels; it is
   *   converted to YUY2 while it is sent
   *
   * The data must stay valid until isReady() is true again.
   */
  bool writeFrame(const uint8_t *data, size_t len, PixelFormat fmt) {
    if (!isReady()) return false;
    if (data == nullptr || len == 0) return false;
    const UVCStreamSettings s = settings_;
    if (s.format == UVCFormat::MJPEG) {
      if (fmt != PixelFormat::MJPEG) {
        TINYUVC_LOGW("writeFrame(): host expects MJPEG, got %s", toString(fmt));
        dropped_++;
        return false;
      }
      if (len > maxFrameSize_) {
        TINYUVC_LOGW("writeFrame(): JPEG of %u bytes exceeds max frame size %u",
                     (unsigned)len, (unsigned)maxFrameSize_);
        dropped_++;
        return false;
      }
    } else {
      const size_t expected = (size_t)s.width * s.height * bytesPerPixel(fmt);
      if (fmt == PixelFormat::MJPEG || len != expected) {
        TINYUVC_LOGW("writeFrame(): host expects %ux%u YUY2: got %u bytes %s",
                     s.width, s.height, (unsigned)len, toString(fmt));
        dropped_++;
        return false;
      }
    }
    frameData_ = data;
    frameLen_ = len;
    frameFmt_ = fmt;
    framePts_ = platform::micros();
    framePending_.store(true);
    platform::AppLock lock;
    kick();
    return true;
  }

  /// Waits until isReady() (true) or the timeout or end of streaming (false)
  bool waitReady(uint32_t timeoutMs) {
    uint32_t start = platform::millis();
    while (!isReady()) {
      if (!isStreaming()) return false;
      if (platform::millis() - start >= timeoutMs) return false;
      platform::idle();
    }
    return true;
  }

  /// Number of frames that were completely sent
  uint32_t framesSent() const { return sent_; }
  /// Number of frames that were rejected or aborted
  uint32_t framesDropped() const { return dropped_; }

  /// Formats as defined with addResolution()
  const UVCFormatSpec *formats() const { return formats_; }
  uint8_t formatCount() const { return formatCount_; }

  // ------------------------------------------------- USB glue interface
  // Called by UVCTinyUSB.h and the platform layers - not by sketches.

  /// The active device
  static UVCDevice *&instance() {
    static UVCDevice *dev = nullptr;
    return dev;
  }

  /// Length of the function descriptors
  uint16_t descriptorLength() const {
    UVCDescriptorParams p;
    p.formats = formats_;
    p.formatCount = formatCount_;
    return UVCDescriptorBuilder::length(p);
  }

  /// Builds the function descriptors and remembers the assigned interface
  /// number and endpoint
  uint16_t buildDescriptor(uint8_t *buf, uint16_t bufLen, uint8_t controlItf,
                           uint8_t endpointIn, uint16_t maxPacketSize,
                           uint8_t stringIndex) {
    controlItf_ = controlItf;
    endpoint_ = (uint8_t)(endpointIn | 0x80);
    UVCDescriptorParams p;
    p.controlInterface = controlItf;
    p.endpointIn = endpoint_;
    p.maxPacketSize = maxPacketSize;
    p.stringIndex = stringIndex;
    p.formats = formats_;
    p.formatCount = formatCount_;
    return UVCDescriptorBuilder::build(p, buf, bufLen);
  }

  uint8_t controlInterface() const { return controlItf_; }
  uint8_t streamingInterface() const { return (uint8_t)(controlItf_ + 1); }
  uint8_t endpoint() const { return endpoint_; }

  /// Transfer function: starts an IN transfer, returns false on error
  using XferFn = bool (*)(uint8_t ep, uint8_t *buf, uint16_t len);

  /// The host configured the device: the bulk endpoint is open
  void usbOpened(XferFn xfer, uint16_t maxPacketSize) {
    xfer_ = xfer;
    if (maxPacketSize != 0 && maxPacketSize != maxPacketSize_) {
      // e.g. a high speed device that enumerated at full speed
      maxPacketSize_ = maxPacketSize;
    }
    TINYUVC_LOGI("USB configured (interfaces %d/%d, endpoint 0x%02x)",
                 controlItf_, controlItf_ + 1, endpoint_);
  }

  /// Bus reset / unconfigured
  void usbReset() {
    stopStream();
    inFlight_.store(false);
    framePending_.store(false);
    zlpPending_ = false;
    encoder_.abort();
  }

  /// Handles the setup stage of a request to our interfaces/endpoint
  UVCControlResult handleControlSetup(const UVCRequest &r, uint8_t *&data,
                                      uint16_t &len) {
    data = ctrlBuf_;
    len = 0;
    if (r.type() == UVCRequest::Standard) return handleStandard(r, data, len);
    if (r.type() != UVCRequest::Class || r.recipient() != UVCRequest::Interface)
      return stall(uvc_spec::ERR_INVALID_REQUEST);

    const uint8_t itf = (uint8_t)(r.wIndex & 0xFF);
    const uint8_t entity = (uint8_t)(r.wIndex >> 8);
    const uint8_t selector = (uint8_t)(r.wValue >> 8);

    if (itf == controlInterface()) {
      if (entity == 0 && selector == uvc_spec::VC_REQUEST_ERROR_CODE_CONTROL) {
        if (r.bRequest == uvc_spec::GET_CUR) return reply(&errorCode_, 1, r, data, len);
        if (r.bRequest == uvc_spec::GET_INFO) {
          static const uint8_t info = 0x01;  // GET supported
          return reply(&info, 1, r, data, len);
        }
      }
      // the camera and output terminal have no controls
      return stall(entity == 0 ? uvc_spec::ERR_INVALID_CONTROL
                               : (entity <= uvc_spec::ID_OUTPUT_TERMINAL
                                      ? uvc_spec::ERR_INVALID_CONTROL
                                      : uvc_spec::ERR_INVALID_UNIT));
    }

    if (itf == streamingInterface() && entity == 0 &&
        (selector == uvc_spec::VS_PROBE_CONTROL || selector == uvc_spec::VS_COMMIT_CONTROL)) {
      const bool probe = selector == uvc_spec::VS_PROBE_CONTROL;
      switch (r.bRequest) {
        case uvc_spec::SET_CUR:
          if (r.wLength == 0) return stall(uvc_spec::ERR_INVALID_REQUEST);
          len = r.wLength < sizeof(ctrlBuf_) ? r.wLength : sizeof(ctrlBuf_);
          errorCode_ = uvc_spec::ERR_NONE;
          return UVCControlResult::DataOut;
        case uvc_spec::GET_CUR:
          return replyProbe(probe ? probe_ : commit_, r, data, len);
        case uvc_spec::GET_DEF:
          return replyProbe(negotiate(UVCProbeCommit()), r, data, len);
        case uvc_spec::GET_MIN:
          return replyProbe(limit(false), r, data, len);
        case uvc_spec::GET_MAX:
          return replyProbe(limit(true), r, data, len);
        case uvc_spec::GET_RES: {
          UVCProbeCommit res;  // all zero: no resolution steps
          return replyProbe(res, r, data, len);
        }
        case uvc_spec::GET_LEN: {
          uint8_t l[2];
          le::put16(l, uvc_spec::PROBE_COMMIT_SIZE);
          return reply(l, 2, r, data, len);
        }
        case uvc_spec::GET_INFO: {
          const uint8_t info = 0x03;  // GET and SET supported
          return reply(&info, 1, r, data, len);
        }
        default:
          return stall(uvc_spec::ERR_INVALID_REQUEST);
      }
    }
    return stall(itf == streamingInterface() ? uvc_spec::ERR_INVALID_CONTROL
                                             : uvc_spec::ERR_INVALID_UNIT);
  }

  /// Handles the data of a SET request (received into the buffer that
  /// handleControlSetup() returned); false = stall
  bool handleControlData(const UVCRequest &r, uint16_t len) {
    if (r.type() != UVCRequest::Class || r.bRequest != uvc_spec::SET_CUR) return true;
    const uint8_t itf = (uint8_t)(r.wIndex & 0xFF);
    const uint8_t selector = (uint8_t)(r.wValue >> 8);
    if (itf != streamingInterface()) return false;
    UVCProbeCommit in;
    memcpy(in.raw, ctrlBuf_,
           len < uvc_spec::PROBE_COMMIT_SIZE ? len : uvc_spec::PROBE_COMMIT_SIZE);
    if (selector == uvc_spec::VS_PROBE_CONTROL) {
      probe_ = negotiate(in);
      TINYUVC_LOGD("probe: format %d frame %d interval %u", probe_.formatIndex(),
                   probe_.frameIndex(), (unsigned)probe_.frameInterval());
      return true;
    }
    if (selector == uvc_spec::VS_COMMIT_CONTROL) {
      commit_ = negotiate(in);
      startStream(commit_);
      return true;
    }
    return false;
  }

  /// A transfer on our endpoint completed
  void usbTransferComplete(bool ok) {
    if (!ok) {
      // the frame in progress is incomplete
      if (encoder_.active()) {
        encoder_.abort();
        finishFrame(false);
      }
      zlpPending_ = false;
    }
    if (!startTransfer()) {
      inFlight_.store(false);
      kick();
    }
  }

 protected:
  UVCFormatSpec formats_[TINYUVC_MAX_FORMATS];
  uint8_t formatCount_ = 0;
  const char *name_ = TINYUVC_DEFAULT_NAME;
  uint32_t payloadSizeCfg_ = TINYUVC_PAYLOAD_SIZE;
  uint32_t payloadSize_ = 0;
  uint16_t maxPacketSize_ = 64;
  bool withPts_ = false;
  bool active_ = false;
  uint8_t *buffer_ = nullptr;
  uint32_t bufferSize_ = 0;
  XferFn xfer_ = nullptr;

  uint8_t controlItf_ = 0;
  uint8_t endpoint_ = 0x81;
  uint8_t errorCode_ = uvc_spec::ERR_NONE;
  uint8_t ctrlBuf_[48] = {};
  UVCProbeCommit probe_;
  UVCProbeCommit commit_;
  UVCStreamSettings settings_;
  uint32_t maxFrameSize_ = 0;

  void (*streamCb_)(bool, const UVCStreamSettings &, void *) = nullptr;
  void *streamCbRef_ = nullptr;

  // shared between app context and USB context
  std::atomic<bool> streaming_{false};
  std::atomic<bool> framePending_{false};
  std::atomic<bool> inFlight_{false};
  std::atomic<uint32_t> generation_{0};
  const uint8_t *volatile frameData_ = nullptr;
  volatile size_t frameLen_ = 0;
  volatile PixelFormat frameFmt_ = PixelFormat::MJPEG;
  volatile uint32_t framePts_ = 0;
  volatile uint32_t sent_ = 0;
  volatile uint32_t dropped_ = 0;

  // owned by whoever holds inFlight_
  UVCPayloadEncoder encoder_;
  uint32_t encoderGeneration_ = 0;
  bool fid_ = false;
  bool zlpPending_ = false;

  /**
   * Payload size actually used: a multiple of the max packet size, and at
   * most 32 packets. Linux (uvcvideo) reads bulk endpoints with URBs of at
   * most 32 packets: a full payload that ends inside a URB has no short
   * packet that terminates it, so the next payload would be glued to it.
   */
  static uint32_t effectivePayloadSize(uint32_t requested, uint16_t mps) {
    uint32_t size = requested;
    if (size > 32u * mps) size = 32u * mps;
    if (size < 2u * mps) size = 2u * mps;
    return size - size % mps;
  }

  UVCFormatSpec *findFormat(UVCFormat format) {
    for (int i = 0; i < formatCount_; i++)
      if (formats_[i].format == format) return &formats_[i];
    return nullptr;
  }

  UVCControlResult stall(uint8_t error) {
    errorCode_ = error;
    return UVCControlResult::Stall;
  }

  UVCControlResult reply(const void *src, uint16_t n, const UVCRequest &r,
                         uint8_t *&data, uint16_t &len) {
    if (n > sizeof(ctrlBuf_)) n = sizeof(ctrlBuf_);
    memcpy(ctrlBuf_, src, n);
    data = ctrlBuf_;
    len = n < r.wLength ? n : r.wLength;
    errorCode_ = uvc_spec::ERR_NONE;
    return UVCControlResult::DataIn;
  }

  UVCControlResult replyProbe(const UVCProbeCommit &pc, const UVCRequest &r,
                              uint8_t *&data, uint16_t &len) {
    return reply(pc.raw, uvc_spec::PROBE_COMMIT_SIZE, r, data, len);
  }

  UVCControlResult handleStandard(const UVCRequest &r, uint8_t *&data,
                                  uint16_t &len) {
    constexpr uint8_t GET_INTERFACE = 0x0A, SET_INTERFACE = 0x0B;
    if (r.recipient() == UVCRequest::Endpoint) {
      // CLEAR_FEATURE(ENDPOINT_HALT): how hosts stop a bulk stream
      constexpr uint8_t CLEAR_FEATURE = 0x01;
      if (r.bRequest == CLEAR_FEATURE && (r.wIndex & 0xFF) == endpoint_) {
        TINYUVC_LOGI("host stopped the stream (clear halt)");
        stopStream();
      }
      return UVCControlResult::Handled;
    }
    if (r.recipient() == UVCRequest::Interface) {
      if (r.bRequest == SET_INTERFACE) {
        if (r.wValue != 0) return stall(uvc_spec::ERR_INVALID_REQUEST);
        if ((r.wIndex & 0xFF) == streamingInterface() && isStreaming()) {
          TINYUVC_LOGI("host stopped the stream (set interface)");
          stopStream();
        }
        return UVCControlResult::Status;
      }
      if (r.bRequest == GET_INTERFACE) {
        const uint8_t alt = 0;
        return reply(&alt, 1, r, data, len);
      }
    }
    return UVCControlResult::Stall;
  }

  /// Validates/completes a probe or commit structure from the host
  UVCProbeCommit negotiate(const UVCProbeCommit &in) const {
    UVCProbeCommit out;
    uint8_t fi = in.formatIndex();
    if (fi < 1 || fi > formatCount_) fi = 1;
    const UVCFormatSpec &fmt = formats_[fi - 1];
    uint8_t fr = in.frameIndex();
    if (fr < 1 || fr > fmt.frameCount) fr = 1;
    const UVCFrameSpec &frame = fmt.frames[fr - 1];
    uint32_t interval = in.frameInterval() == 0
                            ? frame.intervals[0]
                            : frame.closestInterval(in.frameInterval());
    out.setHint(in.bmHint());
    out.setFormatIndex(fi);
    out.setFrameIndex(fr);
    out.setFrameInterval(interval);
    out.setMaxVideoFrameSize(frame.maxFrameSize);
    out.setMaxPayloadTransferSize(payloadSize_);
    out.setClockFrequency(UVCDescriptorBuilder::kClockFrequency);
    out.setFramingInfo(0x03);  // FID required, EOF may be present
    out.setVersions(1, 1, 1);
    return out;
  }

  /// GET_MIN / GET_MAX values
  UVCProbeCommit limit(bool max) const {
    UVCProbeCommit in;
    const uint8_t fi = max ? formatCount_ : 1;
    const UVCFormatSpec &fmt = formats_[fi - 1];
    const uint8_t fr = max ? fmt.frameCount : 1;
    in.setFormatIndex(fi);
    in.setFrameIndex(fr);
    const UVCFrameSpec &frame = fmt.frames[fr - 1];
    in.setFrameInterval(max ? frame.maxInterval() : frame.minInterval());
    return negotiate(in);
  }

  void startStream(const UVCProbeCommit &pc) {
    const UVCFormatSpec &fmt = formats_[pc.formatIndex() - 1];
    const UVCFrameSpec &fr = fmt.frames[pc.frameIndex() - 1];
    UVCStreamSettings s;
    s.format = fmt.format;
    s.width = fr.width;
    s.height = fr.height;
    s.interval = pc.frameInterval();
    s.formatIndex = pc.formatIndex();
    s.frameIndex = pc.frameIndex();
    settings_ = s;
    maxFrameSize_ = fr.maxFrameSize;
    generation_.fetch_add(1);
    streaming_.store(true);
    TINYUVC_LOGI("host started %s %ux%u @ %u fps", toString(s.format), s.width,
                 s.height, (unsigned)s.fps());
    if (streamCb_) streamCb_(true, s, streamCbRef_);
    kick();
  }

  void stopStream() {
    if (!streaming_.load()) return;
    streaming_.store(false);
    generation_.fetch_add(1);
    if (streamCb_) streamCb_(false, settings_, streamCbRef_);
    kick();  // releases a pending frame if no transfer is in flight
  }

  void finishFrame(bool complete) {
    framePending_.store(false);
    if (complete)
      sent_ = sent_ + 1;
    else
      dropped_ = dropped_ + 1;
  }

  /// Starts the next transfer if there is something to send; the caller
  /// owns inFlight_. Returns false if nothing was started.
  bool startTransfer() {
    const uint32_t gen = generation_.load();
    if (encoder_.active() && encoderGeneration_ != gen) {
      // the host restarted or stopped the stream in the middle of a frame
      encoder_.abort();
      zlpPending_ = false;
      finishFrame(false);
    }
    if (!streaming_.load()) {
      zlpPending_ = false;
      if (framePending_.load() && !encoder_.active()) finishFrame(false);
      return false;
    }
    if (xfer_ == nullptr || buffer_ == nullptr) return false;
    if (zlpPending_) {
      zlpPending_ = false;
      return xfer_(endpoint_, buffer_, 0);
    }
    if (!encoder_.active()) {
      if (!framePending_.load()) return false;
      fid_ = !fid_;
      encoder_.begin(frameData_, frameLen_, frameFmt_, fid_, withPts_, framePts_);
      encoderGeneration_ = gen;
    }
    size_t len = encoder_.fill(buffer_, payloadSize_);
    if (!encoder_.active()) {
      // last payload: the frame data is no longer needed
      finishFrame(true);
      // a transfer that ends on a packet boundary needs a zero length packet,
      // or the host keeps waiting for the rest of the payload
      zlpPending_ = len < payloadSize_ && (len % maxPacketSize_) == 0;
    }
    if (xfer_(endpoint_, buffer_, (uint16_t)len)) return true;
    TINYUVC_LOGW("transfer failed");
    zlpPending_ = false;
    if (encoder_.active()) {
      encoder_.abort();
      finishFrame(false);
    }
    return false;
  }

  /// Starts a transfer if none is in flight (callable from any context)
  void kick() {
    for (int i = 0; i < 3; i++) {
      bool expected = false;
      if (!inFlight_.compare_exchange_strong(expected, true)) return;
      if (startTransfer()) return;
      inFlight_.store(false);
      // something might have arrived while we owned inFlight_
      if (!(framePending_.load() && streaming_.load())) return;
    }
  }
};

}  // namespace tinyuvc
