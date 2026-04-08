#pragma once

#include <iostream>
#include <chrono>
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <limits>
#include <algorithm>
#include <ctime>

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

// Trace data structure for storing timing information
struct TraceRecord {
  std::chrono::time_point<std::chrono::high_resolution_clock> start_time;
  std::chrono::time_point<std::chrono::high_resolution_clock> end_time;
  double duration_ms;
  std::string tag;
  std::string subtag;
  std::string function_name;
  std::string file_name;
  int line_number;
};

// Report format for trace output
enum class TraceReportFormat {
  PlainText,
  JSON
};

// Trace event logging options
enum class TraceEventLogging {
  None,       // No event logging
  PlainText,  // Log events as plain text
  JSON        // Log events as JSON
};

// Trace manager for collecting and organizing trace records
class TraceManager {
 private:
  std::unordered_map<std::string, std::vector<TraceRecord>> records_by_tag_;
  std::unordered_map<std::string, bool> tag_enabled_map_;  // Per-tag enable/disable
  std::mutex mutex_;
  bool enabled_ = true;  // Global enable/disable
  TraceReportFormat report_format_ = TraceReportFormat::PlainText;  // Default to plain text
  TraceEventLogging event_logging_ = TraceEventLogging::None;  // Default to no event logging
  LogLevel event_log_level_ = LogLevel::Debug;  // Log level for trace events
  
  TraceManager() = default;
  
 public:
  static TraceManager& getInstance() {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif
    static TraceManager instance;
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    return instance;
  }
  
  TraceManager(const TraceManager&) = delete;
  TraceManager& operator=(const TraceManager&) = delete;
  
  void setEnabled(bool enabled) {
    std::lock_guard<std::mutex> lock(mutex_);
    enabled_ = enabled;
  }
  
  bool isEnabled() const {
    return enabled_;
  }
  
  // Enable/disable tracing for specific tag (and optionally subtag)
  void setTagEnabled(const std::string& tag, bool enabled, const std::string& subtag = "") {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = tag;
    if (!subtag.empty()) {
      key += "::" + subtag;
    }
    tag_enabled_map_[key] = enabled;
  }
  
  // Check if a specific tag (and optionally subtag) is enabled
  bool isTagEnabled(const std::string& tag, const std::string& subtag = "") const {
    if (!enabled_) return false;  // Global disable overrides everything
    
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    // Check for exact match with subtag
    std::string key = tag;
    if (!subtag.empty()) {
      key += "::" + subtag;
      auto it = tag_enabled_map_.find(key);
      if (it != tag_enabled_map_.end()) {
        return it->second;
      }
    }
    
    // Check for tag-level setting
    auto it = tag_enabled_map_.find(tag);
    if (it != tag_enabled_map_.end()) {
      return it->second;
    }
    
    // Default to enabled if not explicitly set
    return true;
  }
  
  // Clear all per-tag settings
  void clearTagSettings() {
    std::lock_guard<std::mutex> lock(mutex_);
    tag_enabled_map_.clear();
  }
  
