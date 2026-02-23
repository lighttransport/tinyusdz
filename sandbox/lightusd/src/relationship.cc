// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Relationship implementation

#include "lightusd/relationship.hh"
#include <algorithm>

namespace lightusd {
namespace v1 {

// ============================================================================
// Constructors / Destructor
// ============================================================================

Relationship::Relationship()
    : authored_(false) {
}

Relationship::~Relationship() = default;

Relationship::Relationship(const Relationship& other)
    : targets_(other.targets_)
    , authored_(other.authored_) {
}

Relationship::Relationship(Relationship&& other) noexcept
    : targets_(std::move(other.targets_))
    , authored_(other.authored_) {
    other.authored_ = false;
}

Relationship& Relationship::operator=(const Relationship& other) {
    if (this != &other) {
        targets_ = other.targets_;
        authored_ = other.authored_;
    }
    return *this;
}

Relationship& Relationship::operator=(Relationship&& other) noexcept {
    if (this != &other) {
        targets_ = std::move(other.targets_);
        authored_ = other.authored_;
        other.authored_ = false;
    }
    return *this;
}

// ============================================================================
// State Queries
// ============================================================================

bool Relationship::is_authored() const {
    return authored_;
}

bool Relationship::has_targets() const {
    return !targets_.empty();
}

size_t Relationship::target_count() const {
    return targets_.size();
}

// ============================================================================
// Target Access
// ============================================================================

const std::vector<Path>& Relationship::targets() const {
    return targets_;
}

Path Relationship::target(size_t index) const {
    if (index >= targets_.size()) {
        return Path();
    }
    return targets_[index];
}

// ============================================================================
// Target Modification
// ============================================================================

void Relationship::set_target(const Path& target) {
    targets_.clear();
    if (target.is_valid()) {
        targets_.push_back(target);
    }
    authored_ = true;
}

void Relationship::set_targets(const std::vector<Path>& targets) {
    targets_.clear();
    targets_.reserve(targets.size());
    for (const auto& t : targets) {
        if (t.is_valid()) {
            targets_.push_back(t);
        }
    }
    authored_ = true;
}

void Relationship::add_target(const Path& target) {
    if (target.is_valid()) {
        targets_.push_back(target);
    }
    authored_ = true;
}

bool Relationship::remove_target(size_t index) {
    if (index >= targets_.size()) {
        return false;
    }
    targets_.erase(targets_.begin() + static_cast<ptrdiff_t>(index));
    return true;
}

void Relationship::clear_targets() {
    targets_.clear();
    // Note: authored_ remains true - clearing is still an authored state
}

// ============================================================================
// Utility
// ============================================================================

void Relationship::swap(Relationship& other) noexcept {
    targets_.swap(other.targets_);
    std::swap(authored_, other.authored_);
}

void Relationship::clear() {
    targets_.clear();
    authored_ = false;
}

} // namespace v1
} // namespace lightusd
