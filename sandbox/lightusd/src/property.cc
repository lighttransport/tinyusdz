// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Property implementation

#include "lightusd/property.hh"
#include <new>  // placement new

namespace lightusd {
namespace v1 {

// ============================================================================
// Internal Helpers
// ============================================================================

void Property::destroy() {
    switch (kind_) {
        case Kind::Attribute:
            reinterpret_cast<Attribute*>(storage_)->~Attribute();
            break;
        case Kind::Relationship:
            reinterpret_cast<Relationship*>(storage_)->~Relationship();
            break;
        case Kind::None:
            break;
    }
    kind_ = Kind::None;
}

void Property::copy_from(const Property& other) {
    kind_ = other.kind_;
    switch (kind_) {
        case Kind::Attribute:
            new (storage_) Attribute(*reinterpret_cast<const Attribute*>(other.storage_));
            break;
        case Kind::Relationship:
            new (storage_) Relationship(*reinterpret_cast<const Relationship*>(other.storage_));
            break;
        case Kind::None:
            break;
    }
}

void Property::move_from(Property&& other) noexcept {
    kind_ = other.kind_;
    switch (kind_) {
        case Kind::Attribute:
            new (storage_) Attribute(std::move(*reinterpret_cast<Attribute*>(other.storage_)));
            break;
        case Kind::Relationship:
            new (storage_) Relationship(std::move(*reinterpret_cast<Relationship*>(other.storage_)));
            break;
        case Kind::None:
            break;
    }
    other.destroy();
}

// ============================================================================
// Constructors / Destructor
// ============================================================================

Property::Property()
    : kind_(Kind::None) {
}

Property::Property(Attribute&& attr)
    : kind_(Kind::Attribute) {
    new (storage_) Attribute(std::move(attr));
}

Property::Property(const Attribute& attr)
    : kind_(Kind::Attribute) {
    new (storage_) Attribute(attr);
}

Property::Property(Relationship&& rel)
    : kind_(Kind::Relationship) {
    new (storage_) Relationship(std::move(rel));
}

Property::Property(const Relationship& rel)
    : kind_(Kind::Relationship) {
    new (storage_) Relationship(rel);
}

Property::~Property() {
    destroy();
}

Property::Property(const Property& other)
    : kind_(Kind::None) {
    copy_from(other);
}

Property::Property(Property&& other) noexcept
    : kind_(Kind::None) {
    move_from(std::move(other));
}

Property& Property::operator=(const Property& other) {
    if (this != &other) {
        destroy();
        copy_from(other);
    }
    return *this;
}

Property& Property::operator=(Property&& other) noexcept {
    if (this != &other) {
        destroy();
        move_from(std::move(other));
    }
    return *this;
}

// ============================================================================
// Kind Queries
// ============================================================================

Property::Kind Property::kind() const {
    return kind_;
}

bool Property::is_empty() const {
    return kind_ == Kind::None;
}

bool Property::is_attribute() const {
    return kind_ == Kind::Attribute;
}

bool Property::is_relationship() const {
    return kind_ == Kind::Relationship;
}

// ============================================================================
// Access
// ============================================================================

const Attribute* Property::as_attribute() const {
    if (kind_ != Kind::Attribute) {
        return nullptr;
    }
    return reinterpret_cast<const Attribute*>(storage_);
}

Attribute* Property::as_attribute_mutable() {
    if (kind_ != Kind::Attribute) {
        return nullptr;
    }
    return reinterpret_cast<Attribute*>(storage_);
}

const Relationship* Property::as_relationship() const {
    if (kind_ != Kind::Relationship) {
        return nullptr;
    }
    return reinterpret_cast<const Relationship*>(storage_);
}

Relationship* Property::as_relationship_mutable() {
    if (kind_ != Kind::Relationship) {
        return nullptr;
    }
    return reinterpret_cast<Relationship*>(storage_);
}

// ============================================================================
// Modification
// ============================================================================

void Property::set_attribute(Attribute&& attr) {
    destroy();
    kind_ = Kind::Attribute;
    new (storage_) Attribute(std::move(attr));
}

void Property::set_attribute(const Attribute& attr) {
    destroy();
    kind_ = Kind::Attribute;
    new (storage_) Attribute(attr);
}

void Property::set_relationship(Relationship&& rel) {
    destroy();
    kind_ = Kind::Relationship;
    new (storage_) Relationship(std::move(rel));
}

void Property::set_relationship(const Relationship& rel) {
    destroy();
    kind_ = Kind::Relationship;
    new (storage_) Relationship(rel);
}

void Property::clear() {
    destroy();
}

// ============================================================================
// Utility
// ============================================================================

void Property::swap(Property& other) noexcept {
    if (this == &other) {
        return;
    }

    // Create temporary storage
    Property temp(std::move(*this));
    move_from(std::move(other));
    other.move_from(std::move(temp));
}

} // namespace v1
} // namespace lightusd