  // Get all tag settings
  std::unordered_map<std::string, bool> getTagSettings() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return tag_enabled_map_;
  }
  
  // Set report format
  void setReportFormat(TraceReportFormat format) {
    std::lock_guard<std::mutex> lock(mutex_);
    report_format_ = format;
  }
  
  // Get report format
  TraceReportFormat getReportFormat() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return report_format_;
  }
  
  // Set event logging mode
  void setEventLogging(TraceEventLogging mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_logging_ = mode;
  }
  
  // Get event logging mode
  TraceEventLogging getEventLogging() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return event_logging_;
  }
  
  // Set log level for trace events
  void setEventLogLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    event_log_level_ = level;
  }
  
  // Get log level for trace events
  LogLevel getEventLogLevel() const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    return event_log_level_;
  }
  
  // Log trace begin event
  void logTraceBegin(const std::string& tag, const std::string& subtag, 
                     const std::string& function, const std::string& file, int line) {
    if (event_logging_ == TraceEventLogging::None) return;
    if (!Logger::getInstance().shouldLog(event_log_level_)) return;
    
    auto& stream = *Logger::getInstance().getStream();
    
    if (event_logging_ == TraceEventLogging::JSON) {
      stream << "{\"event\":\"trace_begin\",\"tag\":\"" << escapeJSON(tag) << "\"";
      if (!subtag.empty()) {
        stream << ",\"subtag\":\"" << escapeJSON(subtag) << "\"";
      }
      stream << ",\"function\":\"" << escapeJSON(function) << "\""
             << ",\"file\":\"" << escapeJSON(file) << "\""
             << ",\"line\":" << line
             << ",\"timestamp_ms\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()
             << "}\n";
    } else {
      stream << "[TRACE_BEGIN] " << tag;
      if (!subtag.empty()) {
        stream << "::" << subtag;
      }
      stream << " in " << function << " at " << file << ":" << line << "\n";
    }
  }
  
  // Log trace end event
  void logTraceEnd(const std::string& tag, const std::string& subtag,
                   const std::string& function, double duration_ms) {
    if (event_logging_ == TraceEventLogging::None) return;
    if (!Logger::getInstance().shouldLog(event_log_level_)) return;
    
    auto& stream = *Logger::getInstance().getStream();
    
    if (event_logging_ == TraceEventLogging::JSON) {
      stream << "{\"event\":\"trace_end\",\"tag\":\"" << escapeJSON(tag) << "\"";
      if (!subtag.empty()) {
        stream << ",\"subtag\":\"" << escapeJSON(subtag) << "\"";
      }
      stream << ",\"function\":\"" << escapeJSON(function) << "\""
             << ",\"duration_ms\":" << std::fixed << std::setprecision(3) << duration_ms
             << ",\"timestamp_ms\":" << std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::system_clock::now().time_since_epoch()).count()
             << "}\n";
    } else {
      stream << "[TRACE_END] " << tag;
      if (!subtag.empty()) {
        stream << "::" << subtag;
      }
      stream << " in " << function << " took " 
             << std::fixed << std::setprecision(3) << duration_ms << " ms\n";
    }
  }
  
  void addRecord(const TraceRecord& record) {
    if (!isTagEnabled(record.tag, record.subtag)) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    std::string key = record.tag;
    if (!record.subtag.empty()) {
      key += "::" + record.subtag;
    }
    records_by_tag_[key].push_back(record);
  }
  
  void clearRecords() {
    std::lock_guard<std::mutex> lock(mutex_);
    records_by_tag_.clear();
  }
  
  void printSummary(std::ostream& out = std::cout) {
    if (report_format_ == TraceReportFormat::JSON) {
      printSummaryJSON(out);
    } else {
      printSummaryPlainText(out);
    }
  }
  
  void printSummaryPlainText(std::ostream& out = std::cout) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    out << "\n=== Trace Summary ===\n";
    for (const auto& kv : records_by_tag_) {
      const std::string& key = kv.first;
      const std::vector<TraceRecord>& records = kv.second;
      
      double total_ms = 0.0;
      double min_ms = (std::numeric_limits<double>::max)();
      double max_ms = 0.0;
      
      for (const auto& record : records) {
        total_ms += record.duration_ms;
        min_ms = (std::min)(min_ms, record.duration_ms);
        max_ms = (std::max)(max_ms, record.duration_ms);
      }
      
      double avg_ms = records.empty() ? 0.0 : total_ms / static_cast<double>(records.size());
      
      out << std::fixed << std::setprecision(3);
      out << "Tag: " << key << "\n";
      out << "  Count: " << records.size() << "\n";
      out << "  Total: " << total_ms << " ms\n";
      out << "  Avg: " << avg_ms << " ms\n";
      out << "  Min: " << min_ms << " ms\n";
      out << "  Max: " << max_ms << " ms\n";
    }
    out << "====================\n";
  }
  
  void printSummaryJSON(std::ostream& out = std::cout) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    out << "{\n";
    out << "  \"trace_summary\": {\n";
    
    bool first_tag = true;
    for (const auto& kv : records_by_tag_) {
      const std::string& key = kv.first;
      const std::vector<TraceRecord>& records = kv.second;
      
      double total_ms = 0.0;
      double min_ms = (std::numeric_limits<double>::max)();
      double max_ms = 0.0;
      
      for (const auto& record : records) {
        total_ms += record.duration_ms;
        min_ms = (std::min)(min_ms, record.duration_ms);
        max_ms = (std::max)(max_ms, record.duration_ms);
      }
      
      double avg_ms = records.empty() ? 0.0 : total_ms / static_cast<double>(records.size());
      
      if (!first_tag) {
        out << ",\n";
      }
      first_tag = false;
      
      out << std::fixed << std::setprecision(3);
      out << "    \"" << escapeJSON(key) << "\": {\n";
      out << "      \"count\": " << records.size() << ",\n";
      out << "      \"total_ms\": " << total_ms << ",\n";
      out << "      \"avg_ms\": " << avg_ms << ",\n";
      out << "      \"min_ms\": " << min_ms << ",\n";
      out << "      \"max_ms\": " << max_ms << "\n";
      out << "    }";
    }
    
    out << "\n  }\n";
    out << "}\n";
  }
  
  // Get detailed records in JSON format
  void printDetailedJSON(std::ostream& out = std::cout) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    out << "{\n";
    out << "  \"trace_records\": [\n";
    
    bool first_record = true;
    for (const auto& kv : records_by_tag_) {
      const std::vector<TraceRecord>& records = kv.second;
      
      for (const auto& record : records) {
        if (!first_record) {
          out << ",\n";
        }
        first_record = false;
        
        out << std::fixed << std::setprecision(3);
        out << "    {\n";
        out << "      \"tag\": \"" << escapeJSON(record.tag) << "\",\n";
        out << "      \"subtag\": \"" << escapeJSON(record.subtag) << "\",\n";
        out << "      \"duration_ms\": " << record.duration_ms << ",\n";
        out << "      \"function\": \"" << escapeJSON(record.function_name) << "\",\n";
        out << "      \"file\": \"" << escapeJSON(record.file_name) << "\",\n";
        out << "      \"line\": " << record.line_number << ",\n";

        // Format timestamp as ISO 8601 string
#ifndef __EMSCRIPTEN__
        // Emscripten has issues with time_point duration conversion
        // Use duration_cast to handle different clock duration types
        auto start_time_t = std::chrono::system_clock::to_time_t(
          std::chrono::system_clock::now() +
          std::chrono::duration_cast<std::chrono::system_clock::duration>(
            record.start_time - std::chrono::high_resolution_clock::now()));

        std::stringstream timestamp_ss;
        timestamp_ss << std::put_time(std::gmtime(&start_time_t), "%Y-%m-%dT%H:%M:%S");

        out << "      \"timestamp\": \"" << timestamp_ss.str() << "Z\"\n";
#else
        // Simplified timestamp for WASM builds
        auto now = std::chrono::system_clock::now();
        auto start_time_t = std::chrono::system_clock::to_time_t(now);
        std::stringstream timestamp_ss;
        timestamp_ss << std::put_time(std::gmtime(&start_time_t), "%Y-%m-%dT%H:%M:%S");
        out << "      \"timestamp\": \"" << timestamp_ss.str() << "Z\"\n";
#endif
        out << "    }";
      }
    }
    
    out << "\n  ]\n";
    out << "}\n";
  }
  
 private:
  // Helper function to escape special characters in JSON strings
  static std::string escapeJSON(const std::string& str) {
    std::stringstream ss;
    for (char ch : str) {
      unsigned char c = static_cast<unsigned char>(ch);
      switch (c) {
        case '"': ss << "\\\""; break;
        case '\\': ss << "\\\\"; break;
        case '\b': ss << "\\b"; break;
        case '\f': ss << "\\f"; break;
        case '\n': ss << "\\n"; break;
        case '\r': ss << "\\r"; break;
        case '\t': ss << "\\t"; break;
        default:
          if (c >= 0x20 && c <= 0x7E) {
            ss << c;
          } else {
            // Control characters and non-ASCII
            ss << "\\u" << std::hex << std::setw(4) << std::setfill('0') 
               << static_cast<int>(c);
          }
      }
    }
    return ss.str();
  }
  
 public:
  
  std::vector<TraceRecord> getRecords(const std::string& tag, 
                                      const std::string& subtag = "") const {
    std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(mutex_));
    
    std::string key = tag;
    if (!subtag.empty()) {
      key += "::" + subtag;
    }
    
    auto it = records_by_tag_.find(key);
    if (it != records_by_tag_.end()) {
      return it->second;
    }
    return {};
  }
};

