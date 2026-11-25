#pragma once
/**
 * @file UVCPlatformDesktop.h
 * @brief Desktop platform: there is no USB device controller, the device
 * is driven by UVCHostSimulator (desktop/UVCHostSimulator.h).
 */
#include <chrono>
#include <cstdlib>
#include <thread>

#include "../UVCDevice.h"
#include "../desktop/TinyUSBSim.h"

namespace tinyuvc {

/// Settings of the simulated USB bus
struct UVCDesktopSettings {
  /// 64 = full speed (ESP32-S2/S3, RP2040, STM32 OTG_FS), 512 = high speed
  uint16_t maxPacketSize = 64;
};

inline UVCDesktopSettings &desktopSettings() {
  static UVCDesktopSettings settings;
  return settings;
}

namespace platform {

inline bool begin(UVCDevice &) { return true; }
inline void end(UVCDevice &) {}
inline uint16_t maxPacketSize() { return desktopSettings().maxPacketSize; }

inline void idle() {
  UVCSimBus &b = simBus();
  if (b.pump)
    b.pump(b.pumpRef);
  else
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

inline uint32_t millis() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return (uint32_t)duration_cast<milliseconds>(steady_clock::now() - start).count();
}

inline uint32_t micros() {
  using namespace std::chrono;
  static const auto start = steady_clock::now();
  return (uint32_t)duration_cast<microseconds>(steady_clock::now() - start).count();
}

inline uint8_t *allocBuffer(size_t size) {
  return (uint8_t *)std::malloc((size + 63) & ~(size_t)63);
}
inline void freeBuffer(uint8_t *buf) { std::free(buf); }

inline AppLock::AppLock() {}
inline AppLock::~AppLock() {}

}  // namespace platform
}  // namespace tinyuvc
