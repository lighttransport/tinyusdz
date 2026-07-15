// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
#pragma once

#include <iostream>
#include <sstream>
#include <string>

namespace tinyusdz {
namespace logging {

///
/// Log severity levels, ordered from least to most severe.
/// The numeric values must increase with severity so that
/// shouldLog(msgLevel) == (msgLevel >= _level) works correctly.
///
enum class LogLevel {
  Debug    = 0,
  Info     = 1,
  Warn     = 2,
  Error    = 3,
  Critical = 4,
  Off      = 5
};

///
/// Simple singleton logger.
/// Default threshold is Warn, so only Warn, Error and Critical messages are
/// printed unless the caller explicitly lowers the level.
///
class Logger {
 public:
  static Logger &getInstance() {
    static Logger instance;
    return instance;
  }

  void setLogLevel(LogLevel level) { _level = level; }
  LogLevel getLogLevel() const { return _level; }

  bool shouldLog(LogLevel msgLevel) const {
    return static_cast<int>(msgLevel) >= static_cast<int>(_level);
  }

  void log(LogLevel msgLevel, const std::string &msg) {
    if (!shouldLog(msgLevel)) {
      return;
    }
    std::string prefix;
    switch (msgLevel) {
      case LogLevel::Debug:    prefix = "[DEBUG] ";    break;
      case LogLevel::Info:     prefix = "[INFO] ";     break;
      case LogLevel::Warn:     prefix = "[WARN] ";     break;
      case LogLevel::Error:    prefix = "[ERROR] ";    break;
      case LogLevel::Critical: prefix = "[CRITICAL] "; break;
      default:                 prefix = "[LOG] ";      break;
    }
    std::cerr << prefix << msg << "\n";
  }

 private:
  Logger() : _level(LogLevel::Warn) {}
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;

  LogLevel _level;
};

}  // namespace logging
}  // namespace tinyusdz

// ---------------------------------------------------------------------------
// Logging macros
//
// Usage:
//   TUSDZ_LOG_W("something went wrong: " << value);
//
// Each macro builds a message via a stringstream and forwards it to the
// singleton Logger only when the current threshold allows it.
// ---------------------------------------------------------------------------

#if defined(TINYUSDZ_PRODUCTION_BUILD)
// In production builds suppress file paths for privacy; show only
// function name and line number.
#define TUSDZ_LOG_MSG_(level, msg)                                       \
  do {                                                                   \
    if (::tinyusdz::logging::Logger::getInstance().shouldLog(level)) {  \
      std::ostringstream _tusdz_ss_;                                     \
      _tusdz_ss_ << __func__ << ":" << __LINE__ << " " << msg;          \
      ::tinyusdz::logging::Logger::getInstance().log(level,             \
                                                     _tusdz_ss_.str()); \
    }                                                                    \
  } while (0)
#else
#define TUSDZ_LOG_MSG_(level, msg)                                       \
  do {                                                                   \
    if (::tinyusdz::logging::Logger::getInstance().shouldLog(level)) {  \
      std::ostringstream _tusdz_ss_;                                     \
      _tusdz_ss_ << __FILE__ << ":" << __func__ << ":" << __LINE__      \
                 << " " << msg;                                          \
      ::tinyusdz::logging::Logger::getInstance().log(level,             \
                                                     _tusdz_ss_.str()); \
    }                                                                    \
  } while (0)
#endif

#define TUSDZ_LOG_D(msg) \
  TUSDZ_LOG_MSG_(::tinyusdz::logging::LogLevel::Debug, msg)

#define TUSDZ_LOG_I(msg) \
  TUSDZ_LOG_MSG_(::tinyusdz::logging::LogLevel::Info, msg)

#define TUSDZ_LOG_W(msg) \
  TUSDZ_LOG_MSG_(::tinyusdz::logging::LogLevel::Warn, msg)

#define TUSDZ_LOG_E(msg) \
  TUSDZ_LOG_MSG_(::tinyusdz::logging::LogLevel::Error, msg)

#define TUSDZ_LOG_C(msg) \
  TUSDZ_LOG_MSG_(::tinyusdz::logging::LogLevel::Critical, msg)
