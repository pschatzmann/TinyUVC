#pragma once
/**
 * @file UVCHostSimulator.h
 * @brief Desktop only: plays the USB host (and the TinyUSB core) for a
 * UVCDevice, so the complete device side - descriptors, class driver,
 * probe/commit and payload streaming - can be run and tested on a PC.
 *
 * The host side follows the Linux uvcvideo driver: the bulk endpoint is
 * read with URBs of at most 32 packets, a payload ends with a short packet
 * (or zero length packet) or when dwMaxPayloadTransferSize bytes were
 * received, frames are delimited by the FID/EOF header bits, payloads that
 * are out of sync are dropped and uncompressed frames of the wrong size are
 * flagged as errors.
 */
#include <functional>
#include <string>
#include <vector>

#include "../UVCDevice.h"
#include "TinyUSBSim.h"

namespace tinyuvc {

/// @brief Desktop only: a simulated USB host (modeled on Linux' uvcvideo)
/// that enumerates, negotiates and streams from a UVCDevice
class UVCHostSimulator {
 public:
  /// A frame received from the device
  struct Frame {
    const uint8_t *data = nullptr;
    size_t size = 0;
    UVCStreamSettings settings;
    /// true if the frame is corrupt (Linux would drop it)
    bool error = false;
    bool hasPts = false;
    uint32_t pts = 0;
    /// running number of received frames (starting at 0)
    uint32_t index = 0;
  };

  /// Frame descriptor as parsed by the host
  struct HostFrame {
    uint8_t index = 0;
    uint16_t width = 0, height = 0;
    uint32_t maxFrameSize = 0;
    uint32_t defaultInterval = 0;
    std::vector<uint32_t> intervals;
  };

  /// Format descriptor as parsed by the host
  struct HostFormat {
    uint8_t index = 0;
    UVCFormat format = UVCFormat::MJPEG;
    std::vector<HostFrame> frames;
  };

  /// @param withCdc puts a CDC serial function in front of the camera, like
  /// on a board that also uses USB Serial (tests interface numbering)
  explicit UVCHostSimulator(bool withCdc = true) : withCdc_(withCdc) {}
  ~UVCHostSimulator() { disconnect(); }

  /// Enumerates the device: builds and validates the configuration
  /// descriptor and lets the class driver open its interfaces
  bool connect(UVCDevice &dev) {
    error_.clear();
    dev_ = &dev;
    if (!dev.isActive()) return fail("device not started: call begin() first");
    uint8_t count = 0;
    driver_ = usbd_app_driver_get_cb(&count);
    if (driver_ == nullptr || count < 1) return fail("no application driver");
    driver_->init();
    simBus().resetEndpoint();
    if (!buildConfiguration(dev)) return false;
    if (!parseConfiguration()) return false;
    if (!openDrivers()) return false;
    simBus().pump = [](void *ref) { ((UVCHostSimulator *)ref)->poll(); };
    simBus().pumpRef = this;
    connected_ = true;
    return true;
  }

  /// Simulates unplugging (bus reset)
  void disconnect() {
    if (!connected_) return;
    if (driver_ && driver_->reset) driver_->reset(0);
    simBus().resetEndpoint();
    simBus().pump = nullptr;
    connected_ = false;
    streaming_ = false;
  }

  bool isConnected() const { return connected_; }

  /// Formats/resolutions as parsed from the descriptors
  const std::vector<HostFormat> &formats() const { return formats_; }

  /// The complete configuration descriptor
  const std::vector<uint8_t> &configDescriptor() const { return config_; }

  /// Last error message
  const std::string &error() const { return error_; }

  /// Streams format/resolution/frame rate (fps 0 = default rate)
  bool startStream(UVCFormat fmt, uint16_t width, uint16_t height,
                   uint32_t fps = 0) {
    for (auto &f : formats_) {
      if (f.format != fmt) continue;
      for (auto &fr : f.frames)
        if (fr.width == width && fr.height == height)
          return startStream(f.index, fr.index, fps ? fpsToInterval(fps) : 0);
    }
    return fail("format/resolution not offered by the device");
  }

