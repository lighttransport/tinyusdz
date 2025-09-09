// SPDX-License-Identifier: Apache 2.0

///
/// @file timesamples.hh
/// @brief USD TimeSamples and Animatable types
///
/// Contains TypedTimeSamples for time-varying data and Animatable wrapper
/// that can hold either scalar values or time-sampled data.
///
#pragma once

#include <algorithm>
#include <vector>
#include <limits>

#include "value-types.hh"
#include "math-util.inc"

namespace tinyusdz {

///
/// @brief Typed time-sampled data container
///
/// TypedTimeSamples stores a collection of time-value pairs for animatable
/// properties. Supports both interpolated (linear) and held (nearest) sampling.
///
/// Example usage:
/// ```cpp
/// TypedTimeSamples<float> samples;
/// samples.add_sample(0.0, 1.0f);
/// samples.add_sample(1.0, 2.0f);
/// 
/// float value;
/// samples.get(&value, 0.5);  // Returns interpolated value 1.5f
/// ```
///
template <typename T>
struct TypedTimeSamples {
 public:
  ///
  /// Individual time-value sample
  ///
  struct Sample {
    double t;           ///< Time value
    T value;           ///< Data value at this time
    bool blocked{false}; ///< True if this is a blocked/None sample
  };

  ///
  /// Check if no samples are stored
  ///
  bool empty() const { return _samples.empty(); }

  ///
  /// Sort samples by time (called automatically when needed)
  ///
  void update() const {
    std::sort(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; });

    _dirty = false;

    return;
  }

  ///
  /// Get value at specified time for non-interpolatable types
  ///
  /// For non-interpolatable types (includes enums and unknown types),
  /// returns held value even when TimeSampleInterpolationType is Linear.
  /// Returns false when specified time is out-of-range.
  ///
  template<typename V = T, std::enable_if_t<!value::LerpTraits<V>::supported(), std::nullptr_t> = nullptr>
  bool get(T *dst, double t = value::TimeCode::Default(),
           value::TimeSampleInterpolationType interp =
               value::TimeSampleInterpolationType::Linear) const {

    (void)interp;

    if (!dst) {
      return false;
    }

    if (empty()) {
      return false;
    }

    if (_dirty) {
      update();
    }

    if (value::TimeCode(t).is_default()) {
      // FIXME: Use the first item for now.
      // TODO: Handle blocked
      (*dst) = _samples[0].value;
      return true;
    } else {

      if (_samples.size() == 1) {
        (*dst) = _samples[0].value;
        return true;
      }

      // Held = nearest preceding value for a given time.
      // example:
      // input = 0.0: 100, 1.0: 200
      //
      // t -1.0 => 100(time 0.0)
      // t 0.0 => 100(time 0.0)
      // t 0.1 => 100(time 0.0)
      // t 0.9 => 100(time 0.0)
      // t 1.0 => 200(time 1.0)
      //
      // This can be achieved by using upper_bound, and subtract 1 from the found position.
      auto it = std::upper_bound(
        _samples.begin(), _samples.end(), t,
        [](double tval, const Sample &a) { return tval < a.t; });

      const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

      (*dst) = it_minus_1->value;
      return true;
    }

  }

