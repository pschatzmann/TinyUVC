#pragma once
/**
 * @file UVCConfig.h
 * @brief Compile-time limits and defaults of TinyUVC. Every value can be
 * overridden with a build flag (`-DTINYUVC_xxx=...`) or a `\#define` before
 * the first `\#include "TinyUVC.h"`.
 */

/// Max number of video formats (e.g. MJPEG + YUY2) a UVCDevice offers.
#ifndef TINYUVC_MAX_FORMATS
#define TINYUVC_MAX_FORMATS 2
#endif

/// Max number of resolutions (frame descriptors) per format.
#ifndef TINYUVC_MAX_FRAMES
#define TINYUVC_MAX_FRAMES 6
#endif

/// Max number of frame rates per resolution.
#ifndef TINYUVC_MAX_INTERVALS
#define TINYUVC_MAX_INTERVALS 4
#endif

/// Max size of one UVC payload transfer (header + data) in bytes. It is
/// reported to the host as dwMaxPayloadTransferSize and is the size of the
/// one transfer buffer the device allocates. Bigger values reduce the idle
/// time between transfers. It is rounded to a multiple of the endpoint's
/// max packet size and limited to 32 packets (2048 bytes full speed, 16384
/// bytes high speed), the URB size of Linux. Change it with
/// UVCDevice::setPayloadSize().
#ifndef TINYUVC_PAYLOAD_SIZE
#define TINYUVC_PAYLOAD_SIZE 16384
#endif

/// Max length of a configuration descriptor that TinyUVC builds for the
/// Adafruit TinyUSB stack (which needs one buffer for all interfaces).
#ifndef TINYUVC_CONFIG_BUFFER_SIZE
#define TINYUVC_CONFIG_BUFFER_SIZE 1024
#endif

/// Interface string, shown e.g. by Linux (v4l2-ctl --list-devices) and in
/// the Windows device manager.
#ifndef TINYUVC_DEFAULT_NAME
#define TINYUVC_DEFAULT_NAME "TinyUVC Camera"
#endif

/// Log level: 0 = off, 1 = error, 2 = warning, 3 = info, 4 = debug
#ifndef TINYUVC_LOG_LEVEL
#define TINYUVC_LOG_LEVEL 2
#endif
