#pragma once
/**
 * @file TinyUSBSim.h
 * @brief Desktop stand-in for the parts of the TinyUSB device API that
 * UVCTinyUSB.h uses. The calls are recorded in UVCSimBus, where
 * UVCHostSimulator (playing the USB host and the TinyUSB core) picks them
 * up. Like this the same class driver code runs on the desktop as on the
 * microcontroller.
 *
 * The type and function declarations below mirror the TinyUSB device API
 * (https://github.com/hathach/tinyusb):
 *   The MIT License (MIT)
 *   Copyright (c) 2019 Ha Thach (tinyusb.org)
 *   Permission is hereby granted, free of charge, to any person obtaining a
 *   copy of this software and associated documentation files (the
 *   "Software"), to deal in the Software without restriction, including
 *   without limitation the rights to use, copy, modify, merge, publish,
 *   distribute, sublicense, and/or sell copies of the Software, and to
 *   permit persons to whom the Software is furnished to do so, subject to
 *   the following conditions:
 *
 *   The above copyright notice and this permission notice shall be included
 *   in all copies or substantial portions of the Software.
 *
 *   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 *   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 *   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
 *   NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
 *   DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 *   OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 *   USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdint.h>
#include <string.h>

// ----------------------------------------------------------- TinyUSB types

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bInterfaceNumber;
  uint8_t bAlternateSetting;
  uint8_t bNumEndpoints;
  uint8_t bInterfaceClass;
  uint8_t bInterfaceSubClass;
  uint8_t bInterfaceProtocol;
  uint8_t iInterface;
} tusb_desc_interface_t;

typedef struct __attribute__((packed)) {
  uint8_t bLength;
  uint8_t bDescriptorType;
  uint8_t bEndpointAddress;
  uint8_t bmAttributes;
  uint16_t wMaxPacketSize;
  uint8_t bInterval;
} tusb_desc_endpoint_t;

typedef struct __attribute__((packed)) {
  uint8_t bmRequestType;
  uint8_t bRequest;
  uint16_t wValue;
  uint16_t wIndex;
  uint16_t wLength;
} tusb_control_request_t;

typedef enum {
  XFER_RESULT_SUCCESS = 0,
  XFER_RESULT_FAILED,
  XFER_RESULT_STALLED,
  XFER_RESULT_TIMEOUT,
  XFER_RESULT_INVALID
} xfer_result_t;

enum {
  CONTROL_STAGE_IDLE,
  CONTROL_STAGE_SETUP,
  CONTROL_STAGE_DATA,
  CONTROL_STAGE_ACK
};

typedef struct {
  char const *name;
  void (*init)(void);
  bool (*deinit)(void);
  void (*reset)(uint8_t rhport);
  uint16_t (*open)(uint8_t rhport, tusb_desc_interface_t const *desc_intf,
                   uint16_t max_len);
  bool (*control_xfer_cb)(uint8_t rhport, uint8_t stage,
                          tusb_control_request_t const *request);
  bool (*xfer_cb)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                  uint32_t xferred_bytes);
  bool (*xfer_isr)(uint8_t rhport, uint8_t ep_addr, xfer_result_t result,
                   uint32_t xferred_bytes);
  void (*sof)(uint8_t rhport, uint32_t frame_count);
} usbd_class_driver_t;

extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(uint8_t *driver_count);

// ------------------------------------------------------ recorded USB state

namespace tinyuvc {

/// What the device side did, as seen by the simulated USB core
struct UVCSimBus {
  // bulk IN endpoint
  uint8_t openEndpoint = 0;
  uint16_t openMaxPacketSize = 0;
  bool claimed = false;
  bool busy = false;
  uint8_t *xferBuf = nullptr;
  uint16_t xferLen = 0;
  bool failNextXfer = false;  ///< test hook: next usbd_edpt_xfer() fails
  // control endpoint
  bool ctrlXfer = false;
  bool ctrlStatus = false;
  uint8_t *ctrlBuf = nullptr;
  uint16_t ctrlLen = 0;
  /// Called by UVCDevice::waitReady() via platform::idle(): lets the
  /// simulated host read the pending transfer
  void (*pump)(void *ref) = nullptr;
  void *pumpRef = nullptr;

  void resetControl() {
    ctrlXfer = ctrlStatus = false;
    ctrlBuf = nullptr;
    ctrlLen = 0;
  }
  void resetEndpoint() {
    openEndpoint = 0;
    claimed = busy = false;
    xferBuf = nullptr;
    xferLen = 0;
  }
};

inline UVCSimBus &simBus() {
  static UVCSimBus bus;
  return bus;
}

}  // namespace tinyuvc

// ------------------------------------------------------- TinyUSB functions

inline bool usbd_edpt_open(uint8_t, tusb_desc_endpoint_t const *ep) {
  auto &b = tinyuvc::simBus();
  b.openEndpoint = ep->bEndpointAddress;
  b.openMaxPacketSize = ep->wMaxPacketSize;
  b.claimed = b.busy = false;
  return true;
}

inline bool usbd_edpt_claim(uint8_t, uint8_t ep) {
  auto &b = tinyuvc::simBus();
  if (ep != b.openEndpoint || b.busy || b.claimed) return false;
  b.claimed = true;
  return true;
}

inline bool usbd_edpt_release(uint8_t, uint8_t ep) {
  auto &b = tinyuvc::simBus();
  if (ep != b.openEndpoint) return false;
  b.claimed = false;
  return true;
}

inline bool usbd_edpt_xfer(uint8_t, uint8_t ep, uint8_t *buf, uint16_t len,
                           bool is_isr) {
  (void)is_isr;
  auto &b = tinyuvc::simBus();
  if (ep != b.openEndpoint || b.busy) return false;
  if (b.failNextXfer) {
    b.failNextXfer = false;
    return false;
  }
  b.busy = true;
  b.xferBuf = buf;
  b.xferLen = len;
  return true;
}

inline bool tud_control_xfer(uint8_t, tusb_control_request_t const *,
                             void *buf, uint16_t len) {
  auto &b = tinyuvc::simBus();
  b.ctrlXfer = true;
  b.ctrlBuf = (uint8_t *)buf;
  b.ctrlLen = len;
  return true;
}

inline bool tud_control_status(uint8_t, tusb_control_request_t const *) {
  tinyuvc::simBus().ctrlStatus = true;
  return true;
}
