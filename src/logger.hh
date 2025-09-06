#pragma once

#include <iostream>

// Simple logging management class
namespace tinyusdz {
namespace logging {

enum class LogLevel {
  Debug = 0,
  Warn = 1,
  Info = 2,
  Error = 3,
  Critical = 4,
  Off = 5
};

class Logger {
 private:
  LogLevel _level = LogLevel::Warn;  // Default to Warn level
  std::ostream* _stream = &std::cout;  // Default to std::cout
  
  // Private constructor for singleton
  Logger() = default;
  
 public:
  // Singleton instance
  static Logger& getInstance() {
    static Logger instance;
    return instance;
  }
  
  // Delete copy constructor and assignment operator
  Logger(const Logger&) = delete;
  Logger& operator=(const Logger&) = delete;
  
  void setLogLevel(LogLevel level) {
    _level = level;
  }
  
  LogLevel getLogLevel() const {
    return _level;
  }
  
  void setStream(std::ostream* stream) {
    if (stream) {
      _stream = stream;
    }
  }
  
  std::ostream* getStream() const {
    return _stream;
  }
  
  bool shouldLog(LogLevel msgLevel) const {
    return static_cast<int>(msgLevel) >= static_cast<int>(_level);
  }
  
  // Helper to get level name
  static const char* getLevelName(LogLevel level) {
    switch (level) {
      case LogLevel::Debug: return "DEBUG";
      case LogLevel::Info: return "INFO";
      case LogLevel::Warn: return "WARN";
      case LogLevel::Error: return "ERROR";
      case LogLevel::Critical: return "CRITICAL";
      case LogLevel::Off: return "UNKNOWN";
    }
  }
};

} // namespace logging
} // namespace tinyusdz

// TUSDZ_LOG_I: Log at INFO level (unless building with WASI)
#if defined(__wasi__)
#define TUSDZ_LOG_I(x)
#else
#define TUSDZ_LOG_I(x)                                                          \
  do {                                                                          \
    if (tinyusdz::logging::Logger::getInstance().shouldLog(                    \
            tinyusdz::logging::LogLevel::Info)) {                              \
      (*tinyusdz::logging::Logger::getInstance().getStream())                  \
          << "[INFO] " << __FILE__ << ":" << __func__ << ":"                   \
          << std::to_string(__LINE__) << " " << x << "\n";                     \
    }                                                                           \
  } while (false)
#endif

// Additional log macros for other levels
#if defined(__wasi__)
#define TUSDZ_LOG_D(x)
#define TUSDZ_LOG_W(x)
#define TUSDZ_LOG_E(x)
#define TUSDZ_LOG_C(x)
#else
#define TUSDZ_LOG_D(x)                                                          \
  do {                                                                          \
    if (tinyusdz::logging::Logger::getInstance().shouldLog(                    \
            tinyusdz::logging::LogLevel::Debug)) {                             \
      (*tinyusdz::logging::Logger::getInstance().getStream())                  \
          << "[DEBUG] " << __FILE__ << ":" << __func__ << ":"                  \
          << std::to_string(__LINE__) << " " << x << "\n";                     \
    }                                                                           \
  } while (false)

#define TUSDZ_LOG_W(x)                                                          \
  do {                                                                          \
    if (tinyusdz::logging::Logger::getInstance().shouldLog(                    \
            tinyusdz::logging::LogLevel::Warn)) {                              \
      (*tinyusdz::logging::Logger::getInstance().getStream())                  \
          << "[WARN] " << __FILE__ << ":" << __func__ << ":"                   \
          << std::to_string(__LINE__) << " " << x << "\n";                     \
    }                                                                           \
  } while (false)

#define TUSDZ_LOG_E(x)                                                          \
  do {                                                                          \
    if (tinyusdz::logging::Logger::getInstance().shouldLog(                    \
            tinyusdz::logging::LogLevel::Error)) {                             \
      (*tinyusdz::logging::Logger::getInstance().getStream())                  \
          << "[ERROR] " << __FILE__ << ":" << __func__ << ":"                  \
          << std::to_string(__LINE__) << " " << x << "\n";                     \
    }                                                                           \
  } while (false)

#define TUSDZ_LOG_C(x)                                                          \
  do {                                                                          \
    if (tinyusdz::logging::Logger::getInstance().shouldLog(                    \
            tinyusdz::logging::LogLevel::Critical)) {                          \
      (*tinyusdz::logging::Logger::getInstance().getStream())                  \
          << "[CRITICAL] " << __FILE__ << ":" << __func__ << ":"               \
          << std::to_string(__LINE__) << " " << x << "\n";                     \
    }                                                                           \
  } while (false)
#endif
