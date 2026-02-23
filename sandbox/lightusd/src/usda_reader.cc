// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA reader implementation

#include "lightusd/usda_reader.hh"
#include "lightusd/usda_lexer.hh"
#include "lightusd/usda_parser.hh"
#include <fstream>
#include <sstream>

namespace lightusd {
namespace v1 {

// ============================================================================
// String utilities
// ============================================================================

std::string get_source_line(const std::string& source, uint32_t line) {
    if (source.empty()) return "";

    size_t pos = 0;
    uint32_t current_line = 0;

    // Find start of requested line
    while (pos < source.size() && current_line < line) {
        if (source[pos] == '\n') {
            current_line++;
        }
        pos++;
    }

    if (pos >= source.size()) return "";

    // Find end of line
    size_t end = pos;
    while (end < source.size() && source[end] != '\n' && source[end] != '\r') {
        end++;
    }

    return source.substr(pos, end - pos);
}

std::string format_diagnostic_with_context(const Diagnostic& diag,
                                           const std::string& source) {
    std::string line = get_source_line(source, diag.location.cursor.line);
    return diag.format_with_context(line, true);
}

// ============================================================================
// Format checking
// ============================================================================

bool is_usda_string(const std::string& source) {
    // Skip leading whitespace
    size_t pos = 0;
    while (pos < source.size() &&
           (source[pos] == ' ' || source[pos] == '\t' ||
            source[pos] == '\r' || source[pos] == '\n')) {
        pos++;
    }

    // Check for #usda
    if (pos + 5 <= source.size()) {
        return source.substr(pos, 5) == "#usda";
    }
    return false;
}

bool is_usda_file(const std::string& filepath, size_t max_check_bytes) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) return false;

    std::string buffer(max_check_bytes, '\0');
    file.read(&buffer[0], static_cast<std::streamsize>(max_check_bytes));
    buffer.resize(static_cast<size_t>(file.gcount()));

    return is_usda_string(buffer);
}

// ============================================================================
// Reading
// ============================================================================

ReadResult read_usda_string(const std::string& source,
                            const std::string& filename,
                            const ReaderOptions& options) {
    ReadResult result;

    // Check for empty source
    if (source.empty()) {
        result.success = false;
        result.error_summary = "Empty source";
        return result;
    }

    // Check size limit
    if (source.size() > options.max_file_size) {
        result.success = false;
        result.error_summary = "Source exceeds maximum size limit";
        return result;
    }

    // Create lexer
    Lexer lexer(source);
    lexer.set_filename(filename);

    // Create parser
    Parser parser(lexer);
    parser.set_filename(filename);

    ParserOptions parser_opts;
    parser_opts.max_depth = options.max_depth;
    parser_opts.max_array_size = options.max_array_size;
    parser_opts.allow_unknown_types = options.allow_unknown_types;
    parser_opts.strict_mode = options.strict_mode;
    parser.set_options(parser_opts);

    // Parse
    auto stage_result = parser.parse();

    // Collect diagnostics
    result.diagnostics = parser.diagnostics();

    if (stage_result.ok()) {
        result.success = true;
        result.stage = std::move(stage_result.value());
    } else {
        result.success = false;
        result.error_summary = stage_result.error().message;

        // Add context to first error
        if (!result.diagnostics.empty()) {
            const Diagnostic& first_error = result.diagnostics.diagnostics()[0];
            std::string line = get_source_line(source, first_error.location.cursor.line);
            result.error_summary = first_error.format_with_context(line, true);
        }
    }

    return result;
}

ReadResult read_usda_file(const std::string& filepath,
                          const ReaderOptions& options) {
    ReadResult result;

    // Open file
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        result.success = false;
        result.error_summary = "Failed to open file: " + filepath;
        return result;
    }

    // Check file size
    auto size = file.tellg();
    if (size < 0) {
        result.success = false;
        result.error_summary = "Failed to get file size: " + filepath;
        return result;
    }

    if (static_cast<size_t>(size) > options.max_file_size) {
        result.success = false;
        result.error_summary = "File exceeds maximum size limit: " + filepath;
        return result;
    }

    // Read file content
    file.seekg(0, std::ios::beg);
    std::string source(static_cast<size_t>(size), '\0');
    if (!file.read(&source[0], size)) {
        result.success = false;
        result.error_summary = "Failed to read file: " + filepath;
        return result;
    }

    // Parse
    return read_usda_string(source, filepath, options);
}

} // namespace v1
} // namespace lightusd
