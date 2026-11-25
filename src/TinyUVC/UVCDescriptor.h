#pragma once
/**
 * @file UVCDescriptor.h
 * @brief Builds the USB descriptors of a UVC 1.1 camera function: an
 * Interface Association, a VideoControl interface (camera terminal ->
 * output terminal) and a VideoStreaming interface with one bulk IN
 * endpoint, offering MJPEG and/or YUY2 in several resolutions.
 */
#include "UVCDefs.h"

namespace tinyuvc {

/// Input of UVCDescriptorBuilder::build()
struct UVCDescriptorParams {
  /// Interface number of the VideoControl interface. The VideoStreaming
  /// interface is controlInterface + 1.
  uint8_t controlInterface = 0;
  /// Address of the bulk IN endpoint (with bit 7 set)
  uint8_t endpointIn = 0x81;
  /// Max packet size of the bulk endpoint (64 full speed, 512 high speed)
  uint16_t maxPacketSize = 64;
  /// String index for the function and interface names (0 = none)
  uint8_t stringIndex = 0;
  /// The formats to offer
  const UVCFormatSpec *formats = nullptr;
  uint8_t formatCount = 0;
};

/**
 * @brief Writes the UVC function descriptors (everything that belongs into
 * the configuration descriptor after its 9 byte header).
 */
class UVCDescriptorBuilder {
 public:
  /// Number of interfaces the function uses
  static constexpr uint8_t kInterfaceCount = 2;

  /// Length in bytes of the descriptors for the given parameters
  static uint16_t length(const UVCDescriptorParams &p) {
    return build(p, nullptr, 0);
  }

  /// Writes the descriptors into buf (if not null) and returns their length;
  /// returns 0 if the buffer is too small.
  static uint16_t build(const UVCDescriptorParams &p, uint8_t *buf,
                        uint16_t bufLen) {
    Writer w(buf, bufLen);
    const uint8_t itfVC = p.controlInterface;
    const uint8_t itfVS = (uint8_t)(itfVC + 1);

    // --- Interface Association Descriptor
    w.u8(8);
    w.u8(uvc_spec::DESC_IAD);
    w.u8(itfVC);
    w.u8(kInterfaceCount);
    w.u8(uvc_spec::CC_VIDEO);
    w.u8(uvc_spec::SC_VIDEO_INTERFACE_COLLECTION);
    w.u8(0);  // protocol undefined
    w.u8(p.stringIndex);

    // --- Standard VideoControl interface
    interfaceDesc(w, itfVC, 0, uvc_spec::SC_VIDEOCONTROL, p.stringIndex);

    // --- Class specific VideoControl interface
    const uint16_t vcHeaderLen = 13;  // with one baInterfaceNr
    const uint16_t vcTotal = vcHeaderLen + kCameraTerminalLen + kOutputTerminalLen;
    w.u8(vcHeaderLen);
    w.u8(uvc_spec::CS_INTERFACE);
    w.u8(uvc_spec::VC_HEADER);
    w.u16(uvc_spec::BCD_UVC);
    w.u16(vcTotal);
    w.u32(kClockFrequency);
    w.u8(1);      // bInCollection
    w.u8(itfVS);  // baInterfaceNr(1)

    // Camera (input) terminal: no controls
    w.u8(kCameraTerminalLen);
    w.u8(uvc_spec::CS_INTERFACE);
    w.u8(uvc_spec::VC_INPUT_TERMINAL);
    w.u8(uvc_spec::ID_CAMERA_TERMINAL);
    w.u16(uvc_spec::ITT_CAMERA);
    w.u8(0);   // bAssocTerminal
    w.u8(0);   // iTerminal
    w.u16(0);  // wObjectiveFocalLengthMin
    w.u16(0);  // wObjectiveFocalLengthMax
    w.u16(0);  // wOcularFocalLength
    w.u8(3);   // bControlSize
    w.u8(0);
    w.u8(0);
    w.u8(0);  // bmControls

    // Output terminal (USB streaming), fed by the camera terminal
    w.u8(kOutputTerminalLen);
    w.u8(uvc_spec::CS_INTERFACE);
    w.u8(uvc_spec::VC_OUTPUT_TERMINAL);
    w.u8(uvc_spec::ID_OUTPUT_TERMINAL);
    w.u16(uvc_spec::TT_STREAMING);
    w.u8(0);  // bAssocTerminal
    w.u8(uvc_spec::ID_CAMERA_TERMINAL);
    w.u8(0);  // iTerminal

    // --- Standard VideoStreaming interface: alt 0 with the bulk endpoint
    interfaceDesc(w, itfVS, 1, uvc_spec::SC_VIDEOSTREAMING, 0);

    // --- Class specific VideoStreaming interface
    const uint8_t nFormats = p.formatCount;
    const uint16_t vsHeaderLen = (uint16_t)(13 + nFormats);
    uint16_t vsTotal = vsHeaderLen;
    for (int f = 0; f < nFormats; f++) vsTotal += formatBlockLength(p.formats[f]);
    w.u8((uint8_t)vsHeaderLen);
    w.u8(uvc_spec::CS_INTERFACE);
    w.u8(uvc_spec::VS_INPUT_HEADER);
    w.u8(nFormats);
    w.u16(vsTotal);
    w.u8(p.endpointIn);
    w.u8(0);  // bmInfo: no dynamic format change
    w.u8(uvc_spec::ID_OUTPUT_TERMINAL);
    w.u8(0);  // bStillCaptureMethod
    w.u8(0);  // bTriggerSupport
    w.u8(0);  // bTriggerUsage
    w.u8(1);  // bControlSize
    for (int f = 0; f < nFormats; f++) w.u8(0);  // bmaControls

    for (int f = 0; f < nFormats; f++) formatBlock(w, p.formats[f], (uint8_t)(f + 1));

    // --- Bulk IN endpoint
    w.u8(7);
    w.u8(uvc_spec::DESC_ENDPOINT);
    w.u8(p.endpointIn);
    w.u8(0x02);  // bulk
    w.u16(p.maxPacketSize);
    w.u8(0);  // bInterval

    return w.ok() ? w.length() : 0;
  }