  /// Negotiates like Linux: GET_DEF, SET_CUR/GET_CUR probe, SET_CUR commit
  bool startStream(uint8_t formatIndex, uint8_t frameIndex, uint32_t interval) {
    if (!connected_) return fail("not connected");
    UVCProbeCommit pc;
    if (!getProbe(uvc_spec::GET_DEF, false, pc)) return fail("GET_DEF probe failed");
    pc.setHint(1);
    pc.setFormatIndex(formatIndex);
    pc.setFrameIndex(frameIndex);
    pc.setFrameInterval(interval);
    if (!setProbe(false, pc)) return fail("SET_CUR probe failed");
    UVCProbeCommit cur;
    if (!getProbe(uvc_spec::GET_CUR, false, cur)) return fail("GET_CUR probe failed");
    if (cur.formatIndex() != formatIndex || cur.frameIndex() != frameIndex)
      return fail("device changed the format/frame in the probe");
    if (cur.maxPayloadTransferSize() == 0) return fail("dwMaxPayloadTransferSize is 0");
    if (!setProbe(true, cur)) return fail("SET_CUR commit failed");
    const HostFormat *f = findFormat(formatIndex);
    const HostFrame *fr = f ? findFrame(*f, frameIndex) : nullptr;
    if (fr == nullptr) return fail("invalid format/frame index");
    settings_.format = f->format;
    settings_.width = fr->width;
    settings_.height = fr->height;
    settings_.interval = cur.frameInterval();
    settings_.formatIndex = formatIndex;
    settings_.frameIndex = frameIndex;
    maxVideoFrameSize_ = cur.maxVideoFrameSize();
    maxPayload_ = cur.maxPayloadTransferSize();
    uint32_t packets = (maxPayload_ + mps_ - 1) / mps_;
    if (packets > 32) packets = 32;  // UVC_MAX_PACKETS of uvcvideo
    urbSize_ = packets * mps_;
    urb_.clear();
    resetPayload();
    frameActive_ = false;
    lastFid_ = -1;
    streaming_ = true;
    return true;
  }

  /// Stops streaming like Linux (clear halt of the bulk endpoint) or with
  /// SET_INTERFACE(0) (useSetInterface)
  bool stopStream(bool useSetInterface = false) {
    if (!connected_) return false;
    streaming_ = false;
    UVCRequest r;
    if (useSetInterface) {
      r.bmRequestType = 0x01;  // standard, interface
      r.bRequest = 0x0B;       // SET_INTERFACE
      r.wIndex = dev_->streamingInterface();
    } else {
      r.bmRequestType = 0x02;  // standard, endpoint
      r.bRequest = 0x01;       // CLEAR_FEATURE
      r.wValue = 0;            // ENDPOINT_HALT
      r.wIndex = ep_;
      // the host side aborts its URBs: a pending transfer stays queued on
      // the device until the next read
    }
    return control(r, nullptr, 0, nullptr);
  }

  /// Reads the pending IN transfer (if any) like the host controller and
  /// returns true if there was one
  bool poll() {
    UVCSimBus &b = simBus();
    if (!connected_ || !b.busy || !streaming_) return false;
    uint8_t *buf = b.xferBuf;
    uint16_t len = b.xferLen;
    transfers_++;
    if (len == 0) {
      zlps_++;
      packet(nullptr, 0);
    } else {
      for (uint16_t off = 0; off < len; off += mps_) {
        uint16_t n = (uint16_t)(len - off < mps_ ? len - off : mps_);
        packet(buf + off, n);
      }
    }
    b.busy = false;
    b.claimed = false;
    b.xferBuf = nullptr;
    driver_->xfer_cb(0, ep_, XFER_RESULT_SUCCESS, len);
    return true;
  }

  /// Polls until no transfer is pending (or max transfers)
  size_t pollAll(size_t maxTransfers = 1000000) {
    size_t n = 0;
    while (n < maxTransfers && poll()) n++;
    return n;
  }

