// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Attribute implementation

#include "lightusd/attribute.hh"

namespace lightusd {
namespace v1 {

// ============================================================================
// Implementation structure (PIMPL)
// ============================================================================

struct Attribute::Impl {
    TypeId type_id_ = TypeId::Invalid;
    Variability variability_ = Variability::Varying;

    bool has_default_ = false;
    bool blocked_ = false;

    Value default_value_;
    std::unique_ptr<TimeSamples> timesamples_;
    std::vector<Path> connections_;

    Impl() = default;

    Impl(TypeId type_id)
        : type_id_(type_id) {
    }

    Impl(const Impl& other)
        : type_id_(other.type_id_)
        , variability_(other.variability_)
        , has_default_(other.has_default_)
        , blocked_(other.blocked_)
        , default_value_(other.default_value_)
        , connections_(other.connections_) {
        if (other.timesamples_) {
            timesamples_.reset(new TimeSamples(*other.timesamples_));
        }
    }

    Impl(Impl&& other) noexcept
        : type_id_(other.type_id_)
        , variability_(other.variability_)
        , has_default_(other.has_default_)
        , blocked_(other.blocked_)
        , default_value_(std::move(other.default_value_))
        , timesamples_(std::move(other.timesamples_))
        , connections_(std::move(other.connections_)) {
        other.type_id_ = TypeId::Invalid;
        other.has_default_ = false;
        other.blocked_ = false;
    }

    bool is_authored() const {
        return has_default_ || blocked_ ||
               (timesamples_ && !timesamples_->empty()) ||
               !connections_.empty();
    }
};

// ============================================================================
// Constructors / Destructor
// ============================================================================

Attribute::Attribute()
    : impl_(new Impl()) {
}

Attribute::Attribute(TypeId type_id)
    : impl_(new Impl(type_id)) {
}

Attribute::~Attribute() = default;

Attribute::Attribute(const Attribute& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {
}

Attribute::Attribute(Attribute&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

Attribute& Attribute::operator=(const Attribute& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

Attribute& Attribute::operator=(Attribute&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

// ============================================================================
// Type
// ============================================================================

TypeId Attribute::type_id() const {
    return impl_->type_id_;
}

const char* Attribute::type_name() const {
    const TypeDescriptor* desc = get_type_descriptor(impl_->type_id_);
    return desc ? desc->name : "unknown";
}

bool Attribute::set_type_id(TypeId id) {
    // Can only set type if not yet authored
    if (impl_->is_authored()) {
        return false;
    }
    impl_->type_id_ = id;
    return true;
}

bool Attribute::is_array_type() const {
    return (static_cast<uint32_t>(impl_->type_id_) & static_cast<uint32_t>(TypeId::ArrayBit)) != 0;
}

// ============================================================================
// State Queries
// ============================================================================

bool Attribute::is_authored() const {
    return impl_->is_authored();
}

bool Attribute::is_blocked() const {
    return impl_->blocked_;
}

bool Attribute::has_default() const {
    return impl_->has_default_;
}

bool Attribute::has_timesamples() const {
    return impl_->timesamples_ && !impl_->timesamples_->empty();
}

bool Attribute::has_connection() const {
    return !impl_->connections_.empty();
}

// ============================================================================
// Default Value
// ============================================================================

Result<Value> Attribute::get_default() const {
    if (!impl_->has_default_) {
        return Error("No default value");
    }
    return impl_->default_value_;
}

bool Attribute::set_default(const Value& v) {
    TypeId val_type = v.type_id();

    // Allow ValueBlock (blocked)
    if (val_type == TypeId::ValueBlock) {
        impl_->blocked_ = true;
        impl_->has_default_ = false;
        impl_->default_value_ = Value();
        return true;
    }

    // Check type compatibility
    if (impl_->type_id_ == TypeId::Invalid) {
        // Set type from value
        impl_->type_id_ = val_type;
    } else if (val_type != impl_->type_id_) {
        // Type mismatch
        return false;
    }

    impl_->default_value_ = v;
    impl_->has_default_ = true;
    impl_->blocked_ = false;
    return true;
}

void Attribute::clear_default() {
    impl_->default_value_ = Value();
    impl_->has_default_ = false;
}

// ============================================================================
// Time Samples
// ============================================================================

const TimeSamples* Attribute::timesamples() const {
    return impl_->timesamples_.get();
}

TimeSamples* Attribute::timesamples_mutable() {
    if (!impl_->timesamples_) {
        impl_->timesamples_.reset(new TimeSamples());
    }
    return impl_->timesamples_.get();
}

void Attribute::set_timesamples(TimeSamples&& ts) {
    // Verify type compatibility
    TypeId ts_type = ts.value_type_id();
    if (ts_type != TypeId::Invalid) {
        if (impl_->type_id_ == TypeId::Invalid) {
            impl_->type_id_ = ts_type;
        } else if (ts_type != impl_->type_id_) {
            // Type mismatch - don't set
            return;
        }
    }

    impl_->timesamples_.reset(new TimeSamples(std::move(ts)));
}

void Attribute::set_timesamples(const TimeSamples& ts) {
    // Verify type compatibility
    TypeId ts_type = ts.value_type_id();
    if (ts_type != TypeId::Invalid) {
        if (impl_->type_id_ == TypeId::Invalid) {
            impl_->type_id_ = ts_type;
        } else if (ts_type != impl_->type_id_) {
            // Type mismatch - don't set
            return;
        }
    }

    impl_->timesamples_.reset(new TimeSamples(ts));
}

void Attribute::clear_timesamples() {
    impl_->timesamples_.reset();
}

// ============================================================================
// Value Access
// ============================================================================

Result<Value> Attribute::get(double time) const {
    // Check blocked first
    if (impl_->blocked_) {
        return Value::make_none();
    }

    // Check time samples first
    if (impl_->timesamples_ && !impl_->timesamples_->empty()) {
        return impl_->timesamples_->get_at_time(time);
    }

    // Fall back to default
    if (impl_->has_default_) {
        return impl_->default_value_;
    }

    return Error("No value authored");
}

// ============================================================================
// Connections
// ============================================================================

const std::vector<Path>& Attribute::connections() const {
    return impl_->connections_;
}

void Attribute::set_connection(const Path& target) {
    impl_->connections_.clear();
    if (target.is_valid()) {
        impl_->connections_.push_back(target);
    }
}

void Attribute::add_connection(const Path& target) {
    if (target.is_valid()) {
        impl_->connections_.push_back(target);
    }
}

void Attribute::clear_connections() {
    impl_->connections_.clear();
}

// ============================================================================
// Blocked State
// ============================================================================

void Attribute::set_blocked() {
    impl_->blocked_ = true;
}

void Attribute::clear_blocked() {
    impl_->blocked_ = false;
}

// ============================================================================
// Variability
// ============================================================================

Variability Attribute::variability() const {
    return impl_->variability_;
}

void Attribute::set_variability(Variability v) {
    impl_->variability_ = v;
}

// ============================================================================
// Utility
// ============================================================================

void Attribute::swap(Attribute& other) noexcept {
    impl_.swap(other.impl_);
}

void Attribute::clear() {
    impl_->type_id_ = TypeId::Invalid;
    impl_->variability_ = Variability::Varying;
    impl_->has_default_ = false;
    impl_->blocked_ = false;
    impl_->default_value_ = Value();
    impl_->timesamples_.reset();
    impl_->connections_.clear();
}

} // namespace v1
} // namespace lightusd
