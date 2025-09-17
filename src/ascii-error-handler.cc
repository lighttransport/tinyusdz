// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.

#include "ascii-error-handler.hh"
#include <algorithm>
#include <iomanip>

namespace tinyusdz {
namespace ascii {

void AsciiErrorHandler::PushError(const std::string& msg) {
  PushError(msg, current_location_);
}

void AsciiErrorHandler::PushError(const std::string& msg, const ErrorLocation& loc) {
  if (error_count_ >= max_errors_) {
    return;
  }
  
  ParseError error(ErrorLevel::ERROR, msg, loc);
  error.context = current_context_;
  errors_.push_back(error);
  error_count_++;
}

void AsciiErrorHandler::PushError(const ParseError& error) {
  if (error_count_ >= max_errors_) {
    return;
  }
  
  errors_.push_back(error);
  error_count_++;
}

void AsciiErrorHandler::PushWarning(const std::string& msg) {
  PushWarning(msg, current_location_);
}

void AsciiErrorHandler::PushWarning(const std::string& msg, const ErrorLocation& loc) {
  ParseError warning(ErrorLevel::WARNING, msg, loc);
  warning.context = current_context_;
  warnings_.push_back(warning);
  warning_count_++;
}

void AsciiErrorHandler::PushInfo(const std::string& msg) {
  if (verbose_) {
    PushInfo(msg, current_location_);
  }
}

void AsciiErrorHandler::PushInfo(const std::string& msg, const ErrorLocation& loc) {
  if (verbose_) {
    ParseError info(ErrorLevel::INFO, msg, loc);
    info.context = current_context_;
    info_.push_back(info);
  }
}

void AsciiErrorHandler::RecoverFromError() {
  // Mark current error as recovered
  // This could be used to implement error recovery strategies
}

void AsciiErrorHandler::Clear() {
  errors_.clear();
  warnings_.clear();
  info_.clear();
  error_count_ = 0;
  warning_count_ = 0;
}

std::string AsciiErrorHandler::GetFormattedErrors() const {
  std::stringstream ss;
  
  for (const auto& error : errors_) {
    ss << FormatErrorWithContext(error) << "\n";
  }
  
  return ss.str();
}

std::string AsciiErrorHandler::GetFormattedWarnings() const {
  std::stringstream ss;
  
  for (const auto& warning : warnings_) {
    ss << FormatErrorWithContext(warning) << "\n";
  }
  
  return ss.str();
}

std::string AsciiErrorHandler::GetSummary() const {
  std::stringstream ss;
  
  if (error_count_ > 0) {
    ss << error_count_ << " error(s)";
  }
  
  if (warning_count_ > 0) {
    if (error_count_ > 0) {
      ss << ", ";
    }
    ss << warning_count_ << " warning(s)";
  }
  
  if (error_count_ == 0 && warning_count_ == 0) {
    ss << "No errors or warnings";
  }
  
  return ss.str();
}

// Static error message helpers
std::string AsciiErrorHandler::UnexpectedToken(const std::string& found, const std::string& expected) {
  return "Unexpected token '" + found + "', expected '" + expected + "'";
}

std::string AsciiErrorHandler::UnexpectedEOF(const std::string& context) {
  return "Unexpected end of file while " + context;
}

std::string AsciiErrorHandler::InvalidSyntax(const std::string& context) {
  return "Invalid syntax in " + context;
}

std::string AsciiErrorHandler::UndefinedIdentifier(const std::string& identifier) {
  return "Undefined identifier '" + identifier + "'";
}

std::string AsciiErrorHandler::TypeMismatch(const std::string& expected, const std::string& found) {
  return "Type mismatch: expected '" + expected + "', found '" + found + "'";
}

std::string AsciiErrorHandler::InvalidValue(const std::string& value, const std::string& type) {
  return "Invalid " + type + " value: '" + value + "'";
}

std::string AsciiErrorHandler::DuplicateDefinition(const std::string& name) {
  return "Duplicate definition of '" + name + "'";
}

std::string AsciiErrorHandler::CircularReference(const std::string& path) {
  return "Circular reference detected: " + path;
}

std::string AsciiErrorHandler::FileNotFound(const std::string& path) {
  return "File not found: " + path;
}

std::string AsciiErrorHandler::MemoryLimitExceeded(size_t requested, size_t limit) {
  std::stringstream ss;
  ss << "Memory limit exceeded: requested " << requested << " bytes, limit is " << limit << " bytes";
  return ss.str();
}

// Format helpers
std::string AsciiErrorHandler::FormatLocation(const ErrorLocation& loc) {
  std::stringstream ss;
  
  if (!loc.filename.empty()) {
    ss << loc.filename << ":";
  }
  
  ss << loc.line << ":" << loc.column;
  
  return ss.str();
}

std::string AsciiErrorHandler::FormatErrorWithContext(const ParseError& error) {
  std::stringstream ss;
  
  // Format: filename:line:col: error: message
  std::string level_str;
  switch (error.level) {
    case ErrorLevel::INFO: level_str = "info"; break;
    case ErrorLevel::WARNING: level_str = "warning"; break;
    case ErrorLevel::ERROR: level_str = "error"; break;
    case ErrorLevel::FATAL: level_str = "fatal"; break;
  }
  
  ss << FormatLocation(error.location) << ": " << level_str << ": " << error.message;
  
  // Add context if available
  if (!error.context.empty()) {
    ss << "\n  " << error.context;
    // Note: MakeContextArrow is an instance method, not static
    // Would need an instance to call it properly
  }
  
  // Add suggestion if available
  if (!error.suggestion.empty()) {
    ss << "\n  note: " << error.suggestion;
  }
  
  return ss.str();
}

std::string AsciiErrorHandler::MakeContextArrow(const std::string& line, uint64_t column) const {
  if (column == 0 || column > line.length() + 1) {
    return "";
  }
  
  std::string arrow(column - 1, ' ');
  arrow += '^';
  
  return arrow;
}

std::string AsciiErrorHandler::TruncateContext(const std::string& line, uint64_t column) const {
  const size_t max_width = 80;
  const size_t context_window = 40;
  
  if (line.length() <= max_width) {
    return line;
  }
  
  // Try to center the error column
  size_t start = 0;
  if (column > context_window) {
    start = column - context_window;
  }
  
  size_t end = std::min(start + max_width, line.length());
  
  std::string result;
  if (start > 0) {
    result = "...";
  }
  
  result += line.substr(start, end - start);
  
  if (end < line.length()) {
    result += "...";
  }
  
  return result;
}

}  // namespace ascii
}  // namespace tinyusdz