// RAII Trace class for automatic timing
class Trace {
 private:
  TraceRecord record_;
  bool enabled_;
  
 public:
  Trace(const std::string& tag, 
        const std::string& subtag = "",
        const char* function = "",
        const char* file = "",
        int line = 0)
      : enabled_(TraceManager::getInstance().isTagEnabled(tag, subtag)) {
    if (!enabled_) return;
    
    record_.tag = tag;
    record_.subtag = subtag;
    record_.function_name = function;
    record_.file_name = file;
    record_.line_number = line;
    record_.start_time = std::chrono::high_resolution_clock::now();
    
    // Log trace begin event
    TraceManager::getInstance().logTraceBegin(tag, subtag, function, file, line);
  }
  
  ~Trace() {
    if (!enabled_) return;
    
    record_.end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        record_.end_time - record_.start_time);
    record_.duration_ms = static_cast<double>(duration.count()) / 1000.0;
    
    // Log trace end event
    TraceManager::getInstance().logTraceEnd(record_.tag, record_.subtag, 
                                            record_.function_name, record_.duration_ms);
    
    TraceManager::getInstance().addRecord(record_);
  }
  
  // Disable copy/move to ensure proper RAII behavior
  Trace(const Trace&) = delete;
  Trace& operator=(const Trace&) = delete;
  Trace(Trace&&) = delete;
  Trace& operator=(Trace&&) = delete;
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

