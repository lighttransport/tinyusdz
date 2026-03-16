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
#include <cstdint>  // for SIZE_MAX
#include <limits>
#include <vector>
#include <type_traits>

#include "nonstd/optional.hpp"
#include "typed-array.hh"
#include "value-types.hh"
#include "buffer-util.hh"

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

// Forward declaration
struct TimeSamples;

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

  // Sentinel value for blocked samples in _data_offsets
  static constexpr uint32_t BLOCKED_OFFSET = UINT32_MAX;

  bool empty() const {
    // Check unified storage first, then legacy storage
    return _times.empty() && _samples.empty();
  }

  size_t size() const {
    if (_dirty) {
      update();
    }
    // Prefer unified storage, fallback to legacy
    return !_times.empty() ? _times.size() : _samples.size();
  }

  bool is_initialized() const {
    return _type_id != 0;
  }

  void clear();

  /// Pre-allocate internal vectors for the expected number of samples.
  /// Call before adding samples in a loop when the total count is known.
  void reserve(size_t n) {
    _times.reserve(n);
    _blocked.reserve(n);
  }

  /// Move constructor
  TimeSamples(TimeSamples&& other) noexcept;

  /// Move assignment operator
  TimeSamples& operator=(TimeSamples&& other) noexcept;

  /// Copy constructor
  TimeSamples(const TimeSamples& other);

  /// Copy assignment operator
  TimeSamples& operator=(const TimeSamples& other);

  // Default constructor
  TimeSamples() = default;

  /// Set the type_id for metadata-only use (e.g. all-blocked TimeSamples).
  /// For normal usage, add_sample<T>() auto-detects the type on first call.
  /// Replaces the old init() method.
  void set_type_id(uint32_t tid) {
    if (tid != 0 && _type_id == 0) {
      _type_id = tid;
    }
  }


  /// Cast the TimeSamples' type to a role type if the underlying types are compatible.
  /// This allows reinterpreting stored base types (e.g., float3) as role types (e.g., color3f).
  /// @param role_type_id The target role type's type_id
  /// @return true if the cast was successful, false if the underlying types don't match
  bool cast_to_role_type(uint32_t role_type_id);

  /// Check if unified binary storage has samples (i.e., _times is not empty)
  bool is_using_binary_storage() const { return !_times.empty(); }

  /// Check if storing array data
  bool is_array() const { return _is_array; }

  /// Element size for binary storage (sizeof(T))
  uint32_t element_size() const { return _element_size; }

 private:
  bool has_unified_samples() const {
    return !_times.empty();
  }

  bool has_generic_samples() const {
    return !_samples.empty() && !has_unified_samples();
  }

  bool validate_type_or_init(uint32_t expected_type_id, std::string* err,
                             const char* op_name) {
    if (!is_initialized()) {
      set_type_id(expected_type_id);
      return true;
    }

    if (_type_id != expected_type_id) {
      if (err) {
        (*err) += std::string(op_name) + " type mismatch: expected type_id " +
                  std::to_string(_type_id) + " but got " +
                  std::to_string(expected_type_id) + ".\n";
      }
      return false;
    }

    return true;
  }

  template<typename T>
  struct UnifiedArrayRef {
    const T* data{nullptr};
    size_t count{0};
    bool blocked{false};
    bool available{false};
  };

  template<typename T>
  bool resolve_unified_array_ref(size_t idx, uint32_t expected_type_id,
                                 UnifiedArrayRef<T>* ref) const {
    if (!ref) {
      return false;
    }

    *ref = {};

    if (_dirty) {
      update();
    }

    if (_times.empty() || !_is_array) {
      return false;
    }

    if (idx >= _times.size() || idx >= _blocked.size()) {
      return false;
    }

    if (_type_id != expected_type_id) {
      return false;
    }

    ref->available = true;
    ref->blocked = (_blocked[idx] != 0);
    if (ref->blocked) {
      return true;
    }

    if (idx >= _data_offsets.size()) {
      return false;
    }

    uint32_t byte_offset = _data_offsets[idx];
    if (byte_offset == BLOCKED_OFFSET) {
      ref->blocked = true;
      return true;
    }

    ref->count = get_array_count(idx);
    if (ref->count == 0) {
      ref->data = nullptr;
      return true;
    }

    if (static_cast<size_t>(byte_offset) + sizeof(T) * ref->count > _data.size()) {
      return false;
    }

    ref->data = reinterpret_cast<const T*>(_data.data() + byte_offset);
    return true;
  }

  template<typename T>
  bool reconstruct_unified_array_value(size_t idx, uint32_t expected_type_id,
                                       value::Value* dst) const {
    if (!dst) {
      return false;
    }

    UnifiedArrayRef<T> ref;
    if (!resolve_unified_array_ref<T>(idx, expected_type_id, &ref) || ref.blocked) {
      return false;
    }

    if (expected_type_id == value::TypeTraits<std::vector<T>>::type_id()) {
      std::vector<T> vec;
      if (ref.count > 0) {
        vec.assign(ref.data, ref.data + ref.count);
      }
      *dst = value::Value(std::move(vec));
      return true;
    }

    if (expected_type_id == value::TypeTraits<TypedArray<T>>::type_id()) {
      TypedArray<T> typed;
      typed.resize(ref.count);
      if (ref.count > 0) {
        std::memcpy(typed.data(), ref.data, sizeof(T) * ref.count);
      }
      *dst = value::Value(std::move(typed));
      return true;
    }

    return false;
  }

  bool reconstruct_binary_sample(size_t idx, Sample* sample) const;  // Defined in timesamples.cc


 public:
  void update() const;

  bool has_sample_at(const double t) const;
  bool get_sample_at(const double t, Sample **s);

  nonstd::optional<double> get_time(size_t idx) const {
    // Check unified storage first
    if (!_times.empty()) {
      if (idx >= _times.size()) {
        return nonstd::nullopt;
      }
      if (_dirty) {
        update();
      }
      return _times[idx];
    }

    // Fallback to generic sample storage
    if (idx >= _samples.size()) {
      return nonstd::nullopt;
    }
    if (_dirty) {
      update();
    }
    return _samples[idx].t;
  }

  nonstd::optional<value::Value> get_value(size_t idx) const {
    const auto &samples = get_samples();
    if (idx >= samples.size()) {
      return nonstd::nullopt;
    }
    return samples[idx].value;
  }

  uint32_t type_id() const {
    // Unified storage uses _type_id directly
    if (!_times.empty() || _type_id != 0) {
      return _type_id;
    }
    // Legacy storage: get from samples if available
    if (_samples.size()) {
      if (_dirty) {
        update();
      }
      // If first sample has valid type (not type_id 1), use it
      uint32_t sample_type_id = _samples[0].value.type_id();
      if (sample_type_id != 1) {
        return sample_type_id;
      }
      // Otherwise use stored type_id (for all-VALUE_BLOCK case)
      return _type_id;
    } else {
      return _type_id; // Return stored type_id if initialized
    }
  }

  std::string type_name() const {
    // Check if using unified storage or have a type_id
    if (!_times.empty() || _type_id != 0) {
      // Get type name from type_id
      if (_type_id != 0) {
        return value::GetTypeName(_type_id);
      } else {
        return std::string();
      }
    }

    // Original path for generic Value storage
    if (_samples.size()) {
      if (_dirty) {
        update();
      }
      return _samples[0].value.type_name();
    } else {
      return std::string();
    }
  }

  bool add_sample(const Sample &s, std::string *err = nullptr);  // Defined in timesamples.cc

  // Value may be None(ValueBlock)
  bool add_sample(double t, const value::Value &v, std::string *err = nullptr);      // Defined in timesamples.cc

  // We still need "dummy" value for type_name() and type_id()
  bool add_blocked_sample(double t, const value::Value &v, std::string *err = nullptr);  // Defined in timesamples.cc


  template <typename T,
            typename std::enable_if<
                !std::is_same<typename std::decay<T>::type, value::Value>::value &&
                !std::is_same<typename std::decay<T>::type, Sample>::value,
                int>::type = 0>
  bool add_sample(double t, const T& value, std::string *err = nullptr,
                  size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::uses_binary_timesample_scalar_storage_v<T>) {
      set_type_id(value::TypeTraits<T>::type_id());
      return add_binary_sample<T>(t, value, err);
    } else {
      return add_sample(t, value::Value(value), err);
    }
  }

  template <typename T>
  bool add_array_sample(double t, const std::vector<T>& value,
                        std::string *err = nullptr,
                        size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
      if (!validate_type_or_init(value::TypeTraits<std::vector<T>>::type_id(),
                                   err, "add_array_sample<std::vector>")) {
        return false;
      }
      return add_array_sample<T>(t, value.data(), value.size(), err);
    } else {
      return add_sample(t, value::Value(value), err);
    }
  }

  template <typename T>
  bool add_array_sample(double t, const TypedArray<T>& value,
                        std::string *err = nullptr,
                        size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
      if (!validate_type_or_init(value::TypeTraits<TypedArray<T>>::type_id(),
                                   err, "add_array_sample<TypedArray>")) {
        return false;
      }
      return add_array_sample<T>(t, value.data(), value.size(), err);
    } else {
      std::vector<T> vec(value.data(), value.data() + value.size());
      return add_sample(t, value::Value(vec), err);
    }
  }

  template<typename T>
  bool add_blocked_sample(double t, std::string *err = nullptr,
                          size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::uses_binary_timesample_scalar_storage_v<T>) {
      set_type_id(value::TypeTraits<T>::type_id());
      return add_binary_blocked_sample<T>(t, err);
    } else {
      return add_blocked_sample(t, value::Value(T{}), err);
    }
  }

  /// Get TypedArray sample at specific time
  template<typename T>
  bool get_typed_array_at_time(double t, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
      return false;
    }

    if (_dirty) {
      update();
    }

    // Check if storing TypedArray data
    if (_type_id != value::TypeTraits<TypedArray<T>>::type_id()) {
      return false;
    }

    // Use unified storage - find index by time
    if (!_times.empty()) {
      size_t idx = find_time_index_in_unified(t);
      if (idx != kNotFound) {
        return get_typed_array_at<T>(idx, typed_array, blocked);
      }
    }

    return false;
  }

  /// Get TypedArray sample at specific index
  template<typename T>
  bool get_typed_array_at(size_t idx, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
      return false;
    }

    if (_dirty) {
      update();
    }

    UnifiedArrayRef<T> ref;
    if (resolve_unified_array_ref<T>(idx, value::TypeTraits<TypedArray<T>>::type_id(), &ref)) {
      if (ref.blocked) {
        if (blocked) *blocked = true;
        return false;
      }

      typed_array->resize(ref.count);
      if (ref.count > 0) {
        std::memcpy(typed_array->data(), ref.data, sizeof(T) * ref.count);
      }
      if (blocked) *blocked = false;
      return true;
    }

    return false;
  }

  /// Get TypedArrayView at specific index
  /// Returns a view for TypedArray or array data (std::vector)
  /// Returns an empty view for blocked values or non-array data
  template<typename T>
  TypedArrayView<const T> get_typed_array_view_at(size_t idx) const {
    if (_dirty) {
      update();
    }

    UnifiedArrayRef<T> ref;
    if (resolve_unified_array_ref<T>(idx, value::TypeTraits<TypedArray<T>>::type_id(), &ref)) {
      if (ref.blocked || ref.count == 0) {
        return TypedArrayView<const T>();
      }
      return TypedArrayView<const T>(ref.data, ref.count);
    }

    // For regular Value storage (generic path)
    if (idx >= _samples.size()) {
      return TypedArrayView<const T>();  // Empty view
    }

    const Sample& sample = _samples[idx];

    // Check if blocked
    if (sample.blocked) {
      return TypedArrayView<const T>();  // Empty view for blocked values
    }

    // Try to get as TypedArray first
    if (const TypedArray<T>* typed_array = sample.value.as<TypedArray<T>>()) {
      if (typed_array->data() && typed_array->size() > 0) {
        return TypedArrayView<const T>(*typed_array);
      }
    }

    // Try to get as std::vector
    if (const std::vector<T>* vec = sample.value.as<std::vector<T>>()) {
      if (!vec->empty()) {
        return TypedArrayView<const T>(*vec);
      }
    }

    // Not array data or unsupported type
    return TypedArrayView<const T>();  // Empty view
  }

  /// Get TypedArrayView at specific time
  template<typename T>
  TypedArrayView<const T> get_typed_array_view_at_time(double t) const {
    if (_dirty) {
      update();
    }

    // Check unified storage first
    if (!_times.empty()) {
      size_t idx = find_time_index_in_unified(t);
      if (idx != kNotFound) {
        return get_typed_array_view_at<T>(idx);
      }
    }

    // For regular Value storage
    size_t idx = find_time_index_in_samples(t);
    if (idx != kNotFound) {
      return get_typed_array_view_at<T>(idx);
    }

    return TypedArrayView<const T>();  // Empty view
  }

  const std::vector<Sample> &get_samples() const;  // Defined in timesamples.cc

  std::vector<Sample> &samples();  // Defined in timesamples.cc

  // Get value at specified time.
  // For non-interpolatable types (includes enums and unknown types)
  //
  // Return `Held` value even when TimeSampleInterpolationType is
  // Linear. Returns false when specified time is out-of-range.
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

    const auto &samples = get_samples();
    if (samples.empty()) {
      return false;
    }

    if (value::TimeCode(t).is_default()) {
        // Return the first non-blocked sample.
        for (const auto &s : samples) {
          if (!s.blocked) {
            if (const auto pv = s.value.as<T>()) {
              (*dst) = *pv;
              return true;
            }
            return false;
          }
        }
        return false;
      } else {

        if (samples.size() == 1) {
          if (samples[0].blocked) return false;
          if (const auto pv = samples[0].value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }

        auto it = std::upper_bound(
          samples.begin(), samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_held = (it == samples.begin()) ? samples.begin() : (it - 1);

        if (it_held->blocked) {
          return false;
        }

        const value::Value &v = it_held->value;

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

    const auto &samples = get_samples();
    if (samples.empty()) {
      return false;
    }

    if (value::TimeCode(t).is_default()) {
      // Return the first non-blocked sample.
      for (const auto &s : samples) {
        if (!s.blocked) {
          if (const auto pv = s.value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }
      }
      return false;
    } else {

      if (samples.size() == 1) {
        if (samples[0].blocked) return false;
        if (const auto pv = samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return false;
      }

      if (interp == TimeSampleInterpolationType::Linear) {
        auto it = std::lower_bound(
            samples.begin(), samples.end(), t,
            [](const Sample &a, double tval) { return a.t < tval; });

        // MS STL does not allow seek vector iterator before begin
        // Issue #110
        const auto it_minus_1 = (it == samples.begin()) ? samples.begin() : (it - 1);

        size_t idx0 = size_t(std::max(
            int64_t(0),
            std::min(int64_t(samples.size() - 1),
                     int64_t(std::distance(samples.begin(), it_minus_1)))));
        size_t idx1 =
            size_t(std::max(int64_t(0), std::min(int64_t(samples.size() - 1),
                                                 int64_t(idx0) + 1)));

        // If either endpoint is blocked, fall back to the non-blocked one.
        if (samples[idx0].blocked && samples[idx1].blocked) {
          return false;
        }
        if (samples[idx0].blocked) {
          if (const auto pv = samples[idx1].value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }
        if (samples[idx1].blocked) {
          if (const auto pv = samples[idx0].value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }

        double tl = samples[idx0].t;
        double tu = samples[idx1].t;

        double dt = (t - tl);
        if (std::fabs(tu - tl) < std::numeric_limits<double>::epsilon()) {
          // slope is zero.
          dt = 0.0;
        } else {
          dt /= (tu - tl);
        }

        // Just in case.
        dt = std::max(0.0, std::min(1.0, dt));

        const value::Value &p0 = samples[idx0].value;
        const value::Value &p1 = samples[idx1].value;

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
        // Held interpolation
        auto it = std::upper_bound(
          samples.begin(), samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_held = (it == samples.begin()) ? samples.begin() : (it - 1);

        if (it_held->blocked) {
          return false;
        }

        const value::Value &v = it_held->value;

        if (const T *pv = v.as<T>()) {
          (*dst) = *pv;
          return true;
        }

        return false;
      }
    }
  }

  size_t estimate_memory_usage() const;  // Defined in timesamples.cc

  //
  // Unified array methods (work directly with TimeSamples storage)
  //

  /// Add array sample to flat byte buffer
  template<typename T>
  bool add_array_sample(double t, const T* values, size_t count, std::string* err = nullptr) {
    static_assert(value::uses_binary_timesample_array_storage_v<T>,
                  "add_array_sample requires binary-serializable element types except bool");

    if (!validate_type_or_init(_type_id != 0 ? _type_id : value::TypeTraits<std::vector<T>>::type_id(),
                                 err, "add_array_sample")) {
      return false;
    }

    _element_size = static_cast<uint32_t>(sizeof(T));
    _is_array = true;

    _times.push_back(t);
    _blocked.push_back(0);

    // Append array data to flat buffer
    uint32_t byte_offset = static_cast<uint32_t>(_data.size());
    size_t data_size = sizeof(T) * count;
    _data.resize(_data.size() + data_size);
    std::memcpy(_data.data() + byte_offset, values, data_size);

    _data_offsets.push_back(byte_offset);
    _array_counts.push_back(static_cast<uint32_t>(count));

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add matrix array sample (delegates to add_array_sample)
  template<typename T>
  bool add_matrix_array_sample(double t, const T* matrices, size_t count, std::string* err = nullptr) {
    return add_array_sample<T>(t, matrices, count, err);
  }

  bool add_bool_array_sample(double t, const std::vector<bool>& value,
                             std::string *err = nullptr) {
    return add_sample(t, value::Value(value), err);
  }

  //
  // Unified binary scalar sample methods — all types go into _data
  //

  /// Add a binary-serializable scalar sample to flat byte buffer
  template<typename T>
  bool add_binary_sample(double t, const T& value, std::string* err = nullptr) {
    static_assert(value::uses_binary_timesample_scalar_storage_v<T>,
                  "add_binary_sample requires binary-serializable types except bool");

    if (!validate_type_or_init(value::TypeTraits<T>::type_id(), err,
                                 "add_binary_sample")) {
      return false;
    }

    _element_size = static_cast<uint32_t>(sizeof(T));

    _times.push_back(t);
    _blocked.push_back(0);

    uint32_t byte_offset = static_cast<uint32_t>(_data.size());
    _data.resize(_data.size() + sizeof(T));
    std::memcpy(_data.data() + byte_offset, &value, sizeof(T));
    _data_offsets.push_back(byte_offset);

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add a blocked binary-serializable scalar sample
  template<typename T>
  bool add_binary_blocked_sample(double t, std::string* err = nullptr) {
    static_assert(value::uses_binary_timesample_scalar_storage_v<T>,
                  "add_binary_blocked_sample requires binary-serializable types except bool");

    if (!validate_type_or_init(value::TypeTraits<T>::type_id(), err,
                                 "add_binary_blocked_sample")) {
      return false;
    }

    _element_size = static_cast<uint32_t>(sizeof(T));

    _times.push_back(t);
    _blocked.push_back(1);  // Blocked
    _data_offsets.push_back(BLOCKED_OFFSET);

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  //
  // std::vector<T> support (Phase 2 Step 3)
  //
  // NOTE: We do NOT override add_sample(double t, const std::vector<T>&) because
  // that would break interpolation support for std::vector<T> types.
  // The existing Value-based add_sample works correctly with interpolation.
  // We only provide convenience getters below.

  /// Get sample as std::vector<T> - retrieves array data into a vector
  /// Works with both unified binary storage and generic Value storage
  /// @param idx Sample index
  /// @param out_vec Output vector to fill with data
  /// @param out_blocked Optional: set to true if sample is blocked
  /// @return true if successful, false if index out of range or wrong type
  template<typename T>
  bool get_vector_at(size_t idx, std::vector<T>* out_vec, bool* out_blocked = nullptr) const {
    if (!out_vec) {
      return false;
    }

    if (_dirty) {
      update();
    }

    if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
      UnifiedArrayRef<T> ref;
      if (resolve_unified_array_ref<T>(idx, value::TypeTraits<std::vector<T>>::type_id(), &ref)) {
        if (ref.blocked) {
          if (out_blocked) *out_blocked = true;
          return false;
        }

        if (ref.count == 0) {
          out_vec->clear();
        } else {
          out_vec->assign(ref.data, ref.data + ref.count);
        }
        if (out_blocked) *out_blocked = false;
        return true;
      }
    }

    // Check generic Value storage
    if (idx >= _samples.size()) {
      return false;
    }

    const Sample& sample = _samples[idx];

    // Check if blocked
    if (sample.blocked) {
      if (out_blocked) *out_blocked = true;
      return false;
    }

    // Try to get as std::vector<T>
    if (const std::vector<T>* vec = sample.value.as<std::vector<T>>()) {
      *out_vec = *vec;
      if (out_blocked) *out_blocked = false;
      return true;
    }

    // Try to get as TypedArray<T>
    if constexpr (value::uses_binary_timesample_array_storage_v<T>) {
      if (const TypedArray<T>* typed_array = sample.value.as<TypedArray<T>>()) {
        if (typed_array->data() && typed_array->size() > 0) {
          out_vec->assign(typed_array->data(), typed_array->data() + typed_array->size());
          if (out_blocked) *out_blocked = false;
          return true;
        }
      }
    }

    // Type mismatch or unsupported
    return false;
  }

  /// Get sample as std::vector<T> at specific time
  /// @param t Time value
  /// @param out_vec Output vector to fill with data
  /// @param out_blocked Optional: set to true if sample is blocked
  /// @return true if successful, false if no sample at time or wrong type
  template<typename T>
  bool get_vector_at_time(double t, std::vector<T>* out_vec, bool* out_blocked = nullptr) const {
    if (!out_vec) {
      return false;
    }

    if (_dirty) {
      update();
    }

    // Check unified binary storage
    if (!_times.empty()) {
      size_t idx = find_time_index_in_unified(t);
      if (idx != kNotFound) {
        return get_vector_at<T>(idx, out_vec, out_blocked);
      }
      return false;  // Time not found
    }

    // Check generic Value storage
    size_t idx = find_time_index_in_samples(t);
    if (idx != kNotFound) {
      return get_vector_at<T>(idx, out_vec, out_blocked);
    }

    return false;  // Time not found
  }

  //
  // Accessor methods for binary storage
  //

  const std::vector<double>& get_times() const {
    if (_dirty) { update(); }
    return _times;
  }

  const Buffer<16>& get_blocked() const {
    if (_dirty) { update(); }
    return _blocked;
  }

  const std::vector<uint8_t>& get_data() const {
    if (_dirty) { update(); }
    return _data;
  }

  const std::vector<uint32_t>& get_data_offsets() const {
    if (_dirty) { update(); }
    return _data_offsets;
  }

  size_t get_array_count(size_t idx) const {
    if (idx >= _array_counts.size()) {
      return 0;
    }
    return _array_counts[idx];
  }

  const std::vector<uint32_t>& get_array_counts() const {
    return _array_counts;
  }

 private:
  // Generic path storage (for non-binary Value types: string, token, dict, etc.)
  mutable std::vector<Sample> _samples;

  // Flat binary storage (for trivially-copyable POD types)
  mutable std::vector<double> _times;
  mutable Buffer<16> _blocked;                      // Blocked flags (one byte per sample)
  mutable std::vector<uint8_t> _data;               // Flat byte buffer for ALL binary values
  mutable std::vector<uint32_t> _data_offsets;      // Per-sample byte offset into _data
  mutable std::vector<uint32_t> _array_counts;      // Per-sample element count (arrays only)

  // Metadata
  uint32_t _type_id{0};
  uint32_t _element_size{0};                        // sizeof(T) for binary elements
  mutable bool _dirty{false};
  bool _is_array{false};                            // true if storing array data

  // _pod_samples removed - using unified storage directly

  void invalidate_reconstructed_samples_cache() {
    _samples.clear();
  }

  /// Find index for time value in _times vector using epsilon comparison
  /// @param t Time value to search for
  /// @return Index if found, or size_t(-1) if not found
  size_t find_time_index_in_unified(double t) const {
    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(_times.begin(), _times.end(), t - eps);
    if ((it != _times.end()) && (std::fabs(*it - t) < eps)) {
      return static_cast<size_t>(std::distance(_times.begin(), it));
    }
    if (it != _times.begin()) {
      auto prev = it - 1;
      if (std::fabs(*prev - t) < eps) {
        return static_cast<size_t>(std::distance(_times.begin(), prev));
      }
    }
    return static_cast<size_t>(-1);  // Not found
  }

  /// Find index for time value in _samples vector using epsilon comparison
  /// @param t Time value to search for
  /// @return Index if found, or size_t(-1) if not found
  size_t find_time_index_in_samples(double t) const {
    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(
        _samples.begin(), _samples.end(), t - eps,
        [](const Sample &s, double v) { return s.t < v; });
    if ((it != _samples.end()) && (std::fabs(it->t - t) < eps)) {
      return static_cast<size_t>(std::distance(_samples.begin(), it));
    }
    if (it != _samples.begin()) {
      auto prev = it - 1;
      if (std::fabs(prev->t - t) < eps) {
        return static_cast<size_t>(std::distance(_samples.begin(), prev));
      }
    }
    return static_cast<size_t>(-1);  // Not found
  }

  static constexpr size_t kNotFound = static_cast<size_t>(-1);

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
    if (_samples.size() < 2 ||
        std::is_sorted(_samples.begin(), _samples.end(),
                       [](const Sample &a, const Sample &b) { return a.t < b.t; })) {
      _dirty = false;
      return;
    }

    // Adaptive sort: use insertion sort for nearly-sorted data (common for animation)
    size_t inversions = 0;
    const size_t scan = std::min(_samples.size() - 1, size_t(100));
    for (size_t i = 0; i < scan; ++i) {
      if (_samples[i].t > _samples[i + 1].t) ++inversions;
    }

    if (inversions * 20 < _samples.size()) {
      // Insertion sort — O(n) for nearly-sorted data
      for (size_t i = 1; i < _samples.size(); ++i) {
        if (_samples[i].t >= _samples[i - 1].t) continue;
        Sample key = std::move(_samples[i]);
        size_t j = i;
        while (j > 0 && _samples[j - 1].t > key.t) {
          _samples[j] = std::move(_samples[j - 1]);
          --j;
        }
        _samples[j] = std::move(key);
      }
    } else {
      std::sort(_samples.begin(), _samples.end(),
                [](const Sample &a, const Sample &b) { return a.t < b.t; });
    }

    _dirty = false;
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
    std::vector<uint8_t> sorted_blocked(_blocked.size());

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
               value::TimeSampleInterpolationType::Linear) const;

  // Get value at specified time.
  // Return linearly interpolated value when TimeSampleInterpolationType is
  // Linear. Returns nullopt when specified time is out-of-range.
  template<typename V = T, std::enable_if_t<value::LerpTraits<V>::supported(), std::nullptr_t> = nullptr>
  bool get(T *dst, double t = value::TimeCode::Default(),
           value::TimeSampleInterpolationType interp =
               value::TimeSampleInterpolationType::Linear) const;

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
    _blocked.push_back(0);  // false = 0
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
    _blocked.push_back(1);  // true = 1
#endif
    _dirty = true;
  }

#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
  // Set value at a specific time (SoA only)
  bool set_value_at(const double t, const T &v) {
    if (_dirty) {
      update();
    }

    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(_times.begin(), _times.end(), t - eps);
    if ((it == _times.end()) || (std::fabs(*it - t) >= eps)) {
      if ((it == _times.begin()) || (std::fabs(*(it - 1) - t) >= eps)) {
        return false;
      }
      it = it - 1;
    }

    size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
    _values[idx] = v;
    _blocked[idx] = 0;  // false = 0
    return true;
  }
#endif

  bool has_sample_at(const double t) const {
    if (_dirty) {
      update();
    }

#ifndef TINYUSDZ_USE_TIMESAMPLES_SOA
    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(
        _samples.begin(), _samples.end(), t - eps,
        [](const Sample &s, double v) { return s.t < v; });

    if ((it != _samples.end()) && (std::fabs(it->t - t) < eps)) {
      return true;
    }
    if (it != _samples.begin()) {
      return std::fabs((it - 1)->t - t) < eps;
    }
    return false;
#else
    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(_times.begin(), _times.end(), t - eps);

    if ((it != _times.end()) && (std::fabs(*it - t) < eps)) {
      return true;
    }
    if (it != _times.begin()) {
      return std::fabs(*(it - 1) - t) < eps;
    }
    return false;
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

    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(
        _samples.begin(), _samples.end(), t - eps,
        [](const Sample &sample, double v) { return sample.t < v; });
    if ((it == _samples.end()) || (std::fabs(it->t - t) >= eps)) {
      if ((it == _samples.begin()) || (std::fabs((it - 1)->t - t) >= eps)) {
        return false;
      }
      it = it - 1;
    }

    (*dst) = &(*it);
    return true;
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

    const double eps = std::numeric_limits<double>::epsilon();
    auto it = std::lower_bound(_times.begin(), _times.end(), t - eps);
    if ((it == _times.end()) || (std::fabs(*it - t) >= eps)) {
      if ((it == _times.begin()) || (std::fabs(*(it - 1) - t) >= eps)) {
        return false;
      }
      it = it - 1;
    }

    size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
    *value = _values[idx];
    if (blocked) {
      *blocked = _blocked[idx];
    }
    return true;
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

  const std::vector<uint8_t> &get_blocked() const {
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
  mutable std::vector<uint8_t> _blocked;
#endif
  mutable bool _dirty{false};
};

//
// Extern template declarations to reduce compile time
// These are instantiated in timesamples.cc
//

// Integer types (binary-serializable, non-lerp'able)
extern template struct TypedTimeSamples<bool>;
extern template struct TypedTimeSamples<int32_t>;
extern template struct TypedTimeSamples<uint32_t>;
extern template struct TypedTimeSamples<int64_t>;
extern template struct TypedTimeSamples<uint64_t>;

// Floating point scalar types (binary-serializable, lerp'able)
extern template struct TypedTimeSamples<value::half>;
extern template struct TypedTimeSamples<float>;
extern template struct TypedTimeSamples<double>;

// Vector types (binary-serializable, lerp'able)
extern template struct TypedTimeSamples<value::half2>;
extern template struct TypedTimeSamples<value::half3>;
extern template struct TypedTimeSamples<value::half4>;
extern template struct TypedTimeSamples<value::float2>;
extern template struct TypedTimeSamples<value::float3>;
extern template struct TypedTimeSamples<value::float4>;
extern template struct TypedTimeSamples<value::double2>;
extern template struct TypedTimeSamples<value::double3>;
extern template struct TypedTimeSamples<value::double4>;

// Integer vector types (binary-serializable, non-lerp'able)
extern template struct TypedTimeSamples<value::int2>;
extern template struct TypedTimeSamples<value::int3>;
extern template struct TypedTimeSamples<value::int4>;

// Quaternion types (binary-serializable, lerp'able)
extern template struct TypedTimeSamples<value::quath>;
extern template struct TypedTimeSamples<value::quatf>;
extern template struct TypedTimeSamples<value::quatd>;

// Matrix types (lerp'able)
extern template struct TypedTimeSamples<value::matrix2f>;
extern template struct TypedTimeSamples<value::matrix3f>;
extern template struct TypedTimeSamples<value::matrix4f>;
extern template struct TypedTimeSamples<value::matrix2d>;
extern template struct TypedTimeSamples<value::matrix3d>;
extern template struct TypedTimeSamples<value::matrix4d>;

// Role types (binary-serializable, lerp'able)
extern template struct TypedTimeSamples<value::normal3h>;
extern template struct TypedTimeSamples<value::normal3f>;
extern template struct TypedTimeSamples<value::normal3d>;
extern template struct TypedTimeSamples<value::vector3h>;
extern template struct TypedTimeSamples<value::vector3f>;
extern template struct TypedTimeSamples<value::vector3d>;
extern template struct TypedTimeSamples<value::point3h>;
extern template struct TypedTimeSamples<value::point3f>;
extern template struct TypedTimeSamples<value::point3d>;
extern template struct TypedTimeSamples<value::color3h>;
extern template struct TypedTimeSamples<value::color3f>;
extern template struct TypedTimeSamples<value::color3d>;
extern template struct TypedTimeSamples<value::color4h>;
extern template struct TypedTimeSamples<value::color4f>;
extern template struct TypedTimeSamples<value::color4d>;
extern template struct TypedTimeSamples<value::texcoord2h>;
extern template struct TypedTimeSamples<value::texcoord2f>;
extern template struct TypedTimeSamples<value::texcoord2d>;
extern template struct TypedTimeSamples<value::texcoord3h>;
extern template struct TypedTimeSamples<value::texcoord3f>;
extern template struct TypedTimeSamples<value::texcoord3d>;

// Other types
extern template struct TypedTimeSamples<value::timecode>;
extern template struct TypedTimeSamples<value::frame4d>;
extern template struct TypedTimeSamples<std::string>;
extern template struct TypedTimeSamples<value::token>;
extern template struct TypedTimeSamples<value::dict>;
extern template struct TypedTimeSamples<value::AssetPath>;

// Common array types
extern template struct TypedTimeSamples<std::vector<bool>>;
extern template struct TypedTimeSamples<std::vector<int32_t>>;
extern template struct TypedTimeSamples<std::vector<uint32_t>>;
extern template struct TypedTimeSamples<std::vector<int64_t>>;
extern template struct TypedTimeSamples<std::vector<uint64_t>>;
extern template struct TypedTimeSamples<std::vector<value::half>>;
extern template struct TypedTimeSamples<std::vector<float>>;
extern template struct TypedTimeSamples<std::vector<double>>;
extern template struct TypedTimeSamples<std::vector<value::float2>>;
extern template struct TypedTimeSamples<std::vector<value::float3>>;
extern template struct TypedTimeSamples<std::vector<value::float4>>;
extern template struct TypedTimeSamples<std::vector<value::double2>>;
extern template struct TypedTimeSamples<std::vector<value::double3>>;
extern template struct TypedTimeSamples<std::vector<value::double4>>;
extern template struct TypedTimeSamples<std::vector<value::int2>>;
extern template struct TypedTimeSamples<std::vector<value::int3>>;
extern template struct TypedTimeSamples<std::vector<value::int4>>;
extern template struct TypedTimeSamples<std::vector<value::quath>>;
extern template struct TypedTimeSamples<std::vector<value::quatf>>;
extern template struct TypedTimeSamples<std::vector<value::quatd>>;
extern template struct TypedTimeSamples<std::vector<value::matrix2f>>;
extern template struct TypedTimeSamples<std::vector<value::matrix3f>>;
extern template struct TypedTimeSamples<std::vector<value::matrix4f>>;
extern template struct TypedTimeSamples<std::vector<value::matrix2d>>;
extern template struct TypedTimeSamples<std::vector<value::matrix3d>>;
extern template struct TypedTimeSamples<std::vector<value::matrix4d>>;
extern template struct TypedTimeSamples<std::vector<std::string>>;
extern template struct TypedTimeSamples<std::vector<value::token>>;
extern template struct TypedTimeSamples<std::vector<value::AssetPath>>;
extern template struct TypedTimeSamples<std::vector<value::frame4d>>;
// Special types used by tydra
extern template struct TypedTimeSamples<std::vector<value::StringData>>;
// Additional vector array types
extern template struct TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>;
extern template struct TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>;
extern template struct TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>;

//
// Extern template declarations for TypedTimeSamples::get()
//

// For non-interpolatable integer types
extern template bool TypedTimeSamples<bool>::get<bool>(bool*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<int32_t>::get<int32_t>(int32_t*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<uint32_t>::get<uint32_t>(uint32_t*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<int64_t>::get<int64_t>(int64_t*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<uint64_t>::get<uint64_t>(uint64_t*, double, value::TimeSampleInterpolationType) const;

// For interpolatable floating-point types
extern template bool TypedTimeSamples<value::half>::get<value::half>(value::half*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<float>::get<float>(float*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<double>::get<double>(double*, double, value::TimeSampleInterpolationType) const;

// For interpolatable vector types - using std::array forms
extern template bool TypedTimeSamples<std::array<value::half, 2>>::get<std::array<value::half, 2>>(std::array<value::half, 2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<value::half, 3>>::get<std::array<value::half, 3>>(std::array<value::half, 3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<value::half, 4>>::get<std::array<value::half, 4>>(std::array<value::half, 4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<float, 2>>::get<std::array<float, 2>>(std::array<float, 2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<float, 3>>::get<std::array<float, 3>>(std::array<float, 3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<float, 4>>::get<std::array<float, 4>>(std::array<float, 4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<double, 2>>::get<std::array<double, 2>>(std::array<double, 2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<double, 3>>::get<std::array<double, 3>>(std::array<double, 3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<double, 4>>::get<std::array<double, 4>>(std::array<double, 4>*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable integer vector types
extern template bool TypedTimeSamples<std::array<int, 2>>::get<std::array<int, 2>>(std::array<int, 2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<int, 3>>::get<std::array<int, 3>>(std::array<int, 3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<int, 4>>::get<std::array<int, 4>>(std::array<int, 4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<uint32_t, 2>>::get<std::array<uint32_t, 2>>(std::array<uint32_t, 2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<uint32_t, 3>>::get<std::array<uint32_t, 3>>(std::array<uint32_t, 3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::array<uint32_t, 4>>::get<std::array<uint32_t, 4>>(std::array<uint32_t, 4>*, double, value::TimeSampleInterpolationType) const;

// For interpolatable quaternion types
extern template bool TypedTimeSamples<value::quath>::get<value::quath>(value::quath*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::quatf>::get<value::quatf>(value::quatf*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::quatd>::get<value::quatd>(value::quatd*, double, value::TimeSampleInterpolationType) const;

// For interpolatable matrix types
extern template bool TypedTimeSamples<value::matrix2f>::get<value::matrix2f>(value::matrix2f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::matrix3f>::get<value::matrix3f>(value::matrix3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::matrix4f>::get<value::matrix4f>(value::matrix4f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::matrix2d>::get<value::matrix2d>(value::matrix2d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::matrix3d>::get<value::matrix3d>(value::matrix3d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::matrix4d>::get<value::matrix4d>(value::matrix4d*, double, value::TimeSampleInterpolationType) const;

// For interpolatable role types
extern template bool TypedTimeSamples<value::normal3h>::get<value::normal3h>(value::normal3h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::normal3f>::get<value::normal3f>(value::normal3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::normal3d>::get<value::normal3d>(value::normal3d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::vector3h>::get<value::vector3h>(value::vector3h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::vector3f>::get<value::vector3f>(value::vector3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::vector3d>::get<value::vector3d>(value::vector3d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::point3h>::get<value::point3h>(value::point3h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::point3f>::get<value::point3f>(value::point3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::point3d>::get<value::point3d>(value::point3d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color3h>::get<value::color3h>(value::color3h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color3f>::get<value::color3f>(value::color3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color3d>::get<value::color3d>(value::color3d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color4h>::get<value::color4h>(value::color4h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color4f>::get<value::color4f>(value::color4f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::color4d>::get<value::color4d>(value::color4d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord2h>::get<value::texcoord2h>(value::texcoord2h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord2f>::get<value::texcoord2f>(value::texcoord2f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord2d>::get<value::texcoord2d>(value::texcoord2d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord3h>::get<value::texcoord3h>(value::texcoord3h*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord3f>::get<value::texcoord3f>(value::texcoord3f*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::texcoord3d>::get<value::texcoord3d>(value::texcoord3d*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable other types
extern template bool TypedTimeSamples<value::timecode>::get<value::timecode>(value::timecode*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::frame4d>::get<value::frame4d>(value::frame4d*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::string>::get<std::string>(std::string*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::token>::get<value::token>(value::token*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::dict>::get<value::dict>(value::dict*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<value::AssetPath>::get<value::AssetPath>(value::AssetPath*, double, value::TimeSampleInterpolationType) const;

// For vector container types (non-interpolatable)
extern template bool TypedTimeSamples<std::vector<bool>>::get<std::vector<bool>>(std::vector<bool>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<int>>::get<std::vector<int>>(std::vector<int>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<int32_t>>::get<std::vector<int32_t>>(std::vector<int32_t>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<uint32_t>>::get<std::vector<uint32_t>>(std::vector<uint32_t>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<int64_t>>::get<std::vector<int64_t>>(std::vector<int64_t>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<uint64_t>>::get<std::vector<uint64_t>>(std::vector<uint64_t>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::half>>::get<std::vector<value::half>>(std::vector<value::half>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<float>>::get<std::vector<float>>(std::vector<float>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<double>>::get<std::vector<double>>(std::vector<double>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::half2>>::get<std::vector<value::half2>>(std::vector<value::half2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::half3>>::get<std::vector<value::half3>>(std::vector<value::half3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::half4>>::get<std::vector<value::half4>>(std::vector<value::half4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::float2>>::get<std::vector<value::float2>>(std::vector<value::float2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::float3>>::get<std::vector<value::float3>>(std::vector<value::float3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::float4>>::get<std::vector<value::float4>>(std::vector<value::float4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::double2>>::get<std::vector<value::double2>>(std::vector<value::double2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::double3>>::get<std::vector<value::double3>>(std::vector<value::double3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::double4>>::get<std::vector<value::double4>>(std::vector<value::double4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::int2>>::get<std::vector<value::int2>>(std::vector<value::int2>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::int3>>::get<std::vector<value::int3>>(std::vector<value::int3>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::int4>>::get<std::vector<value::int4>>(std::vector<value::int4>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::quath>>::get<std::vector<value::quath>>(std::vector<value::quath>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::quatf>>::get<std::vector<value::quatf>>(std::vector<value::quatf>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::quatd>>::get<std::vector<value::quatd>>(std::vector<value::quatd>*, double, value::TimeSampleInterpolationType) const;
// Role types vectors (needed by usdGeom.cc and usdSkel.cc)
extern template bool TypedTimeSamples<std::vector<value::point3h>>::get<std::vector<value::point3h>>(std::vector<value::point3h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::point3f>>::get<std::vector<value::point3f>>(std::vector<value::point3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::point3d>>::get<std::vector<value::point3d>>(std::vector<value::point3d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::normal3h>>::get<std::vector<value::normal3h>>(std::vector<value::normal3h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::normal3f>>::get<std::vector<value::normal3f>>(std::vector<value::normal3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::normal3d>>::get<std::vector<value::normal3d>>(std::vector<value::normal3d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::vector3h>>::get<std::vector<value::vector3h>>(std::vector<value::vector3h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::vector3f>>::get<std::vector<value::vector3f>>(std::vector<value::vector3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::vector3d>>::get<std::vector<value::vector3d>>(std::vector<value::vector3d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color3h>>::get<std::vector<value::color3h>>(std::vector<value::color3h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color3f>>::get<std::vector<value::color3f>>(std::vector<value::color3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color3d>>::get<std::vector<value::color3d>>(std::vector<value::color3d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color4h>>::get<std::vector<value::color4h>>(std::vector<value::color4h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color4f>>::get<std::vector<value::color4f>>(std::vector<value::color4f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::color4d>>::get<std::vector<value::color4d>>(std::vector<value::color4d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord2h>>::get<std::vector<value::texcoord2h>>(std::vector<value::texcoord2h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord2f>>::get<std::vector<value::texcoord2f>>(std::vector<value::texcoord2f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord2d>>::get<std::vector<value::texcoord2d>>(std::vector<value::texcoord2d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord3h>>::get<std::vector<value::texcoord3h>>(std::vector<value::texcoord3h>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord3f>>::get<std::vector<value::texcoord3f>>(std::vector<value::texcoord3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::texcoord3d>>::get<std::vector<value::texcoord3d>>(std::vector<value::texcoord3d>*, double, value::TimeSampleInterpolationType) const;
// Matrix types vectors
extern template bool TypedTimeSamples<std::vector<value::matrix2f>>::get<std::vector<value::matrix2f>>(std::vector<value::matrix2f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::matrix3f>>::get<std::vector<value::matrix3f>>(std::vector<value::matrix3f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::matrix4f>>::get<std::vector<value::matrix4f>>(std::vector<value::matrix4f>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::matrix2d>>::get<std::vector<value::matrix2d>>(std::vector<value::matrix2d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::matrix3d>>::get<std::vector<value::matrix3d>>(std::vector<value::matrix3d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::matrix4d>>::get<std::vector<value::matrix4d>>(std::vector<value::matrix4d>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<std::string>>::get<std::vector<std::string>>(std::vector<std::string>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::token>>::get<std::vector<value::token>>(std::vector<value::token>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::AssetPath>>::get<std::vector<value::AssetPath>>(std::vector<value::AssetPath>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<value::frame4d>>::get<std::vector<value::frame4d>>(std::vector<value::frame4d>*, double, value::TimeSampleInterpolationType) const;
// Special types - these are instantiated in timesamples.cc
extern template bool TypedTimeSamples<std::vector<value::StringData>>::get<std::vector<value::StringData>>(std::vector<value::StringData>*, double, value::TimeSampleInterpolationType) const;
// Additional vector array types
extern template bool TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>::get<std::vector<std::array<unsigned int, 2>>>(std::vector<std::array<unsigned int, 2>>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>::get<std::vector<std::array<unsigned int, 3>>>(std::vector<std::array<unsigned int, 3>>*, double, value::TimeSampleInterpolationType) const;
extern template bool TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>::get<std::vector<std::array<unsigned int, 4>>>(std::vector<std::array<unsigned int, 4>>*, double, value::TimeSampleInterpolationType) const;

} // namespace tinyusdz
