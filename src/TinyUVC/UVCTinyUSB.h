#pragma once
/**
 * @file UVCTinyUSB.h
 * @brief Connects UVCDevice to TinyUSB as an application class driver
 * (usbd_app_driver_get_cb()). This works with every TinyUSB build -
 * the one in the ESP32 Arduino core, the Adafruit TinyUSB Library and the
 * Pico SDK of arduino-pico - whether its own video class (CFG_TUD_VIDEO)
 * is compiled in or not, because application drivers are asked first.
 *
 * TinyUSB knows only one usbd_app_driver_get_cb(): this header defines it
 * unless TINYUVC_NO_APP_DRIVER is defined. Define TINYUVC_NO_APP_DRIVER in
 * every additional .cpp file that includes TinyUVC.h, or if you need to
 * combine TinyUVC with another application driver: then return
 * tinyuvc::tusb_glue::driver() from your own usbd_app_driver_get_cb().
 */
#include "UVCDevice.h"

#if defined(TINYUVC_DESKTOP)
#include "desktop/TinyUSBSim.h"
#else
#include "tusb.h"
#include "device/usbd_pvt.h"
#endif

namespace tinyuvc {
namespace tusb_glue {

inline uint8_t &rhport() {
  static uint8_t port = 0;
  return port;
}

// usbd_edpt_xfer() got an additional is_isr parameter in TinyUSB 0.18:
// dispatch on the signature of the function that is actually declared.
inline bool callXfer(bool (*f)(uint8_t, uint8_t, uint8_t *, uint16_t),
                     uint8_t port, uint8_t ep, uint8_t *buf, uint16_t len) {
  return f(port, ep, buf, len);
}
inline bool callXfer(bool (*f)(uint8_t, uint8_t, uint8_t *, uint16_t, bool),
                     uint8_t port, uint8_t ep, uint8_t *buf, uint16_t len) {
  return f(port, ep, buf, len, false);
}

/// Starts an IN transfer on the bulk endpoint
inline bool xfer(uint8_t ep, uint8_t *buf, uint16_t len) {
  const uint8_t port = rhport();
  if (!usbd_edpt_claim(port, ep)) return false;
  if (!callXfer(&usbd_edpt_xfer, port, ep, buf, len)) {
    usbd_edpt_release(port, ep);
    return false;
  }
  return true;
}

inline void drvInit(void) {}

inline bool drvDeinit(void) { return true; }

inline void drvReset(uint8_t port) {
  (void)port;
  UVCDevice *dev = UVCDevice::instance();
  if (dev) dev->usbReset();
}

/// Claims the VideoControl + VideoStreaming interfaces of our function and
/// opens the bulk endpoint. Returns the length of the descriptors used.
inline uint16_t drvOpen(uint8_t port, tusb_desc_interface_t const *itf,
                        uint16_t maxLen) {
  UVCDevice *dev = UVCDevice::instance();
  if (dev == nullptr) return 0;
  if (itf->bInterfaceClass != uvc_spec::CC_VIDEO ||
      itf->bInterfaceSubClass != uvc_spec::SC_VIDEOCONTROL ||
      itf->bInterfaceNumber != dev->controlInterface())
    return 0;
  const uint8_t *p = (const uint8_t *)itf;
  const uint8_t *end = p + maxLen;
  uint16_t len = 0;
  uint16_t mps = 0;
  bool epOpen = false;
  while (p + 2 <= end) {
    const uint8_t bLength = p[0], type = p[1];
    if (bLength < 2 || p + bLength > end) break;
    if (len > 0) {
      if (type == uvc_spec::DESC_IAD) break;
      if (type == uvc_spec::DESC_INTERFACE && p[2] != dev->controlInterface() &&
          p[2] != dev->streamingInterface())
        break;
    }
    if (type == uvc_spec::DESC_ENDPOINT && p[2] == dev->endpoint()) {
      if (!usbd_edpt_open(port, (tusb_desc_endpoint_t const *)p)) {
        TINYUVC_LOGE("could not open endpoint 0x%02x", p[2]);
        return 0;
      }
      mps = (uint16_t)(le::get16(p + 4) & 0x7FF);
      epOpen = true;
    }
    p += bLength;
    len = (uint16_t)(len + bLength);
  }
  if (!epOpen) {
    TINYUVC_LOGE("no streaming endpoint found in descriptor");
    return 0;
  }
  rhport() = port;
  dev->usbOpened(&xfer, mps);
  return len;
}

inline bool drvControl(uint8_t port, uint8_t stage,
                       tusb_control_request_t const *request) {
  UVCDevice *dev = UVCDevice::instance();
  if (dev == nullptr) return false;
  UVCRequest r;
  r.bmRequestType = *(const uint8_t *)request;
  r.bRequest = request->bRequest;
  r.wValue = request->wValue;
  r.wIndex = request->wIndex;
  r.wLength = request->wLength;

  if (stage == CONTROL_STAGE_SETUP) {
    uint8_t *data = nullptr;
    uint16_t len = 0;
    switch (dev->handleControlSetup(r, data, len)) {
      case UVCControlResult::DataIn:
      case UVCControlResult::DataOut:
        return tud_control_xfer(port, request, data, len);
      case UVCControlResult::Status:
        return tud_control_status(port, request);
      case UVCControlResult::Handled:
        return true;
      case UVCControlResult::Stall:
      default:
        return false;
    }
  }
  if (stage == CONTROL_STAGE_DATA && !r.isIn() && r.wLength > 0) {
    return dev->handleControlData(r, r.wLength);
  }
  return true;
}

inline bool drvXfer(uint8_t port, uint8_t ep, xfer_result_t result,
                    uint32_t xferred) {
  (void)port;
  (void)xferred;
  UVCDevice *dev = UVCDevice::instance();
  if (dev == nullptr || ep != dev->endpoint()) return false;
  dev->usbTransferComplete(result == XFER_RESULT_SUCCESS);
  return true;
}

// Fields that do not exist in every TinyUSB version are set via SFINAE
template <typename T>
auto setName(T &d, int) -> decltype(d.name = "", void()) {
  d.name = "TinyUVC";
}
template <typename T>
void setName(T &, long) {}
template <typename T>
auto setDeinit(T &d, int) -> decltype(d.deinit = &drvDeinit, void()) {
  d.deinit = &drvDeinit;
}
template <typename T>
void setDeinit(T &, long) {}

inline usbd_class_driver_t makeDriver() {
  usbd_class_driver_t d;
  memset(&d, 0, sizeof(d));
  setName(d, 0);
  setDeinit(d, 0);
  d.init = &drvInit;
  d.reset = &drvReset;
  d.open = &drvOpen;
  d.control_xfer_cb = &drvControl;
  d.xfer_cb = &drvXfer;
  return d;
}

/// The TinyUVC class driver
inline usbd_class_driver_t const *driver() {
  static usbd_class_driver_t drv = makeDriver();
  return &drv;
}

}  // namespace tusb_glue
}  // namespace tinyuvc

#if !defined(TINYUVC_NO_APP_DRIVER)
#if defined(ENABLE_PICOTOOL_USB)
#error \
    "TinyUVC: the picotool upload method defines its own TinyUSB application " \
    "driver. Select Tools > Upload Method > 'Default (UF2)', or define "      \
    "TINYUVC_NO_APP_DRIVER and return both drivers yourself."
#endif
/// TinyUSB asks for application class drivers once, in tud_init()
extern "C" usbd_class_driver_t const *usbd_app_driver_get_cb(
    uint8_t *driver_count) {
  *driver_count = 1;
  return tinyuvc::tusb_glue::driver();
}
#endif