// Trace macros for function timing
#if defined(__wasi__)
#define TUSDZ_TRACE(tag)
#define TUSDZ_TRACE_TAG(tag, subtag)
#else
// Basic trace macro with just tag
#define TUSDZ_TRACE(tag) \
  tinyusdz::logging::Trace _trace_##__LINE__(tag, "", __func__, __FILE__, __LINE__)

// Trace macro with tag and subtag for nested tracing
#define TUSDZ_TRACE_TAG(tag, subtag) \
  tinyusdz::logging::Trace _trace_##__LINE__(tag, subtag, __func__, __FILE__, __LINE__)
#endif

// Helper macros for common trace operations
#define TUSDZ_TRACE_ENABLE() \
  tinyusdz::logging::TraceManager::getInstance().setEnabled(true)

#define TUSDZ_TRACE_DISABLE() \
  tinyusdz::logging::TraceManager::getInstance().setEnabled(false)

#define TUSDZ_TRACE_CLEAR() \
  tinyusdz::logging::TraceManager::getInstance().clearRecords()

#define TUSDZ_TRACE_SUMMARY() \
  tinyusdz::logging::TraceManager::getInstance().printSummary()

#define TUSDZ_TRACE_SUMMARY_TO(stream) \
  tinyusdz::logging::TraceManager::getInstance().printSummary(stream)

// Per-tag enable/disable macros
#define TUSDZ_TRACE_ENABLE_TAG(tag) \
  tinyusdz::logging::TraceManager::getInstance().setTagEnabled(tag, true)

#define TUSDZ_TRACE_DISABLE_TAG(tag) \
  tinyusdz::logging::TraceManager::getInstance().setTagEnabled(tag, false)

