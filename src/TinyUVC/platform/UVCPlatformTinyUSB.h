#pragma once
/**
 * @file UVCPlatformTinyUSB.h
 * @brief The generic TinyUSB platform layer, for every board that uses the
 * Adafruit TinyUSB Library (Adafruit_USBD_Device): RP2040/RP2350 with
 * Tools > USB Stack > "Adafruit TinyUSB", STM32 (OTG_FS, e.g. F4/F7/H7),
 * SAMD, nRF52 - and optionally the ESP32 (TINYUVC_USE_ADAFRUIT_TINYUSB).
 * The camera is added as an additional interface next to the other
 * interfaces of the device (e.g. the USB serial port).
 */
#include <Arduino.h>
#include <Adafruit_TinyUSB.h>
#include <stdlib.h>

#include "../UVCDevice.h"

namespace tinyuvc {
namespace platform {

inline uint16_t maxPacketSize() { return TUD_OPT_HIGH_SPEED ? 512 : 64; }

/// The camera as interface of Adafruit_USBD_Device
class UVCTinyUSBInterface : public Adafruit_USBD_Interface {
 public:
  uint16_t getInterfaceDescriptor(uint8_t itfnum_deprecated, uint8_t *buf,
                                  uint16_t bufsize) override {
    (void)itfnum_deprecated;
    UVCDevice *dev = UVCDevice::instance();
    if (dev == nullptr) return 0;
    const uint16_t len = dev->descriptorLength();
    if (buf == nullptr) return len;  // length query
    if (bufsize < len) {
      TINYUVC_LOGE("configuration buffer too small (TINYUVC_CONFIG_BUFFER_SIZE)");
      return 0;
    }
    const uint8_t itf = TinyUSBDevice.allocInterface(2);
    const uint8_t ep = TinyUSBDevice.allocEndpoint(TUSB_DIR_IN);
    return dev->buildDescriptor(buf, bufsize, itf, ep, maxPacketSize(), _strid);
  }
};

inline UVCTinyUSBInterface &tinyusbInterface() {
  static UVCTinyUSBInterface itf;
  return itf;
}

inline bool begin(UVCDevice &dev) {
  // cores without built in TinyUSB support (e.g. STM32) do not start it
  if (!TinyUSBDevice.isInitialized()) TinyUSBDevice.begin(0);
  // the default 256 byte buffer is too small for UVC + CDC
  static uint8_t config[TINYUVC_CONFIG_BUFFER_SIZE];
  TinyUSBDevice.setConfigurationBuffer(config, sizeof(config));
  UVCTinyUSBInterface &itf = tinyusbInterface();
  itf.setStringDescriptor(dev.name());
  if (!TinyUSBDevice.addInterface(itf)) return false;
  // if the host already enumerated the device it does not know the camera:
  // re-enumerate
  if (TinyUSBDevice.mounted()) {
    TinyUSBDevice.detach();
    delay(10);
    TinyUSBDevice.attach();
  }
  return true;
}

inline void end(UVCDevice &) {}  // interfaces can not be removed

inline void idle() {
  yield();  // runs tud_task() on STM32
  delay(1);
}
inline uint32_t millis() { return ::millis(); }
inline uint32_t micros() { return ::micros(); }

inline uint8_t *allocBuffer(size_t size) {
  // 4 byte alignment is required by the USB DMA of the supported MCUs
  void *p = malloc((size + 3) & ~(size_t)3);
  return (uint8_t *)p;
}
inline void freeBuffer(uint8_t *buf) { free(buf); }

inline AppLock::AppLock() {}
inline AppLock::~AppLock() {}

}  // namespace platform
}  // namespace tinyuvc