  ///
  /// Get value at specified time for interpolatable types
  ///
  /// Returns linearly interpolated value when TimeSampleInterpolationType is
  /// Linear. Returns false when specified time is out-of-range.
  ///
  template<typename V = T, std::enable_if_t<value::LerpTraits<V>::supported(), std::nullptr_t> = nullptr>
  bool get(T *dst, double t = value::TimeCode::Default(),
           value::TimeSampleInterpolationType interp =
               value::TimeSampleInterpolationType::Linear) const {
    if (!dst) {
      return false;
    }

    if (empty()) {
      return false;
    }

    if (_dirty) {
      update();
    }

    if (value::TimeCode(t).is_default()) {
      // FIXME: Use the first item for now.
      // TODO: Handle blocked
      (*dst) = _samples[0].value;
      return true;
    } else {

      if (_samples.size() == 1) {
        (*dst) = _samples[0].value;
        return true;
      }

      auto it = std::lower_bound(
        _samples.begin(), _samples.end(), t,
        [](const Sample &a, double tval) { return a.t < tval; });

      if (interp == value::TimeSampleInterpolationType::Linear) {

        // MS STL does not allow seek vector iterator before begin
        // Issue #110
        const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

        size_t idx0 = size_t((std::max)(
            int64_t(0),
            (std::min)(int64_t(_samples.size() - 1),
                     int64_t(std::distance(_samples.begin(), it_minus_1)))));
        size_t idx1 =
            size_t((std::max)(int64_t(0), (std::min)(int64_t(_samples.size() - 1),
                                                 int64_t(idx0) + 1)));

        double tl = _samples[idx0].t;
        double tu = _samples[idx1].t;

        double dt = (t - tl);
        if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
          // slope is zero.
          dt = 0.0;
        } else {
          dt /= (tu - tl);
        }

        // Just in case.
        dt = (std::max)(0.0, (std::min)(1.0, dt));

        const value::Value &pv0 = _samples[idx0].value;
        const value::Value &pv1 = _samples[idx1].value;

        if (pv0.type_id() != pv1.type_id()) {
          // Type mismatch.
          return false;
        }

        // To concrete type
        const T *p0 = pv0.as<T>();
        const T *p1 = pv1.as<T>();

        if (!p0 || !p1) {
          return false;
        }

        const T p = lerp(*p0, *p1, dt);

        (*dst) = std::move(p);
        return true;
      } else {
        if (it == _samples.end()) {
          // ???
          return false;
        }

        (*dst) = it->value;
        return true;
      }
    }

    return false;
  }

  ///
  /// Add a time-value sample
  ///
  void add_sample(const Sample &s) {
    _samples.push_back(s);
    _dirty = true;
  }

  ///
  /// Add a time-value sample
  ///
  void add_sample(const double t, const T &v) {
    Sample s;
    s.t = t;
    s.value = v;
    _samples.emplace_back(s);
    _dirty = true;
  }

  ///
  /// Add a blocked (None/ValueBlock) sample
  ///
  void add_blocked_sample(const double t) {
    Sample s;
    s.t = t;
    s.blocked = true;
    _samples.emplace_back(s);
    _dirty = true;
  }

  ///
  /// Check if there's a sample at the specified time
  ///
  bool has_sample_at(const double t) const {
    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &s) {
      return tinyusdz::math::is_close(t, s.t);
    });

    return (it != _samples.end());
  }

  ///
  /// Get sample at the specified time
  ///
  bool get_sample_at(const double t, Sample **dst) {
    if (!dst) {
      return false;
    }

    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &sample) {
      return math::is_close(t, sample.t);
    });

    if (it != _samples.end()) {
      (*dst) = &(*it); 
    }
    return false;
  }

  ///
  /// Get all samples (const version)
  ///
  const std::vector<Sample> &get_samples() const {
    if (_dirty) {
      update();
    }

    return _samples;
  }

  ///
  /// Get all samples (mutable version)
  ///
  std::vector<Sample> &samples() {
    if (_dirty) {
      update();
    }

    return _samples;
  }

  ///
  /// Initialize from typeless timesamples
  ///
  bool from_timesamples(const value::TimeSamples &ts) {
    std::vector<Sample> buf;
    for (size_t i = 0; i < ts.size(); i++) {
      if (ts.get_samples()[i].value.type_id() != value::TypeTraits<T>::type_id()) {
        return false;
      }
      Sample s;
      s.t = ts.get_samples()[i].t;
      s.blocked = ts.get_samples()[i].blocked;
      if (const auto pv = ts.get_samples()[i].value.as<T>()) {
        s.value = (*pv);
      } else {
        return false;
      }

      buf.push_back(s);
    }


    _samples = std::move(buf);
    _dirty = true;

    return true;
  }

  ///
  /// Get number of samples
  ///
  size_t size() const {
    if (_dirty) {
      update();
    }
    return _samples.size();
  }

 private:
  // Need to be sorted when looking up the value.
  mutable std::vector<Sample> _samples;
  mutable bool _dirty{false};
};