  /// Callback for every received frame
  void onFrame(std::function<void(const Frame &)> cb) { onFrame_ = cb; }

  uint32_t framesReceived() const { return frames_; }
  uint32_t framesWithError() const { return errorFrames_; }
  uint32_t payloads() const { return payloads_; }
  uint32_t droppedPayloads() const { return droppedPayloads_; }
  uint32_t transfers() const { return transfers_; }
  uint32_t zeroLengthPackets() const { return zlps_; }
  uint16_t maxPacketSize() const { return mps_; }
  uint32_t urbSize() const { return urbSize_; }
  bool isStreaming() const { return streaming_; }

  // ------------------------------------------------- control transfers

  /// Runs a control request through the class driver like the TinyUSB
  /// core: setup stage, data stage, status stage. Returns false on stall.
  bool control(const UVCRequest &r, const uint8_t *out, uint16_t outLen,
               std::vector<uint8_t> *in) {
    UVCSimBus &b = simBus();
    b.resetControl();
    tusb_control_request_t req;
    req.bmRequestType = r.bmRequestType;
    req.bRequest = r.bRequest;
    req.wValue = r.wValue;
    req.wIndex = r.wIndex;
    req.wLength = r.isIn() ? r.wLength : outLen;
    bool ok = driver_->control_xfer_cb(0, CONTROL_STAGE_SETUP, &req);
    if (!ok) {
      // the TinyUSB core answers GET/SET_INTERFACE itself if the driver
      // does not
      return r.type() == UVCRequest::Standard &&
             r.recipient() == UVCRequest::Interface &&
             (r.bRequest == 0x0A || r.bRequest == 0x0B);
    }
    if (b.ctrlXfer) {
      if (r.isIn()) {
        uint16_t n = b.ctrlLen < r.wLength ? b.ctrlLen : r.wLength;
        if (in) in->assign(b.ctrlBuf, b.ctrlBuf + n);
      } else {
        if (b.ctrlLen < outLen) return false;
        memcpy(b.ctrlBuf, out, outLen);
      }
      if (req.wLength > 0 &&
          !driver_->control_xfer_cb(0, CONTROL_STAGE_DATA, &req))
        return false;
    }
    driver_->control_xfer_cb(0, CONTROL_STAGE_ACK, &req);
    return true;
  }

  /// Class request GET_xxx of the probe (or commit) control
  bool getProbe(uint8_t request, bool commit, UVCProbeCommit &pc) {
    std::vector<uint8_t> data;
    UVCRequest r;
    r.bmRequestType = 0xA1;  // in, class, interface
    r.bRequest = request;
    r.wValue = (uint16_t)((commit ? uvc_spec::VS_COMMIT_CONTROL : uvc_spec::VS_PROBE_CONTROL) << 8);
    r.wIndex = dev_->streamingInterface();
    r.wLength = uvc_spec::PROBE_COMMIT_SIZE;
    if (!control(r, nullptr, 0, &data)) return false;
    if (data.size() != uvc_spec::PROBE_COMMIT_SIZE) return fail("short probe reply");
    memcpy(pc.raw, data.data(), data.size());
    return true;
  }

  /// Class request SET_CUR of the probe (or commit) control
  bool setProbe(bool commit, const UVCProbeCommit &pc) {
    UVCRequest r;
    r.bmRequestType = 0x21;  // out, class, interface
    r.bRequest = uvc_spec::SET_CUR;
    r.wValue = (uint16_t)((commit ? uvc_spec::VS_COMMIT_CONTROL : uvc_spec::VS_PROBE_CONTROL) << 8);
    r.wIndex = dev_->streamingInterface();
    return control(r, pc.raw, uvc_spec::PROBE_COMMIT_SIZE, nullptr);
  }

