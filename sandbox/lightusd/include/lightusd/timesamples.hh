// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - TimeSamples class for time-keyed animation data

#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "lightusd/types.hh"
#include "lightusd/result.hh"
#include "lightusd/value.hh"

namespace lightusd {
namespace v1 {

/// TimeSamples - stores time-keyed values for animation.
/// Values are stored as type-erased Value objects.
/// No interpolation - returns exact or bracketing samples.
class TimeSamples {
public:
    /// Default constructor - creates empty TimeSamples
    TimeSamples();

    /// Destructor
    ~TimeSamples();

    /// Copy constructor
    TimeSamples(const TimeSamples& other);

    /// Move constructor
    TimeSamples(TimeSamples&& other) noexcept;

    /// Copy assignment
    TimeSamples& operator=(const TimeSamples& other);

    /// Move assignment
    TimeSamples& operator=(TimeSamples&& other) noexcept;

    // ========== Queries ==========

    /// Get the value type (established on first sample added)
    TypeId value_type_id() const;

    /// Check if empty
    bool empty() const;

    /// Get number of samples
    size_t size() const;

    /// Get all time values
    const std::vector<double>& times() const;

    /// Check if sample at index is blocked (ValueBlock)
    bool is_blocked(size_t index) const;

    // ========== Adding Samples ==========

    /// Add a sample at the given time.
    /// First sample establishes the value type.
    /// Returns false if type doesn't match or time is invalid.
    bool add_sample(double time, const Value& value);

    /// Add a blocked sample (ValueBlock) at the given time.
    bool add_blocked_sample(double time);

    // ========== Getting Samples ==========

    /// Get sample value at index
    Result<Value> get_sample(size_t index) const;

    /// Get time at index
    Result<double> get_time(size_t index) const;

    /// Get value at exact time (no interpolation).
    /// Returns error if time not found.
    Result<Value> get_at_time_exact(double time) const;

    /// Get value at time (returns nearest or lower bracket).
    /// No interpolation - returns the sample at or before the given time.
    Result<Value> get_at_time(double time) const;

    /// Find bracketing samples for a time.
    /// Returns indices of samples before and after (or equal to) the time.
    struct Bracket {
        size_t lower_index;
        size_t upper_index;
        double lower_time;
        double upper_time;
        bool exact_match;  // time matches a sample exactly

        Bracket() : lower_index(0), upper_index(0), lower_time(0), upper_time(0), exact_match(false) {}
    };
    Result<Bracket> find_bracket(double time) const;

    // ========== Modification ==========

    /// Remove sample at index
    bool remove_sample(size_t index);

    /// Clear all samples
    void clear();

    // ========== Utility ==========

    /// Swap contents
    void swap(TimeSamples& other) noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/// Swap specialization
inline void swap(TimeSamples& a, TimeSamples& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