#define TUSDZ_TRACE_ENABLE_TAG_SUBTAG(tag, subtag) \
  tinyusdz::logging::TraceManager::getInstance().setTagEnabled(tag, true, subtag)

#define TUSDZ_TRACE_DISABLE_TAG_SUBTAG(tag, subtag) \
  tinyusdz::logging::TraceManager::getInstance().setTagEnabled(tag, false, subtag)

#define TUSDZ_TRACE_CLEAR_TAG_SETTINGS() \
  tinyusdz::logging::TraceManager::getInstance().clearTagSettings()

// Report format macros
#define TUSDZ_TRACE_SET_FORMAT_JSON() \
  tinyusdz::logging::TraceManager::getInstance().setReportFormat(tinyusdz::logging::TraceReportFormat::JSON)

#define TUSDZ_TRACE_SET_FORMAT_TEXT() \
  tinyusdz::logging::TraceManager::getInstance().setReportFormat(tinyusdz::logging::TraceReportFormat::PlainText)

#define TUSDZ_TRACE_SUMMARY_JSON() \
  do { \
    tinyusdz::logging::TraceManager::getInstance().setReportFormat(tinyusdz::logging::TraceReportFormat::JSON); \
    tinyusdz::logging::TraceManager::getInstance().printSummary(); \
  } while(false)

#define TUSDZ_TRACE_SUMMARY_JSON_TO(stream) \
  do { \
    tinyusdz::logging::TraceManager::getInstance().setReportFormat(tinyusdz::logging::TraceReportFormat::JSON); \
    tinyusdz::logging::TraceManager::getInstance().printSummary(stream); \
  } while(false)

#define TUSDZ_TRACE_DETAILED_JSON() \
  tinyusdz::logging::TraceManager::getInstance().printDetailedJSON()

#define TUSDZ_TRACE_DETAILED_JSON_TO(stream) \
  tinyusdz::logging::TraceManager::getInstance().printDetailedJSON(stream)

// Event logging macros
#define TUSDZ_TRACE_SET_EVENT_LOGGING_NONE() \
  tinyusdz::logging::TraceManager::getInstance().setEventLogging(tinyusdz::logging::TraceEventLogging::None)

#define TUSDZ_TRACE_SET_EVENT_LOGGING_TEXT() \
  tinyusdz::logging::TraceManager::getInstance().setEventLogging(tinyusdz::logging::TraceEventLogging::PlainText)

#define TUSDZ_TRACE_SET_EVENT_LOGGING_JSON() \
  tinyusdz::logging::TraceManager::getInstance().setEventLogging(tinyusdz::logging::TraceEventLogging::JSON)

#define TUSDZ_TRACE_SET_EVENT_LOG_LEVEL(level) \
  tinyusdz::logging::TraceManager::getInstance().setEventLogLevel(tinyusdz::logging::LogLevel::level)

// Global flag to control DCOUT output. Set via TINYUSDZ_ENABLE_DCOUT environment variable.
namespace tinyusdz {
extern bool g_enable_dcout_output;
}

// DCOUT macro for debug output (requires TINYUSDZ_DEBUG_PRINT to be defined during compilation)
#if !defined(TINYUSDZ_PRODUCTION_BUILD) && !defined(TINYUSDZ_FUZZER_BUILD)
#if defined(TINYUSDZ_DEBUG_PRINT)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
// DCOUT macro - accepts iostream expressions with << operator
// Example usage: DCOUT("ptr = " << std::hex << ptr)
#define DCOUT(x)                                               \
  do {                                                         \
    if (tinyusdz::g_enable_dcout_output) {                     \
      std::ostringstream dcout_ss;                             \
      dcout_ss << x;                                           \
      std::cout << __FILE__ << ":" << __func__ << ":"          \
                << std::to_string(__LINE__) << " "             \
                << dcout_ss.str() << "\n";                     \
    }                                                          \
  } while (false)
#else
#define DCOUT(x)
#endif

#undef TINYUSDZ_LOCAL_DEBUG_PRINT
