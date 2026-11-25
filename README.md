# TinyUVC

[![Arduino Library](https://img.shields.io/badge/Arduino-Library-blue.svg)](https://www.arduino.cc/reference/en/libraries/)
[![CMake](https://img.shields.io/badge/CMake-Supported-blue.svg)](https://github.com/pschatzmann/TinyUVC/blob/main/CMakeLists.txt)
[![License: Apache](https://img.shields.io/badge/License-Apache-yellow.svg)](https://opensource.org/licenses/Apache-2.0)

A header-only C++ **USB Video Class (UVC)** device library for Arduino: it
turns an ESP32-S2/S3/P4, a Raspberry Pi Pico (RP2040/RP2350) or an STM32
into a **USB webcam** that Linux, Windows and macOS use without any driver.

```cpp
#include "TinyUVC.h"

UVCDevice uvc;

void setup() {
  uvc.addResolution(UVCFormat::MJPEG, 320, 240, 15);
  uvc.begin();
}

void loop() {
  if (uvc.isReady()) uvc.writeFrame(jpeg, jpegLen, PixelFormat::MJPEG);
}
```

## Features

- Header-only: just `#include "TinyUVC.h"`.
- UVC 1.1 with a bulk endpoint: MJPEG and/or uncompressed YUY2, up to
  6 resolutions per format and 4 frame rates per resolution. The host picks
  format, resolution and frame rate; your sketch is told via
  `settings()` / `setStreamCallback()`.
- Frames in RGB565 (both byte orders), RGB888, BGR888 or grayscale are
  converted to YUY2 **while they are sent**: no second frame buffer.
- The frame is copied payload by payload into a single transfer buffer,
  so a new frame can be prepared while the previous one is still on the
  wire.
- [TinyCamera](https://github.com/pschatzmann/TinyCamera) integration
  (`TinyUVCCamera.h`): camera sensors on ESP32 (OV2640/OV3660/OV5640 with
  hardware JPEG), ESP32-P4 MIPI-CSI, RP2040 and STM32 DCMI become webcams in
  a few lines. The camera is reconfigured when the host switches resolution
  or format.
- [arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools)
  integration (`TinyUVCAudioTools.h`): `UVCVideoOutput` is a `VideoOutput`,
  so the video decoders/encoders and demuxers (e.g. `MJPEGEncoder`,
  `DemuxerAVI`, `VideoPlayer`) can feed the webcam; `UVCVideoCopy` streams
  any `VideoFrameSource`.
- Desktop build with CMake: the device side runs unchanged against a
  simulated USB host that behaves like Linux' `uvcvideo` driver, for
  tests and development without hardware.

## Supported platforms

| Platform | USB stack | Board settings |
| --- | --- | --- |
| ESP32-S2 / S3 / P4 | TinyUSB of the arduino-esp32 core (3.x) | USB Mode: **USB-OTG (TinyUSB)**, USB CDC On Boot: **Disabled** |
| RP2040 / RP2350 (arduino-pico) | Pico SDK (default) or Adafruit TinyUSB | USB Stack: **Pico SDK** or **Adafruit TinyUSB**; not with Upload Method "Picotool" |
| STM32 with USB OTG FS (e.g. F4, F7, H7) | [Adafruit TinyUSB Library](https://github.com/adafruit/Adafruit_TinyUSB_Arduino) | USB support: **None** |
| SAMD, nRF52 | Adafruit TinyUSB Library | (untested) |
| Linux / macOS / Windows | simulated host (`UVCHostSimulator`) | CMake |

Status: verified on hardware with a Linux host (ffmpeg/V4L2, VLC, Cheese
and guvcview):
- **ESP32-S3** + OV5640: YUY2 test pattern, MJPEG from `MJPEGEncoder`, the
  camera as MJPEG at 320x240 and 640x480 (15 fps), repeated stream start/stop
- **Raspberry Pi Pico (RP2040)**, both USB stacks (Pico SDK and Adafruit
  TinyUSB): YUY2 test pattern at 15 fps next to the USB serial port,
  repeated stream start/stop; a one hour soak test (Adafruit TinyUSB) of
  48600 frames and 540 open/close cycles without a single error

RP2350, STM32, ESP32-S2 and ESP32-P4 compile (see `test_compile.sh`) but
are not yet verified on hardware; Windows and macOS hosts are not yet
tested.

**ESP32:** the core builds its USB descriptor when USB starts, and
`uvc.begin()` starts it. With "USB CDC On Boot: Enabled" USB is already
running before `setup()`, and `begin()` fails. To still have a USB serial
port, declare a `USBCDC USBSerial;` object as in the examples: it shares
the OTG port with the camera and supports the 1200 baud reset into the
bootloader (`stty -F /dev/ttyACM0 1200`, then upload).

## Bandwidth

All supported MCUs except the ESP32-P4 have a full speed USB port (12
Mbit/s). A bulk endpoint delivers roughly 1 MB/s there. An uncompressed
YUY2 frame needs 2 bytes per pixel: 160x120 at 15 fps (576 KB/s) works,
320x240 only at about 5 fps. Use **MJPEG** for bigger pictures: with a
JPEG capable sensor (ESP32 cameras), or with the software `MJPEGEncoder`
of arduino-audio-tools.

## Examples

| Example | What it shows |
| --- | --- |
| [TestPatternWebcam](examples/TestPatternWebcam) | No camera: moving color bars as YUY2 - start here |
| [ESP32CameraWebcam](examples/ESP32CameraWebcam) | ESP32-S3 camera board as MJPEG webcam with two resolutions |
| [RP2040CameraWebcam](examples/RP2040CameraWebcam) | Pico + OV7670/OV2640, RGB565 converted to YUY2 |
| [STM32CameraWebcam](examples/STM32CameraWebcam) | WeAct STM32H750 + OV7725 via DCMI |
| [AudioToolsJpegWebcam](examples/AudioToolsJpegWebcam) | Software JPEG compression with `MJPEGEncoder` -> `UVCVideoOutput` |
| [AudioToolsAviWebcam](examples/AudioToolsAviWebcam) | Plays an MJPEG AVI from SD as webcam picture (`DemuxerAVI`) |
| [desktop/webcam_simulation](examples/desktop/webcam_simulation.cpp) | The whole chain on the PC, output playable with ffplay |

## Using it

### Plain frames

```cpp
UVCDevice uvc;
uvc.addResolution(UVCFormat::YUY2, 160, 120, {15, 10});   // two frame rates
uvc.addResolution(UVCFormat::MJPEG, 640, 480, 15, 60000);  // max JPEG size
uvc.begin();
...
if (uvc.isReady()) {
  UVCStreamSettings s = uvc.settings();   // what the host selected
  uvc.writeFrame(data, len, PixelFormat::RGB565_LE);
}
```

`writeFrame()` rejects frames that do not match the host's selection: MJPEG
needs `PixelFormat::MJPEG`, YUY2 needs exactly width x height pixels in any
raw format. The data must stay valid until `isReady()` is true again.

### With TinyCamera

```cpp
#include "TinyCameraPins.h"
#include "TinyUVCCamera.h"

UVCDevice uvc;
UVCCamera camera(uvc);

void setup() {
  camera_config_t cfg = TinyCamera::defaultConfig();
  setPinsXiaoEsp32S3(cfg);
  cfg.pixel_format = PIXFORMAT_JPEG;          // -> MJPEG, RGB565 -> YUY2
  camera.addFrameSize(FRAMESIZE_QVGA);
  camera.addFrameSize(FRAMESIZE_VGA);
  camera.begin(cfg);                          // starts uvc as well
}

void loop() { camera.copy(); }
```

### With arduino-audio-tools

```cpp
#include "AudioTools.h"
#include "AudioTools/Video/CodecJPEG.h"
#include "TinyUVCAudioTools.h"

UVCDevice uvc;
UVCVideoOutput out(uvc, PixelFormat::MJPEG);
MJPEGEncoder encoder(out);       // RGB565 in, JPEG to the webcam
```

The video classes of arduino-audio-tools need FreeRTOS: on RP2040/RP2350
select Tools > Operating System > "FreeRTOS SMP" and add
`#include "AudioTools/Concurrency/RTOS.h"` after `AudioTools.h` (see the
AudioTools examples); on STM32 this needs the STM32FreeRTOS library.

`UVCVideoOutput` collects the `write()` calls of a frame and sends it on
`flush()` (a complete JPEG in a single `write()` is sent right away).
`setPacing(true)` plays files at the frame rate the host selected,
`setTimeout()` lets it wait for the USB transfer instead of dropping frames.

## Desktop / CMake

```bash
cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure
./build/examples/desktop/webcam_simulation --format mjpeg --frames 60 --out cam.mjpeg
ffplay -f mjpeg cam.mjpeg
```

TinyCamera, TinyJPEG and arduino-audio-tools are taken from sibling folders
(your Arduino `libraries` folder) or downloaded. Point to other local
checkouts with `-DTINYUVC_TINYCAMERA_DIR=<path>` and
`-DTINYUVC_AUDIO_TOOLS_DIR=<path>`. `-DTINYUVC_SANITIZE=ON`
builds with address/undefined behavior sanitizers. See
[docs/Desktop.md](docs/Desktop.md) for what the simulator checks.

## Configuration

Compile-time limits (define before including, or as build flags):
`TINYUVC_MAX_FORMATS` (2), `TINYUVC_MAX_FRAMES` (6),
`TINYUVC_MAX_INTERVALS` (4), `TINYUVC_PAYLOAD_SIZE` (16384, capped at 32
packets: 2048 bytes at full speed), `TINYUVC_CONFIG_BUFFER_SIZE` (1024,
Adafruit TinyUSB), `TINYUVC_LOG_LEVEL`.

TinyUVC registers itself as TinyUSB *application class driver*
(`usbd_app_driver_get_cb()`), which is defined in `TinyUVC.h`. If you
include `TinyUVC.h` in more than one `.cpp` file, define
`TINYUVC_NO_APP_DRIVER` in all but one of them.

## Limitations

- One camera (one UVCDevice) per device; no camera controls (brightness
  etc.) or still image capture are exposed to the host.
- Bulk transfer only (no isochronous endpoint).
- When the host stops a stream in the middle of a frame, the transfer that
  was already queued is delivered when the stream is restarted: the first
  frame after such a restart can be corrupt.

## Documentation

- [Desktop build and tests](docs/Desktop.md): CMake, the simulated USB host
  and what it checks
- [API reference](https://pschatzmann.github.io/TinyUVC/namespaces.html)
  Doxygen Class Documentation
- [License](LICENSE)

## License

TinyUVC is licensed under the [Apache License 2.0](LICENSE).

It uses the TinyUSB device stack (MIT license) that comes with the platform:
the ESP32 Arduino core, the Pico SDK of arduino-pico or the Adafruit TinyUSB
Library. TinyUVC does not contain TinyUSB code; the desktop simulator
(`src/TinyUVC/desktop/TinyUSBSim.h`) re-declares a few TinyUSB types and keeps
TinyUSB's MIT notice.

The optional integrations `TinyUVCCamera.h` (TinyCamera) and
`TinyUVCAudioTools.h` (arduino-audio-tools) use libraries that are licensed
under the GPLv3. The Apache License 2.0 is compatible with the GPLv3, but a
program that includes one of these headers must be distributed under the
terms of the GPLv3.

## Installation

Clone into your Arduino `libraries` folder:

```bash
cd ~/Documents/Arduino/libraries
git clone https://github.com/pschatzmann/TinyUVC.git
```

Optional: [TinyCamera](https://github.com/pschatzmann/TinyCamera),
[arduino-audio-tools](https://github.com/pschatzmann/arduino-audio-tools),
[TinyJPEG](https://github.com/pschatzmann/TinyJPEG); for STM32 the
Adafruit TinyUSB Library.
