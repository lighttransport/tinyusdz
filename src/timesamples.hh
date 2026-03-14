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
#include "logger.hh"
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

  // Offset encoding constants (moved from unified binary storage)
  static constexpr uint64_t OFFSET_DEDUP_FLAG = 0x8000000000000000ULL;        // Bit 63: dedup flag
  static constexpr uint64_t OFFSET_ARRAY_FLAG = 0x4000000000000000ULL;        // Bit 62: array data flag
  static constexpr uint64_t OFFSET_ARRAY_BUFFER_FLAG = 0x2000000000000000ULL; // Bit 61: array data in _array_values buffer
  static constexpr uint64_t OFFSET_VALUE_MASK = 0x1FFFFFFFFFFFFFFFULL;        // Bits 60-0: index/offset value
  static constexpr uint64_t OFFSET_FLAGS_MASK = 0xE000000000000000ULL;        // Bits 63-61: flags

  // Offset manipulation helpers
  static constexpr uint64_t make_offset(size_t byte_offset, bool is_array) {
    return (is_array ? OFFSET_ARRAY_FLAG : 0ULL) | (byte_offset & OFFSET_VALUE_MASK);
  }

  static constexpr uint64_t make_array_buffer_offset(size_t array_index) {
    return OFFSET_ARRAY_FLAG | OFFSET_ARRAY_BUFFER_FLAG | (array_index & OFFSET_VALUE_MASK);
  }

  static constexpr uint64_t make_dedup_offset(size_t sample_index, bool is_array) {
    return OFFSET_DEDUP_FLAG | (is_array ? OFFSET_ARRAY_FLAG : 0ULL) | (sample_index & OFFSET_VALUE_MASK);
  }

  static constexpr bool is_dedup(uint64_t offset_value) {
    return (offset_value & OFFSET_DEDUP_FLAG) != 0;
  }

  static constexpr bool is_array_offset(uint64_t offset_value) {
    return (offset_value & OFFSET_ARRAY_FLAG) != 0;
  }

  static constexpr bool is_array_buffer_offset(uint64_t offset_value) {
    return (offset_value & OFFSET_ARRAY_BUFFER_FLAG) != 0;
  }

  static constexpr size_t get_raw_value(uint64_t offset_value) {
    return static_cast<size_t>(offset_value & OFFSET_VALUE_MASK);
  }

  /// Resolve offset value to actual byte offset, following dedup chain if necessary
  static bool resolve_offset_static(const std::vector<uint64_t>& offsets, size_t sample_idx,
                                     size_t* out_byte_offset, bool* out_is_array = nullptr,
                                     bool* out_use_array_buffer = nullptr, size_t max_depth = 100,
                                     size_t* out_resolved_idx = nullptr) {
    if (sample_idx >= offsets.size()) {
      return false;
    }

    uint64_t offset_value = offsets[sample_idx];
    size_t current_idx = sample_idx;
    size_t depth = 0;

    // Follow dedup chain
    while (is_dedup(offset_value)) {
      if (++depth > max_depth) {
        return false; // Dedup chain too deep, likely circular
      }

      size_t ref_idx = get_raw_value(offset_value);
      if (ref_idx >= offsets.size() || ref_idx == sample_idx) {
        return false; // Invalid or self-referencing index
      }

      current_idx = ref_idx;
      offset_value = offsets[ref_idx];
    }

    // Now we have a non-dedup offset
    if (out_byte_offset) {
      *out_byte_offset = get_raw_value(offset_value);
    }
    if (out_is_array) {
      *out_is_array = is_array_offset(offset_value);
    }
    if (out_use_array_buffer) {
      *out_use_array_buffer = is_array_buffer_offset(offset_value);
    }
    if (out_resolved_idx) {
      *out_resolved_idx = current_idx;
    }

    return true;
  }

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

  /// Move constructor
  TimeSamples(TimeSamples&& other) noexcept;

  /// Move assignment operator
  TimeSamples& operator=(TimeSamples&& other) noexcept;

  /// Copy constructor - implements deep copy for _array_values
  TimeSamples(const TimeSamples& other);

  /// Copy assignment operator - implements deep copy for _array_values
  TimeSamples& operator=(const TimeSamples& other);

  // Default constructor
  TimeSamples() = default;

  /// type_id = TypeId
  /// Initialize TimeSamples with a specific type_id.
  /// This determines whether to use unified binary storage or generic Value storage.
  bool init(uint32_t type_id);

  /// Cast the TimeSamples' type to a role type if the underlying types are compatible.
  /// This allows reinterpreting stored base types (e.g., float3) as role types (e.g., color3f).
  /// @param role_type_id The target role type's type_id
  /// @return true if the cast was successful, false if the underlying types don't match
  bool cast_to_role_type(uint32_t role_type_id);

  /// Check if unified binary storage has samples (i.e., _times is not empty)
  /// Returns true if any samples have been added to unified storage path.
  bool is_using_binary_storage() const { return !_times.empty(); }

  /// Check if storing std::vector-based array data
  /// @return true if using unified storage with STL arrays, false otherwise
  bool is_stl_array() const {
    return _storage.is_stl_array();
  }

  /// Check if storing TypedArray data
  /// @return true if using unified storage with TypedArray, false otherwise
  bool is_typed_array() const {
    return _storage.is_typed_array();
  }

 private:
  enum class UnifiedStorageBackend : uint8_t {
    None,
    SmallScalar,
    OffsetScalar,
    ArrayOffset,
    ValueArray,
  };

  enum class ArrayLayoutKind : uint8_t {
    None,
    StdVector,
    TypedArray,
  };

  static const char* backend_name(UnifiedStorageBackend backend) {
    switch (backend) {
      case UnifiedStorageBackend::None:
        return "none";
      case UnifiedStorageBackend::SmallScalar:
        return "small-scalar";
      case UnifiedStorageBackend::OffsetScalar:
        return "offset-scalar";
      case UnifiedStorageBackend::ArrayOffset:
        return "array-offset";
      case UnifiedStorageBackend::ValueArray:
        return "value-array";
    }

    return "unknown";
  }

  static const char* array_layout_name(ArrayLayoutKind layout) {
    switch (layout) {
      case ArrayLayoutKind::None:
        return "none";
      case ArrayLayoutKind::StdVector:
        return "std::vector";
      case ArrayLayoutKind::TypedArray:
        return "TypedArray";
    }

    return "unknown";
  }

  struct ScalarStorageDescriptor {
    UnifiedStorageBackend backend{UnifiedStorageBackend::None};
    size_t element_size{0};

    bool active() const { return backend != UnifiedStorageBackend::None; }

    bool uses_offsets() const {
      return backend == UnifiedStorageBackend::OffsetScalar;
    }

    bool uses_small_scalars() const {
      return backend == UnifiedStorageBackend::SmallScalar;
    }

    bool validate_or_init(UnifiedStorageBackend requested_backend,
                          size_t requested_element_size, std::string* err,
                          const char* op_name) {
      if ((requested_backend != UnifiedStorageBackend::SmallScalar) &&
          (requested_backend != UnifiedStorageBackend::OffsetScalar)) {
        if (err) {
          (*err) += std::string(op_name) +
                    " requested a non-scalar unified storage backend.\n";
        }
        return false;
      }

      if (backend == UnifiedStorageBackend::None) {
        backend = requested_backend;
        element_size = requested_element_size;
        return true;
      }

      if (backend != requested_backend) {
        if (err) {
          (*err) += std::string(op_name) +
                    " backend mismatch: existing backend is `" +
                    backend_name(backend) + "`, requested backend is `" +
                    backend_name(requested_backend) + "`.\n";
        }
        return false;
      }

      if ((requested_element_size != 0) && (element_size != 0) &&
          (element_size != requested_element_size)) {
        if (err) {
          (*err) += std::string(op_name) +
                    " element size mismatch: existing element size is " +
                    std::to_string(element_size) +
                    ", requested element size is " +
                    std::to_string(requested_element_size) + ".\n";
        }
        return false;
      }

      if ((element_size == 0) && (requested_element_size != 0)) {
        element_size = requested_element_size;
      }

      return true;
    }

    void clear() {
      backend = UnifiedStorageBackend::None;
      element_size = 0;
    }
  };

  struct ArrayStorageDescriptor {
    UnifiedStorageBackend backend{UnifiedStorageBackend::None};
    ArrayLayoutKind layout{ArrayLayoutKind::None};
    size_t uniform_array_count{0};
    size_t element_size{0};

    bool active() const { return backend != UnifiedStorageBackend::None; }

    bool uses_value_array() const {
      return backend == UnifiedStorageBackend::ValueArray;
    }

    bool uses_offsets() const {
      return backend == UnifiedStorageBackend::ArrayOffset;
    }

    bool is_stl_array() const { return layout == ArrayLayoutKind::StdVector; }

    bool is_typed_array() const {
      return layout == ArrayLayoutKind::TypedArray;
    }

    bool validate_or_init(UnifiedStorageBackend requested_backend,
                          ArrayLayoutKind requested_layout,
                          size_t requested_element_size, std::string* err,
                          const char* op_name) {
      if ((requested_backend != UnifiedStorageBackend::ArrayOffset) &&
          (requested_backend != UnifiedStorageBackend::ValueArray)) {
        if (err) {
          (*err) += std::string(op_name) +
                    " requested a non-array unified storage backend.\n";
        }
        return false;
      }

      if (backend == UnifiedStorageBackend::None) {
        backend = requested_backend;
        layout = requested_layout;
        element_size = requested_element_size;
        return true;
      }

      if (backend != requested_backend) {
        if (err) {
          (*err) += std::string(op_name) +
                    " backend mismatch: existing backend is `" +
                    backend_name(backend) + "`, requested backend is `" +
                    backend_name(requested_backend) + "`.\n";
        }
        return false;
      }

      if ((requested_layout != ArrayLayoutKind::None) &&
          (layout != ArrayLayoutKind::None) &&
          (layout != requested_layout)) {
        if (err) {
          (*err) += std::string(op_name) +
                    " array layout mismatch: existing layout is `" +
                    array_layout_name(layout) + "`, requested layout is `" +
                    array_layout_name(requested_layout) + "`.\n";
        }
        return false;
      }

      if ((layout == ArrayLayoutKind::None) &&
          (requested_layout != ArrayLayoutKind::None)) {
        layout = requested_layout;
      }

      if ((requested_element_size != 0) && (element_size != 0) &&
          (element_size != requested_element_size)) {
        if (err) {
          (*err) += std::string(op_name) +
                    " element size mismatch: existing element size is " +
                    std::to_string(element_size) +
                    ", requested element size is " +
                    std::to_string(requested_element_size) + ".\n";
        }
        return false;
      }

      if ((element_size == 0) && (requested_element_size != 0)) {
        element_size = requested_element_size;
      }

      return true;
    }

    void update_metadata(size_t count, size_t requested_element_size,
                         ArrayLayoutKind requested_layout) {
      uniform_array_count = count;
      if (requested_element_size != 0) {
        element_size = requested_element_size;
      }
      if (requested_layout != ArrayLayoutKind::None) {
        layout = requested_layout;
      }
    }

    void clear() {
      backend = UnifiedStorageBackend::None;
      layout = ArrayLayoutKind::None;
      uniform_array_count = 0;
      element_size = 0;
    }
  };

  struct StorageDescriptor {
    ScalarStorageDescriptor scalar;
    ArrayStorageDescriptor array;

    bool is_array_backend() const { return array.active(); }

    bool is_scalar_backend() const { return scalar.active(); }

    bool uses_value_array() const { return array.uses_value_array(); }

    bool uses_offsets() const {
      return scalar.uses_offsets() || array.uses_offsets();
    }

    bool uses_small_scalars() const {
      return scalar.uses_small_scalars();
    }

    bool is_stl_array() const { return array.is_stl_array(); }

    bool is_typed_array() const { return array.is_typed_array(); }

    const char* active_backend_name() const {
      if (array.active()) {
        return backend_name(array.backend);
      }
      if (scalar.active()) {
        return backend_name(scalar.backend);
      }
      return backend_name(UnifiedStorageBackend::None);
    }

    size_t uniform_count() const {
      if (array.active()) {
        return array.uniform_array_count;
      }
      if (scalar.active()) {
        return 1;
      }
      return 0;
    }

    void clear() {
      scalar.clear();
      array.clear();
    }
  };

  bool has_unified_samples() const {
    return !_times.empty();
  }

  bool has_generic_samples() const {
    return !_samples.empty() && !has_unified_samples();
  }

  bool ensure_initialized_type(uint32_t expected_type_id, std::string* err,
                               const char* op_name) {
    if (!is_initialized()) {
      if (!init(expected_type_id)) {
        if (err) {
          (*err) += std::string(op_name) + " failed to initialize TimeSamples.\n";
        }
        return false;
      }
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

  bool ensure_array_storage_backend(UnifiedStorageBackend backend,
                                    ArrayLayoutKind array_layout,
                                    size_t element_size, std::string* err,
                                    const char* op_name) {
    if (has_generic_samples()) {
      if (err) {
        (*err) += std::string(op_name) +
                  " cannot mix unified storage with existing generic Value samples.\n";
      }
      return false;
    }

    if (_storage.is_scalar_backend()) {
      if (err) {
        (*err) += std::string(op_name) +
                  " cannot mix array unified storage with existing `" +
                  _storage.active_backend_name() + "` scalar samples.\n";
      }
      return false;
    }

    return _storage.array.validate_or_init(backend, array_layout, element_size,
                                           err, op_name);
  }

  bool ensure_scalar_storage_backend(UnifiedStorageBackend backend,
                                     size_t element_size, std::string* err,
                                     const char* op_name) {
    if (has_generic_samples()) {
      if (err) {
        (*err) += std::string(op_name) +
                  " cannot mix unified storage with existing generic Value samples.\n";
      }
      return false;
    }

    if (_storage.is_array_backend()) {
      if (err) {
        (*err) += std::string(op_name) +
                  " cannot mix scalar unified storage with existing `" +
                  _storage.active_backend_name() + "` array samples.\n";
      }
      return false;
    }

    return _storage.scalar.validate_or_init(backend, element_size, err,
                                            op_name);
  }

  void update_array_metadata(size_t count, size_t element_size,
                             ArrayLayoutKind layout) {
    _storage.array.update_metadata(count, element_size, layout);
  }

  void update_scalar_metadata(size_t element_size) {
    if (element_size != 0) {
      _storage.scalar.element_size = element_size;
    }
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

    if (_times.empty() || !_storage.is_array_backend()) {
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

    if (_storage.uses_value_array()) {
      if (idx >= _value_array_refs.size()) {
        return false;
      }

      const size_t storage_idx = get_value_array_index(_value_array_refs[idx]);
      if (storage_idx >= _value_array_storage.size()) {
        return false;
      }

      if (const auto *typed = _value_array_storage[storage_idx].as<TypedArray<T>>()) {
        ref->data = typed->data();
        ref->count = typed->size();
        return true;
      }

      if (const auto *vec = _value_array_storage[storage_idx].as<std::vector<T>>()) {
        ref->data = vec->data();
        ref->count = vec->size();
        return true;
      }

      return false;
    }

    size_t byte_offset = 0;
    bool use_array_buffer = false;
    if (!resolve_offset_static(_offsets, idx, &byte_offset, nullptr, &use_array_buffer)) {
      return false;
    }

    ref->count = get_array_count(idx);
    if (ref->count == 0) {
      ref->data = nullptr;
      return true;
    }

    if (use_array_buffer) {
      if (byte_offset >= _array_values.size() || !_array_values[byte_offset]) {
        return false;
      }
      ref->data = reinterpret_cast<const T*>(_array_values[byte_offset]->data());
      return true;
    }

    ref->data = reinterpret_cast<const T*>(_values.data() + byte_offset);
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

  bool reconstruct_value_array_sample(size_t idx, Sample* sample) const {
    if (!sample || idx >= _times.size()) {
      return false;
    }

    sample->t = _times[idx];
    sample->value = value::Value();
    sample->blocked = true;

    if (idx < _blocked.size() && (_blocked[idx] != 0)) {
      return true;
    }

    if (idx >= _value_array_refs.size()) {
      return true;
    }

    const size_t storage_idx = get_value_array_index(_value_array_refs[idx]);
    if (storage_idx >= _value_array_storage.size()) {
      return true;
    }

    sample->value = _value_array_storage[storage_idx];
    sample->blocked = false;
    return true;
  }

  bool reconstruct_binary_sample(size_t idx, Sample* sample) const {
    if (!sample || idx >= _times.size() || idx >= _blocked.size()) {
      return false;
    }

    sample->t = _times[idx];
    sample->blocked = (_blocked[idx] != 0);
    sample->value = value::Value();

    if (sample->blocked) {
      return true;
    }

    if (_storage.is_array_backend()) {
      if (_type_id == value::TypeTraits<std::vector<bool>>::type_id()) {
        UnifiedArrayRef<uint8_t> ref;
        if (resolve_unified_array_ref<uint8_t>(
                idx, value::TypeTraits<std::vector<bool>>::type_id(), &ref) &&
            !ref.blocked) {
          std::vector<bool> vec;
          vec.reserve(ref.count);
          for (size_t i = 0; i < ref.count; ++i) {
            vec.push_back(ref.data[i] != 0);
          }
          sample->value = value::Value(std::move(vec));
        }
        return true;
      }

#define RECONSTRUCT_ARRAY_SAMPLE(TYPE)                                            \
      if (reconstruct_unified_array_value<TYPE>(                                  \
              idx, value::TypeTraits<std::vector<TYPE>>::type_id(),               \
              &sample->value)) {                                                  \
        return true;                                                              \
      }                                                                           \
      if (reconstruct_unified_array_value<TYPE>(                                  \
              idx, value::TypeTraits<TypedArray<TYPE>>::type_id(),                \
              &sample->value)) {                                                  \
        return true;                                                              \
      }

      RECONSTRUCT_ARRAY_SAMPLE(float)
      RECONSTRUCT_ARRAY_SAMPLE(double)
      RECONSTRUCT_ARRAY_SAMPLE(int32_t)
      RECONSTRUCT_ARRAY_SAMPLE(uint32_t)
      RECONSTRUCT_ARRAY_SAMPLE(int64_t)
      RECONSTRUCT_ARRAY_SAMPLE(uint64_t)
      RECONSTRUCT_ARRAY_SAMPLE(value::half)
      RECONSTRUCT_ARRAY_SAMPLE(value::half2)
      RECONSTRUCT_ARRAY_SAMPLE(value::half3)
      RECONSTRUCT_ARRAY_SAMPLE(value::half4)
      RECONSTRUCT_ARRAY_SAMPLE(value::float2)
      RECONSTRUCT_ARRAY_SAMPLE(value::float3)
      RECONSTRUCT_ARRAY_SAMPLE(value::float4)
      RECONSTRUCT_ARRAY_SAMPLE(value::double2)
      RECONSTRUCT_ARRAY_SAMPLE(value::double3)
      RECONSTRUCT_ARRAY_SAMPLE(value::double4)
      RECONSTRUCT_ARRAY_SAMPLE(value::int2)
      RECONSTRUCT_ARRAY_SAMPLE(value::int3)
      RECONSTRUCT_ARRAY_SAMPLE(value::int4)
      RECONSTRUCT_ARRAY_SAMPLE(value::quath)
      RECONSTRUCT_ARRAY_SAMPLE(value::quatf)
      RECONSTRUCT_ARRAY_SAMPLE(value::quatd)
      RECONSTRUCT_ARRAY_SAMPLE(value::point3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::point3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::normal3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::normal3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::vector3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::vector3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::color3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::color3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::color4f)
      RECONSTRUCT_ARRAY_SAMPLE(value::color4d)
      RECONSTRUCT_ARRAY_SAMPLE(value::texcoord2f)
      RECONSTRUCT_ARRAY_SAMPLE(value::texcoord2d)
      RECONSTRUCT_ARRAY_SAMPLE(value::texcoord3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::texcoord3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix2f)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix2d)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix3f)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix3d)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix4f)
      RECONSTRUCT_ARRAY_SAMPLE(value::matrix4d)

#undef RECONSTRUCT_ARRAY_SAMPLE
      return true;
    }

    const uint32_t type_id = _type_id;
    if (idx < _small_values.size()) {
      uint64_t stored = _small_values[idx];
      switch (type_id) {
        case value::TypeTraits<value::half>::type_id(): {
          value::half hval;
          std::memcpy(&hval, &stored, sizeof(value::half));
          sample->value = value::Value(hval);
          break;
        }
        case value::TypeTraits<float>::type_id(): {
          float fval = 0.0f;
          std::memcpy(&fval, &stored, sizeof(float));
          sample->value = value::Value(fval);
          break;
        }
        case value::TypeTraits<double>::type_id(): {
          double dval = 0.0;
          std::memcpy(&dval, &stored, sizeof(double));
          sample->value = value::Value(dval);
          break;
        }
        case value::TypeTraits<int32_t>::type_id(): {
          int32_t ival = 0;
          std::memcpy(&ival, &stored, sizeof(int32_t));
          sample->value = value::Value(ival);
          break;
        }
        case value::TypeTraits<uint32_t>::type_id(): {
          uint32_t uval = 0;
          std::memcpy(&uval, &stored, sizeof(uint32_t));
          sample->value = value::Value(uval);
          break;
        }
        case value::TypeTraits<int64_t>::type_id(): {
          int64_t lval = 0;
          std::memcpy(&lval, &stored, sizeof(int64_t));
          sample->value = value::Value(lval);
          break;
        }
        case value::TypeTraits<uint64_t>::type_id(): {
          uint64_t ulval = 0;
          std::memcpy(&ulval, &stored, sizeof(uint64_t));
          sample->value = value::Value(ulval);
          break;
        }
        case value::TypeTraits<bool>::type_id(): {
          const bool bval = (stored != 0);
          sample->value = value::Value(bval);
          break;
        }
        default:
          break;
      }
      return true;
    }

    if (idx >= _offsets.size()) {
      return true;
    }

    const uint64_t encoded_offset = _offsets[idx];
    const size_t byte_offset = static_cast<size_t>(encoded_offset & OFFSET_VALUE_MASK);

#define RECONSTRUCT_VALUE(TYPE) \
    case value::TypeTraits<TYPE>::type_id(): { \
      if (byte_offset + sizeof(TYPE) <= _values.size()) { \
        TYPE val; \
        std::memcpy(&val, _values.data() + byte_offset, sizeof(TYPE)); \
        sample->value = value::Value(val); \
      } \
      break; \
    }

    switch (type_id) {
      RECONSTRUCT_VALUE(value::float2)
      RECONSTRUCT_VALUE(value::float3)
      RECONSTRUCT_VALUE(value::float4)
      RECONSTRUCT_VALUE(value::double2)
      RECONSTRUCT_VALUE(value::double3)
      RECONSTRUCT_VALUE(value::double4)
      RECONSTRUCT_VALUE(value::int2)
      RECONSTRUCT_VALUE(value::int3)
      RECONSTRUCT_VALUE(value::int4)
      RECONSTRUCT_VALUE(value::half2)
      RECONSTRUCT_VALUE(value::half3)
      RECONSTRUCT_VALUE(value::half4)
      RECONSTRUCT_VALUE(value::quath)
      RECONSTRUCT_VALUE(value::quatf)
      RECONSTRUCT_VALUE(value::quatd)
      RECONSTRUCT_VALUE(value::point3f)
      RECONSTRUCT_VALUE(value::point3d)
      RECONSTRUCT_VALUE(value::normal3f)
      RECONSTRUCT_VALUE(value::normal3d)
      RECONSTRUCT_VALUE(value::vector3f)
      RECONSTRUCT_VALUE(value::vector3d)
      RECONSTRUCT_VALUE(value::color3f)
      RECONSTRUCT_VALUE(value::color3d)
      RECONSTRUCT_VALUE(value::color4f)
      RECONSTRUCT_VALUE(value::color4d)
      RECONSTRUCT_VALUE(value::texcoord2f)
      RECONSTRUCT_VALUE(value::texcoord2d)
      RECONSTRUCT_VALUE(value::texcoord3f)
      RECONSTRUCT_VALUE(value::texcoord3d)
      RECONSTRUCT_VALUE(value::matrix2f)
      RECONSTRUCT_VALUE(value::matrix2d)
      RECONSTRUCT_VALUE(value::matrix3f)
      RECONSTRUCT_VALUE(value::matrix3d)
      RECONSTRUCT_VALUE(value::matrix4f)
      RECONSTRUCT_VALUE(value::matrix4d)
      default:
        break;
    }

#undef RECONSTRUCT_VALUE
    return true;
  }

  bool reconstruct_unified_sample(size_t idx, Sample* sample) const {
    if (_storage.uses_value_array()) {
      return reconstruct_value_array_sample(idx, sample);
    }

    return reconstruct_binary_sample(idx, sample);
  }

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

  bool add_sample(const Sample &s, std::string *err = nullptr) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    // Auto-initialize on first sample
    if (!is_initialized() && !s.value.is_none()) {
      init(s.value.type_id());
    } else if (!s.value.is_none() && is_initialized()) {
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

    // Add to generic Value storage
    _samples.push_back(s);
    _dirty = true;
    return true;
  }

  // Value may be None(ValueBlock)
  bool add_sample(double t, const value::Value &v, std::string *err = nullptr) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    // Auto-initialize on first sample
    if (!is_initialized() && !v.is_none()) {
      init(v.type_id());
    } else if (!v.is_none() && is_initialized()) {
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

    // Add to generic Value storage
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
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_blocked_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    // Auto-initialize on first sample, but NOT if the value is uninitialized (type_id == 1)
    // This allows deferred initialization for all-blocked TimeSamples
    // Type ID 1 indicates an uninitialized/invalid Value
    if (!is_initialized() && !v.is_none() && v.type_id() != 1) {
      init(v.type_id());
    } else if (!v.is_none() && is_initialized() && v.type_id() != 1) {
      // Validate type_id matches on subsequent samples
      if (v.type_id() != _type_id) {
        if (err) {
          (*err) += "Type mismatch in TimeSamples (blocked sample): expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(v.type_id()) + ".\n";
        }
        return false;
      }
    }

    // Add to generic Value storage
    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = true;

    _samples.emplace_back(s);
    _dirty = true;
    return true;
  }

  /// Add an array sample using value::Value storage with dedup support
  /// This stores the value::Value in _value_array_storage and records the index
  /// @param t Time value for this sample
  /// @param v The array value to add (will be moved)
  /// @param err Optional error string
  bool add_value_array_sample(double t, value::Value &&v, std::string *err = nullptr) {
    if (!v.is_array() || v.is_none()) {
      if (err) {
        (*err) += "add_value_array_sample requires a non-blocked array value.\n";
      }
      return false;
    }

    if (!ensure_initialized_type(v.type_id(), err, "add_value_array_sample")) {
      return false;
    }

    if (!ensure_array_storage_backend(UnifiedStorageBackend::ValueArray,
                                      ArrayLayoutKind::None, 0, err,
                                      "add_value_array_sample")) {
      return false;
    }

    // Store in value array storage
    size_t storage_index = _value_array_storage.size();
    _value_array_storage.push_back(std::move(v));

    // Record time and reference
    _times.push_back(t);
    _blocked.push_back(0);
    _value_array_refs.push_back(make_value_array_ref(storage_index, false));
    _array_counts.push_back(_value_array_storage.back().array_size());

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add a deduplicated array sample that references an existing sample's value
  /// This uses the offset table to avoid copying value::Value
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample (in _times) whose value to reuse
  /// @param err Optional error string
  bool add_dedup_sample(double t, size_t ref_index, std::string *err = nullptr) {
    // Check if using value array storage
    if (_storage.uses_value_array()) {
      // Validate reference
      if (ref_index >= _times.size()) {
        if (err) {
          (*err) += "Invalid ref_index in add_dedup_sample: " +
                    std::to_string(ref_index) + " >= " + std::to_string(_times.size()) + ".\n";
        }
        return false;
      }

      // Get the storage index from the referenced sample
      uint64_t ref_entry = _value_array_refs[ref_index];
      size_t storage_index = get_value_array_index(ref_entry);

      // If the referenced sample is itself a dedup, follow the chain
      // (though we should always reference original data)
      if (is_value_array_dedup(ref_entry)) {
        if (err) {
          (*err) += "Cannot deduplicate from already deduplicated sample.\n";
        }
        return false;
      }

      // Add time and dedup reference
      _times.push_back(t);
      _blocked.push_back(0);
      _value_array_refs.push_back(make_value_array_ref(storage_index, true));
      const size_t ref_array_count =
          (ref_index < _array_counts.size()) ? _array_counts[ref_index] : 0;
      _array_counts.push_back(ref_array_count);

      invalidate_reconstructed_samples_cache();
      _dirty = true;
      return true;
    }

    // Fallback to old _samples based storage
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_dedup_sample cannot fall back to generic Value storage once unified storage is active.\n";
      }
      return false;
    }
    if (ref_index >= _samples.size()) {
      if (err) {
        (*err) += "Invalid ref_index in add_dedup_sample: " +
                  std::to_string(ref_index) + " >= " + std::to_string(_samples.size()) + ".\n";
      }
      return false;
    }

    // Reuse the value from the referenced sample
    Sample s;
    s.t = t;
    s.value = _samples[ref_index].value;  // Share the value::Value (copy, but underlying data may be shared)
    s.blocked = _samples[ref_index].blocked;
    _samples.push_back(s);
    _dirty = true;
    return true;
  }

  template <typename T,
            typename std::enable_if<
                !std::is_same<typename std::decay<T>::type, value::Value>::value &&
                !std::is_same<typename std::decay<T>::type, Sample>::value,
                int>::type = 0>
  bool add_sample(double t, const T& value, std::string *err = nullptr,
                  size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::is_binary_serializable_v<T> &&
                  !std::is_same<typename std::decay<T>::type, bool>::value) {
      if (!is_initialized()) {
        init(value::TypeTraits<T>::type_id());
      }
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

    if constexpr (std::is_same<T, bool>::value) {
      return add_sample(t, value::Value(value), err);
    } else if constexpr (value::is_binary_serializable_v<T>) {
      if (!ensure_initialized_type(value::TypeTraits<std::vector<T>>::type_id(),
                                   err, "add_array_sample<std::vector>")) {
        return false;
      }

      if (!ensure_array_storage_backend(UnifiedStorageBackend::ArrayOffset,
                                        ArrayLayoutKind::StdVector, sizeof(T),
                                        err,
                                        "add_array_sample<std::vector>")) {
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

    if constexpr (value::is_binary_serializable_v<T> &&
                  !std::is_same<T, bool>::value) {
      if (!ensure_initialized_type(value::TypeTraits<TypedArray<T>>::type_id(),
                                   err, "add_array_sample<TypedArray>")) {
        return false;
      }

      if (!ensure_array_storage_backend(UnifiedStorageBackend::ArrayOffset,
                                        ArrayLayoutKind::TypedArray,
                                        sizeof(T), err,
                                        "add_array_sample<TypedArray>")) {
        return false;
      }

      return add_array_sample<T>(t, value.data(), value.size(), err);
    } else {
      std::vector<T> vec(value.data(), value.data() + value.size());
      return add_sample(t, value::Value(vec), err);
    }
  }

  /// Add a deduplicated bool array sample - reuses data from an existing sample.
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  bool add_dedup_bool_array_sample(double t, size_t ref_index, std::string *err = nullptr) {
    size_t new_idx = _times.size();

    // Validate reference
    if (ref_index >= new_idx) {
      if (err) {
        (*err) += "Invalid ref_index in add_dedup_bool_array_sample: " +
                  std::to_string(ref_index) + " >= " + std::to_string(new_idx) + ".\n";
      }
      return false;
    }

    if (is_dedup(_offsets[ref_index])) {
      if (err) {
        (*err) += "Cannot deduplicate from deduplicated sample.\n";
      }
      return false;
    }

    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Copy array count from the referenced sample
    size_t ref_array_count = (ref_index < _array_counts.size())
                                 ? _array_counts[ref_index]
                                 : _storage.uniform_count();
    _array_counts.push_back(ref_array_count);

    // Create dedup offset: bit 63=1 (dedup), bit 62=1 (array), bits 61-0=ref_index
    uint64_t dedup_offset = make_dedup_offset(ref_index, true);
    _offsets.push_back(dedup_offset);

    update_array_metadata(ref_array_count, sizeof(uint8_t),
                          ArrayLayoutKind::StdVector);
    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  template<typename T>
  bool add_blocked_sample(double t, std::string *err = nullptr,
                          size_t expected_total_samples = 0) {
    (void)expected_total_samples;

    if constexpr (value::is_binary_serializable_v<T> &&
                  !std::is_same<typename std::decay<T>::type, bool>::value) {
      if (!is_initialized()) {
        init(value::TypeTraits<T>::type_id());
      }
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

  const std::vector<Sample> &get_samples() const {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif

    static const std::vector<Sample> empty;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

    // If unified storage has data, convert to generic samples on demand.
    if (!_times.empty() && _samples.empty()) {
      if (_dirty) {
        update();
      }

      _samples.clear();
      _samples.reserve(_times.size());

      for (size_t i = 0; i < _times.size(); ++i) {
        Sample s;
        if (!reconstruct_unified_sample(i, &s)) {
          s.t = (i < _times.size()) ? _times[i] : 0.0;
          s.value = value::Value();
          s.blocked = true;
        }
        _samples.push_back(s);
      }
      return _samples;
    }

    if (_dirty) {
      update();
    }
    return _samples;
  }

  std::vector<Sample> &samples() {
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wexit-time-destructors"
#endif

    static std::vector<Sample> empty;

#ifdef __clang__
#pragma clang diagnostic pop
#endif

    if (!_times.empty() && _samples.empty()) {
      (void)get_samples();
    }

    if (_dirty) {
      update();
    }
    return _samples;
  }

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
      // TODO: Handle blocked
        if (const auto pv = samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
        return false;
      } else {

        if (samples.size() == 1) {
          if (const auto pv = samples[0].value.as<T>()) {
            (*dst) = *pv;
            return true;
          }
          return false;
        }

        auto it = std::upper_bound(
          samples.begin(), samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_minus_1 = (it == samples.begin()) ? samples.begin() : (it - 1);

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

    const auto &samples = get_samples();
    if (samples.empty()) {
      return false;
    }

    if (value::TimeCode(t).is_default()) {
      // FIXME: Use the first item for now.
      // TODO: Handle blocked
      if (!samples.empty()) {
        if (const auto pv = samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
      }
      return false;
    } else {

      if (samples.size() == 1) {
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
        // Held
        auto it = std::upper_bound(
          samples.begin(), samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

        const auto it_minus_1 = (it == samples.begin()) ? samples.begin() : (it - 1);

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

  size_t estimate_memory_usage() const {
    size_t total = sizeof(TimeSamples);

    // Account for unified storage
    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity();
    total += _values.capacity();
    total += _offsets.capacity() * sizeof(uint64_t);
    total += _small_values.capacity() * sizeof(uint64_t);

    // _array_values vector overhead + each buffer
    total += _array_values.capacity() * sizeof(std::unique_ptr<Buffer<16>>);
    for (const auto& buf : _array_values) {
      if (buf) {
        total += sizeof(Buffer<16>) + buf->capacity();
      }
    }

    // value::Value array storage (for generic Value array types)
    total += _value_array_storage.capacity() * sizeof(value::Value);
    for (const auto& val : _value_array_storage) {
      total += val.estimate_memory_usage();
    }
    total += _value_array_refs.capacity() * sizeof(uint64_t);

    // Per-sample array counts
    total += _array_counts.capacity() * sizeof(size_t);

    // Account for generic Value storage (_samples)
    total += _samples.capacity() * sizeof(Sample);
    for (const auto &sample : _samples) {
      total += sample.value.estimate_memory_usage();
    }

    return total;
  }

  //
  // Unified array methods (work directly with TimeSamples storage)
  //

  /// Add array sample using unified storage
  /// Array data is stored in _array_values using unique_ptr for proper ownership semantics
  template<typename T>
  bool add_array_sample(double t, const T* values, size_t count, std::string* err = nullptr) {
    static_assert(value::is_binary_serializable_v<T>,
                  "add_array_sample requires binary-serializable element types");

    // Auto-initialize on first sample
    if (!ensure_initialized_type(_type_id != 0 ? _type_id : value::TypeTraits<std::vector<T>>::type_id(),
                                 err, "add_array_sample")) {
      return false;
    }

    const ArrayLayoutKind layout =
        (_storage.array.layout == ArrayLayoutKind::None)
            ? ArrayLayoutKind::StdVector
            : _storage.array.layout;
    if (!ensure_array_storage_backend(UnifiedStorageBackend::ArrayOffset,
                                      layout, sizeof(T), err,
                                      "add_array_sample")) {
      return false;
    }

    // Use unified storage
    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Allocate new buffer for this array sample in _array_values
    auto array_buffer = std::make_unique<Buffer<16>>();
    size_t data_size = sizeof(T) * count;
    array_buffer->resize(data_size);
    std::memcpy(array_buffer->data(), values, data_size);

    // Store the index of the newly allocated buffer
    size_t array_index = _array_values.size();
    _array_values.push_back(std::move(array_buffer));

    // Create encoded offset with array buffer flag and index
    uint64_t encoded_offset = make_array_buffer_offset(array_index);
    _offsets.push_back(encoded_offset);
    _array_counts.push_back(count);

    // Update array metadata
    update_array_metadata(count, sizeof(T), layout);

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add deduplicated array sample (Phase 2 path)
  template<typename T>
  bool add_dedup_array_sample(double t, size_t ref_index, std::string* err = nullptr) {
    static_assert(value::is_binary_serializable_v<T>,
                  "add_dedup_array_sample requires binary-serializable element types");

    const ArrayLayoutKind layout =
        (_storage.array.layout == ArrayLayoutKind::None)
            ? ArrayLayoutKind::StdVector
            : _storage.array.layout;
    if (!ensure_array_storage_backend(UnifiedStorageBackend::ArrayOffset,
                                      layout, sizeof(T), err,
                                      "add_dedup_array_sample")) {
      return false;
    }

    // Validate reference
    if (ref_index >= _times.size()) {
      if (err) *err = "Invalid ref_index: " + std::to_string(ref_index) + " >= " + std::to_string(_times.size());
      return false;
    }

    if (ref_index == _times.size()) {
      if (err) *err = "Self-reference detected";
      return false;
    }

    if (_offsets[ref_index] == SIZE_MAX) {
      if (err) *err = "Cannot deduplicate from blocked sample";
      return false;
    }

    if (is_dedup(_offsets[ref_index])) {
      if (err) *err = "Cannot deduplicate from deduplicated sample";
      return false;
    }

    // Add dedup sample
    _times.push_back(t);
    _blocked.push_back(0);
    const size_t ref_array_count =
        (ref_index < _array_counts.size()) ? _array_counts[ref_index]
                                           : _storage.uniform_count();
    _array_counts.push_back(ref_array_count);

    uint64_t dedup_offset = make_dedup_offset(ref_index, true);
    _offsets.push_back(dedup_offset);

    update_array_metadata(ref_array_count, sizeof(T), layout);
    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }


  /// Add matrix array sample (Phase 2 path)
  template<typename T>
  bool add_matrix_array_sample(double t, const T* matrices, size_t count, std::string* err = nullptr) {
    // Matrices are stored the same way as arrays
    return add_array_sample<T>(t, matrices, count, err);
  }

  /// Add deduplicated matrix array sample (Phase 2 path)
  template<typename T>
  bool add_dedup_matrix_array_sample(double t, size_t ref_index, std::string* err = nullptr) {
    return add_dedup_array_sample<T>(t, ref_index, err);
  }

  bool add_bool_array_sample(double t, const std::vector<bool>& value,
                             std::string *err = nullptr) {
    if (!ensure_initialized_type(value::TypeTraits<std::vector<bool>>::type_id(),
                                 err, "add_bool_array_sample")) {
      return false;
    }

    if (!ensure_array_storage_backend(UnifiedStorageBackend::ArrayOffset,
                                      ArrayLayoutKind::StdVector,
                                      sizeof(uint8_t), err,
                                      "add_bool_array_sample")) {
      return false;
    }

    std::vector<uint8_t> byte_array;
    byte_array.reserve(value.size());
    for (bool b : value) {
      byte_array.push_back(b ? 1 : 0);
    }

    _times.push_back(t);
    _blocked.push_back(0);
    _array_counts.push_back(value.size());

    size_t byte_offset = _values.size();
    uint64_t encoded_offset = make_offset(byte_offset, true);
    _offsets.push_back(encoded_offset);

    size_t byte_size = sizeof(uint8_t) * byte_array.size();
    _values.resize(_values.size() + byte_size);
    std::memcpy(_values.data() + byte_offset, byte_array.data(), byte_size);

    update_array_metadata(value.size(), sizeof(uint8_t),
                          ArrayLayoutKind::StdVector);
    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  //
  // Unified binary scalar sample methods
  //

  /// Add a binary-serializable scalar sample using unified storage.
  /// Small types (sizeof(T) <= 8) are stored directly in _small_values.
  template<typename T>
  typename std::enable_if<(sizeof(T) <= 8), bool>::type
  add_binary_sample(double t, const T& value, std::string* err = nullptr) {
    static_assert(value::is_binary_serializable_v<T>,
                  "add_binary_sample requires binary-serializable types");

    // Auto-initialize on first sample
    if (!ensure_initialized_type(value::TypeTraits<T>::type_id(), err,
                                 "add_binary_sample")) {
      return false;
    }

    if (!ensure_scalar_storage_backend(UnifiedStorageBackend::SmallScalar,
                                       sizeof(T), err, "add_binary_sample")) {
      return false;
    }

    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Direct storage for small scalars - no offset entry needed
    uint64_t small_value = 0;
    std::memcpy(&small_value, &value, sizeof(T));
    _small_values.push_back(small_value);

    // Update metadata (scalar, not array)
    update_scalar_metadata(sizeof(T));

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add a binary-serializable scalar sample using unified storage.
  /// Large types (sizeof(T) > 8) are stored in _values with an offset table.
  template<typename T>
  typename std::enable_if<(sizeof(T) > 8), bool>::type
  add_binary_sample(double t, const T& value, std::string* err = nullptr) {
    static_assert(value::is_binary_serializable_v<T>,
                  "add_binary_sample requires binary-serializable types");

    // Auto-initialize on first sample
    if (!ensure_initialized_type(value::TypeTraits<T>::type_id(), err,
                                 "add_binary_sample")) {
      return false;
    }

    if (!ensure_scalar_storage_backend(UnifiedStorageBackend::OffsetScalar,
                                       sizeof(T), err, "add_binary_sample")) {
      return false;
    }

    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Offset-based storage for large scalars
    size_t byte_offset = _values.size();
    _values.resize(byte_offset + sizeof(T));
    std::memcpy(_values.data() + byte_offset, &value, sizeof(T));

    // Create encoded offset (not array)
    uint64_t encoded_offset = make_offset(byte_offset, false);
    _offsets.push_back(encoded_offset);

    // Update metadata (scalar, not array)
    update_scalar_metadata(sizeof(T));

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
  }

  /// Add a blocked binary-serializable scalar sample using unified storage.
  template<typename T>
  bool add_binary_blocked_sample(double t, std::string* err = nullptr) {
    static_assert(value::is_binary_serializable_v<T>,
                  "add_binary_blocked_sample requires binary-serializable types");

    // Auto-initialize on first sample
    if (!ensure_initialized_type(value::TypeTraits<T>::type_id(), err,
                                 "add_binary_blocked_sample")) {
      return false;
    }

    if (!ensure_scalar_storage_backend(
            (sizeof(T) > 8) ? UnifiedStorageBackend::OffsetScalar
                            : UnifiedStorageBackend::SmallScalar,
            sizeof(T), err, "add_binary_blocked_sample")) {
      return false;
    }

    // Add blocked sample to unified storage
    _times.push_back(t);
    _blocked.push_back(1);  // Blocked

    // For small types (sizeof <= 8), don't use offsets - just rely on _blocked flag
    // For large types (sizeof > 8), need offset table entry
    if (sizeof(T) > 8) {
      _offsets.push_back(SIZE_MAX);  // Special marker for blocked
    } else {
      _small_values.push_back(0);
    }

    update_scalar_metadata(sizeof(T));

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
    if (const TypedArray<T>* typed_array = sample.value.as<TypedArray<T>>()) {
      if (typed_array->data() && typed_array->size() > 0) {
        out_vec->assign(typed_array->data(), typed_array->data() + typed_array->size());
        if (out_blocked) *out_blocked = false;
        return true;
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
  // Accessor methods for unified storage
  // These provide read-only access to internal binary storage for utilities like pprint
  //

  const std::vector<double>& get_times() const {
    if (_dirty) {
      update();
    }
    return _times;
  }

  const Buffer<16>& get_blocked() const {
    if (_dirty) {
      update();
    }
    return _blocked;
  }

  const Buffer<16>& get_values() const {
    if (_dirty) {
      update();
    }
    return _values;
  }

  const std::vector<uint64_t>& get_offsets() const {
    if (_dirty) {
      update();
    }
    return _offsets;
  }

  const std::vector<uint64_t>& get_small_values() const {
    if (_dirty) {
      update();
    }
    return _small_values;
  }

  bool is_array() const {
    return _storage.is_array_backend();
  }

  size_t get_array_size() const {
    if (_array_counts.empty()) {
      return _storage.uniform_count();
    }

    const size_t first = _array_counts.front();
    for (size_t count : _array_counts) {
      if (count != first) {
        return 0;
      }
    }

    return first;
  }

  size_t get_array_count(size_t idx) const {
    if (_array_counts.empty()) {
      return _storage.uniform_count();
    }

    if (idx >= _array_counts.size()) {
      return 0;
    }

    return _array_counts[idx];
  }

  const std::vector<size_t>& get_array_counts() const {
    return _array_counts;
  }

 private:
  // Generic path storage (for generic Value types: string, token, dict, etc.)
  mutable std::vector<Sample> _samples;

  // binary-storage path storage (moved from unified binary storage for Phase 2 unification)
  mutable std::vector<double> _times;
  mutable Buffer<16> _blocked;
  mutable std::vector<uint64_t> _small_values;                       // Direct storage for small scalar binary-serializable types (sizeof(T) <= 8 bytes), stored as uint64
  mutable Buffer<16> _values;                                        // Raw byte storage for large scalar binary-serializable types and arrays
  mutable std::vector<std::unique_ptr<Buffer<16>>> _array_values;    // Array data storage: each entry is a separate allocated buffer for one array sample
  mutable std::vector<uint64_t> _offsets;                            // Offset table for large types and arrays with dedup/array/buffer flags

  // value::Value array storage with dedup support (for generic Value array types)
  // Stores unique value::Value objects; _value_array_refs contains indices or dedup references
  mutable std::vector<value::Value> _value_array_storage;  // Stores unique array values
  mutable std::vector<uint64_t> _value_array_refs;         // bit 63 = dedup flag, bits 0-62 = storage index or ref index

  // Type information
  uint32_t _type_id{0};
  StorageDescriptor _storage{};
  mutable size_t _blocked_count{0};
  mutable std::vector<size_t> _array_counts; // Per-sample array element counts (for variable-sized arrays)

  mutable bool _dirty{false};
  mutable size_t _dirty_start{0};
  mutable size_t _dirty_end{0};

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

 public:
  // Helper constants for value array dedup
  static constexpr uint64_t VALUE_ARRAY_DEDUP_BIT = uint64_t(1) << 63;

  static bool is_value_array_dedup(uint64_t ref) {
    return (ref & VALUE_ARRAY_DEDUP_BIT) != 0;
  }

  static uint64_t make_value_array_ref(size_t index, bool is_dedup) {
    return is_dedup ? (VALUE_ARRAY_DEDUP_BIT | index) : index;
  }

  static size_t get_value_array_index(uint64_t ref) {
    return static_cast<size_t>(ref & ~VALUE_ARRAY_DEDUP_BIT);
  }
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

// Integer types (POD, non-lerp'able)
extern template struct TypedTimeSamples<bool>;
extern template struct TypedTimeSamples<int32_t>;
extern template struct TypedTimeSamples<uint32_t>;
extern template struct TypedTimeSamples<int64_t>;
extern template struct TypedTimeSamples<uint64_t>;

// Floating point scalar types (POD, lerp'able)
extern template struct TypedTimeSamples<value::half>;
extern template struct TypedTimeSamples<float>;
extern template struct TypedTimeSamples<double>;

// Vector types (POD, lerp'able)
extern template struct TypedTimeSamples<value::half2>;
extern template struct TypedTimeSamples<value::half3>;
extern template struct TypedTimeSamples<value::half4>;
extern template struct TypedTimeSamples<value::float2>;
extern template struct TypedTimeSamples<value::float3>;
extern template struct TypedTimeSamples<value::float4>;
extern template struct TypedTimeSamples<value::double2>;
extern template struct TypedTimeSamples<value::double3>;
extern template struct TypedTimeSamples<value::double4>;

// Integer vector types (POD, non-lerp'able)
extern template struct TypedTimeSamples<value::int2>;
extern template struct TypedTimeSamples<value::int3>;
extern template struct TypedTimeSamples<value::int4>;

// Quaternion types (POD, lerp'able)
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

// Role types (POD, lerp'able)
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
