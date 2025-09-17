// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Error handling and reporting module for USD ASCII parser
#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <cstdint>
#include <cstddef>

namespace tinyusdz {
namespace ascii {

// Error severity levels
enum class ErrorLevel {
  INFO,
  WARNING,
  ERROR,
  FATAL
};

// Error location information
struct ErrorLocation {
  uint64_t line;
  uint64_t column;
  uint64_t byte_offset;
  std::string filename;
  
  ErrorLocation() : line(0), column(0), byte_offset(0) {}
  ErrorLocation(uint64_t l, uint64_t c, uint64_t off, const std::string& f = "")
      : line(l), column(c), byte_offset(off), filename(f) {}
};

// Parse error with location and context
struct ParseError {
  ErrorLevel level;
  std::string message;
  ErrorLocation location;
  std::string context;  // Line of code where error occurred
  std::string suggestion;  // Optional suggestion for fixing
  
  ParseError(ErrorLevel l, const std::string& msg, const ErrorLocation& loc)
      : level(l), message(msg), location(loc) {}
};

///
/// Error handler for USD ASCII parser.
/// Manages error collection, formatting, and reporting with context.
///
class AsciiErrorHandler {
 public:
  AsciiErrorHandler() : max_errors_(100), error_count_(0), warning_count_(0) {}
  
  // Configuration
  void SetMaxErrors(size_t max) { max_errors_ = max; }
  void SetFilename(const std::string& filename) { current_filename_ = filename; }
  void SetVerbose(bool verbose) { verbose_ = verbose; }
  
  // Error reporting
  void PushError(const std::string& msg);
  void PushError(const std::string& msg, const ErrorLocation& loc);
  void PushError(const ParseError& error);
  
  // Warning reporting
  void PushWarning(const std::string& msg);
  void PushWarning(const std::string& msg, const ErrorLocation& loc);
  
  // Info reporting (verbose mode only)
  void PushInfo(const std::string& msg);
  void PushInfo(const std::string& msg, const ErrorLocation& loc);
  
  // Error recovery
  void RecoverFromError();
  bool CanContinue() const { return error_count_ < max_errors_; }
  bool HasErrors() const { return error_count_ > 0; }
  bool HasWarnings() const { return warning_count_ > 0; }
  
  // Formatted error output
  std::string GetFormattedErrors() const;
  std::string GetFormattedWarnings() const;
  std::string GetSummary() const;
  
  // Get raw errors/warnings
  const std::vector<ParseError>& GetErrors() const { return errors_; }
  const std::vector<ParseError>& GetWarnings() const { return warnings_; }
  
  // Clear all errors and warnings
  void Clear();
  
  // Context helpers
  void SetCurrentLocation(const ErrorLocation& loc) { current_location_ = loc; }
  void SetCurrentContext(const std::string& context) { current_context_ = context; }
  std::string GetCurrentContext() const { return current_context_; }
  
  // Common error messages (for consistency)
  static std::string UnexpectedToken(const std::string& found, const std::string& expected);
  static std::string UnexpectedEOF(const std::string& context);
  static std::string InvalidSyntax(const std::string& context);
  static std::string UndefinedIdentifier(const std::string& identifier);
  static std::string TypeMismatch(const std::string& expected, const std::string& found);
  static std::string InvalidValue(const std::string& value, const std::string& type);
  static std::string DuplicateDefinition(const std::string& name);
  static std::string CircularReference(const std::string& path);
  static std::string FileNotFound(const std::string& path);
  static std::string MemoryLimitExceeded(size_t requested, size_t limit);
  
  // Format helpers
  static std::string FormatLocation(const ErrorLocation& loc);
  static std::string FormatErrorWithContext(const ParseError& error);
  
 private:
  std::vector<ParseError> errors_;
  std::vector<ParseError> warnings_;
  std::vector<ParseError> info_;
  
  size_t max_errors_;
  size_t error_count_;
  size_t warning_count_;
  
  std::string current_filename_;
  ErrorLocation current_location_;
  std::string current_context_;
  
  bool verbose_{false};
  
  // Helper to add context arrow (points to error column)
  std::string MakeContextArrow(const std::string& line, uint64_t column) const;
  
  // Helper to truncate long lines
  std::string TruncateContext(const std::string& line, uint64_t column) const;
};

// RAII-style error context manager
class ErrorContext {
 public:
  ErrorContext(AsciiErrorHandler* handler, const std::string& context)
      : handler_(handler), prev_context_(handler->GetCurrentContext()) {
    handler_->SetCurrentContext(context);
  }
  
  ~ErrorContext() {
    handler_->SetCurrentContext(prev_context_);
  }
  
 private:
  AsciiErrorHandler* handler_;
  std::string prev_context_;
};

// Macro helpers for common error patterns
#define PUSH_ERROR(handler, msg) \
  (handler)->PushError(msg)

#define PUSH_ERROR_LOC(handler, msg, loc) \
  (handler)->PushError(msg, loc)

#define PUSH_WARNING(handler, msg) \
  (handler)->PushWarning(msg)

#define PUSH_WARNING_LOC(handler, msg, loc) \
  (handler)->PushWarning(msg, loc)

// Error recovery helpers
class RecoveryPoint {
 public:
  RecoveryPoint(AsciiErrorHandler* handler) : handler_(handler) {
    error_count_ = handler->GetErrors().size();
  }
  
  void Commit() { committed_ = true; }
  
  ~RecoveryPoint() {
    if (!committed_ && handler_->GetErrors().size() > error_count_) {
      // Rollback errors added since recovery point
      // This is useful for speculative parsing
    }
  }
  
 private:
  AsciiErrorHandler* handler_;
  size_t error_count_;
  bool committed_{false};
};

}  // namespace ascii
}  // namespace tinyusdz