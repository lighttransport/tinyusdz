// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Relationship class for path-based connections

#pragma once

#include <vector>

#include "lightusd/path.hh"

namespace lightusd {
namespace v1 {

/// Relationship - a property that targets other prims or properties.
/// Unlike Attribute, Relationship holds paths, not values.
class Relationship {
public:
    /// Default constructor
    Relationship();

    /// Destructor
    ~Relationship();

    /// Copy constructor
    Relationship(const Relationship& other);

    /// Move constructor
    Relationship(Relationship&& other) noexcept;

    /// Copy assignment
    Relationship& operator=(const Relationship& other);

    /// Move assignment
    Relationship& operator=(Relationship&& other) noexcept;

    // ========== State Queries ==========

    /// Check if relationship has been authored
    bool is_authored() const;

    /// Check if relationship has any targets
    bool has_targets() const;

    /// Get number of targets
    size_t target_count() const;

    // ========== Target Access ==========

    /// Get all targets
    const std::vector<Path>& targets() const;

    /// Get target at index (returns empty path if out of range)
    Path target(size_t index) const;

    // ========== Target Modification ==========

    /// Set single target (replaces all existing)
    void set_target(const Path& target);

    /// Set multiple targets
    void set_targets(const std::vector<Path>& targets);

    /// Add a target
    void add_target(const Path& target);

    /// Remove target at index
    bool remove_target(size_t index);

    /// Clear all targets
    void clear_targets();

    // ========== Utility ==========

    /// Swap contents
    void swap(Relationship& other) noexcept;

    /// Clear all data
    void clear();

private:
    std::vector<Path> targets_;
    bool authored_ = false;
};

/// Swap specialization
inline void swap(Relationship& a, Relationship& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
