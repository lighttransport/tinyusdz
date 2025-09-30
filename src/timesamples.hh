// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file timesamples.hh
/// @brief TimeSamples and TypedTimeSamples data structures for USD time-varying values
///
/// Contains data structures for handling time-sampled values in USD,
/// including both type-erased (TimeSamples) and strongly-typed (TypedTimeSamples)
/// variants with support for interpolation and value blocking.
///
/// TypedTimeSamples supports both AoS (Array of Structs) and SoA (Structure of Arrays)
/// layouts for optimal memory access patterns. The layout is controlled by the
/// TINYUSDZ_USE_TIMESAMPLES_SOA macro.
///
#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "nonstd/optional.hpp"

// Enable SoA (Structure of Arrays) layout for TypedTimeSamples
// Default is AoS (Array of Structs) layout
// #define TINYUSDZ_USE_TIMESAMPLES_SOA

namespace tinyusdz {

// Forward declaration of lerp for TypedTimeSamples
template<typename T>
T lerp(const T& a, const T& b, double t);

namespace value {

// Forward declarations from value-types.hh
class Value;
template<typename T> struct TypeTraits;
template<typename T> struct LerpTraits;
class TimeCode;
enum class TimeSampleInterpolationType;
bool Lerp(const Value &p0, const Value &p1, double dt, Value *result);

///
/// Type-erased time samples container
/// Each sample contains a time value and associated Value object.
/// `None`(ValueBlock) is represented by setting `Sample::blocked` true.
///
struct TimeSamples {
  struct Sample {
    double t;
    value::Value value;
    bool blocked{false};
  };

  bool empty() const { return _samples.empty(); }

  size_t size() const { return _samples.size(); }

  void clear() {
    _samples.clear();
    _dirty = true;
  }

  void update() const {
    std::sort(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; });

    _dirty = false;
  }

  bool has_sample_at(const double t) const;
  bool get_sample_at(const double t, Sample **s);

  nonstd::optional<double> get_time(size_t idx) const {
    if (idx >= _samples.size()) {
      return nonstd::nullopt;
    }

    if (_dirty) {
      update();
    }

    return _samples[idx].t;
  }

  nonstd::optional<value::Value> get_value(size_t idx) const {
    if (idx >= _samples.size()) {
      return nonstd::nullopt;
    }

    if (_dirty) {
      update();
    }

    return _samples[idx].value;
  }

  uint32_t type_id() const {
    if (_samples.size()) {
      if (_dirty) {
        update();
      }
      return _samples[0].value.type_id();
    } else {
      return 0; // TYPE_ID_INVALID equivalent
    }
  }

  std::string type_name() const {
    if (_samples.size()) {
      if (_dirty) {
        update();
      }
      return _samples[0].value.type_name();
    } else {
      return std::string();
    }
  }

  void add_sample(const Sample &s) {
    _samples.push_back(s);
    _dirty = true;
  }

  // Value may be None(ValueBlock)
  void add_sample(double t, const value::Value &v) {
    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = v.is_none();
    _samples.push_back(s);
    _dirty = true;
  }

  // We still need "dummy" value for type_name() and type_id()
  void add_blocked_sample(double t, const value::Value &v) {
    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = true;

    _samples.emplace_back(s);
    _dirty = true;
  }

  const std::vector<Sample> &get_samples() const {
    if (_dirty) {
      update();
    }
    return _samples;
  }

  std::vector<Sample> &samples() {
    if (_dirty) {
      update();
    }
    return _samples;
  }

#if 1  // TODO: Write implementation in .cc