  /// VC_REQUEST_ERROR_CODE_CONTROL (0xFF if the request failed)
  uint8_t requestErrorCode() {
    std::vector<uint8_t> data;
    UVCRequest r;
    r.bmRequestType = 0xA1;
    r.bRequest = uvc_spec::GET_CUR;
    r.wValue = (uint16_t)(uvc_spec::VC_REQUEST_ERROR_CODE_CONTROL << 8);
    r.wIndex = dev_->controlInterface();
    r.wLength = 1;
    if (!control(r, nullptr, 0, &data) || data.size() != 1) return 0xFF;
    return data[0];
  }

 protected:
  bool withCdc_;
  UVCDevice *dev_ = nullptr;
  usbd_class_driver_t const *driver_ = nullptr;
  bool connected_ = false;
  bool streaming_ = false;
  std::string error_;
  std::vector<uint8_t> config_;
  std::vector<HostFormat> formats_;
  uint8_t ep_ = 0;
  uint16_t mps_ = 64;
  uint8_t vcItf_ = 0;

  UVCStreamSettings settings_;
  uint32_t maxVideoFrameSize_ = 0;
  uint32_t maxPayload_ = 0;
  uint32_t urbSize_ = 0;
  std::vector<uint8_t> urb_;
  // payload state (uvcvideo's stream->bulk)
  uint32_t payloadSize_ = 0;
  uint8_t headerSize_ = 0;
  uint8_t header_[256] = {};
  bool skipPayload_ = false;
  // frame state
  int lastFid_ = -1;
  bool frameActive_ = false;
  bool frameError_ = false;
  std::vector<uint8_t> frame_;
  bool framePts_ = false;
  uint32_t framePtsValue_ = 0;

  std::function<void(const Frame &)> onFrame_;
  uint32_t frames_ = 0, errorFrames_ = 0, payloads_ = 0, droppedPayloads_ = 0;
  uint32_t transfers_ = 0, zlps_ = 0;

  bool fail(const std::string &msg) {
    error_ = msg;
    return false;
  }

  const HostFormat *findFormat(uint8_t index) const {
    for (auto &f : formats_)
      if (f.index == index) return &f;
    return nullptr;
  }
  static const HostFrame *findFrame(const HostFormat &f, uint8_t index) {
    for (auto &fr : f.frames)
      if (fr.index == index) return &fr;
    return nullptr;
  }

  // ------------------------------------------------ enumeration

  bool buildConfiguration(UVCDevice &dev) {
    // what a board with USB serial + camera reports
    static const uint8_t cdc[] = {
        8, 0x0B, 0, 2, 0x02, 0x02, 0x00, 0,  // IAD
        9, 0x04, 0, 0, 1, 0x02, 0x02, 0x00, 0,  // CDC control interface
        5, 0x24, 0x00, 0x20, 0x01,  // header
        5, 0x24, 0x01, 0x00, 1,     // call management
        4, 0x24, 0x02, 0x02,        // ACM
        5, 0x24, 0x06, 0, 1,        // union
        7, 0x05, 0x81, 0x03, 8, 0, 16,     // notification endpoint
        9, 0x04, 1, 0, 2, 0x0A, 0x00, 0x00, 0,  // CDC data interface
        7, 0x05, 0x02, 0x02, 64, 0, 0,  // bulk out
        7, 0x05, 0x82, 0x02, 64, 0, 0,  // bulk in
    };
    const uint8_t firstItf = withCdc_ ? 2 : 0;
    const uint8_t ep = withCdc_ ? 0x83 : 0x81;
    mps_ = platform::maxPacketSize();
    uint16_t uvcLen = dev.descriptorLength();
    config_.assign(9, 0);
    if (withCdc_) config_.insert(config_.end(), cdc, cdc + sizeof(cdc));
    size_t off = config_.size();
    config_.resize(off + uvcLen);
    uint16_t len = dev.buildDescriptor(config_.data() + off, uvcLen, firstItf,
                                       ep, mps_, 4);
    if (len != uvcLen) return fail("descriptor length mismatch");
    config_[0] = 9;
    config_[1] = 0x02;
    le::put16(&config_[2], (uint16_t)config_.size());
    config_[4] = (uint8_t)(firstItf + 2);  // bNumInterfaces
    config_[5] = 1;                         // bConfigurationValue
    config_[6] = 0;
    config_[7] = 0x80;
    config_[8] = 250;
    return true;
  }

