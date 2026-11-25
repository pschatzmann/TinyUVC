#pragma once
/**
 * @file UVCPlatformPico.h
 * @brief RP2040/RP2350 with arduino-pico and its default USB stack
 * (Tools > USB Stack > "Pico SDK"). The camera is added as an additional
 * interface of the core's USB class, next to the USB serial port.
 */
#include <Arduino.h>
#include <CoreMutex.h>
#include <USB.h>
#include <stdlib.h>

#include <new>

#include "../UVCDevice.h"

namespace tinyuvc {
namespace platform {

struct PicoState {
  uint8_t endpoint = 0;
  uint8_t stringIndex = 0;
  int id = -1;
};

inline PicoState &picoState() {
  static PicoState state;
  return state;
}

inline uint16_t maxPacketSize() { return 64; }

inline void picoDescriptor(int itf, uint8_t *dst, int len, void *param) {
  UVCDevice *dev = (UVCDevice *)param;
  PicoState &s = picoState();
  dev->buildDescriptor(dst, (uint16_t)len, (uint8_t)itf, s.endpoint,
                       maxPacketSize(), s.stringIndex);
}

inline bool begin(UVCDevice &dev) {
  PicoState &s = picoState();
  const uint16_t len = dev.descriptorLength();
  if (len > 4095) {
    TINYUVC_LOGE("descriptor too long for arduino-pico (%u)", len);
    return false;
  }
  USB.disconnect();
  s.endpoint = USB.registerEndpointIn();
  if (s.endpoint == 0) {
    USB.connect();
    TINYUVC_LOGE("no free IN endpoint");
    return false;
  }
  s.stringIndex = USB.registerString(dev.name());
  // the PID bit makes Windows treat the composite device as new
  s.id = USB.registerInterface(2, picoDescriptor, &dev, len, 3, 0x0040);
  USB.connect();
  return true;
}

inline void end(UVCDevice &) {
  PicoState &s = picoState();
  if (s.id < 0) return;
  USB.disconnect();
  USB.unregisterInterface(s.id);
  USB.unregisterEndpointIn(s.endpoint & 0x7F);
  s.id = -1;
  USB.connect();
}

inline void idle() { delay(1); }
inline uint32_t millis() { return ::millis(); }
inline uint32_t micros() { return ::micros(); }

inline uint8_t *allocBuffer(size_t size) {
  return (uint8_t *)malloc((size + 3) & ~(size_t)3);
}
inline void freeBuffer(uint8_t *buf) { free(buf); }

/// The core runs tud_task() from an interrupt (or task) that skips while
/// the USB mutex is taken: hold it while starting transfers from the sketch
static_assert(sizeof(CoreMutex) <= sizeof(AppLock::state), "AppLock too small");
inline AppLock::AppLock() { new (state) CoreMutex(&USB.mutex); }
inline AppLock::~AppLock() { ((CoreMutex *)state)->~CoreMutex(); }

}  // namespace platform
}  // namespace tinyuvc
