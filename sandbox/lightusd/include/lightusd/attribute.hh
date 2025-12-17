// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Attribute class for typed property values

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "lightusd/types.hh"
#include "lightusd/result.hh"
#include "lightusd/value.hh"
#include "lightusd/path.hh"
#include "lightusd/timesamples.hh"

namespace lightusd {
namespace v1 {

/// Variability - specifies whether attribute can be animated
enum class Variability : uint8_t {
    Varying,  // Can vary over time (default)
    Uniform,  // Constant for all time
};

/// Attribute - a typed property that can hold:
/// - A default value
/// - Time-sampled values for animation
/// - Connections to other attributes
class Attribute {
public:
    /// Default constructor - creates untyped attribute
    Attribute();

    /// Construct with type
    explicit Attribute(TypeId type_id);

    /// Destructor
    ~Attribute();

    /// Copy constructor
    Attribute(const Attribute& other);

    /// Move constructor
    Attribute(Attribute&& other) noexcept;

    /// Copy assignment
    Attribute& operator=(const Attribute& other);

    /// Move assignment
    Attribute& operator=(Attribute&& other) noexcept;

    // ========== Type ==========

    /// Get attribute type
    TypeId type_id() const;

    /// Get type name
    const char* type_name() const;

    /// Set attribute type (only valid if not yet authored)
    bool set_type_id(TypeId id);

    /// Check if attribute type is an array type
    bool is_array_type() const;

    // ========== State Queries ==========

    /// Check if any value has been authored
    bool is_authored() const;

    /// Check if blocked (explicitly set to no value)
    bool is_blocked() const;

    /// Check if has default value
    bool has_default() const;

    /// Check if has time samples
    bool has_timesamples() const;

    /// Check if has connections
    bool has_connection() const;

    // ========== Default Value ==========

    /// Get default value
    Result<Value> get_default() const;

    /// Set default value (must match type)
    bool set_default(const Value& v);

    /// Clear default value
    void clear_default();

    // ========== Time Samples ==========

    /// Get read-only access to time samples
    const TimeSamples* timesamples() const;

    /// Get mutable access to time samples (creates if needed)
    TimeSamples* timesamples_mutable();

    /// Set time samples (replaces existing)
    void set_timesamples(TimeSamples&& ts);

    /// Set time samples (copies)
    void set_timesamples(const TimeSamples& ts);

    /// Clear time samples
    void clear_timesamples();

    // ========== Value Access ==========

    /// Get value at time.
    /// Checks time samples first, then default value.
    Result<Value> get(double time = 0.0) const;

    // ========== Connections ==========

    /// Get connections list
    const std::vector<Path>& connections() const;

    /// Set single connection (replaces existing)
    void set_connection(const Path& target);

    /// Add connection
    void add_connection(const Path& target);

    /// Clear all connections
    void clear_connections();

    // ========== Blocked State ==========

    /// Mark attribute as blocked (explicitly no value)
    void set_blocked();

    /// Clear blocked state
    void clear_blocked();

    // ========== Variability ==========

    /// Get variability
    Variability variability() const;

    /// Set variability
    void set_variability(Variability v);

    // ========== Utility ==========

    /// Swap contents
    void swap(Attribute& other) noexcept;

    /// Clear all authored data
    void clear();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Swap specialization
inline void swap(Attribute& a, Attribute& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
