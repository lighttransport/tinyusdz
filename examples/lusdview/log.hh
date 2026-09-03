// SPDX-License-Identifier: Apache-2.0
// lusdview - a tiny leveled logger (writes to stderr). Header-only.
//
// Use the LOG* macros (printf-style, format-checked by the compiler):
//   LOGI("loaded %s", path);     // info  -> "[lusdview] loaded ..."
//   LOGW("texture skipped");     // warn  -> "[lusdview][warn] texture skipped"
//   LOGE("init failed: %s", e);  // error -> "[lusdview][error] init failed: ..."
//   LOGD("rtSupported_=%d", n);  // debug -> only shown when the level is Debug
//
// The minimum level defaults to Info. Override with the env var
//   LUSDVIEW_LOG=debug|info|warn|error
// (LUSDVIEW_RT_DEBUG=1 is honored as a shortcut for debug for ray-tracing work).
#pragma once

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace lusdview {

enum class LogLevel { Debug = 0, Info = 1, Warn = 2, Error = 3 };

// Current minimum level (lazily initialized from the environment on first use).
inline LogLevel& logLevelRef() {
  static LogLevel level = [] {
    if (const char* e = std::getenv("LUSDVIEW_LOG")) {
      if (!std::strcmp(e, "debug")) return LogLevel::Debug;
      if (!std::strcmp(e, "info")) return LogLevel::Info;
      if (!std::strcmp(e, "warn") || !std::strcmp(e, "warning")) return LogLevel::Warn;
      if (!std::strcmp(e, "error")) return LogLevel::Error;
    }
    if (const char* rt = std::getenv("LUSDVIEW_RT_DEBUG")) {
      if (rt[0] && std::strcmp(rt, "0") != 0) return LogLevel::Debug;
    }
    return LogLevel::Info;
  }();
  return level;
}

inline void setLogLevel(LogLevel lvl) { logLevelRef() = lvl; }

#if defined(__GNUC__) || defined(__clang__)
__attribute__((format(printf, 2, 3)))
#endif
inline void
logMessage(LogLevel lvl, const char* fmt, ...) {
  if (static_cast<int>(lvl) < static_cast<int>(logLevelRef())) return;
  const char* prefix = "[lusdview] ";
  switch (lvl) {
    case LogLevel::Debug: prefix = "[lusdview][debug] "; break;
    case LogLevel::Warn: prefix = "[lusdview][warn] "; break;
    case LogLevel::Error: prefix = "[lusdview][error] "; break;
    case LogLevel::Info:
    default: prefix = "[lusdview] "; break;
  }
  std::fputs(prefix, stderr);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);
  std::fputc('\n', stderr);
}

}  // namespace lusdview

#define LOGD(...) ::lusdview::logMessage(::lusdview::LogLevel::Debug, __VA_ARGS__)
#define LOGI(...) ::lusdview::logMessage(::lusdview::LogLevel::Info, __VA_ARGS__)
#define LOGW(...) ::lusdview::logMessage(::lusdview::LogLevel::Warn, __VA_ARGS__)
#define LOGE(...) ::lusdview::logMessage(::lusdview::LogLevel::Error, __VA_ARGS__)
