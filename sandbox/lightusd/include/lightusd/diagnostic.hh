// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Diagnostic messages for error reporting

#pragma once

#include <string>
#include <vector>

#include "lightusd/cursor.hh"

namespace lightusd {
namespace v1 {

/// Diagnostic severity levels
enum class DiagnosticSeverity : uint8_t {
    Note,       // Informational
    Warning,    // Non-fatal issue
    Error,      // Parse/semantic error
};

/// Diagnostic - a single error/warning message with location
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    SourceLocation location;
    std::string message;
    std::string suggestion;  // Optional fix suggestion

    Diagnostic() = default;

    Diagnostic(DiagnosticSeverity sev, const SourceLocation& loc,
               const std::string& msg)
        : severity(sev), location(loc), message(msg) {}

    Diagnostic(DiagnosticSeverity sev, const Cursor& cur,
               const std::string& msg)
        : severity(sev), location(cur), message(msg) {}

    /// Check if this is an error
    bool is_error() const { return severity == DiagnosticSeverity::Error; }

    /// Format the diagnostic for display
    std::string format() const {
        std::string result;

        // Severity prefix
        switch (severity) {
            case DiagnosticSeverity::Note:    result = "note: ";    break;
            case DiagnosticSeverity::Warning: result = "warning: "; break;
            case DiagnosticSeverity::Error:   result = "error: ";   break;
        }

        // Location
        std::string loc_str = location.to_string();
        if (!loc_str.empty()) {
            result = loc_str + ": " + result;
        }

        // Message
        result += message;

        return result;
    }

    /// Format with source context
    std::string format_with_context(const std::string& source_line,
                                    bool show_caret = true) const {
        std::string result = format();
        result += "\n";

        // Show source line
        if (!source_line.empty()) {
            result += "  " + source_line + "\n";

            // Show caret pointing to error location
            if (show_caret && location.cursor.column < source_line.size()) {
                result += "  ";
                for (uint32_t i = 0; i < location.cursor.column; ++i) {
                    result += (source_line[i] == '\t') ? '\t' : ' ';
                }
                result += "^\n";
            }
        }

        // Show suggestion
        if (!suggestion.empty()) {
            result += "  suggestion: " + suggestion + "\n";
        }

        return result;
    }
};

/// DiagnosticList - collection of diagnostics
class DiagnosticList {
public:
    DiagnosticList() = default;

    /// Add a diagnostic
    void add(const Diagnostic& diag) {
        diagnostics_.push_back(diag);
        if (diag.is_error()) {
            ++error_count_;
        }
    }

    /// Add error at location
    void error(const Cursor& cursor, const std::string& message) {
        add(Diagnostic(DiagnosticSeverity::Error, cursor, message));
    }

    void error(const SourceLocation& loc, const std::string& message) {
        add(Diagnostic(DiagnosticSeverity::Error, loc, message));
    }

    /// Add warning at location
    void warning(const Cursor& cursor, const std::string& message) {
        add(Diagnostic(DiagnosticSeverity::Warning, cursor, message));
    }

    /// Add note
    void note(const Cursor& cursor, const std::string& message) {
        add(Diagnostic(DiagnosticSeverity::Note, cursor, message));
    }

    /// Check if has any errors
    bool has_errors() const { return error_count_ > 0; }

    /// Get error count
    size_t error_count() const { return error_count_; }

    /// Get all diagnostics
    const std::vector<Diagnostic>& diagnostics() const { return diagnostics_; }

    /// Get diagnostics by severity
    std::vector<Diagnostic> errors() const {
        std::vector<Diagnostic> result;
        for (const auto& d : diagnostics_) {
            if (d.is_error()) {
                result.push_back(d);
            }
        }
        return result;
    }

    /// Clear all diagnostics
    void clear() {
        diagnostics_.clear();
        error_count_ = 0;
    }

    /// Check if empty
    bool empty() const { return diagnostics_.empty(); }

    /// Get size
    size_t size() const { return diagnostics_.size(); }

    /// Format all diagnostics
    std::string format_all() const {
        std::string result;
        for (const auto& d : diagnostics_) {
            result += d.format() + "\n";
        }
        return result;
    }

private:
    std::vector<Diagnostic> diagnostics_;
    size_t error_count_ = 0;
};

} // namespace v1
} // namespace lightusd
