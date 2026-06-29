// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Attribute implementation

#include "attribute.hh"

namespace tinyusdz {
namespace next {

Attribute::Attribute(const std::string& name, TypeId type_id)
    : name_(name), type_id_(type_id) {}

Attribute::Attribute(std::string&& name, TypeId type_id)
    : name_(std::move(name)), type_id_(type_id) {}

bool Attribute::has_value() const {
  return !default_value_.is_empty() || !time_samples_.empty();
}

void Attribute::set_default(Value value) {
  default_value_ = std::move(value);
}

void Attribute::add_time_sample(double time, Value value) {
  time_samples_[time] = std::move(value);
}

Value Attribute::value_at(double time) const {
  if (time_samples_.empty()) {
    return default_value_;
  }

  // Exact match
  auto it = time_samples_.find(time);
  if (it != time_samples_.end()) {
    return it->second;
  }

  // Find bracketing samples
  auto upper = time_samples_.upper_bound(time);
  if (upper == time_samples_.begin()) {
    // Before first sample - use first
    return upper->second;
  }

  auto lower = std::prev(upper);
  if (upper == time_samples_.end()) {
    // After last sample - use last
    return lower->second;
  }

  // Linear interpolation between bracketing samples (USD default for numeric
  // types). LerpValue falls back to held for non-interpolatable types.
  const double t0 = lower->first;
  const double t1 = upper->first;
  const double frac = (t1 > t0) ? (time - t0) / (t1 - t0) : 0.0;
  return LerpValue(lower->second, upper->second, frac);
}

std::vector<double> Attribute::sample_times() const {
  std::vector<double> times;
  times.reserve(time_samples_.size());
  for (const auto& pair : time_samples_) {
    times.push_back(pair.first);
  }
  return times;
}

void Attribute::clear_time_samples() {
  time_samples_.clear();
}

void Attribute::set_connection(const std::string& path) {
  connection_path_ = path;
}

}  // namespace next
}  // namespace tinyusdz
