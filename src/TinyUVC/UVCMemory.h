#pragma once
/**
 * @file UVCMemory.h
 * @brief Memory for frame sized buffers, taken from PSRAM where the board
 * has it: ESP32 with PSRAM enabled (Tools > PSRAM), and RP2350 boards with
 * PSRAM (arduino-pico defines RP2350_PSRAM_CS). Elsewhere it is normal heap
 * memory.
 *
 * The USB transfer buffer of UVCDevice is not allocated here: the USB DMA
 * needs internal RAM. Frames that are written to UVCDevice::writeFrame()
 * can be anywhere (PSRAM too), because they are copied payload by payload
 * into the transfer buffer.
 */
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
#include <Arduino.h>
#include "esp_heap_caps.h"
#elif defined(ARDUINO_ARCH_RP2040)
#include <Arduino.h>
#endif

namespace tinyuvc {

/// Where frame buffers are allocated
enum class UVCMemoryType {
  Auto,   ///< PSRAM if the board has it, else RAM
  PSRAM,  ///< PSRAM only: the allocation fails without PSRAM
  RAM     ///< internal RAM only
};

/// @brief Allocates frame sized buffers in PSRAM or RAM (see UVCMemoryType)
class UVCMemory {
 public:
  /// true if PSRAM is available
  static bool hasPSRAM() {
#if defined(ARDUINO_ARCH_ESP32)
    return psramFound();
#elif defined(ARDUINO_ARCH_RP2040) && defined(RP2350_PSRAM_CS)
    return rp2040.getPSRAMSize() > 0;
#else
    return false;
#endif
  }

  /// Allocates size bytes; nullptr if that is not possible. Release with
  /// UVCMemory::free().
  static uint8_t *alloc(size_t size, UVCMemoryType type = UVCMemoryType::Auto) {
    if (size == 0) return nullptr;
    if (type != UVCMemoryType::RAM) {
      uint8_t *p = allocPSRAM(size);
      if (p != nullptr || type == UVCMemoryType::PSRAM) return p;
    }
    return allocRAM(size);
  }

  static void free(void *p) {
    // ESP-IDF and arduino-pico release PSRAM with the normal free()
    ::free(p);
  }

 protected:
  static uint8_t *allocPSRAM(size_t size) {
#if defined(ARDUINO_ARCH_ESP32)
    if (!psramFound()) return nullptr;
    return (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#elif defined(ARDUINO_ARCH_RP2040) && defined(RP2350_PSRAM_CS)
    if (rp2040.getPSRAMSize() == 0) return nullptr;
    return (uint8_t *)pmalloc(size);
#else
    (void)size;
    return nullptr;
#endif
  }

  static uint8_t *allocRAM(size_t size) {
#if defined(ARDUINO_ARCH_ESP32)
    // plain malloc() may hand out PSRAM as well (CONFIG_SPIRAM_USE_MALLOC)
    return (uint8_t *)heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#else
    return (uint8_t *)malloc(size);
#endif
  }
};

/**
 * @brief A growing byte buffer for one frame, allocated with UVCMemory (so
 * in PSRAM where available). It grows in steps while a frame is written and
 * keeps its memory when it is cleared, so after the first frames no more
 * allocations happen.
 */
class UVCFrameBuffer {
 public:
  UVCFrameBuffer() = default;
  UVCFrameBuffer(const UVCFrameBuffer &) = delete;
  UVCFrameBuffer &operator=(const UVCFrameBuffer &) = delete;
  ~UVCFrameBuffer() { release(); }

  void setMemoryType(UVCMemoryType type) { type_ = type; }
  UVCMemoryType memoryType() const { return type_; }

  /// Makes sure that size bytes fit without a new allocation
  bool reserve(size_t size) {
    if (size <= capacity_) return true;
    uint8_t *p = UVCMemory::alloc(size, type_);
    if (p == nullptr) return false;
    if (size_ > 0) memcpy(p, data_, size_);
    UVCMemory::free(data_);
    data_ = p;
    capacity_ = size;
    return true;
  }

  /// Appends data; false if no memory is left (the buffer is unchanged)
  bool append(const uint8_t *data, size_t len) {
    const size_t needed = size_ + len;
    if (needed > capacity_) {
      // grow by 50% to limit the number of copies; fall back to the
      // exact size if that is too much
      size_t cap = capacity_ + capacity_ / 2;
      if (cap < needed) cap = needed;
      if (cap < 4096) cap = 4096;
      if (!reserve(cap) && !reserve(needed)) return false;
    }
    memcpy(data_ + size_, data, len);
    size_ = needed;
    return true;
  }

  void clear() { size_ = 0; }

  /// Frees the memory
  void release() {
    UVCMemory::free(data_);
    data_ = nullptr;
    size_ = capacity_ = 0;
  }

  uint8_t *data() { return data_; }
  const uint8_t *data() const { return data_; }
  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  bool empty() const { return size_ == 0; }

 protected:
  uint8_t *data_ = nullptr;
  size_t size_ = 0;
  size_t capacity_ = 0;
  UVCMemoryType type_ = UVCMemoryType::Auto;
};

}  // namespace tinyuvc
