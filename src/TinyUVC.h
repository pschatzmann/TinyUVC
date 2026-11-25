#pragma once
/**
 * @file TinyUVC.h
 * @brief TinyUVC: header-only USB Video Class (UVC) device - turns a
 * microcontroller into a USB webcam.
 *
 * The platform layer is selected automatically:
 * | Platform                         | USB stack                                |
 * |----------------------------------|------------------------------------------|
 * | ESP32-S2/S3/P4                   | arduino-esp32 core, USB Mode "USB-OTG"   |
 * | all other Arduino boards: RP2040/| generic: Adafruit TinyUSB Library        |
 * | RP2350, STM32, SAMD, nRF52       | (Adafruit_USBD_Device)                   |
 * | Linux/macOS/Windows              | simulated (UVCHostSimulator)             |
 *
 * Override with -DTINYUVC_PLATFORM_ESP32 / _TINYUSB / _DESKTOP, or
 * with TINYUVC_PLATFORM_CUSTOM to provide your own tinyuvc::platform
 * functions (see UVCDevice.h).
 *
 * Integrations (include separately):
 * - TinyUVCCamera.h: stream a TinyCamera (esp32-camera, RP2040, STM32 DCMI)
 * - TinyUVCAudioTools.h: VideoOutput / VideoFrameSource of arduino-audio-tools
 */

/// @namespace tinyuvc
/// @brief TinyUVC: USB Video Class (UVC) device for Arduino and the desktop
#include "TinyUVC/UVCConfig.h"

// ------------------------------------------------------- platform selection
#if !defined(TINYUVC_PLATFORM_ESP32) && !defined(TINYUVC_PLATFORM_TINYUSB) && \
    !defined(TINYUVC_PLATFORM_DESKTOP) && !defined(TINYUVC_PLATFORM_CUSTOM)

#if defined(ARDUINO_ARCH_ESP32)
#include "sdkconfig.h"
#include "soc/soc_caps.h"
#if !SOC_USB_OTG_SUPPORTED
#error "TinyUVC: this ESP32 has no USB OTG peripheral - use an ESP32-S2, S3 or P4"
#elif defined(TINYUVC_USE_ADAFRUIT_TINYUSB)
#define TINYUVC_PLATFORM_TINYUSB
#elif CONFIG_TINYUSB_ENABLED && !ARDUINO_USB_MODE
#define TINYUVC_PLATFORM_ESP32
#else
#error "TinyUVC: select Tools > USB Mode > 'USB-OTG (TinyUSB)'"
#endif

#elif defined(ARDUINO_ARCH_RP2040) && !defined(USE_TINYUSB)
#error "TinyUVC: select Tools > USB Stack > 'Adafruit TinyUSB'"

#elif defined(ARDUINO) && !defined(HOST)
#define TINYUVC_PLATFORM_TINYUSB

#elif defined(__linux__) || defined(__APPLE__) || defined(_WIN32) || defined(HOST)
#define TINYUVC_PLATFORM_DESKTOP

#else
#error "TinyUVC: unsupported platform"
#endif
#endif

#if defined(TINYUVC_PLATFORM_DESKTOP) && !defined(TINYUVC_DESKTOP)
#define TINYUVC_DESKTOP
#endif

// ------------------------------------------------------------ the library
#include "TinyUVC/UVCDevice.h"

#if defined(TINYUVC_PLATFORM_ESP32)
#include "TinyUVC/platform/UVCPlatformESP32.h"
#elif defined(TINYUVC_PLATFORM_TINYUSB)
#include "TinyUVC/platform/UVCPlatformTinyUSB.h"
#elif defined(TINYUVC_PLATFORM_DESKTOP)
#include "TinyUVC/platform/UVCPlatformDesktop.h"
#endif

#include "TinyUVC/UVCTinyUSB.h"

#if defined(TINYUVC_PLATFORM_DESKTOP)
#include "TinyUVC/desktop/UVCHostSimulator.h"
#endif

#if !defined(TINYUVC_NO_USING_NAMESPACE)
using namespace tinyuvc;
#endif
