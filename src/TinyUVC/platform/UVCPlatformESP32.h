#pragma once
/**
 * @file UVCPlatformESP32.h
 * @brief ESP32-S2/S3/P4 with the TinyUSB stack of the arduino-esp32 core
 * (Tools > USB Mode > "USB-OTG (TinyUSB)").
 *
 * The core builds the configuration descriptor from the interfaces enabled
 * before USB.begin(). UVCDevice::begin() enables the camera and calls
 * USB.begin(); it therefore fails if USB was already started - which the
 * core does before setup() when "USB CDC On Boot" is enabled. Use
 * "USB CDC On Boot: Disabled" (Serial is then UART0), and if you want a USB
 * serial port in addition, declare a USBCDC object (see the examples).
 */
#include <Arduino.h>
#include <USB.h>
#include <esp_heap_caps.h>

#include "../UVCDevice.h"
#include "esp32-hal-tinyusb.h"

namespace tinyuvc {
namespace platform {

inline uint16_t maxPacketSize() {
#if defined(TUD_OPT_HIGH_SPEED) && TUD_OPT_HIGH_SPEED
  return 512;
#else
  return 64;
#endif
}

/// Descriptor callback of the core (called from USB.begin())
inline uint16_t esp32LoadDescriptor(uint8_t *dst, uint8_t *itf) {
  UVCDevice *dev = UVCDevice::instance();
  if (dev == nullptr) return 0;
  const uint8_t str = tinyusb_add_string_descriptor(dev->name());
  const uint8_t ep = tinyusb_get_free_in_endpoint();
  if (ep == 0) {
    TINYUVC_LOGE("no free IN endpoint");
    return 0;
  }
  const uint16_t len = dev->descriptorLength();
  if (dev->buildDescriptor(dst, len, *itf, (uint8_t)(0x80 | ep), maxPacketSize(),
                           str) != len)
    return 0;
  *itf += 2;
  return len;
}

inline bool begin(UVCDevice &dev) {
  // the interface association needs the "miscellaneous / IAD" device class
  USB.usbClass(TUSB_CLASS_MISC);
  USB.usbSubClass(MISC_SUBCLASS_COMMON);
  USB.usbProtocol(MISC_PROTOCOL_IAD);
  if (tinyusb_enable_interface(USB_INTERFACE_CUSTOM, dev.descriptorLength(),
                               esp32LoadDescriptor) != ESP_OK) {
    TINYUVC_LOGE(
        "could not add the camera: USB was already started. Select Tools > "
        "USB CDC On Boot: Disabled and call begin() before USB.begin()");
    return false;
  }
  return USB.begin();
}

inline void end(UVCDevice &) {}  // the core can not remove interfaces

inline void idle() { delay(1); }
inline uint32_t millis() { return ::millis(); }
inline uint32_t micros() { return ::micros(); }

inline uint8_t *allocBuffer(size_t size) {
  size = (size + 63) & ~(size_t)63;
  return (uint8_t *)heap_caps_aligned_alloc(64, size,
                                            MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
}
inline void freeBuffer(uint8_t *buf) { heap_caps_free(buf); }

inline AppLock::AppLock() {}
inline AppLock::~AppLock() {}

}  // namespace platform
}  // namespace tinyuvc
