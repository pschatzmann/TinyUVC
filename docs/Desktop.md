# Desktop / CMake

A PC has no USB *device* controller, so on the desktop TinyUVC replaces
the bottom of the stack: `desktop/TinyUSBSim.h` provides the few TinyUSB
device functions the class driver calls (`usbd_edpt_open`,
`usbd_edpt_xfer`, `tud_control_xfer`, ...) and `UVCHostSimulator` plays the
USB host and the TinyUSB core. Everything above - `UVCDevice`, the
descriptors, the class driver in `UVCTinyUSB.h`, `UVCCamera`,
`UVCVideoOutput` - is the code that runs on the microcontroller.

## What the simulator checks

Enumeration (`connect()`):
- the configuration descriptor tiles exactly (`bLength`s add up to
  `wTotalLength`), `bNumInterfaces` is right;
- the video IAD, VideoControl header (`bcdUVC` 1.10, `wTotalLength`,
  `baInterfaceNr`), VideoStreaming input header (`wTotalLength`,
  `bNumFormats`, `bTerminalLink`), the format and frame descriptors
  (sequential indexes, frame counts, interval lists, YUY2 buffer sizes) and
  the bulk endpoint;
- the class driver claims only its own two interfaces (by default a CDC
  serial function sits in front of it, as on a board with USB Serial) and
  opens the endpoint.

Negotiation (`startStream()`): GET_DEF, SET_CUR/GET_CUR of the probe and
SET_CUR of the commit control, like Linux; stopping with CLEAR_FEATURE
(ENDPOINT_HALT) like Linux or with SET_INTERFACE(0).

Streaming (`poll()`): each device transfer is split into packets of the
endpoint's max packet size and read with URBs of at most 32 packets, as the
Linux `uvcvideo` driver does. A payload ends with a short packet, a zero
length packet, or when `dwMaxPayloadTransferSize` bytes have arrived.
Frames are delimited by the FID/EOF header bits, out of sync payloads are
dropped, and YUY2 frames of the wrong size or overflowing frames are
flagged as errors. Missing zero length packets or wrong payload sizes show
up as corrupt frames.

## Running

```bash
cmake -B build -DTINYUVC_SANITIZE=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

| Test | Covers |
| --- | --- |
| `convert` | RGB/gray -> YUY2 conversion, payload encoder |
| `control` | descriptors (full/high speed, with/without CDC), probe/commit, error codes |
| `streaming` | MJPEG frames of all critical sizes (packet and payload boundaries), YUY2 from every pixel format, rejected frames, stop/restart mid-frame, failing transfers, time stamps |
| `camera` | `UVCCamera` with TinyCamera's test pattern: YUY2, MJPEG, host switching resolution and format, frame pacing |
| `audiotools` | `UVCVideoOutput`, `UVCVideoCopy`, `MJPEGEncoder` -> webcam |

The `webcam_simulation` program writes what the simulated host receives to
a file:

```bash
./build/examples/desktop/webcam_simulation --format yuy2 --size QQVGA --out cam.yuv
ffplay -f rawvideo -pixel_format yuyv422 -video_size 160x120 cam.yuv
./build/examples/desktop/webcam_simulation --source webcam --format mjpeg --out cam.mjpeg
ffplay -f mjpeg cam.mjpeg
```

`--source webcam` uses your PC's webcam through TinyCamera (V4L2 on Linux,
SDL3 on macOS/Windows), `--hs` simulates a high speed device.

## Using TinyUVC in your own CMake project

```cmake
add_subdirectory(TinyUVC)
target_link_libraries(myapp PRIVATE TinyUVC)
```