  /// Parses and validates the configuration like a UVC host
  bool parseConfiguration() {
    formats_.clear();
    const uint8_t *d = config_.data();
    const size_t n = config_.size();
    if (le::get16(d + 2) != n) return fail("config wTotalLength wrong");
    // check that the descriptors exactly tile the configuration
    size_t pos = 0;
    int interfaces = 0;
    while (pos < n) {
      if (d[pos] < 2 || pos + d[pos] > n) return fail("bad bLength in configuration");
      if (d[pos + 1] == uvc_spec::DESC_INTERFACE && d[pos + 3] == 0) interfaces++;
      pos += d[pos];
    }
    if (interfaces != d[4]) return fail("bNumInterfaces wrong");

    // find our IAD
    pos = 9;
    size_t iad = 0;
    while (pos < n) {
      if (d[pos + 1] == uvc_spec::DESC_IAD && d[pos + 4] == uvc_spec::CC_VIDEO) {
        iad = pos;
        break;
      }
      pos += d[pos];
    }
    if (!iad) return fail("no video interface association");
    if (d[iad + 5] != uvc_spec::SC_VIDEO_INTERFACE_COLLECTION || d[iad + 3] != 2)
      return fail("bad video IAD");
    vcItf_ = d[iad + 2];
    pos = iad + d[iad];
    // VideoControl interface
    if (d[pos + 1] != uvc_spec::DESC_INTERFACE || d[pos + 2] != vcItf_ ||
        d[pos + 5] != uvc_spec::CC_VIDEO || d[pos + 6] != uvc_spec::SC_VIDEOCONTROL)
      return fail("IAD not followed by VideoControl interface");
    pos += d[pos];
    if (d[pos + 1] != uvc_spec::CS_INTERFACE || d[pos + 2] != uvc_spec::VC_HEADER)
      return fail("no VC header");
    if (le::get16(d + pos + 3) != uvc_spec::BCD_UVC) return fail("bcdUVC is not 1.10");
    const uint16_t vcTotal = le::get16(d + pos + 5);
    const uint8_t vsItf = d[pos + 12];
    if (d[pos + 11] != 1 || vsItf != vcItf_ + 1) return fail("VC header baInterfaceNr wrong");
    size_t vcEnd = pos;
    while (vcEnd < n && d[vcEnd + 1] == uvc_spec::CS_INTERFACE) vcEnd += d[vcEnd];
    if (vcEnd - pos != vcTotal) return fail("VC header wTotalLength wrong");
    pos = vcEnd;
    // VideoStreaming interface
    if (d[pos + 1] != uvc_spec::DESC_INTERFACE || d[pos + 2] != vsItf ||
        d[pos + 6] != uvc_spec::SC_VIDEOSTREAMING || d[pos + 4] != 1)
      return fail("no VideoStreaming interface with one endpoint");
    pos += d[pos];
    if (d[pos + 1] != uvc_spec::CS_INTERFACE || d[pos + 2] != uvc_spec::VS_INPUT_HEADER)
      return fail("no VS input header");
    const uint8_t numFormats = d[pos + 3];
    const uint16_t vsTotal = le::get16(d + pos + 4);
    ep_ = d[pos + 6];
    if (d[pos + 8] != uvc_spec::ID_OUTPUT_TERMINAL) return fail("bTerminalLink wrong");
    if (d[pos] != 13 + numFormats * d[pos + 12]) return fail("VS header bLength wrong");
    size_t vsStart = pos, vsEnd = pos;
    while (vsEnd < n && d[vsEnd + 1] == uvc_spec::CS_INTERFACE) vsEnd += d[vsEnd];
    if (vsEnd - vsStart != vsTotal) return fail("VS header wTotalLength wrong");
    // formats and frames
    pos += d[pos];
    HostFormat *cur = nullptr;
    int framesLeft = 0;
    while (pos < vsEnd) {
      const uint8_t sub = d[pos + 2];
      if (sub == uvc_spec::VS_FORMAT_MJPEG || sub == uvc_spec::VS_FORMAT_UNCOMPRESSED) {
        if (framesLeft != 0) return fail("bNumFrameDescriptors wrong");
        HostFormat f;
        f.index = d[pos + 3];
        f.format = sub == uvc_spec::VS_FORMAT_MJPEG ? UVCFormat::MJPEG : UVCFormat::YUY2;
        if (f.index != formats_.size() + 1) return fail("bFormatIndex not sequential");
        if (sub == uvc_spec::VS_FORMAT_UNCOMPRESSED &&
            (d[pos] != 27 || memcmp(d + pos + 5, "YUY2", 4) != 0 || d[pos + 21] != 16))
          return fail("bad YUY2 format descriptor");
        if (sub == uvc_spec::VS_FORMAT_MJPEG && d[pos] != 11) return fail("bad MJPEG format descriptor");
        framesLeft = d[pos + 4];
        formats_.push_back(f);
        cur = &formats_.back();
      } else if (sub == uvc_spec::VS_FRAME_MJPEG || sub == uvc_spec::VS_FRAME_UNCOMPRESSED) {
        if (cur == nullptr || framesLeft <= 0) return fail("unexpected frame descriptor");
        if ((sub == uvc_spec::VS_FRAME_MJPEG) != (cur->format == UVCFormat::MJPEG))
          return fail("frame descriptor type does not match format");
        HostFrame fr;
        fr.index = d[pos + 3];
        fr.width = le::get16(d + pos + 5);
        fr.height = le::get16(d + pos + 7);
        fr.maxFrameSize = le::get32(d + pos + 17);
        fr.defaultInterval = le::get32(d + pos + 21);
        const uint8_t nInt = d[pos + 25];
        if (nInt == 0 || d[pos] != 26 + 4 * nInt) return fail("bad frame descriptor length");
        for (int i = 0; i < nInt; i++) fr.intervals.push_back(le::get32(d + pos + 26 + 4 * i));
        bool hasDefault = false;
        for (auto iv : fr.intervals) hasDefault |= iv == fr.defaultInterval;
        if (!hasDefault) return fail("default interval not in the list");
        if (fr.index != cur->frames.size() + 1) return fail("bFrameIndex not sequential");
        if (cur->format == UVCFormat::YUY2 &&
            fr.maxFrameSize != (uint32_t)fr.width * fr.height * 2)
          return fail("YUY2 dwMaxVideoFrameBufferSize wrong");
        cur->frames.push_back(fr);
        framesLeft--;
      } else if (sub != uvc_spec::VS_COLORFORMAT) {
        return fail("unknown VS descriptor");
      }
      pos += d[pos];
    }
    if (framesLeft != 0) return fail("bNumFrameDescriptors wrong");
    if (formats_.size() != numFormats) return fail("bNumFormats wrong");
    // bulk endpoint
    if (pos >= n || d[pos + 1] != uvc_spec::DESC_ENDPOINT || d[pos + 2] != ep_ ||
        (d[pos + 3] & 3) != 2)
      return fail("no bulk endpoint after the VS descriptors");
    if (le::get16(d + pos + 4) != mps_) return fail("wMaxPacketSize wrong");
    return true;
  }

