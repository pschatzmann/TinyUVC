# TinyUVC

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://github.com/pschatzmann/TinyUVC/blob/main/CMakeLists.txt)
[![License: Apache](https://img.shields.io/badge/License-Apache-yellow.svg)](https://opensource.org/licenses/Apache-2.0)

## Introduction

TinyUVC is a header-only C++ **USB Video Class (UVC)** device library for
Arduino. It turns an ESP32-S2/S3/P4, a Raspberry Pi Pico (RP2040/RP2350) or
an STM32 into a **USB webcam** that Linux, Windows and macOS use without any
driver.

## Features

- Header only: `#include "TinyUVC.h"`.
- One generic TinyUSB implementation (Adafruit TinyUSB Library) for RP2040,
  RP2350, STM32, SAMD and nRF52; the ESP32 uses the TinyUSB of its core.
- UVC 1.1 with a bulk endpoint: MJPEG and/or uncompressed YUY2, several
  resolutions and frame rates. The host picks, your sketch is told.
- Frames in RGB565, RGB888, BGR888 or grayscale are converted to YUY2 while
  they are sent: no second frame buffer.
- PSRAM: frame buffers are taken from PSRAM where the board has it (ESP32,
  RP2350).
- [TinyCamera](https://github.com/pschatzmann/TinyCamera) integration: ESP32,
  ESP32-P4, RP2040 and STM32 camera sensors become webcams in a few lines.
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
  integration: video encoders, decoders and demuxers (e.g. `MJPEGEncoder`,
  `DemuxerAVI`) feed the webcam.
- Desktop build with CMake: the device code runs against a simulated USB host
  that behaves like Linux' `uvcvideo` driver.

Verified on an ESP32-S3 and a Raspberry Pi Pico with Linux hosts; see
[Supported platforms](https://github.com/pschatzmann/TinyUVC/wiki/Platforms)
for the board settings and the test status, and
[Limitations](https://github.com/pschatzmann/TinyUVC/wiki/Limitations) for
what is not supported.

## Documentation

- [Usage](https://github.com/pschatzmann/TinyUVC/wiki/Usage): plain frames,
  TinyCamera, arduino-audio-tools, and the examples
- [Supported platforms](https://github.com/pschatzmann/TinyUVC/wiki/Platforms):
  board settings, tested hardware, and USB bandwidth
- [Configuration](https://github.com/pschatzmann/TinyUVC/wiki/Configuration):
  compile-time settings
- [Desktop build and tests](https://github.com/pschatzmann/TinyUVC/wiki/Desktop):
  CMake, the simulated USB host, and what it checks
- [Limitations](https://github.com/pschatzmann/TinyUVC/wiki/Limitations): what
  is not supported
- [API reference](https://pschatzmann.github.io/TinyUVC/namespaces.html)
  Doxygen Class Documentation
- [License](LICENSE): Apache 2.0; the
  [License notes](https://github.com/pschatzmann/TinyUVC/wiki/License) cover
  TinyUSB and the GPLv3 integrations

## Installation in Arduino

You can download the library as zip and call include Library -> zip library. Or you can git clone this project into the Arduino libraries folder e.g. with

```
cd  ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyUVC.git
```

I recommend to use git because you can easily update to the latest version just by executing the ```git pull``` command in the project folder.

Optional: [TinyCamera](https://github.com/pschatzmann/TinyCamera),
[arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools) and
[TinyJPEG](https://github.com/pschatzmann/TinyJPEG); for STM32 the
[Adafruit TinyUSB Library](https://github.com/adafruit/Adafruit_TinyUSB_Arduino).

Then add `#include "TinyUVC.h"` to your sketch. See the [usage guide](https://github.com/pschatzmann/TinyUVC/wiki/Usage) and the `TestPatternWebcam` example.

For desktop builds with CMake, see [Desktop build and tests](https://github.com/pschatzmann/TinyUVC/wiki/Desktop).
