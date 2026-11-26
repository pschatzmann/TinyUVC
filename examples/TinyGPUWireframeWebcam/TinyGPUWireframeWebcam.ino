/**
 * @file TinyGPUWireframeWebcam.ino
 * @brief The rotating wireframe cube of TinyGPU
 * (https://github.com/pschatzmann/TinyGPU, wireframe-cube example) as USB
 * webcam picture: the cube is rendered into a TinyGPU framebuffer and
 * UVCDisplayDriver sends it to the PC instead of to a display. Open the
 * camera with e.g. guvcview, Cheese, VLC or `ffplay /dev/video0`.
 *
 * The host chooses the resolution and the framebuffer follows its choice.
 * - ESP32-S3 with PSRAM: MJPEG 640x480 and 320x240 (compressed with
 *   https://github.com/pschatzmann/TinyJPEG), YUY2 160x120
 * - RP2040: YUY2 160x120 and 176x144 (264 KB RAM: framebuffer, its copy and
 *   the depth buffer of WireFrame3D need 8 bytes per pixel)
 *
 * Board settings: see TestPatternWebcam; ESP32-S3: PSRAM > "OPI PSRAM" (or
 * "QSPI PSRAM", depending on your board).
 */
#define JE_MAX_WIDTH 640      // widest MJPEG frame
#include "TinyJPEGEncoder.h"  // enables MJPEG
#include "TinyUVC/TinyGPU.h"

#if defined(ARDUINO_ARCH_ESP32)
USBCDC USBSerial;  // USB serial port in addition to the camera
#define LOG USBSerial
#else
#define LOG Serial
#endif

UVCDevice uvc;
UVCDisplayDriver<RGB565> display(uvc);

FrameBufferRGB565 framebuffer(160, 120, FontRGB565);
WireFrame3D_RGB565 wireframe(framebuffer);
auto cubeMesh = WireFrame3D_RGB565::cube(2.0f);
float angle = 0.0f;

void setup() {
  LOG.begin(115200);
  UVCLog.begin(LOG, UVCLogLevel::Info);

#if TINYUVC_HAS_JPEG && (defined(ARDUINO_ARCH_ESP32) || defined(RP2350_PSRAM_CS))
  // compressed: big pictures at a good frame rate (first = default)
  display.addResolution(UVCFormat::MJPEG, 640, 480, {10, 5});
  display.addResolution(UVCFormat::MJPEG, 320, 240, {15, 10});
#endif
  // uncompressed: full speed USB carries about 1 MB/s
  display.addResolution(UVCFormat::YUY2, 160, 120, {15, 10});
#if defined(ARDUINO_ARCH_RP2040)
  display.addResolution(UVCFormat::YUY2, 176, 144, {15, 10});
#endif
  if (!display.begin()) LOG.println("display.begin() failed");

  framebuffer.begin();
  wireframe.begin();
  wireframe.setPerspective(60.0f, 0.1f, 100.0f);
  WireFrame3D_RGB565::Camera cam;
  cam.position = {0.0f, 0.0f, 5.0f};
  cam.target = {0.0f, 0.0f, 0.0f};
  cam.up = {0.0f, 1.0f, 0.0f};
  wireframe.setCamera(cam);
}

void loop() {
  if (!uvc.isStreaming()) {
    delay(10);  // nobody is watching
    return;
  }
  // follow the resolution selected by the host
  if (display.sizeChanged(framebuffer.width(), framebuffer.height())) {
    if (!framebuffer.resize(display.width(), display.height())) {
      LOG.println("not enough memory for this resolution");
      delay(1000);
      return;
    }
    // new viewport: resize the depth buffer (begin()) and the projection
    wireframe.setViewport(display.width(), display.height());
    wireframe.begin();
    wireframe.setPerspective(60.0f, 0.1f, 100.0f);
    LOG.printf("%s %dx%d\n", display.isJpeg() ? "MJPEG" : "YUY2",
               (int)display.width(), (int)display.height());
  }

  uint32_t t0 = micros();
  framebuffer.clear(RGB565(255, 255, 255));

  auto printer = framebuffer.linePrinter();
  printer.setColor(RGB565(0, 0, 255));
  printer.setScale(framebuffer.width() / 160);
  printer.print("TinyUVC + TinyGPU");

  auto model = WireFrame3D_RGB565::rotationY(angle) *
               WireFrame3D_RGB565::rotationX(angle * 0.7f);
  wireframe.renderWireframe(framebuffer, cubeMesh, model, RGB565(0, 0, 0));

  // waits for the frame interval selected by the host, then sends a copy
  // (or JPEG) of the framebuffer: the next frame is rendered while this one
  // is on the wire
  uint32_t t1 = micros();
  display.writeData(framebuffer);
  uint32_t t2 = micros();

  // statistics every 5 seconds
  static uint32_t lastLog = millis(), renderUs = 0, writeUs = 0, n = 0;
  renderUs += t1 - t0;
  writeUs += t2 - t1;
  n++;
  if (millis() - lastLog >= 5000) {
    LOG.printf("%.1f fps, render %u ms, writeData %u ms, jpeg %u bytes\n",
               n * 1000.0f / (millis() - lastLog), (unsigned)(renderUs / n / 1000),
               (unsigned)(writeUs / n / 1000), (unsigned)display.lastJpegSize());
    lastLog = millis();
    renderUs = writeUs = n = 0;
  }

  angle += 0.05f;
}
