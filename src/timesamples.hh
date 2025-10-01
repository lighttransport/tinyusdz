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
#include <type_traits>

#include "nonstd/optional.hpp"
#include "typed-array.hh"

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

// Helper function to check if a type_id represents a POD type
// POD types are numeric types that are trivial and standard layout
inline bool is_pod_type_id(uint32_t type_id) {
  // POD types: bool, numeric types (char, int, uint, float, double, half),
  // and their vector variants (float2, float3, etc.)
  // Excludes: string, token, path, array types, complex types
  return (type_id >= 8 && type_id <= 75) || // Basic numeric types and vectors
         (type_id == 5); // bool
}

} // namespace value

///
/// POD (Plain Old Data) time samples container with SoA layout
///
/// Stores time-sampled values for POD types in a Structure of Arrays layout
/// for optimal memory access patterns. The value data is stored as a raw byte
/// array (TypedArray<uint8_t>) with size sizeof(T) * N elements.
///
/// Only works with types that satisfy std::is_trivial and std::is_standard_layout.
/// This is defined before TimeSamples so it can be used as a member.
///
struct PODTimeSamples {
  uint32_t _type_id{0}; // TypeId from value-types.hh
  mutable std::vector<double> _times;
  mutable std::vector<bool> _blocked; // ValueBlock flags
  mutable TypedArray<uint8_t> _values; // Raw byte storage: sizeof(type_id) * N elements
  mutable bool _dirty{false};

  bool empty() const { return _times.empty(); }

  size_t size() const {
    if (_dirty) {
      update();
    }
    return _times.size();
  }

  void clear() {
    _times.clear();
    _blocked.clear();
    _values.clear();
    _type_id = 0;
    _dirty = true;
  }

  uint32_t type_id() const { return _type_id; }

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
    std::vector<bool> sorted_blocked(_blocked.size());

    // Need to know element size to reorder values
    if (_type_id == 0 || _values.empty()) {
      _dirty = false;
      return;
    }

    // Calculate element size from total size and number of elements
    size_t element_size = _values.size() / _times.size();
    TypedArray<uint8_t> sorted_values;
    sorted_values.resize(_values.size());

    for (size_t i = 0; i < indices.size(); ++i) {
      sorted_times[i] = _times[indices[i]];
      sorted_blocked[i] = _blocked[indices[i]];

      // Copy element bytes
      const uint8_t* src = _values.data() + (indices[i] * element_size);
      uint8_t* dst = sorted_values.data() + (i * element_size);
      std::copy(src, src + element_size, dst);
    }

    _times = std::move(sorted_times);
    _blocked = std::move(sorted_blocked);
    _values = std::move(sorted_values);

