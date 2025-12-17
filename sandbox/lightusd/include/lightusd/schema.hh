// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Schema definition and validation
// JSON-based schema system for validating USD prims

#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "lightusd/result.hh"
#include "lightusd/types.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Prim;
class Value;

// ============================================================================
// Schema types
// ============================================================================

/// Property requirement level
enum class PropertyRequirement {
    Required,     // Must be present
    Optional,     // May be present (has default)
    Recommended,  // Should be present but not required
};

/// Validation severity
enum class ValidationSeverity {
    Error,    // Invalid, must be fixed
    Warning,  // Suboptimal, should be reviewed
    Info,     // Informational note
};

/// A single validation issue
struct ValidationIssue {
    ValidationSeverity severity;
    std::string property_name;  // Empty for prim-level issues
    std::string message;
    std::string suggestion;     // Optional fix suggestion
};

/// Result of validation
struct ValidationResult {
    bool valid = true;
    std::vector<ValidationIssue> issues;

    /// Add an error
    void add_error(const std::string& prop, const std::string& msg,
                   const std::string& suggestion = "");

    /// Add a warning
    void add_warning(const std::string& prop, const std::string& msg,
                     const std::string& suggestion = "");

    /// Add info
    void add_info(const std::string& prop, const std::string& msg);

    /// Check if there are any errors
    bool has_errors() const;

    /// Check if there are any warnings
    bool has_warnings() const;

    /// Get error count
    size_t error_count() const;

    /// Get all issues as formatted strings
    std::vector<std::string> format_issues() const;

    /// Merge another validation result
    void merge(const ValidationResult& other);
};

// ============================================================================
// Property Schema
// ============================================================================

/// Schema for a single property (attribute or relationship)
struct PropertySchema {
    std::string name;                  // Property name
    std::string type_name;             // USD type name (e.g., "float3", "token[]")
    TypeId type_id = TypeId::Invalid;  // Resolved type ID
    bool is_array = false;             // Whether it's an array type
    PropertyRequirement requirement = PropertyRequirement::Optional;

    // Constraints
    std::optional<size_t> min_array_size;
    std::optional<size_t> max_array_size;
    std::vector<std::string> allowed_values;  // For token types
    std::optional<double> min_value;          // For numeric types
    std::optional<double> max_value;

    // Default value (as JSON string for simplicity)
    std::string default_value_json;

    // Documentation
    std::string doc;

    // Interpolation (for primvar properties)
    std::string interpolation;  // "constant", "uniform", "vertex", "faceVarying"

    /// Create from JSON object string
    static Result<PropertySchema> from_json(std::string_view json);

    /// Convert to JSON string
    std::string to_json() const;

    /// Validate a value against this schema
    ValidationResult validate(const Value& value) const;
};

// ============================================================================
// Prim Schema
// ============================================================================

/// Schema for a USD prim type
struct PrimSchema {
    std::string type_name;                        // e.g., "Mesh", "Xform", "Material"
    std::string doc;                              // Documentation string
    std::string inherits_from;                    // Parent schema (inheritance)

    std::vector<PropertySchema> properties;       // Property definitions
    std::vector<std::string> required_properties; // Names of required properties
    std::vector<std::string> api_schemas;         // Applied API schemas

    // Child prim constraints
    std::vector<std::string> allowed_child_types; // Empty = any type allowed
    bool allow_additional_properties = true;      // Allow properties not in schema

    // Schema metadata
    std::string version;                          // Schema version
    std::string source;                           // "builtin" or file path

    /// Create from JSON string
    static Result<PrimSchema> from_json(std::string_view json);

    /// Convert to JSON string
    std::string to_json() const;

    /// Get property schema by name
    const PropertySchema* get_property(std::string_view name) const;

    /// Check if property is required
    bool is_required(std::string_view name) const;

    /// Validate a prim against this schema
    ValidationResult validate(const Prim& prim) const;

    /// Merge with parent schema (for inheritance)
    void merge_parent(const PrimSchema& parent);
};

// ============================================================================
// JSON Parser (minimal, dependency-free)
// ============================================================================

/// Simple JSON value type for schema parsing
class JsonValue {
public:
    using Object = std::map<std::string, JsonValue>;
    using Array = std::vector<JsonValue>;
    using Variant = std::variant<std::nullptr_t, bool, double, std::string, Array, Object>;

    JsonValue() : value_(nullptr) {}
    JsonValue(std::nullptr_t) : value_(nullptr) {}
    JsonValue(bool v) : value_(v) {}
    JsonValue(double v) : value_(v) {}
    JsonValue(int v) : value_(static_cast<double>(v)) {}
    JsonValue(const std::string& v) : value_(v) {}
    JsonValue(std::string&& v) : value_(std::move(v)) {}
    JsonValue(const char* v) : value_(std::string(v)) {}
    JsonValue(const Array& v) : value_(v) {}
    JsonValue(Array&& v) : value_(std::move(v)) {}
    JsonValue(const Object& v) : value_(v) {}
    JsonValue(Object&& v) : value_(std::move(v)) {}

    // Type checking
    bool is_null() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool is_bool() const { return std::holds_alternative<bool>(value_); }
    bool is_number() const { return std::holds_alternative<double>(value_); }
    bool is_string() const { return std::holds_alternative<std::string>(value_); }
    bool is_array() const { return std::holds_alternative<Array>(value_); }
    bool is_object() const { return std::holds_alternative<Object>(value_); }

    // Value access
    bool as_bool(bool default_val = false) const;
    double as_number(double default_val = 0.0) const;
    int as_int(int default_val = 0) const;
    const std::string& as_string() const;
    std::string as_string(const std::string& default_val) const;
    const Array& as_array() const;
    const Object& as_object() const;

    // Object access
    bool has(const std::string& key) const;
    const JsonValue& operator[](const std::string& key) const;
    const JsonValue& operator[](size_t index) const;

    // Size (for arrays and objects)
    size_t size() const;

    // Parsing
    static Result<JsonValue> parse(std::string_view json);

    // Serialization
    std::string to_string(bool pretty = false, int indent = 0) const;

private:
    Variant value_;
    static const JsonValue null_value_;
    static const std::string empty_string_;
    static const Array empty_array_;
    static const Object empty_object_;
};

// ============================================================================
// Schema utilities
// ============================================================================

/// Parse type name to TypeId and array flag
Result<std::pair<TypeId, bool>> parse_type_name(std::string_view type_name);

/// Get type name from TypeId
std::string type_id_to_name(TypeId id, bool is_array = false);

/// Check if a value matches a type
bool value_matches_type(const Value& value, TypeId expected_type, bool expected_array);

} // namespace v1
} // namespace lightusd
