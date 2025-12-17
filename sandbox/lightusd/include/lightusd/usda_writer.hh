// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA writer with customizable formatting

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <iosfwd>

#include "lightusd/types.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Value;
class Token;
class Path;
class TimeSamples;
class Attribute;
class Relationship;
class Property;
class Prim;
class Stage;

// Specifier enum (must match prim.hh)
enum class Specifier : uint8_t;

// ============================================================================
// Formatting Options
// ============================================================================

/// Options for USDA formatting
struct UsdaFormatOptions {
    /// Indentation string (default: 4 spaces)
    std::string indent_string = "    ";

    /// Use tabs instead of spaces
    bool use_tabs = false;

    /// Number of spaces per indent level (ignored if use_tabs is true)
    uint8_t indent_size = 4;

    /// Maximum line width for array wrapping (0 = no wrapping)
    size_t max_line_width = 80;

    /// Elements per line for arrays (0 = auto based on max_line_width)
    size_t array_elements_per_line = 0;

    /// Include type annotations in output
    bool include_type_annotations = true;

    /// Sort properties alphabetically
    bool sort_properties = false;

    /// Sort children alphabetically
    bool sort_children = false;

    /// Include empty prims (prims with no properties/children)
    bool include_empty_prims = true;

    /// Precision for float output
    int float_precision = 6;

    /// Precision for double output
    int double_precision = 15;

    /// Newline string
    std::string newline = "\n";

    /// Build indent string based on settings
    void build_indent_string() {
        if (use_tabs) {
            indent_string = "\t";
        } else {
            indent_string = std::string(indent_size, ' ');
        }
    }

    /// Create default options
    static UsdaFormatOptions defaults() {
        return UsdaFormatOptions{};
    }

    /// Create compact options (minimal whitespace)
    static UsdaFormatOptions compact() {
        UsdaFormatOptions opts;
        opts.indent_size = 2;
        opts.build_indent_string();
        opts.max_line_width = 0;  // No wrapping
        return opts;
    }

    /// Create pretty options (readable formatting)
    static UsdaFormatOptions pretty() {
        UsdaFormatOptions opts;
        opts.indent_size = 4;
        opts.build_indent_string();
        opts.max_line_width = 80;
        opts.sort_properties = true;
        return opts;
    }
};

// ============================================================================
// UsdaWriter Class
// ============================================================================

/// USDA writer - converts USD data to USDA text format
class UsdaWriter {
public:
    /// Construct with default options
    UsdaWriter();

    /// Construct with custom options
    explicit UsdaWriter(const UsdaFormatOptions& options);

    /// Set formatting options
    void set_options(const UsdaFormatOptions& options) { options_ = options; }

    /// Get formatting options
    const UsdaFormatOptions& options() const { return options_; }

    // ========== Value Formatting ==========

    /// Format value to string
    std::string format(const Value& value) const;

    /// Format value with explicit type
    std::string format(const Value& value, TypeId type_hint) const;

    // ========== Type Formatting ==========

    /// Format type name
    static const char* format_type_name(TypeId type);

    /// Format specifier keyword
    static const char* format_specifier(Specifier spec);

    // ========== Component Formatting ==========

    /// Format Token to string
    std::string format(const Token& token) const;

    /// Format Path to string
    std::string format(const Path& path) const;

    /// Format TimeSamples to string
    std::string format(const TimeSamples& ts, int depth = 0) const;

    /// Format Attribute to string
    std::string format(const Attribute& attr, const std::string& name, int depth = 0) const;

    /// Format Relationship to string
    std::string format(const Relationship& rel, const std::string& name, int depth = 0) const;

    /// Format Property to string
    std::string format(const Property& prop, const std::string& name, int depth = 0) const;

    /// Format Prim to string
    std::string format(const Prim& prim, int depth = 0) const;

    /// Format Stage to string
    std::string format(const Stage& stage) const;

    // ========== Stream Output ==========

    /// Write Stage to stream
    void write(std::ostream& os, const Stage& stage) const;

    /// Write Prim to stream
    void write(std::ostream& os, const Prim& prim, int depth = 0) const;

    /// Write Value to stream
    void write(std::ostream& os, const Value& value) const;

private:
    UsdaFormatOptions options_;

    // Internal helpers
    std::string make_indent(int depth) const;
    std::string escape_string(const std::string& s) const;
    std::string format_scalar(const Value& value) const;
    std::string format_array(const Value& value) const;
    std::string format_tuple(const Value& value) const;
    std::string format_matrix(const Value& value) const;
};

// ============================================================================
// to_string Functions
// ============================================================================

/// Convert TypeId to string
std::string to_string(TypeId type);

/// Convert Specifier to string
std::string to_string(Specifier spec);

/// Convert Value to string (default formatting)
std::string to_string(const Value& value);

/// Convert Token to string
std::string to_string(const Token& token);

/// Convert Path to string
std::string to_string(const Path& path);

/// Convert TimeSamples to string
std::string to_string(const TimeSamples& ts);

/// Convert Attribute to string (with name)
std::string to_string(const Attribute& attr, const std::string& name = "");

/// Convert Relationship to string (with name)
std::string to_string(const Relationship& rel, const std::string& name = "");

/// Convert Property to string (with name)
std::string to_string(const Property& prop, const std::string& name = "");

/// Convert Prim to string (USDA format)
std::string to_string(const Prim& prim);

/// Convert Stage to string (full USDA)
std::string to_string(const Stage& stage);

// ============================================================================
// Stream Operators
// ============================================================================

/// Stream output for TypeId
std::ostream& operator<<(std::ostream& os, TypeId type);

/// Stream output for Specifier
std::ostream& operator<<(std::ostream& os, Specifier spec);

/// Stream output for Value
std::ostream& operator<<(std::ostream& os, const Value& value);

/// Stream output for Token
std::ostream& operator<<(std::ostream& os, const Token& token);

/// Stream output for Path
std::ostream& operator<<(std::ostream& os, const Path& path);

/// Stream output for TimeSamples
std::ostream& operator<<(std::ostream& os, const TimeSamples& ts);

/// Stream output for Attribute
std::ostream& operator<<(std::ostream& os, const Attribute& attr);

/// Stream output for Relationship
std::ostream& operator<<(std::ostream& os, const Relationship& rel);

/// Stream output for Property
std::ostream& operator<<(std::ostream& os, const Property& prop);

/// Stream output for Prim
std::ostream& operator<<(std::ostream& os, const Prim& prim);

/// Stream output for Stage
std::ostream& operator<<(std::ostream& os, const Stage& stage);

} // namespace v1
} // namespace lightusd