    _dirty = false;
  }

  /// Add a time/value sample with POD type checking
  /// T must satisfy std::is_trivial and std::is_standard_layout
  template<typename T>
  bool add_sample(double t, const T& value, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Set type_id on first sample
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::type_id();
    } else {
      // Verify type consistency
      if (_type_id != value::TypeTraits<T>::type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples: expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::type_id()) +
                    " (expected type: " + std::string(value::TypeTraits<T>::type_name()) + ").\n";
        }
        return false; // Type mismatch
      }
    }

    _times.push_back(t);
    _blocked.push_back(false);

    // Append raw bytes to values
    size_t old_size = _values.size();
    _values.resize(old_size + sizeof(T));
    std::memcpy(_values.data() + old_size, &value, sizeof(T));

    _dirty = true;
    return true;
  }

  /// Add a blocked sample (ValueBlock)
  template<typename T>
  bool add_blocked_sample(double t, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Set type_id on first sample
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::type_id();
    } else {
      // Verify type consistency
      if (_type_id != value::TypeTraits<T>::type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples (blocked sample): expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::type_id()) +
                    " (expected type: " + std::string(value::TypeTraits<T>::type_name()) + ").\n";
        }
        return false;
      }
    }

    _times.push_back(t);
    _blocked.push_back(true);

    // Still need to allocate space for blocked values
    size_t old_size = _values.size();
    _values.resize(old_size + sizeof(T));
    std::memset(_values.data() + old_size, 0, sizeof(T)); // Zero initialize

    _dirty = true;
    return true;
  }

  /// Get value at specific index with type checking
  template<typename T>
  bool get_value_at(size_t idx, T* value, bool* blocked = nullptr) const {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    if (!value) {
      return false;
    }

    if (_dirty) {
      update();
    }

    if (idx >= _times.size()) {
      return false;
    }

    // Verify type
    if (_type_id != value::TypeTraits<T>::type_id()) {
      return false;
    }

    // Copy bytes from values array
    const uint8_t* src = _values.data() + (idx * sizeof(T));
    std::memcpy(value, src, sizeof(T));

    if (blocked) {
      *blocked = _blocked[idx];
    }

    return true;
  }

  /// Check if sample exists at specific time
  bool has_sample_at(double t) const {
    if (_dirty) {
      update();
    }

    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    return (it != _times.end());
  }

  /// Get value at specific time with type checking
  template<typename T>
  bool get_value_at_time(double t, T* value, bool* blocked = nullptr) const {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

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
      return get_value_at<T>(idx, value, blocked);
    }

    return false;
  }

  const std::vector<double>& get_times() const {
    if (_dirty) {
      update();
    }
    return _times;
  }

  const std::vector<bool>& get_blocked() const {
    if (_dirty) {
      update();
    }
    return _blocked;
  }

  const TypedArray<uint8_t>& get_values() const {
    if (_dirty) {
      update();
    }
    return _values;
  }

  size_t estimate_memory_usage() const {
    size_t total = sizeof(PODTimeSamples);
    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity() * sizeof(bool);
    total += _values.capacity() * sizeof(uint8_t);
    return total;
  }
};

namespace value {

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

  bool empty() const {
    return _use_pod ? _pod_samples.empty() : _samples.empty();
  }

  size_t size() const {
    return _use_pod ? _pod_samples.size() : _samples.size();
  }

  void clear() {
    _samples.clear();
    _pod_samples.clear();
    _type_id = 0;
    _use_pod = false;
    _dirty = true;
  }

  /// Initialize TimeSamples with a specific type_id
  /// This determines whether to use POD optimization or regular storage
  bool init(uint32_t type_id) {
    if (!empty()) {
      return false; // Already initialized
    }
    _type_id = type_id;
    _use_pod = value::is_pod_type_id(type_id);
    if (_use_pod) {
      _pod_samples._type_id = type_id;
    }
    return true;
  }

  bool is_using_pod() const { return _use_pod; }

  void update() const {
    if (_use_pod) {
      _pod_samples.update();
    } else {
      std::sort(_samples.begin(), _samples.end(),
                [](const Sample &a, const Sample &b) { return a.t < b.t; });
    }
    _dirty = false;
  }

  bool has_sample_at(const double t) const;
  bool get_sample_at(const double t, Sample **s);

  nonstd::optional<double> get_time(size_t idx) const {
    if (_use_pod) {
      if (idx >= _pod_samples.size()) {
        return nonstd::nullopt;
      }
      if (_dirty) {
        update();
      }
      return _pod_samples.get_times()[idx];
    } else {
      if (idx >= _samples.size()) {
        return nonstd::nullopt;
      }
      if (_dirty) {
        update();
      }
      return _samples[idx].t;
    }
  }

  nonstd::optional<value::Value> get_value(size_t idx) const {
    if (_use_pod) {
      // Cannot return Value from POD storage without type info
      // User should use typed access methods instead
      return nonstd::nullopt;
    }

    if (idx >= _samples.size()) {
      return nonstd::nullopt;
    }

    if (_dirty) {
      update();
    }

    return _samples[idx].value;
  }

