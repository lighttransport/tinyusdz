// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Property class (union of Attribute or Relationship)

#pragma once

#include <cstdint>

#include "lightusd/attribute.hh"
#include "lightusd/relationship.hh"

namespace lightusd {
namespace v1 {

/// Property - can be either an Attribute or a Relationship.
/// This is a discriminated union type.
class Property {
public:
    /// Property kind
    enum class Kind : uint8_t {
        None,         // Empty/uninitialized
        Attribute,    // Holds typed value(s)
        Relationship, // Holds path target(s)
    };

    /// Default constructor - creates empty property
    Property();

    /// Construct from Attribute (move)
    explicit Property(Attribute&& attr);

    /// Construct from Attribute (copy)
    explicit Property(const Attribute& attr);

    /// Construct from Relationship (move)
    explicit Property(Relationship&& rel);

    /// Construct from Relationship (copy)
    explicit Property(const Relationship& rel);

    /// Destructor
    ~Property();

    /// Copy constructor
    Property(const Property& other);

    /// Move constructor
    Property(Property&& other) noexcept;

    /// Copy assignment
    Property& operator=(const Property& other);

    /// Move assignment
    Property& operator=(Property&& other) noexcept;

    // ========== Kind Queries ==========

    /// Get property kind
    Kind kind() const;

    /// Check if empty
    bool is_empty() const;

    /// Check if attribute
    bool is_attribute() const;

    /// Check if relationship
    bool is_relationship() const;

    // ========== Access ==========

    /// Get as attribute (returns nullptr if not attribute)
    const Attribute* as_attribute() const;
    Attribute* as_attribute_mutable();

    /// Get as relationship (returns nullptr if not relationship)
    const Relationship* as_relationship() const;
    Relationship* as_relationship_mutable();

    // ========== Modification ==========

    /// Set to attribute (move)
    void set_attribute(Attribute&& attr);

    /// Set to attribute (copy)
    void set_attribute(const Attribute& attr);

    /// Set to relationship (move)
    void set_relationship(Relationship&& rel);

    /// Set to relationship (copy)
    void set_relationship(const Relationship& rel);

    /// Clear to empty state
    void clear();

    // ========== Utility ==========

    /// Swap contents
    void swap(Property& other) noexcept;

private:
    void destroy();
    void copy_from(const Property& other);
    void move_from(Property&& other) noexcept;

    Kind kind_ = Kind::None;

    // Using explicit storage to avoid union member lifetime issues
    // Size is max of Attribute and Relationship
    alignas(Attribute) alignas(Relationship)
    char storage_[sizeof(Attribute) > sizeof(Relationship)
                  ? sizeof(Attribute) : sizeof(Relationship)];
};

/// Swap specialization
inline void swap(Property& a, Property& b) noexcept {
    a.swap(b);
}

} // namespace v1
} // namespace lightusd