  /// Like process_set_config() of TinyUSB: offers each interface to the
  /// application driver; interfaces it does not claim belong to others
  bool openDrivers() {
    const uint8_t *d = config_.data();
    const size_t n = config_.size();
    size_t pos = 9;
    bool opened = false;
    while (pos < n) {
      if (d[pos + 1] == uvc_spec::DESC_INTERFACE) {
        uint16_t len = driver_->open(0, (const tusb_desc_interface_t *)(d + pos),
                                     (uint16_t)(n - pos));
        if (len > 0) {
          if (d[pos + 2] != vcItf_) return fail("driver claimed a foreign interface");
          opened = true;
          pos += len;
          continue;
        }
      }
      pos += d[pos];
    }
    if (!opened) return fail("the class driver did not open the video function");
    if (simBus().openEndpoint != ep_) return fail("bulk endpoint not opened");
    return true;
  }

  // ------------------------------------------------ streaming

  void resetPayload() {
    payloadSize_ = 0;
    headerSize_ = 0;
    skipPayload_ = false;
  }

  /// One USB packet arrives at the host controller
  void packet(const uint8_t *data, uint16_t len) {
    if (len) urb_.insert(urb_.end(), data, data + len);
    if (len < mps_ || urb_.size() >= urbSize_) {
      urbComplete(urb_.data(), urb_.size(), urb_.size() < urbSize_);
      urb_.clear();
    }
  }