  /// dwClockFrequency reported in the headers: 1 MHz (PTS in microseconds)
  static constexpr uint32_t kClockFrequency = 1000000;

 private:
  static constexpr uint8_t kCameraTerminalLen = 18;
  static constexpr uint8_t kOutputTerminalLen = 9;
  static constexpr uint8_t kMjpegFormatLen = 11;
  static constexpr uint8_t kYuy2FormatLen = 27;
  static constexpr uint8_t kColorMatchingLen = 6;

  /// Bounds checked little endian writer; with a null buffer it only counts
  class Writer {
   public:
    Writer(uint8_t *buf, uint16_t cap) : buf_(buf), cap_(cap) {}
    void u8(uint8_t v) {
      if (buf_ != nullptr) {
        if (len_ < cap_)
          buf_[len_] = v;
        else
          overflow_ = true;
      }
      len_++;
    }
    void u16(uint16_t v) {
      u8((uint8_t)v);
      u8((uint8_t)(v >> 8));
    }
    void u32(uint32_t v) {
      u16((uint16_t)v);
      u16((uint16_t)(v >> 16));
    }
    uint16_t length() const { return len_; }
    bool ok() const { return !overflow_; }

   private:
    uint8_t *buf_;
    uint16_t cap_;
    uint16_t len_ = 0;
    bool overflow_ = false;
  };

  static void interfaceDesc(Writer &w, uint8_t num, uint8_t numEndpoints,
                            uint8_t subclass, uint8_t str) {
    w.u8(9);
    w.u8(uvc_spec::DESC_INTERFACE);
    w.u8(num);
    w.u8(0);  // bAlternateSetting
    w.u8(numEndpoints);
    w.u8(uvc_spec::CC_VIDEO);
    w.u8(subclass);
    w.u8(0);  // protocol: PC_PROTOCOL_UNDEFINED (UVC 1.1)
    w.u8(str);
  }

