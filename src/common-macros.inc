#pragma once

#include <iostream>
#include <sstream>
#include <string>

#if !defined(TINYUSDZ_PRODUCTION_BUILD) && !defined(TINYUSDZ_FUZZER_BUILD)
#if defined(TINYUSDZ_DEBUG_PRINT)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif
#endif

// Simple logging management class
namespace tinyusdz {
namespace logging {

enum class LogLevel {
  Debug = 0,
  Info = 1,
  Warn = 2,
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

#if defined(TINYUSDZ_PRODUCTION_BUILD)
// Do not include full filepath for privacy.

#define PUSH_ERROR_AND_RETURN(s)                         \
  do {                                                   \
    std::ostringstream ss_e;                             \
    ss_e << __func__ << "():" << __LINE__ << " ";       \
    ss_e << s;                                           \
    PushError(ss_e.str());                               \
    return false;                                        \
  } while (0)

#define PUSH_ERROR_AND_RETURN_TAG(tag, s)                                    \
  do {                                                                       \
    std::ostringstream ss_e;                                                 \
    ss_e << tag << ":" << __func__ << "():" << __LINE__ << " ";              \
    ss_e << s;                                                               \
    PushError(ss_e.str());                                                   \
    return false;                                                            \
  } while (0)

#define PUSH_ERROR(s)                                    \
  do {                                                   \
    std::ostringstream ss_e;                             \
    ss_e << __func__ << "():" << __LINE__ << " ";       \
    ss_e << s;                                           \
    PushError(ss_e.str());                               \
  } while (0)

#define PUSH_WARN(s)                                     \
  do {                                                   \
    std::ostringstream ss_w;                             \
    ss_w << __func__ << "():" << __LINE__ << " ";       \
    ss_w << s;                                           \
    PushWarn(ss_w.str());                                \
  } while (0)

#else  // TINYUSDZ_PRODUCTION_BUILD

#define PUSH_ERROR_AND_RETURN(s)                                          \
  do {                                                                    \
    std::ostringstream ss_e;                                              \
    ss_e << __FILE__ << ":" << __func__ << "():" << __LINE__              \
         << " ";                                                          \
    ss_e << s;                                                            \
    PushError(ss_e.str());                                                \
    return false;                                                         \
  } while (0)

#define PUSH_ERROR_AND_RETURN_TAG(tag, s)                   \
  do {                                                      \
    std::ostringstream ss_e;                                \
    ss_e << __FILE__ << tag << ":" << __func__             \
         << "():" << __LINE__ << " ";                       \
    ss_e << s;                                              \
    PushError(ss_e.str());                                  \
    return false;                                           \
  } while (0)

#define PUSH_ERROR(s)                                                     \
  do {                                                                    \
    std::ostringstream ss_e;                                              \
    ss_e << __FILE__ << ":" << __func__ << "():" << __LINE__              \
         << " ";                                                          \
    ss_e << s;                                                            \
    PushError(ss_e.str());                                                \
  } while (0)

#define PUSH_WARN(s)                                                     \
  do {                                                                   \
    std::ostringstream ss_w;                                             \
    ss_w << __FILE__ << ":" << __func__ << "():" << __LINE__              \
         << " ";                                                         \
    ss_w << s;                                                           \
    PushWarn(ss_w.str());                                                \
  } while (0)

#endif  // TINYUSDZ_PRODUCTION_BUILD

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

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

// Simple auto-free class
// Use this class when saving stack size is required(e.g. recursive function call).
// T must have default constructor
template<typename T>
class AutoFree {
 private:
  T *_v{nullptr}; 

 public:

  AutoFree() : _v(new T()) { 
  }

  ~AutoFree() {
    delete _v;
  }

  T &value() { return *_v; }
};