///
/// @brief Animatable property wrapper
///
/// Animatable can hold either a scalar (default) value or time-sampled data.
/// This is the primary container for USD properties that can change over time.
///
/// Example usage:
/// ```cpp
/// Animatable<float> animated_float;
/// 
/// // Set scalar value
/// animated_float.set(3.14f);
/// 
/// // Or set time samples
/// animated_float.add_sample(0.0, 1.0f);
/// animated_float.add_sample(1.0, 2.0f);
/// ```
///
template <typename T>
struct Animatable {
 public:
  ///
  /// Check if this property is blocked (has ValueBlock/None)
  ///
  bool is_blocked() const { return _blocked; }

  ///
  /// Check if this property contains time-sampled data
  ///
  bool is_timesamples() const {
    if (is_blocked()) {
      return false;
    }

    if (_has_value) {
      return false;
    }

    return !_ts.empty();
  }

  ///
  /// Check if this property contains only scalar data
  ///
  bool is_scalar() const {
    if (is_blocked()) {
      return false;
    }
    
    return _ts.empty();
  }

  ///
  /// Get value at specific time
  ///
  /// For scalar properties, returns the scalar value regardless of time.
  /// For time-sampled properties, returns interpolated or held value at the given time.
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
  /// Get scalar (default) value
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

  ///
  /// Get default value (alias for get_scalar)
  ///
  bool get_default(T *v) const {
    return get_scalar(v);
  }

  ///
  /// Add a time-value sample
  ///
  void add_sample(const double t, const T &v) { _ts.add_sample(t, v); }

  ///
  /// Add a blocked (None/ValueBlock) sample to timesamples
  ///
  void add_blocked_sample(const double t) { _ts.add_blocked_sample(t); }

  ///
  /// Set scalar value
  ///
  void set(const T &v) {
    _value = v;
    _blocked = false;
    _has_value = true;
  }

  ///
  /// Set default value (alias for set)
  ///
  void set_default(const T &v) {
    set(v);
  }

  ///
  /// Set time-sampled data
  ///
  void set(const TypedTimeSamples<T> &ts) {
    _ts = ts;
  }

  ///
  /// Set time-sampled data (move version)
  ///
  void set(TypedTimeSamples<T> &&ts) {
    _ts = std::move(ts);
  }

  ///
  /// Set time-sampled data
  ///
  void set_timesamples(const TypedTimeSamples<T> &ts) {
    return set(ts);
  }

  ///
  /// Set time-sampled data (move version)
  ///
  void set_timesamples(TypedTimeSamples<T> &&ts) {
    return set(ts);
  }

  ///
  /// Clear scalar value
  ///
  void clear_scalar() {
    _has_value = false;
  }

  ///
  /// Clear all time samples
  ///
  void clear_timesamples() {
    _ts.samples().clear();
  }

  ///
  /// Check if scalar value is set
  ///
  bool has_value() const {
    return _has_value;
  }

  ///
  /// Check if default value is set (alias for has_value)
  ///
  bool has_default() const {
    return has_value();
  }

  ///
  /// Check if time samples are present
  ///
  bool has_timesamples() const {
    return _ts.size();
  }

  ///
  /// Get time-sampled data container
  ///
  const TypedTimeSamples<T> &get_timesamples() const { return _ts; }

  ///
  /// Default constructor
  ///
  Animatable() {}

  ///
  /// Constructor with scalar value
  ///
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