  /// uvc_video_decode_bulk()
  void urbComplete(const uint8_t *mem, size_t len, bool shortUrb) {
    if (len == 0 && headerSize_ == 0) return;  // ZLP between payloads
    payloadSize_ += (uint32_t)len;
    if (headerSize_ == 0 && !skipPayload_) {
      int ret = decodeStart(mem, len);
      if (ret < 0) {
        skipPayload_ = true;
      } else {
        headerSize_ = (uint8_t)ret;
        memcpy(header_, mem, ret);
        mem += ret;
        len -= ret;
      }
    }
    if (!skipPayload_ && frameActive_) decodeData(mem, len);
    if (shortUrb || payloadSize_ >= maxPayload_) {
      if (!skipPayload_ && headerSize_ > 0) decodeEnd(header_);
      resetPayload();
    }
  }

  /// uvc_video_decode_start(): returns the header length or -1 (skip)
  int decodeStart(const uint8_t *data, size_t len) {
    if (len < 2 || data[0] < 2 || data[0] > len) {
      droppedPayloads_++;
      return -1;
    }
    payloads_++;
    const uint8_t flags = data[1];
    const int fid = flags & uvc_spec::HDR_FID;
    if (flags & uvc_spec::HDR_ERR) frameError_ = true;
    // FID toggled while a frame was active: the previous one is complete
    if (frameActive_ && fid != lastFid_ && !frame_.empty()) completeFrame();
    if (!frameActive_) {
      if (fid == lastFid_) {
        droppedPayloads_++;  // out of sync, wait for the next frame
        return -1;
      }
      frameActive_ = true;
      frameError_ = (flags & uvc_spec::HDR_ERR) != 0;
      frame_.clear();
      framePts_ = (flags & uvc_spec::HDR_PTS) && data[0] >= 6;
      framePtsValue_ = framePts_ ? le::get32(data + 2) : 0;
    }
    lastFid_ = fid;
    return data[0];
  }

  /// uvc_video_decode_data()
  void decodeData(const uint8_t *mem, size_t len) {
    if (len == 0) return;
    if (frame_.size() + len > maxVideoFrameSize_) {
      frameError_ = true;  // overflow: data is dropped
      len = maxVideoFrameSize_ - frame_.size();
    }
    frame_.insert(frame_.end(), mem, mem + len);
  }

  /// uvc_video_decode_end()
  void decodeEnd(const uint8_t *header) {
    if ((header[1] & uvc_spec::HDR_EOF) && frameActive_ && !frame_.empty()) completeFrame();
  }

  void completeFrame() {
    // uvc_video_validate_buffer(): uncompressed frames must be complete
    if (settings_.format == UVCFormat::YUY2 && frame_.size() != maxVideoFrameSize_)
      frameError_ = true;
    Frame f;
    f.data = frame_.data();
    f.size = frame_.size();
    f.settings = settings_;
    f.error = frameError_;
    f.hasPts = framePts_;
    f.pts = framePtsValue_;
    f.index = frames_;
    frames_++;
    if (frameError_) errorFrames_++;
    if (onFrame_) onFrame_(f);
    frameActive_ = false;
    frameError_ = false;
    frame_.clear();
  }
};

}  // namespace tinyuvc
