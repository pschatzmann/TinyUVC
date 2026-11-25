// TinyUVC.h included from two translation units (with test_multi_tu_b.cpp):
// must link, with exactly one usbd_app_driver_get_cb() (defined here)
#include "TestUtils.h"
#include "TinyUVC.h"

// implemented in test_multi_tu_b.cpp, which defines TINYUVC_NO_APP_DRIVER
bool configureInOtherFile(UVCDevice &uvc);

static void testTwoTranslationUnits() {
  UVCDevice uvc;
  CHECK(configureInOtherFile(uvc));
  CHECK(uvc.begin());
  UVCHostSimulator host;
  CHECK(host.connect(uvc));
  CHECK(host.startStream(UVCFormat::MJPEG, 320, 240));
  CHECK(uvc.isStreaming());
}

int main() {
  RUN(testTwoTranslationUnits);
  return testMain();
}
