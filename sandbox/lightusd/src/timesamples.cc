// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - TimeSamples implementation

#include "lightusd/timesamples.hh"
#include <algorithm>
#include <cmath>

namespace lightusd {
namespace v1 {

// ============================================================================
// Implementation structure (PIMPL)
// ============================================================================

struct TimeSamples::Impl {
    std::vector<double> times_;
    std::vector<Value> values_;
    TypeId value_type_id_ = TypeId::Invalid;

    Impl() = default;

    Impl(const Impl& other)
        : times_(other.times_)
        , values_(other.values_)
        , value_type_id_(other.value_type_id_) {
    }

    Impl(Impl&& other) noexcept
        : times_(std::move(other.times_))
        , values_(std::move(other.values_))
        , value_type_id_(other.value_type_id_) {
        other.value_type_id_ = TypeId::Invalid;
    }

    // Find insertion point for time (maintains sorted order)
    size_t find_insertion_point(double time) const {
        auto it = std::lower_bound(times_.begin(), times_.end(), time);
        return static_cast<size_t>(it - times_.begin());
    }

    // Find exact time match
    bool find_exact(double time, size_t& out_index) const {
        auto it = std::lower_bound(times_.begin(), times_.end(), time);
        if (it != times_.end() && *it == time) {
            out_index = static_cast<size_t>(it - times_.begin());
            return true;
        }
        return false;
    }
};

// ============================================================================
// Constructors / Destructor
// ============================================================================

TimeSamples::TimeSamples()
    : impl_(new Impl()) {
}

TimeSamples::~TimeSamples() = default;

TimeSamples::TimeSamples(const TimeSamples& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {
}

TimeSamples::TimeSamples(TimeSamples&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

TimeSamples& TimeSamples::operator=(const TimeSamples& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

TimeSamples& TimeSamples::operator=(TimeSamples&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

// ============================================================================
// Queries
// ============================================================================

TypeId TimeSamples::value_type_id() const {
    return impl_->value_type_id_;
}

bool TimeSamples::empty() const {
    return impl_->times_.empty();
}

size_t TimeSamples::size() const {
    return impl_->times_.size();
}

const std::vector<double>& TimeSamples::times() const {
    return impl_->times_;
}

bool TimeSamples::is_blocked(size_t index) const {
    if (index >= impl_->values_.size()) {
        return false;
    }
    return impl_->values_[index].is_none();
}

// ============================================================================
// Adding Samples
// ============================================================================

bool TimeSamples::add_sample(double time, const Value& value) {
    // Check for NaN/Inf time
    if (std::isnan(time) || std::isinf(time)) {
        return false;
    }

    // Check type consistency
    TypeId new_type = value.type_id();
    if (new_type == TypeId::Invalid) {
        return false;
    }

    // First sample establishes type (unless it's a blocked value)
    if (impl_->value_type_id_ == TypeId::Invalid) {
        if (new_type != TypeId::ValueBlock) {
            impl_->value_type_id_ = new_type;
        }
    } else if (new_type != impl_->value_type_id_ && new_type != TypeId::ValueBlock) {
        // Type mismatch (ValueBlock is always allowed)
        return false;
    }

    // Find insertion point
    size_t idx = impl_->find_insertion_point(time);

    // Check if replacing existing sample
    if (idx < impl_->times_.size() && impl_->times_[idx] == time) {
        impl_->values_[idx] = value;
    } else {
        // Insert new sample
        impl_->times_.insert(impl_->times_.begin() + static_cast<ptrdiff_t>(idx), time);
        impl_->values_.insert(impl_->values_.begin() + static_cast<ptrdiff_t>(idx), value);
    }

    return true;
}

bool TimeSamples::add_blocked_sample(double time) {
    return add_sample(time, Value::make_none());
}

// ============================================================================
// Getting Samples
// ============================================================================

Result<Value> TimeSamples::get_sample(size_t index) const {
    if (index >= impl_->values_.size()) {
        return Error("Index out of range");
    }
    return impl_->values_[index];
}

Result<double> TimeSamples::get_time(size_t index) const {
    if (index >= impl_->times_.size()) {
        return Error("Index out of range");
    }
    return impl_->times_[index];
}

Result<Value> TimeSamples::get_at_time_exact(double time) const {
    size_t idx;
    if (!impl_->find_exact(time, idx)) {
        return Error("Time not found");
    }
    return impl_->values_[idx];
}

Result<Value> TimeSamples::get_at_time(double time) const {
    if (impl_->times_.empty()) {
        return Error("No samples");
    }

    // Find lower bound
    auto it = std::lower_bound(impl_->times_.begin(), impl_->times_.end(), time);

    if (it == impl_->times_.end()) {
        // Time is past all samples - return last
        return impl_->values_.back();
    }

    size_t idx = static_cast<size_t>(it - impl_->times_.begin());

    if (*it == time) {
        // Exact match
        return impl_->values_[idx];
    }

    if (idx == 0) {
        // Time is before first sample - return first
        return impl_->values_.front();
    }

    // Return sample at or before time (held value)
    return impl_->values_[idx - 1];
}

Result<TimeSamples::Bracket> TimeSamples::find_bracket(double time) const {
    if (impl_->times_.empty()) {
        return Error("No samples");
    }

    Bracket bracket;

    auto it = std::lower_bound(impl_->times_.begin(), impl_->times_.end(), time);

    if (it == impl_->times_.end()) {
        // Past end - both indices point to last
        size_t last = impl_->times_.size() - 1;
        bracket.lower_index = last;
        bracket.upper_index = last;
        bracket.lower_time = impl_->times_[last];
        bracket.upper_time = impl_->times_[last];
        bracket.exact_match = false;
        return bracket;
    }

    size_t idx = static_cast<size_t>(it - impl_->times_.begin());

    if (*it == time) {
        // Exact match
        bracket.lower_index = idx;
        bracket.upper_index = idx;
        bracket.lower_time = time;
        bracket.upper_time = time;
        bracket.exact_match = true;
        return bracket;
    }

    if (idx == 0) {
        // Before first - both indices point to first
        bracket.lower_index = 0;
        bracket.upper_index = 0;
        bracket.lower_time = impl_->times_[0];
        bracket.upper_time = impl_->times_[0];
        bracket.exact_match = false;
        return bracket;
    }

    // Between two samples
    bracket.lower_index = idx - 1;
    bracket.upper_index = idx;
    bracket.lower_time = impl_->times_[idx - 1];
    bracket.upper_time = impl_->times_[idx];
    bracket.exact_match = false;
    return bracket;
}

// ============================================================================
// Modification
// ============================================================================

bool TimeSamples::remove_sample(size_t index) {
    if (index >= impl_->times_.size()) {
        return false;
    }

    impl_->times_.erase(impl_->times_.begin() + static_cast<ptrdiff_t>(index));
    impl_->values_.erase(impl_->values_.begin() + static_cast<ptrdiff_t>(index));

    // Reset type if all samples removed
    if (impl_->times_.empty()) {
        impl_->value_type_id_ = TypeId::Invalid;
    }

    return true;
}

void TimeSamples::clear() {
    impl_->times_.clear();
    impl_->values_.clear();
    impl_->value_type_id_ = TypeId::Invalid;
}

// ============================================================================
// Utility
// ============================================================================

void TimeSamples::swap(TimeSamples& other) noexcept {
    impl_.swap(other.impl_);
}

} // namespace v1
} // namespace lightusd
