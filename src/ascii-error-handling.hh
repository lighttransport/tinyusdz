// SPDX-License-Identifier: Apache 2.0
// Copyright 2021-2025 Syoyo Fujita.
// Copyright 2023-Present Light Transport Entertainment Inc.
//
// Error handling and reporting module for USD ASCII parser
#pragma once

#include <string>
#include <vector>
#include <functional>
#include <sstream>
#include "ascii-lexer.hh"

namespace tinyusdz {
namespace ascii {

///
/// Error severity levels
///
enum class ErrorLevel {
  Info,
  Warning,
  Error,
  Fatal
};

///
/// Diagnostic information for an error
///
struct ErrorDiagnostic {
  std::string message;
  Cursor cursor;
  ErrorLevel level;
  std::string context;  // Code context around error
  std::string filename;
  
  std::string ToString() const;
};

///
/// Error recovery strategy
///
enum class RecoveryStrategy {
  None,           // Don't attempt recovery
  SkipToken,      // Skip current token and continue
  SkipLine,       // Skip to next line
  SkipBlock,      // Skip to matching closing brace
  SkipStatement,  // Skip to next semicolon or newline
  Resync          // Resynchronize at known boundary
};

///
/// Parse context for better error messages
///
class ParseContext {
 public:
  void Push(const std::string& context);
  void Pop();
  std::string GetContext() const;
  
 private:
  std::vector<std::string> _stack;
};

///
/// Error handler for USD ASCII parser.
/// Manages error collection, reporting, and recovery strategies.
///
class AsciiErrorHandler {
 public:
  AsciiErrorHandler(size_t max_errors = 100)
      : _max_errors(max_errors) {}

  // Error reporting
  void ReportError(const std::string& msg, const Cursor& cursor);
  void ReportWarning(const std::string& msg, const Cursor& cursor);
  void ReportInfo(const std::string& msg, const Cursor& cursor);
  void ReportFatal(const std::string& msg, const Cursor& cursor);
  
  // Formatted error reporting
  void ReportErrorf(const Cursor& cursor, const char* fmt, ...);
  void ReportWarningf(const Cursor& cursor, const char* fmt, ...);
  
  // Context-aware error reporting
  void ReportErrorWithContext(const std::string& msg, 
                              const Cursor& cursor,
                              const std::string& code_context);
  
  // Expected token errors
  void ReportExpectedToken(const std::string& expected, 
                           const std::string& got,
                           const Cursor& cursor);
  
  void ReportUnexpectedToken(const std::string& token,
                             const Cursor& cursor);
  
  // Type errors
  void ReportTypeMismatch(const std::string& expected_type,
                          const std::string& actual_type,
                          const Cursor& cursor);
  
  void ReportInvalidValue(const std::string& value,
                          const std::string& type,
                          const Cursor& cursor);
  
  // Syntax errors
  void ReportMissingClosingBrace(const Cursor& open_cursor);
  void ReportMissingOpeningBrace(const Cursor& close_cursor);
  void ReportUnmatchedQuote(const Cursor& cursor);
  void ReportInvalidIdentifier(const std::string& identifier,
                               const Cursor& cursor);
  
  // Semantic errors
  void ReportDuplicateDefinition(const std::string& name,
                                 const Cursor& cursor,
                                 const Cursor& prev_cursor);
  
  void ReportUndefinedReference(const std::string& name,
                                const Cursor& cursor);
  
  void ReportIncompatibleAttribute(const std::string& attr_name,
                                   const std::string& reason,
                                   const Cursor& cursor);
  
  // Memory limit errors
  void ReportMemoryLimitExceeded(size_t limit, size_t requested,
                                 const Cursor& cursor);
  
  // Error recovery
  RecoveryStrategy SuggestRecovery(const ErrorDiagnostic& error) const;
  bool TryRecover(AsciiLexer* lexer, RecoveryStrategy strategy);
  
  // Error collection and queries
  bool HasErrors() const { return !_errors.empty(); }
  bool HasWarnings() const { return !_warnings.empty(); }
  bool HasFatalError() const { return _has_fatal; }
  size_t GetErrorCount() const { return _errors.size(); }
  size_t GetWarningCount() const { return _warnings.size(); }
  
  const std::vector<ErrorDiagnostic>& GetErrors() const { return _errors; }
  const std::vector<ErrorDiagnostic>& GetWarnings() const { return _warnings; }
  
  // Error formatting
  std::string GetFormattedErrors() const;
  std::string GetFormattedWarnings() const;
  std::string GetAllDiagnostics() const;
  
  // Error limits
  bool ExceededErrorLimit() const { return _errors.size() >= _max_errors; }
  void SetMaxErrors(size_t max) { _max_errors = max; }
  
  // Clear errors
  void Clear();
  void ClearErrors() { _errors.clear(); _has_fatal = false; }
  void ClearWarnings() { _warnings.clear(); }
  
  // Parse context management
  ParseContext& GetContext() { return _context; }
  
  // Error callback for custom handling
  using ErrorCallback = std::function<void(const ErrorDiagnostic&)>;
  void SetErrorCallback(ErrorCallback callback) { _error_callback = callback; }

 private:
  std::vector<ErrorDiagnostic> _errors;
  std::vector<ErrorDiagnostic> _warnings;
  std::vector<ErrorDiagnostic> _infos;
  bool _has_fatal{false};
  size_t _max_errors;
  ParseContext _context;
  ErrorCallback _error_callback;
  
  void AddDiagnostic(const ErrorDiagnostic& diag);
  std::string FormatDiagnostic(const ErrorDiagnostic& diag) const;
};

///
/// RAII helper for parse context
///
class ScopedParseContext {
 public:
  ScopedParseContext(ParseContext& context, const std::string& name)
      : _context(context) {
    _context.Push(name);
  }
  
  ~ScopedParseContext() {
    _context.Pop();
  }
  
 private:
  ParseContext& _context;
};

// Macro helpers for common error patterns
#define REPORT_AND_RETURN_ERROR(handler, msg, cursor) \
  do { \
    (handler).ReportError((msg), (cursor)); \
    return false; \
  } while(0)

#define REPORT_AND_CONTINUE_WARNING(handler, msg, cursor) \
  do { \
    (handler).ReportWarning((msg), (cursor)); \
  } while(0)

}  // namespace ascii
}  // namespace tinyusdz