  static uint16_t frameLength(const UVCFrameSpec &fr) {
    return (uint16_t)(26 + 4 * fr.intervalCount);
  }

  static uint16_t formatBlockLength(const UVCFormatSpec &fmt) {
    uint16_t len = fmt.format == UVCFormat::MJPEG ? kMjpegFormatLen : kYuy2FormatLen;
    for (int i = 0; i < fmt.frameCount; i++) len += frameLength(fmt.frames[i]);
    return (uint16_t)(len + kColorMatchingLen);
  }

  static void formatBlock(Writer &w, const UVCFormatSpec &fmt, uint8_t index) {
    const bool mjpeg = fmt.format == UVCFormat::MJPEG;
    if (mjpeg) {
      w.u8(kMjpegFormatLen);
      w.u8(uvc_spec::CS_INTERFACE);
      w.u8(uvc_spec::VS_FORMAT_MJPEG);
      w.u8(index);
      w.u8(fmt.frameCount);
      w.u8(0);  // bmFlags: no fixed size samples
      w.u8(1);  // bDefaultFrameIndex
      w.u8(0);  // bAspectRatioX
      w.u8(0);  // bAspectRatioY
      w.u8(0);  // bmInterlaceFlags
      w.u8(0);  // bCopyProtect
    } else {
      static const uint8_t kGuidYUY2[16] = {'Y',  'U',  'Y',  '2',  0x00, 0x00,
                                            0x10, 0x00, 0x80, 0x00, 0x00, 0xAA,
                                            0x00, 0x38, 0x9B, 0x71};
      w.u8(kYuy2FormatLen);
      w.u8(uvc_spec::CS_INTERFACE);
      w.u8(uvc_spec::VS_FORMAT_UNCOMPRESSED);
      w.u8(index);
      w.u8(fmt.frameCount);
      for (uint8_t b : kGuidYUY2) w.u8(b);
      w.u8(16);  // bBitsPerPixel
      w.u8(1);   // bDefaultFrameIndex
      w.u8(0);   // bAspectRatioX
      w.u8(0);   // bAspectRatioY
      w.u8(0);   // bmInterlaceFlags
      w.u8(0);   // bCopyProtect
    }

    for (int i = 0; i < fmt.frameCount; i++) {
      const UVCFrameSpec &fr = fmt.frames[i];
      const uint32_t maxFps = intervalToFps(fr.minInterval());
      const uint32_t minFps = intervalToFps(fr.maxInterval());
      w.u8((uint8_t)frameLength(fr));
      w.u8(uvc_spec::CS_INTERFACE);
      w.u8(mjpeg ? uvc_spec::VS_FRAME_MJPEG : uvc_spec::VS_FRAME_UNCOMPRESSED);
      w.u8((uint8_t)(i + 1));  // bFrameIndex
      w.u8(0);                 // bmCapabilities
      w.u16(fr.width);
      w.u16(fr.height);
      w.u32(fr.maxFrameSize * 8 * (minFps ? minFps : 1));  // dwMinBitRate
      w.u32(fr.maxFrameSize * 8 * (maxFps ? maxFps : 1));  // dwMaxBitRate
      w.u32(fr.maxFrameSize);  // dwMaxVideoFrameBufferSize
      w.u32(fr.intervals[0]);  // dwDefaultFrameInterval
      w.u8(fr.intervalCount);  // bFrameIntervalType: discrete
      for (int j = 0; j < fr.intervalCount; j++) w.u32(fr.intervals[j]);
    }

    // Color matching: BT.709 primaries / transfer, SMPTE 170M (BT.601) matrix
    w.u8(kColorMatchingLen);
    w.u8(uvc_spec::CS_INTERFACE);
    w.u8(uvc_spec::VS_COLORFORMAT);
    w.u8(1);
    w.u8(1);
    w.u8(4);
  }
};

}  // namespace tinyuvc
