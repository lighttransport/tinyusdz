// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA reader public API

#pragma once

#include <string>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/diagnostic.hh"
#include "lightusd/stage.hh"

namespace lightusd {
namespace v1 {

/// Reader options
struct ReaderOptions {
    size_t max_file_size = 1024 * 1024 * 1024;  // 1GB default
    size_t max_depth = 256;
    size_t max_array_size = 10000000;
    bool allow_unknown_types = true;
    bool strict_mode = false;
};

/// USDA reader result with diagnostics
struct ReadResult {
    bool success = false;
    Stage stage;
    DiagnosticList diagnostics;
    std::string error_summary;

    /// Check if read was successful
    bool ok() const { return success; }

    /// Get formatted error message with context
    std::string format_errors() const {
        if (success) return "";
        if (!error_summary.empty()) return error_summary;
        return diagnostics.format_all();
    }
};

/// Read USDA from string
ReadResult read_usda_string(const std::string& source,
                            const std::string& filename = "<string>",
                            const ReaderOptions& options = ReaderOptions());

/// Read USDA from file
ReadResult read_usda_file(const std::string& filepath,
                          const ReaderOptions& options = ReaderOptions());

/// Check if string looks like USDA format (starts with #usda)
bool is_usda_string(const std::string& source);

/// Check if file is USDA format
bool is_usda_file(const std::string& filepath, size_t max_check_bytes = 256);

/// Get line from source at given line number (0-based)
std::string get_source_line(const std::string& source, uint32_t line);

/// Format diagnostic with source context
std::string format_diagnostic_with_context(const Diagnostic& diag,
                                           const std::string& source);

} // namespace v1
} // namespace lightusd