    // Get value at specified time.
    // For non-interpolatable types(includes enums and unknown types)
    //
    // Return `Held` value even when TimeSampleInterpolationType is
    // Linear. Returns nullopt when specified time is out-of-range.
    template<typename T, std::enable_if_t<!value::LerpTraits<T>::supported(), std::nullptr_t> = nullptr>
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
        // TODO: Handle bloked
        if (const auto pv = _samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return false;
      } else {

        if (_samples.size() == 1) {
          if (const auto pv = _samples[0].value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }

        auto it = std::upper_bound(
          _samples.begin(), _samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

        const value::Value &v = it_minus_1->value;

        if (const T *pv = v.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return false;
      }
  }

  // Get value at specified time.
  // Return linearly interpolated value when TimeSampleInterpolationType is
  // Linear. Returns false when samples is empty or some internal error.
  template<typename T, std::enable_if_t<value::LerpTraits<T>::supported(), std::nullptr_t> = nullptr>
  bool get(T *dst, double t = value::TimeCode::Default(),
           TimeSampleInterpolationType interp =
               TimeSampleInterpolationType::Linear) const {
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
      // TODO: Handle bloked
      if (const auto pv = _samples[0].value.as<T>()) {
        (*dst) = *pv;
        return true;
      }
      return false;
    } else {

      if (_samples.size() == 1) {
        if (const auto pv = _samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return true;
      }

      if (interp == TimeSampleInterpolationType::Linear) {
        auto it = std::lower_bound(
            _samples.begin(), _samples.end(), t,
            [](const Sample &a, double tval) { return a.t < tval; });


        // MS STL does not allow seek vector iterator before begin
        // Issue #110
        const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

        size_t idx0 = size_t(std::max(
            int64_t(0),
            std::min(int64_t(_samples.size() - 1),
                     int64_t(std::distance(_samples.begin(), it_minus_1)))));
        size_t idx1 =
            size_t(std::max(int64_t(0), std::min(int64_t(_samples.size() - 1),
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
        dt = std::max(0.0, std::min(1.0, dt));

        const value::Value &p0 = _samples[idx0].value;
        const value::Value &p1 = _samples[idx1].value;

        value::Value p;
        if (!Lerp(p0, p1, dt, &p)) {
          return false;
        }

        if (const auto pv = p.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return false;
      } else {
        // Held
        auto it = std::upper_bound(
          _samples.begin(), _samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

        const value::Value &v = it_minus_1->value;

        if (const T *pv = v.as<T>()) {
          (*dst) = *pv;
          return true;
        }

        return false;
      }
    }

    return false;
  }
#endif

  size_t estimate_memory_usage() const {
    size_t total = sizeof(TimeSamples);
    for (const auto &sample : _samples) {
      total += sizeof(Sample);
      total += sample.value.estimate_memory_usage();
    }

    return total;
  }

 private:
  mutable std::vector<Sample> _samples;
  mutable bool _dirty{false};
};

} // namespace value

///
/// Strongly-typed time samples container with SoA/AoS layout support
///
/// Supports two memory layouts:
/// - AoS (Array of Structs): Default layout with Sample structs containing time, value, and blocked flag
/// - SoA (Structure of Arrays): Separate arrays for times, values, and blocked flags for better cache locality
///
/// The layout is controlled by TINYUSDZ_USE_TIMESAMPLES_SOA macro.
///
template <typename T>
struct TypedTimeSamples {
 public:
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  // AoS layout - Array of Structs (default)
  struct Sample {
    double t;
    T value;
    bool blocked{false};
  };

  bool empty() const { return _samples.empty(); }

  void update() const {
    std::sort(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; });

    _dirty = false;
    return;
  }
#else
  // SoA layout - Structure of Arrays
  bool empty() const { return _times.empty(); }

  void update() const {
    if (_times.empty()) {
      _dirty = false;
      return;
    }

    // Create index array for sorting
    std::vector<size_t> indices(_times.size());
    for (size_t i = 0; i < indices.size(); ++i) {
      indices[i] = i;
    }

    // Sort indices based on times
    std::sort(indices.begin(), indices.end(),
              [this](size_t a, size_t b) { return _times[a] < _times[b]; });

    // Reorder arrays based on sorted indices
    std::vector<double> sorted_times(_times.size());
    std::vector<T> sorted_values(_values.size());
    std::vector<bool> sorted_blocked(_blocked.size());

    for (size_t i = 0; i < indices.size(); ++i) {
      sorted_times[i] = _times[indices[i]];
      sorted_values[i] = _values[indices[i]];
      sorted_blocked[i] = _blocked[indices[i]];
    }

    _times = std::move(sorted_times);
    _values = std::move(sorted_values);
    _blocked = std::move(sorted_blocked);

    _dirty = false;
    return;
  }
#endif

  // Get value at specified time.
  // For non-interpolatable types(includes enums and unknown types)
  //
  // Return `Held` value even when TimeSampleInterpolationType is
  // Linear. Returns nullopt when specified time is out-of-range.
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

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    // AoS layout implementation
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
      auto it = std::upper_bound(
        _samples.begin(), _samples.end(), t,
        [](double tval, const Sample &a) { return tval < a.t; });

      const auto it_minus_1 = (it == _samples.begin()) ? _samples.begin() : (it - 1);

      (*dst) = it_minus_1->value;
      return true;
    }
#else
    // SoA layout implementation
    if (value::TimeCode(t).is_default()) {
      // FIXME: Use the first item for now.
      // TODO: Handle blocked
      (*dst) = _values[0];
      return true;
    } else {

      if (_times.size() == 1) {
        (*dst) = _values[0];
        return true;
      }

      // Held = nearest preceding value for a given time.
      auto it = std::upper_bound(_times.begin(), _times.end(), t);
      size_t idx = (it == _times.begin()) ? 0 : static_cast<size_t>(std::distance(_times.begin(), it) - 1);

      (*dst) = _values[idx];
      return true;
    }
#endif
  }

  // TODO: Move to .cc to save compile time.
  // Get value at specified time.
  // Return linearly interpolated value when TimeSampleInterpolationType is
  // Linear. Returns nullopt when specified time is out-of-range.
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

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    // AoS layout implementation
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

        const T &p0 = _samples[idx0].value;
        const T &p1 = _samples[idx1].value;

        const T p = lerp(p0, p1, dt);

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
#else
    // SoA layout implementation
    if (value::TimeCode(t).is_default()) {
      // FIXME: Use the first item for now.
      // TODO: Handle blocked
      (*dst) = _values[0];
      return true;
    } else {

      if (_times.size() == 1) {
        (*dst) = _values[0];
        return true;
      }

      auto it = std::lower_bound(_times.begin(), _times.end(), t);

      if (interp == value::TimeSampleInterpolationType::Linear) {

        // MS STL does not allow seek vector iterator before begin
        // Issue #110
        const auto it_minus_1 = (it == _times.begin()) ? _times.begin() : (it - 1);

        size_t idx0 = size_t((std::max)(
            int64_t(0),
            (std::min)(int64_t(_times.size() - 1),
                     int64_t(std::distance(_times.begin(), it_minus_1)))));
        size_t idx1 =
            size_t((std::max)(int64_t(0), (std::min)(int64_t(_times.size() - 1),
                                                 int64_t(idx0) + 1)));

        double tl = _times[idx0];
        double tu = _times[idx1];

        double dt = (t - tl);
        if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
          // slope is zero.
          dt = 0.0;
        } else {
          dt /= (tu - tl);
        }

        // Just in case.
        dt = (std::max)(0.0, (std::min)(1.0, dt));

        const T &p0 = _values[idx0];
        const T &p1 = _values[idx1];

        const T p = lerp(p0, p1, dt);

        (*dst) = std::move(p);
        return true;
      } else {
        if (it == _times.end()) {
          // ???
          return false;
        }

        size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
        (*dst) = _values[idx];
        return true;
      }
    }
#endif

    return false;
  }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  // AoS layout - Sample struct available
  void add_sample(const Sample &s) {
    _samples.push_back(s);
    _dirty = true;
  }
#endif

  void add_sample(const double t, const T &v) {
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    Sample s;
    s.t = t;
    s.value = v;
    _samples.emplace_back(s);
#else
    _times.push_back(t);
    _values.push_back(v);
    _blocked.push_back(false);
#endif
    _dirty = true;
  }

  void add_blocked_sample(const double t) {
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    Sample s;
    s.t = t;
    s.blocked = true;
    _samples.emplace_back(s);
#else
    _times.push_back(t);
    _values.emplace_back(); // Default construct value
    _blocked.push_back(true);
#endif
    _dirty = true;
  }

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  // Set value at a specific time (SoA only)
  bool set_value_at(const double t, const T &v) {
    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _times.end()) {
      size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
      _values[idx] = v;
      _blocked[idx] = false;
      return true;
    }
    return false;
  }
#endif