  uint32_t type_id() const {
    if (_use_pod) {
      return _pod_samples.type_id();
    }
    if (_samples.size()) {
      if (_dirty) {
        update();
      }
      return _samples[0].value.type_id();
    } else {
      return _type_id; // Return stored type_id if initialized
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

  bool add_sample(const Sample &s, std::string *err = nullptr) {
    // Auto-initialize on first sample
    if (empty() && !s.value.is_none()) {
      init(s.value.type_id());
    } else if (!empty() && !s.value.is_none() && _type_id != 0) {
      // Validate type_id matches on subsequent samples
      if (s.value.type_id() != _type_id) {
        if (err) {
          (*err) += "Type mismatch in TimeSamples: expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(s.value.type_id()) + " (expected type: " +
                    type_name() + ", got: " + s.value.type_name() + ").\n";
        }
        return false;
      }
    }

    if (_use_pod) {
      // Cannot easily redirect Sample to POD - just use regular storage
      _use_pod = false; // Fallback to regular storage
    }

    _samples.push_back(s);
    _dirty = true;
    return true;
  }

  // Value may be None(ValueBlock)
  bool add_sample(double t, const value::Value &v, std::string *err = nullptr) {
    // Auto-initialize on first sample
    if (empty() && !v.is_none()) {
      init(v.type_id());
    } else if (!empty() && !v.is_none() && _type_id != 0) {
      // Validate type_id matches on subsequent samples
      if (v.type_id() != _type_id) {
        if (err) {
          (*err) += "Type mismatch in TimeSamples: expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(v.type_id()) + " (expected type: " +
                    type_name() + ", got: " + v.type_name() + ").\n";
        }
        return false;
      }
    }

    if (_use_pod) {
      // Try to add to POD storage - requires template specialization
      // For now, fallback to regular storage
      // TODO: Add typed add_sample_pod<T>() method for POD optimization
      _use_pod = false;
    }

    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = v.is_none();
    _samples.push_back(s);
    _dirty = true;
    return true;
  }

  // We still need "dummy" value for type_name() and type_id()
  bool add_blocked_sample(double t, const value::Value &v, std::string *err = nullptr) {
    // Auto-initialize on first sample
    if (empty() && !v.is_none()) {
      init(v.type_id());
    } else if (!empty() && !v.is_none() && _type_id != 0) {
      // Validate type_id matches on subsequent samples
      if (v.type_id() != _type_id) {
        if (err) {
          (*err) += "Type mismatch in TimeSamples (blocked sample): expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(v.type_id()) + " (expected type: " +
                    type_name() + ", got: " + v.type_name() + ").\n";
        }
        return false;
      }
    }

    if (_use_pod) {
      // Fallback to regular storage for blocked samples with Value
      _use_pod = false;
    }

    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = true;

    _samples.emplace_back(s);
    _dirty = true;
    return true;
  }

  /// Typed add sample for POD types (optimization path)
  template<typename T>
  bool add_sample_pod(double t, const T& value, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_sample<T>(t, value, err);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + ".\n";
    }
    return false; // Not using POD storage
  }

  /// Typed add blocked sample for POD types (optimization path)
  template<typename T>
  bool add_blocked_sample_pod(double t, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_blocked_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_blocked_sample<T>(t, err);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + ".\n";
    }
    return false; // Not using POD storage
  }

  const std::vector<Sample> &get_samples() const {
    if (_use_pod) {
      // Cannot return samples from POD storage
      // Return empty vector or throw error
      static const std::vector<Sample> empty;
      return empty;
    }

    if (_dirty) {
      update();
    }
    return _samples;
  }

  std::vector<Sample> &samples() {
    if (_use_pod) {
      // Cannot return samples from POD storage
      static std::vector<Sample> empty;
      return empty;
    }

    if (_dirty) {
      update();
    }
    return _samples;
  }

  // Access POD samples directly
  const tinyusdz::PODTimeSamples& get_pod_samples() const {
    return _pod_samples;
  }

  tinyusdz::PODTimeSamples& pod_samples() {
    return _pod_samples;
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

    if (_use_pod) {
      total += _pod_samples.estimate_memory_usage();
    } else {
      for (const auto &sample : _samples) {
        total += sizeof(Sample);
        total += sample.value.estimate_memory_usage();
      }
    }

    return total;
  }

 private:
  mutable std::vector<Sample> _samples;
  mutable tinyusdz::PODTimeSamples _pod_samples;
  uint32_t _type_id{0};
  bool _use_pod{false};
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
