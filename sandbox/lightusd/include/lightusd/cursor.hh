// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Source location cursor for error reporting

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace lightusd {
namespace v1 {

/// Cursor - tracks position in source text for error reporting.
/// Zero-based line and column numbers (use line()+1 for display).
struct Cursor {
    size_t offset = 0;   // Byte offset from start
    uint32_t line = 0;   // Line number (0-based)
    uint32_t column = 0; // Column number (0-based, in bytes)

    Cursor() = default;
    Cursor(size_t off, uint32_t ln, uint32_t col)
        : offset(off), line(ln), column(col) {}

    /// Display-friendly line number (1-based)
    uint32_t display_line() const { return line + 1; }

    /// Display-friendly column number (1-based)
    uint32_t display_column() const { return column + 1; }

    /// Format as "line:column" (1-based for display)
    std::string to_string() const {
        return std::to_string(display_line()) + ":" + std::to_string(display_column());
    }

    bool operator==(const Cursor& other) const {
        return offset == other.offset;
    }

    bool operator<(const Cursor& other) const {
        return offset < other.offset;
    }
};

/// SourceRange - a range of text in source
struct SourceRange {
    Cursor start;
    Cursor end;

    SourceRange() = default;
    SourceRange(const Cursor& s, const Cursor& e) : start(s), end(e) {}
    SourceRange(const Cursor& pos) : start(pos), end(pos) {}

    bool empty() const { return start.offset == end.offset; }
    size_t length() const { return end.offset - start.offset; }
};

/// SourceLocation - cursor with optional filename
struct SourceLocation {
    std::string filename;
    Cursor cursor;

    SourceLocation() = default;
    SourceLocation(const std::string& file, const Cursor& cur)
        : filename(file), cursor(cur) {}
    explicit SourceLocation(const Cursor& cur) : cursor(cur) {}

    /// Format as "filename:line:column" or "line:column"
    std::string to_string() const {
        if (filename.empty()) {
            return cursor.to_string();
        }
        return filename + ":" + cursor.to_string();
    }
};

} // namespace v1
} // namespace lightusd