  bool has_sample_at(const double t) const {
    if (_dirty) {
      update();
    }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &s) {
      return std::fabs(t - s.t) < std::numeric_limits<double>::epsilon();
    });

    return (it != _samples.end());
#else
    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    return (it != _times.end());
#endif
  }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  bool get_sample_at(const double t, Sample **dst) {
    if (!dst) {
      return false;
    }

    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample &sample) {
      return std::fabs(t - sample.t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _samples.end()) {
      (*dst) = &(*it);
      return true;
    }
    return false;
  }
#else
  // SoA layout - return individual components instead of Sample struct
  bool get_value_at(const double t, T *value, bool *blocked = nullptr) const {
    if (!value) {
      return false;
    }

    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _times.end()) {
      size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
      *value = _values[idx];
      if (blocked) {
        *blocked = _blocked[idx];
      }
      return true;
    }
    return false;
  }
#endif

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  const std::vector<Sample> &get_samples() const {
    if (_dirty) {
      update();
    }

    return _samples;
  }

  std::vector<Sample> &samples() {
    if (_dirty) {
      update();
    }

    return _samples;
  }
#else
  // SoA layout - provide access to individual arrays
  const std::vector<double> &get_times() const {
    if (_dirty) {
      update();
    }
    return _times;
  }

  const std::vector<T> &get_values() const {
    if (_dirty) {
      update();
    }
    return _values;
  }

  const std::vector<bool> &get_blocked() const {
    if (_dirty) {
      update();
    }
    return _blocked;
  }
#endif

  // From typeless timesamples.
  bool from_timesamples(const value::TimeSamples &ts) {
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
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
#else
    _times.clear();
    _values.clear();
    _blocked.clear();

    for (size_t i = 0; i < ts.size(); i++) {
      if (ts.get_samples()[i].value.type_id() != value::TypeTraits<T>::type_id()) {
        return false;
      }
      _times.push_back(ts.get_samples()[i].t);
      _blocked.push_back(ts.get_samples()[i].blocked);
      if (const auto pv = ts.get_samples()[i].value.as<T>()) {
        _values.push_back(*pv);
      } else {
        return false;
      }
    }
#endif
    _dirty = true;

    return true;
  }

  size_t size() const {
    if (_dirty) {
      update();
    }
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    return _samples.size();
#else
    return _times.size();
#endif
  }

 private:
  // Need to be sorted when looking up the value.
#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
  mutable std::vector<Sample> _samples;
#else
  // SoA layout - separate arrays for better cache locality
  mutable std::vector<double> _times;
  mutable std::vector<T> _values;
  mutable std::vector<bool> _blocked;
#endif
  mutable bool _dirty{false};
};

} // namespace tinyusdz
