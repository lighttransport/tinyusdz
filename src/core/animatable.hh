// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// animatable.hh - Animatable<T> template for scalar and timeSamples values
//
#pragma once

#include "timesamples.hh"

namespace tinyusdz {

//
// Scalar(default) and/or TimeSamples
//
template <typename T>
struct Animatable {
 public:
  bool is_blocked() const { return _blocked; }

  bool is_timesamples() const {
    if (is_blocked()) {
      return false;
    }

    if (_has_value) {
      return false;
    }

    return !_ts.empty();
  }

  bool is_scalar() const {
    if (is_blocked()) {
      return false;
    }

    return _ts.empty();
  }

  ///
  /// Get value at specific time.
  ///
  bool get(double t, T *v,
           const value::TimeSampleInterpolationType tinerp =
               value::TimeSampleInterpolationType::Linear) const {
    if (!v) {
      return false;
    }

    if (is_blocked()) {
      return false;
    }

    if (value::TimeCode(t).is_default()) {
      if (has_value()) {
        (*v) = _value;
        return true;
      }
    }

    if (has_timesamples()) {
      return _ts.get(v, t, tinerp);
    }

    if (has_default()) {
      return get_scalar(v);
    }

    return false;
  }

  ///
  /// Get scalar(default) value.
  ///
  bool get_scalar(T *v) const {
    if (!v) {
      return false;
    }

    if (is_blocked()) {
      return false;
    } else if (has_value()) {
      (*v) = _value;
      return true;
    }

    // timesamples
    return false;
  }

  bool get_default(T *v) const {
    return get_scalar(v);
  }

  // TimeSamples
  // void set(double t, const T &v);

  void add_sample(const double t, const T &v) { _ts.add_sample(t, v); }

  // Add None(ValueBlock) sample to timesamples
  void add_blocked_sample(const double t) { _ts.add_blocked_sample(t); }

  // Scalar
  void set(const T &v) {
    _value = v;
    _blocked = false;
    _has_value = true;
  }

  // Move overload for scalar - avoids copy for large vectors
  void set(T &&v) {
    _value = std::move(v);
    _blocked = false;
    _has_value = true;
  }

  void set_default(const T &v) {
    set(v);
  }

  void set_default(T &&v) {
    set(std::move(v));
  }

  void set(const TypedTimeSamples<T> &ts) {
    _ts = ts;
  }

  void set(TypedTimeSamples<T> &&ts) {
    _ts = std::move(ts);
  }

  void set_timesamples(const TypedTimeSamples<T> &ts) {
    return set(ts);
  }

  void set_timesamples(TypedTimeSamples<T> &&ts) {
    return set(std::move(ts));
  }

  void clear_scalar() {
    _has_value = false;
  }

  void clear_timesamples() {
    _ts.samples().clear();
  }

  bool has_value() const {
    return _has_value;
  }

  bool has_default() const {
    return has_value();
  }

  bool has_timesamples() const {
    return _ts.size();
  }

  const TypedTimeSamples<T> &get_timesamples() const { return _ts; }

  Animatable() {}

  Animatable(const T &v) {
    set(v);
  }

  // TODO: Init with timesamples

 private:
  // scalar
  T _value{};
  bool _has_value{false};
  bool _blocked{false};

  // timesamples
  TypedTimeSamples<T> _ts;
};

}  // namespace tinyusdz
