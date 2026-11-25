#pragma once
/**
 * @file UVCLogger.h
 * @brief Minimal printf-style logger. On Arduino it writes to a Print
 * (default: Serial), on the desktop to stderr.
 */
#include <stdarg.h>
#include <stdio.h>

#include "UVCConfig.h"

#if defined(ARDUINO) && !defined(TINYUVC_DESKTOP)
#include <Arduino.h>
#endif

namespace tinyuvc {

/// Log levels of UVCLogger
enum class UVCLogLevel : int { None = 0, Error = 1, Warning = 2, Info = 3, Debug = 4 };

/**
 * @brief Logger used by all TinyUVC classes.
 * @code
 * UVCLog.begin(Serial, UVCLogLevel::Info);
 * @endcode
 */
class UVCLoggerClass {
 public:
#if defined(ARDUINO) && !defined(TINYUVC_DESKTOP)
  /// Selects the output and the level
  void begin(Print &out, UVCLogLevel level) {
    out_ = &out;
    level_ = level;
  }
#endif
  /// Changes the level only
  void setLevel(UVCLogLevel level) { level_ = level; }
  UVCLogLevel level() const { return level_; }
  bool isActive(UVCLogLevel level) const {
    return level_ != UVCLogLevel::None && (int)level <= (int)level_;
  }

  void log(UVCLogLevel level, const char *fmt, ...) {
    if (!isActive(level)) return;
    char msg[160];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);
    static const char *const names[] = {"", "Error", "Warning", "Info",
                                        "Debug"};
#if defined(ARDUINO) && !defined(TINYUVC_DESKTOP)
    Print *out = out_ != nullptr ? out_ : &Serial;
    out->print("[TinyUVC] ");
    out->print(names[(int)level]);
    out->print(": ");
    out->println(msg);
#else
    fprintf(stderr, "[TinyUVC] %s: %s\n", names[(int)level], msg);
#endif
  }

 private:
  UVCLogLevel level_ = (UVCLogLevel)TINYUVC_LOG_LEVEL;
#if defined(ARDUINO) && !defined(TINYUVC_DESKTOP)
  Print *out_ = nullptr;
#endif
};

/// The global logger instance
inline UVCLoggerClass UVCLog;

}  // namespace tinyuvc

#define TINYUVC_LOGE(...) \
  ::tinyuvc::UVCLog.log(::tinyuvc::UVCLogLevel::Error, __VA_ARGS__)
#define TINYUVC_LOGW(...) \
  ::tinyuvc::UVCLog.log(::tinyuvc::UVCLogLevel::Warning, __VA_ARGS__)
#define TINYUVC_LOGI(...) \
  ::tinyuvc::UVCLog.log(::tinyuvc::UVCLogLevel::Info, __VA_ARGS__)
#define TINYUVC_LOGD(...) \
  ::tinyuvc::UVCLog.log(::tinyuvc::UVCLogLevel::Debug, __VA_ARGS__)
