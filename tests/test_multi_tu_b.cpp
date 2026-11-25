// Second translation unit of test_multi_tu: TinyUVC's TinyUSB driver hook
// is defined only once (in test_multi_tu.cpp)
#define TINYUVC_NO_APP_DRIVER
#include "TinyUVC.h"

bool configureInOtherFile(UVCDevice &uvc) {
  return uvc.addResolution(UVCFormat::MJPEG, 320, 240, 15);
}
