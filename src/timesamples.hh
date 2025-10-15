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

// Forward declaration for PODTimeSamples::get_samples()
struct TimeSamples;

// Helper function to check if a type_id represents a POD type
// POD types are numeric types that are trivial and standard layout
inline bool is_pod_type_id(uint32_t type_id) {
  // POD types: bool, numeric types (char, int, uint, float, double, half),
  // and their vector variants (float2, float3, etc.)
  
  // turn off 1D array flag
  uint32_t tid = type_id & (~TYPE_ID_1D_ARRAY_BIT);
  
  return (tid >= uint32_t(TYPE_ID_BOOL) && tid <= uint32_t(TYPE_ID_TIMECODE));
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
  // Hot data (frequently accessed, cache-friendly layout)
  uint32_t _type_id{0}; // TypeId from value-types.hh
  bool _is_stl_array{false}; // Whether the stored type is a std::vector<T> array
  bool _is_typed_array{false}; // Whether the stored type is TypedArray<T>
  uint16_t _element_size{0}; // Cached element size (0 = not cached)
  size_t _array_size{0}; // Number of elements per array (fixed for all samples)
  mutable bool _dirty{false};

  // Dirty range tracking for lazy sorting optimization
  mutable size_t _dirty_start{SIZE_MAX};
  mutable size_t _dirty_end{0};

  // Cold data (less frequently accessed)
  mutable std::vector<double> _times;
  mutable Buffer<16> _blocked; // ValueBlock flags with 16-byte alignment
  mutable Buffer<16> _values; // Raw byte storage: compact storage without blocked values
  mutable std::vector<size_t> _offsets; // Offset table for array values (or scalar values with blocks)
  mutable size_t _blocked_count{0}; // Count of blocked samples for O(1) queries

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
    _offsets.clear();
    _type_id = 0;
    _is_stl_array = false;
    _is_typed_array = false;
    _array_size = 0;
    _element_size = 0;
    _blocked_count = 0;
    _dirty = true;
    _dirty_start = SIZE_MAX;
    _dirty_end = 0;
  }

  /// Move constructor
  PODTimeSamples(PODTimeSamples&& other) noexcept
      : _type_id(other._type_id),
        _is_stl_array(other._is_stl_array),
        _is_typed_array(other._is_typed_array),
        _element_size(other._element_size),
        _array_size(other._array_size),
        _dirty(other._dirty),
        _dirty_start(other._dirty_start),
        _dirty_end(other._dirty_end),
        _times(std::move(other._times)),
        _blocked(std::move(other._blocked)),
        _values(std::move(other._values)),
        _offsets(std::move(other._offsets)),
        _blocked_count(other._blocked_count) {
    // Reset moved-from object to valid empty state
    other._type_id = 0;
    other._is_stl_array = false;
    other._is_typed_array = false;
    other._element_size = 0;
    other._array_size = 0;
    other._dirty = false;
    other._dirty_start = SIZE_MAX;
    other._dirty_end = 0;
    other._blocked_count = 0;
  }

  /// Move assignment operator
  PODTimeSamples& operator=(PODTimeSamples&& other) noexcept {
    if (this != &other) {
      // Move data from other
      _type_id = other._type_id;
      _is_stl_array = other._is_stl_array;
      _is_typed_array = other._is_typed_array;
      _element_size = other._element_size;
      _array_size = other._array_size;
      _dirty = other._dirty;
      _dirty_start = other._dirty_start;
      _dirty_end = other._dirty_end;
      _times = std::move(other._times);
      _blocked = std::move(other._blocked);
      _values = std::move(other._values);
      _offsets = std::move(other._offsets);
      _blocked_count = other._blocked_count;

      // Reset moved-from object to valid empty state
      other._type_id = 0;
      other._is_stl_array = false;
      other._is_typed_array = false;
      other._element_size = 0;
      other._array_size = 0;
      other._dirty = false;
      other._dirty_start = SIZE_MAX;
      other._dirty_end = 0;
      other._blocked_count = 0;
    }
    return *this;
  }

  // Default copy operations
  PODTimeSamples(const PODTimeSamples&) = default;
  PODTimeSamples& operator=(const PODTimeSamples&) = default;

  // Default constructor
  PODTimeSamples() = default;

  /// Initialize PODTimeSamples with type information and optional pre-allocation
  /// @param type_id The TypeId from value-types.hh
  /// @param is_array Whether storing array data (std::vector-based)
  /// @param element_size Size of each element in bytes
  /// @param array_size Number of elements per array (for array data)
  /// @param expected_samples Optional: expected number of samples to pre-allocate
  bool init(uint32_t tid, bool is_array_type = false, size_t elem_size = 0,
            size_t arr_size = 0, size_t expected_samples = 0) {
    // Check if already initialized with a different type
    if (_type_id != 0 && _type_id != tid) {
      return false;
    }

    _type_id = tid;
    _is_stl_array = is_array_type;
    _is_typed_array = false; // STL array init doesn't use TypedArray
    _array_size = arr_size;

    // Calculate element size if not provided
    if (elem_size > 0) {
      _element_size = static_cast<uint16_t>(elem_size);
    } else {
      _element_size = static_cast<uint16_t>(get_element_size());
    }

    // Pre-allocate if expected_samples is provided
    if (expected_samples > 0 && _element_size > 0) {
      reserve_with_type(expected_samples);
    }

    return true;
  }

  /// Pre-allocate capacity for known number of samples
  void reserve(size_t n);

  /// Type-aware reserve that properly accounts for element and array sizes
  /// Should be called after type_id is set (after init or first sample addition)
  void reserve_with_type(size_t expected_samples);

  uint32_t type_id() const { return _type_id; }

  void update() const;

private:
  /// Mark a range as dirty for lazy sorting
  void mark_dirty_range(size_t idx) const {
    _dirty_start = std::min(_dirty_start, idx);
    _dirty_end = std::max(_dirty_end, idx + 1);
  }

public:

  /// Add a time/value sample with POD type checking
  /// T must satisfy std::is_trivial and std::is_standard_layout
  /// @param t Time value
  /// @param value The value to add
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_sample(double t, const T& value, std::string *err = nullptr,
                  size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Set type_id on first sample - use underlying_type_id for consistency
    // This allows storing role types (normal3f) as their underlying type (float3)
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::underlying_type_id();
      _is_stl_array = false;  // Single values are not arrays
      _is_typed_array = false;
      _element_size = sizeof(T);  // Cache element size

      // Pre-allocate if requested
      if (expected_total_samples > 0) {
        reserve_with_type(expected_total_samples);
      }
    } else {
      // Verify type consistency - check underlying type
      if (_type_id != value::TypeTraits<T>::underlying_type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples: expected underlying_type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::underlying_type_id()) +
                    " (type: " + std::string(value::TypeTraits<T>::type_name()) + ").\n";
        }
        return false; // Type mismatch
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // For non-blocked values, append to values array
    // If we're using offsets (arrays or when we have any blocked values), update offset table
    if (_is_stl_array || _is_typed_array || !_offsets.empty()) {
      // Using offset table - need to maintain consistency
      // If offsets table exists but is smaller than times, we need to populate missing offsets
      if (_offsets.size() < _times.size() - 1) {
        // This shouldn't happen in normal flow, but handle it
        _offsets.resize(_times.size() - 1, SIZE_MAX);
      }

      _offsets.push_back(_values.size());
      _values.resize(_values.size() + sizeof(T));
      std::memcpy(_values.data() + _offsets.back(), &value, sizeof(T));
    } else {
      // Legacy path: simple append without offsets (no blocked values yet)
      size_t old_size = _values.size();
      _values.resize(old_size + sizeof(T));
      std::memcpy(_values.data() + old_size, &value, sizeof(T));
    }

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Specialized add_sample for TypedArray<T> - stores only the packed 64-bit pointer
  /// This reduces storage to 8 bytes per sample instead of full Value object
  /// @param t Time value
  /// @param typed_array The TypedArray to add (stores its packed pointer value)
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_typed_array_sample(double t, const TypedArray<T>& typed_array, std::string *err = nullptr,
                              size_t expected_total_samples = 0) {
    // TypedArray internally stores a uint64_t packed pointer, so we can treat it as POD
    uint64_t packed_value = typed_array.get_packed_value();

    // Set type_id on first sample
    // We store the packed pointer as a uint64_t
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::type_id();
      _is_stl_array = false;  // Not using std::vector
      _is_typed_array = true;  // Using TypedArray
      _element_size = sizeof(uint64_t);  // Always 8 bytes for packed pointer

      // Pre-allocate if requested
      if (expected_total_samples > 0) {
        reserve_with_type(expected_total_samples);
      }
    } else {
      // Verify we're storing TypedArray data
      if (_type_id != value::TypeTraits<T>::type_id() || _element_size != sizeof(uint64_t)) {
        if (err) {
          (*err) += "Type mismatch: PODTimeSamples is not configured for TypedArray storage.\n";
        }
        return false;
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Store the packed pointer value
    if (_is_stl_array || _is_typed_array || !_offsets.empty()) {
      // Using offset table
      if (_offsets.size() < _times.size() - 1) {
        _offsets.resize(_times.size() - 1, SIZE_MAX);
      }

      _offsets.push_back(_values.size());
      _values.resize(_values.size() + sizeof(uint64_t));
      TUSDZ_LOG_I("offset = " << _offsets.back());
      TUSDZ_LOG_I("packed_value = 0x" << std::hex << packed_value << std::dec);
      TUSDZ_LOG_I("Writing to address: 0x" << std::hex << reinterpret_cast<uintptr_t>(_values.data() + _offsets.back()) << std::dec);
      std::memcpy(_values.data() + _offsets.back(), &packed_value, sizeof(uint64_t));

      // Verify what was written
      uint64_t verify_read;
      std::memcpy(&verify_read, _values.data() + _offsets.back(), sizeof(uint64_t));
      TUSDZ_LOG_I("Verified written value: 0x" << std::hex << verify_read << std::dec);
    } else {
      // Simple append without offsets
      size_t old_size = _values.size();
      _values.resize(old_size + sizeof(uint64_t));
      std::memcpy(_values.data() + old_size, &packed_value, sizeof(uint64_t));
    }

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Add an array sample with POD element type checking
  /// @param t Time value
  /// @param values Pointer to array data
  /// @param count Number of elements in the array
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_array_sample(double t, const T* values, size_t count, std::string *err = nullptr,
                        size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Set type_id and array info on first sample - use underlying_type_id
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::underlying_type_id();
      _is_stl_array = true;  // Using std::vector-based array
      _is_typed_array = false;  // Not using TypedArray
      _array_size = count;
      _element_size = sizeof(T);  // Cache element size

      // Pre-allocate if requested
      if (expected_total_samples > 0) {
        reserve_with_type(expected_total_samples);
      }
    } else {
      // Verify type consistency - check underlying type
      if (_type_id != value::TypeTraits<T>::underlying_type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples array: expected underlying_type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::underlying_type_id()) + ".\n";
        }
        return false;
      }
      // Verify array size consistency
      if (_array_size != count) {
        if (err) {
          (*err) += "Array size mismatch in PODTimeSamples: expected " +
                    std::to_string(_array_size) + " but got " +
                    std::to_string(count) + ".\n";
        }
        return false;
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Store offset and append array data
    _offsets.push_back(_values.size());
    size_t byte_size = sizeof(T) * count;
    _values.resize(_values.size() + byte_size);
    std::memcpy(_values.data() + _offsets.back(), values, byte_size);

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Add an matrix array sample with POD element type checking
  /// @param t Time value
  /// @param values Pointer to array data
  /// @param count Number of matrices in the array
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_matrix_array_sample(double t, const T* values, size_t count, std::string *err = nullptr,
                               size_t expected_total_samples = 0) {
    static_assert((value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX2D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX3D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX4D),
                  "requires matrix type");

    // Set type_id and array info on first sample - use underlying_type_id
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::underlying_type_id();
      _is_stl_array = true;  // Using std::vector-based array
      _is_typed_array = false;  // Not using TypedArray
      _array_size = count;
      _element_size = sizeof(T);  // Cache element size

      // Pre-allocate if requested
      if (expected_total_samples > 0) {
        reserve_with_type(expected_total_samples);
      }
    } else {
      // Verify type consistency - check underlying type
      if (_type_id != value::TypeTraits<T>::underlying_type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples array: expected underlying_type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::underlying_type_id()) + ".\n";
        }
        return false;
      }
      // Verify array size consistency
      if (_array_size != count) {
        if (err) {
          (*err) += "Array size mismatch in PODTimeSamples: expected " +
                    std::to_string(_array_size) + " but got " +
                    std::to_string(count) + ".\n";
        }
        return false;
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Store offset and append array data
    _offsets.push_back(_values.size());
    size_t byte_size = sizeof(T) * count;
    _values.resize(_values.size() + byte_size);
    std::memcpy(_values.data() + _offsets.back(), values, byte_size);

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Add a deduplicated matrix array sample - reuses data from an existing sample
  /// Same as add_dedup_array_sample but without POD type requirements
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  template<typename T>
  bool add_dedup_matrix_array_sample(double t, size_t ref_index, std::string *err = nullptr) {
    static_assert((value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX2D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX3D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX4D),
                  "requires matrix type");

    // Verify ref_index is valid
    if (ref_index >= _times.size()) {
      if (err) {
        (*err) += "Invalid ref_index in add_dedup_matrix_array_sample: " +
                  std::to_string(ref_index) + " >= " +
                  std::to_string(_times.size()) + ".\n";
      }
      return false;
    }

    // Verify we're in array mode with correct type
    if (!_is_stl_array || _type_id != value::TypeTraits<T>::underlying_type_id()) {
      if (err) {
        (*err) += "Type mismatch in add_dedup_matrix_array_sample: not in array mode or wrong type.\n";
      }
      return false;
    }

    // Verify that ref_index has an offset (not blocked)
    if (_offsets[ref_index] == SIZE_MAX) {
      if (err) {
        (*err) += "Cannot deduplicate from blocked sample at index " +
                  std::to_string(ref_index) + ".\n";
      }
      return false;
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Reuse the offset from the reference sample (deduplication!)
    // This makes offset[new_idx] == offset[ref_index], both pointing to the same data
    _offsets.push_back(_offsets[ref_index]);
    // NOTE: No data appended to _values - we reuse existing data

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Add a deduplicated array sample - reuses data from an existing sample
  /// This is a memory-efficient way to handle deduplicated arrays: store the array data once
  /// and have multiple time samples point to the same offset in the _values buffer.
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  template<typename T>
  bool add_dedup_array_sample(double t, size_t ref_index, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Verify ref_index is valid
    if (ref_index >= _times.size()) {
      if (err) {
        (*err) += "Invalid ref_index in add_dedup_array_sample: " +
                  std::to_string(ref_index) + " >= " +
                  std::to_string(_times.size()) + ".\n";
      }
      return false;
    }

    // Verify we're in array mode with correct type
    if (!_is_stl_array || _type_id != value::TypeTraits<T>::underlying_type_id()) {
      if (err) {
        (*err) += "Type mismatch in add_dedup_array_sample: not in array mode or wrong type.\n";
      }
      return false;
    }

    // Verify that ref_index has an offset (not blocked)
    if (_offsets[ref_index] == SIZE_MAX) {
      if (err) {
        (*err) += "Cannot deduplicate from blocked sample at index " +
                  std::to_string(ref_index) + ".\n";
      }
      return false;
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Reuse the offset from the reference sample (deduplication!)
    // This makes offset[new_idx] == offset[ref_index], both pointing to the same data
    _offsets.push_back(_offsets[ref_index]);
    // NOTE: No data appended to _values - we reuse existing data

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Get samples as vector of TimeSamples::Sample for backward compatibility
  /// This converts the POD storage back to value::Value representation
  /// Implementation is in timesamples.cc to avoid circular dependency
  std::vector<std::pair<double, std::pair<value::Value, bool>>> get_samples_converted() const;

  /// Helper function to get element size for the stored type
  size_t get_element_size() const;

  /// Add a blocked sample (ValueBlock) - no memory allocated for value
  /// @param t Time value
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_blocked_sample(double t, std::string *err = nullptr,
                          size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value,
                  "PODTimeSamples requires trivial types");
    static_assert(std::is_standard_layout<T>::value,
                  "PODTimeSamples requires standard layout types");

    // Set type_id on first sample - use underlying_type_id
    if (_times.empty()) {
      _type_id = value::TypeTraits<T>::underlying_type_id();
      _is_stl_array = false;  // Will be set properly if array samples are added
      _is_typed_array = false;
      _element_size = sizeof(T);  // Cache element size

      // Pre-allocate if requested (note: blocked samples don't use value storage)
      if (expected_total_samples > 0) {
        reserve_with_type(expected_total_samples);
      }
    } else {
      // Verify type consistency - check underlying type
      if (_type_id != value::TypeTraits<T>::underlying_type_id()) {
        if (err) {
          (*err) += "Type mismatch in PODTimeSamples (blocked sample): expected underlying_type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(value::TypeTraits<T>::underlying_type_id()) +
                    " (type: " + std::string(value::TypeTraits<T>::type_name()) + ").\n";
        }
        return false;
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(1);  // true = 1
    _blocked_count++;

    // For blocked values, we DON'T allocate any space in _values
    // If this is the first blocked sample and we don't have offsets yet,
    // we need to create the offset table and populate it with existing samples
    if (_offsets.empty() && !_is_stl_array && !_is_typed_array && !_times.empty()) {
      // Transition to offset-based storage
      // Build offset table for existing samples
      size_t offset = 0;
      size_t element_size = get_element_size();
      for (size_t i = 0; i < _times.size() - 1; ++i) {  // -1 because we just added this time
        if (!_blocked[i]) {
          _offsets.push_back(offset);
          offset += element_size;
        } else {
          _offsets.push_back(SIZE_MAX);
        }
      }
    }

    // Add offset marker for this blocked sample
    if (_is_stl_array || _is_typed_array || !_offsets.empty()) {
      // Use SIZE_MAX as a marker for blocked values in offset table
      _offsets.push_back(SIZE_MAX);
    }
    // No allocation in _values array for blocked samples!

    _dirty = true;
    mark_dirty_range(new_idx);
    return true;
  }

  /// Add a blocked array sample - no memory allocated
  /// @param t Time value
  /// @param count Number of elements in the array
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  bool add_blocked_array_sample(double t, size_t count, std::string *err = nullptr,
                                size_t expected_total_samples = 0) {
    // Initialize array info on first sample
    if (_times.empty()) {
      _is_stl_array = true;  // Assume STL array for blocked array samples
      _is_typed_array = false;
      _array_size = count;
      // type_id will be set when first non-blocked sample is added

      // Pre-allocate if requested (note: blocked samples don't use value storage)
      if (expected_total_samples > 0) {
        // We can at least reserve times and blocked arrays
        _times.reserve(expected_total_samples);
        _blocked.reserve(expected_total_samples);
        _offsets.reserve(expected_total_samples);
      }
    } else if (_is_stl_array || _is_typed_array) {
      // Verify array size consistency
      if (_array_size != count) {
        if (err) {
          (*err) += "Array size mismatch in PODTimeSamples: expected " +
                    std::to_string(_array_size) + " but got " +
                    std::to_string(count) + ".\n";
        }
        return false;
      }
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(1);  // true = 1
    _blocked_count++;

    // Mark as blocked in offset table
    _offsets.push_back(SIZE_MAX);

    _dirty = true;
    mark_dirty_range(new_idx);
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

    // Check if blocked
    if (_blocked[idx]) {
      if (blocked) {
        *blocked = true;
      }
      // For blocked values, we don't have data to copy
      // Initialize the value with default constructor
      *value = T{};
      return true;
    }

    // Verify type - check both exact match and underlying type match
    // This allows getting value as normal3f even if stored as float3, etc.
    bool type_match = (_type_id == value::TypeTraits<T>::type_id()) ||
                      (_type_id == value::TypeTraits<T>::underlying_type_id());
    if (!type_match) {
      return false;
    }

    // Find the actual data offset
    if (!_offsets.empty()) {
      // Using offset table
      if (_offsets[idx] == SIZE_MAX) {
        // This is a blocked value (shouldn't happen as we checked above, but be safe)
        if (blocked) {
          *blocked = true;
        }
        *value = T{};
        return true;
      }
      const uint8_t* src = _values.data() + _offsets[idx];
      std::memcpy(value, src, sizeof(T));
    } else {
      // Legacy path: calculate offset by counting non-blocked entries
      size_t data_offset = 0;
      for (size_t i = 0; i < idx; ++i) {
        if (!_blocked[i]) {
          data_offset += sizeof(T);
        }
      }
      const uint8_t* src = _values.data() + data_offset;
      std::memcpy(value, src, sizeof(T));
    }

    if (blocked) {
      *blocked = false;
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

  /// Get TypedArray value at specific index
  /// Reconstructs TypedArray from stored packed pointer value
  template<typename T>
  bool get_typed_array_at(size_t idx, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
      return false;
    }

    if (_dirty) {
      update();
    }

    if (idx >= _times.size()) {
      return false;
    }

    // Check if this PODTimeSamples is storing TypedArray data
    if (_type_id != value::TypeTraits<T>::type_id() || _element_size != sizeof(uint64_t)) {
      return false;  // Not TypedArray storage
    }

    // Check if blocked
    if (_blocked[idx]) {
      if (blocked) {
        *blocked = true;
      }
      // For blocked values, set to null TypedArray
      *typed_array = TypedArray<T>();
      return true;
    }

    // Retrieve the packed pointer value
    uint64_t packed_value = 0;

    // Find the actual data offset
    if (!_offsets.empty()) {
      // Using offset table
      if (_offsets[idx] == SIZE_MAX) {
        // This is a blocked value
        if (blocked) {
          *blocked = true;
        }
        *typed_array = TypedArray<T>();
        return true;
      }
      const uint8_t* src = _values.data() + _offsets[idx];
      std::memcpy(&packed_value, src, sizeof(uint64_t));
    } else {
      // Legacy path: calculate offset by counting non-blocked entries
      size_t data_offset = 0;
      for (size_t i = 0; i < idx; ++i) {
        if (!_blocked[i]) {
          data_offset += sizeof(uint64_t);
        }
      }
      const uint8_t* src = _values.data() + data_offset;
      std::memcpy(&packed_value, src, sizeof(uint64_t));
    }

    TUSDZ_LOG_I("PODTimeSamples::get_typed_array_at idx=" << idx << " packed_value=0x" << std::hex << packed_value << std::dec);

    // Reconstruct TypedArray from packed value
    // Note: This creates a shallow copy - the underlying TypedArrayImpl is shared
    // and marked as dedup to prevent deletion
    TypedArrayImpl<T>* ptr = nullptr;

    // Extract pointer from packed value
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;  // Lower 48 bits
    // Note: dedup flag is in bit 63 but we always mark as dedup when retrieving

    TUSDZ_LOG_I("PODTimeSamples::get_typed_array_at after mask: ptr_bits=0x" << std::hex << ptr_bits << std::dec);

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
      ptr_bits |= 0xFFFF000000000000ULL;
      TUSDZ_LOG_I("PODTimeSamples::get_typed_array_at sign-extended: ptr_bits=0x" << std::hex << ptr_bits << std::dec);
    }

    ptr = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);
    TUSDZ_LOG_I("PODTimeSamples::get_typed_array_at ptr=" << std::hex << ptr << " size=" << std::dec << (ptr ? ptr->size() : 0));

    // Create TypedArray with dedup flag set (to prevent deletion)
    *typed_array = TypedArray<T>(ptr, true);  // Always mark as dedup when retrieving

    if (blocked) {
      *blocked = false;
    }

    return true;
  }

  /// Get TypedArray value at specific time
  template<typename T>
  bool get_typed_array_at_time(double t, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
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
      return get_typed_array_at<T>(idx, typed_array, blocked);
    }

    return false;
  }

  /// Get TypedArrayView at specific index
  /// Returns a view for TypedArray or array data
  /// Returns an empty view for blocked values or non-array data
  template<typename T>
  TypedArrayView<const T> get_typed_array_view_at(size_t idx) const {
    if (_dirty) {
      update();
    }

    if (idx >= _times.size()) {
      return TypedArrayView<const T>();  // Empty view
    }

    // Check if blocked
    if (_blocked[idx]) {
      return TypedArrayView<const T>();  // Empty view for blocked values
    }

    // For TypedArray storage
    if (_type_id == value::TypeTraits<T>::type_id() && _element_size == sizeof(uint64_t)) {
      // Retrieve the TypedArray and create a view from it
      TypedArray<T> typed_array;
      bool blocked_value = false;
      if (get_typed_array_at<T>(idx, &typed_array, &blocked_value)) {
        if (!blocked_value && typed_array.data() && typed_array.size() > 0) {
          return TypedArrayView<const T>(typed_array);
        }
      }
      return TypedArrayView<const T>();  // Empty view if retrieval failed
    }

    // For array data stored directly
    if ((_is_stl_array || _is_typed_array) && _array_size > 0) {
      // Find the actual data offset
      if (!_offsets.empty()) {
        if (_offsets[idx] == SIZE_MAX) {
          // Blocked value
          return TypedArrayView<const T>();
        }
        const T* src = reinterpret_cast<const T*>(_values.data() + _offsets[idx]);
        return TypedArrayView<const T>(src, _array_size);
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

    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _times.end()) {
      size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
      return get_typed_array_view_at<T>(idx);
    }

    return TypedArrayView<const T>();  // Empty view
  }

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

  size_t estimate_memory_usage() const {
    size_t total = sizeof(PODTimeSamples);
    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity();  // Buffer already stores bytes
    total += _values.capacity();   // Buffer already stores bytes
    total += _offsets.capacity() * sizeof(size_t);  // Include offset table
    return total;
  }

  /// Get blocked sample count
  size_t get_blocked_count() const {
    return _blocked_count;
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

  /// Move constructor
  TimeSamples(TimeSamples&& other) noexcept
      : _samples(std::move(other._samples)),
        _pod_samples(std::move(other._pod_samples)),
        _type_id(other._type_id),
        _use_pod(other._use_pod),
        _dirty(other._dirty) {
    // Reset moved-from object to valid empty state
    other._type_id = 0;
    other._use_pod = false;
    other._dirty = false;
  }

  /// Move assignment operator
  TimeSamples& operator=(TimeSamples&& other) noexcept {
    if (this != &other) {
      // Move data from other
      _samples = std::move(other._samples);
      _pod_samples = std::move(other._pod_samples);
      _type_id = other._type_id;
      _use_pod = other._use_pod;
      _dirty = other._dirty;

      // Reset moved-from object to valid empty state
      other._type_id = 0;
      other._use_pod = false;
      other._dirty = false;
    }
    return *this;
  }

  // Default copy operations
  TimeSamples(const TimeSamples&) = default;
  TimeSamples& operator=(const TimeSamples&) = default;

  // Default constructor
  TimeSamples() = default;

  /// type_id = TypeId
  /// Initialize TimeSamples with a specific type_id
  /// This determines whether to use POD optimization or regular storage
  bool init(uint32_t type_id) {
    //TUSDZ_LOG_I("init" << type_id);
    DCOUT("init" << type_id);

    // Allow initialization if empty OR if it contains only uninitialized blocked samples
    if (!empty() && _type_id != 0) {
      return false; // Already initialized with a different type
    }
    _type_id = type_id;
    _use_pod = value::is_pod_type_id(type_id);
    if (_use_pod) {
      //TUSDZ_LOG_I("  use_pod: " << type_id);
      _pod_samples._type_id = type_id;
    }
    return true;
  }

  bool is_using_pod() const { return _use_pod; }

  /// Check if storing std::vector-based array data
  /// @return true if using POD storage with STL arrays, false otherwise
  bool is_stl_array() const {
    return _use_pod ? _pod_samples._is_stl_array : false;
  }

  /// Check if storing TypedArray data
  /// @return true if using POD storage with TypedArray, false otherwise
  bool is_typed_array() const {
    return _use_pod ? _pod_samples._is_typed_array : false;
  }

  /// Get POD storage for direct manipulation (only valid when using POD storage)
  PODTimeSamples* get_pod_storage() {
    return _use_pod ? &_pod_samples : nullptr;
  }

  const PODTimeSamples* get_pod_storage() const {
    return _use_pod ? &_pod_samples : nullptr;
  }

  void update() const;

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
    // Auto-initialize on first sample, but NOT if the value is uninitialized (type_id == 1)
    // This allows deferred initialization for all-blocked TimeSamples
    // Type ID 1 indicates an uninitialized/invalid Value
    if (empty() && !v.is_none() && v.type_id() != 1) {
      init(v.type_id());
    } else if (!empty() && !v.is_none() && _type_id != 0 && v.type_id() != 1) {
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
  bool add_sample_pod(double t, const T& value, std::string *err = nullptr, size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_sample<T>(t, value, err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + ".\n";
    }
    return false; // Not using POD storage
  }

  template<typename T>
  bool add_array_sample_pod(double t, const std::vector<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_array_sample<T>(t, value.data(), value.size(), err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + "[].\n";
    }
    return false; // Not using POD storage
  }

  // TypedArray overload for add_array_sample_pod
  template<typename T>
  bool add_array_sample_pod(double t, const TypedArray<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    TUSDZ_LOG_I("is dedup? " << value.is_dedup());
    TUSDZ_LOG_I("_use_pod? " << _use_pod);

    if (_use_pod) {
      bool result = _pod_samples.add_array_sample<T>(t, value.data(), value.size(), err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + "[].\n";
    }
    return false; // Not using POD storage
  }

  template<typename T>
  bool add_matrix_array_sample_pod(double t, const std::vector<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_matrix_array_sample<T>(t, value.data(), value.size(), err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + "[].\n";
    }
    return false; // Not using POD storage
  }

  // TypedArray overload for add_matrix_array_sample_pod
  template<typename T>
  bool add_matrix_array_sample_pod(double t, const TypedArray<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_matrix_array_sample<T>(t, value.data(), value.size(), err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + "[].\n";
    }
    return false; // Not using POD storage
  }

  /// Add a deduplicated array sample - reuses data from an existing sample
  /// This is a memory-efficient way to handle deduplicated arrays: store the array data once
  /// and have multiple time samples point to the same offset in the _values buffer.
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  template<typename T>
  bool add_dedup_array_sample_pod(double t, size_t ref_index, std::string *err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_dedup_array_sample_pod requires POD types");

    if (_use_pod) {
      bool result = _pod_samples.add_dedup_array_sample<T>(t, ref_index, err);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for dedup array.\n";
    }
    return false; // Not using POD storage
  }

  /// Add a deduplicated matrix array sample - reuses data from an existing sample
  /// For matrix types that don't satisfy POD requirements
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  template<typename T>
  bool add_dedup_matrix_array_sample_pod(double t, size_t ref_index, std::string *err = nullptr) {
    static_assert((value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX2D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX3D) ||
                  (value::TypeTraits<T>::type_id() == value::TYPE_ID_MATRIX4D),
                  "requires matrix type");

    if (_use_pod) {
      bool result = _pod_samples.add_dedup_matrix_array_sample<T>(t, ref_index, err);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for dedup matrix array.\n";
    }
    return false; // Not using POD storage
  }

  /// Typed add blocked sample for POD types (optimization path)
  template<typename T>
  bool add_blocked_sample_pod(double t, std::string *err = nullptr, size_t expected_total_samples = 0) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_blocked_sample_pod requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<T>::type_id());
    }

    if (_use_pod) {
      bool result = _pod_samples.add_blocked_sample<T>(t, err, expected_total_samples);
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type " + std::string(value::TypeTraits<T>::type_name()) + ".\n";
    }
    return false; // Not using POD storage
  }

  /// Add TypedArray sample using PODTimeSamples optimization
  /// Stores only the packed 64-bit pointer, reducing storage to 8 bytes per sample
  template<typename T>
  bool add_typed_array_sample(double t, const TypedArray<T>& typed_array, std::string *err = nullptr,
                              size_t expected_total_samples = 0) {
    // Initialize for TypedArray storage
    if (empty()) {
      _type_id = value::TypeTraits<T>::type_id();
      _use_pod = true; // Always use POD storage for TypedArray
      _pod_samples._type_id = value::TypeTraits<T>::type_id();
    } else if (_type_id != value::TypeTraits<T>::type_id()) {
      if (err) {
        (*err) += "Type mismatch: TimeSamples already initialized with different type.\n";
      }
      return false;
    }

    if (_use_pod) {
      bool result = _pod_samples.add_typed_array_sample<T>(t, typed_array, err, expected_total_samples);
      _dirty = true;
      return result;
    }

    // Fallback: shouldn't reach here for TypedArray
    if (err) {
      (*err) += "TypedArray must use POD storage.\n";
    }
    return false;
  }

  /// Get TypedArray sample at specific time
  template<typename T>
  bool get_typed_array_at_time(double t, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
      return false;
    }

    // Check if storing TypedArray data
    if (_type_id != value::TypeTraits<T>::type_id()) {
      return false;
    }

    if (_use_pod) {
      return _pod_samples.get_typed_array_at_time<T>(t, typed_array, blocked);
    }

    // TypedArray should always use POD storage
    return false;
  }

  /// Get TypedArray sample at specific index
  template<typename T>
  bool get_typed_array_at(size_t idx, TypedArray<T>* typed_array, bool* blocked = nullptr) const {
    if (!typed_array) {
      return false;
    }

    // Check if storing TypedArray data
    if (_type_id != value::TypeTraits<T>::type_id()) {
      return false;
    }

    if (_use_pod) {
      return _pod_samples.get_typed_array_at<T>(idx, typed_array, blocked);
    }

    // TypedArray should always use POD storage
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

    if (_use_pod) {
      // Use PODTimeSamples implementation
      return _pod_samples.get_typed_array_view_at<T>(idx);
    }

    // For regular Value storage
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

    if (_use_pod) {
      // Use PODTimeSamples implementation
      return _pod_samples.get_typed_array_view_at_time<T>(t);
    }

    // For regular Value storage
    const auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample& s) {
      return std::fabs(t - s.t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _samples.end()) {
      size_t idx = static_cast<size_t>(std::distance(_samples.begin(), it));
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

    if (_use_pod) {
      // For POD storage, convert samples on demand
      // This is not ideal for performance, but maintains backward compatibility
      // Users should prefer typed access methods when possible
      if (_dirty) {
        update();
      }

      // Convert POD samples to regular samples
      _samples.clear();
      auto converted = _pod_samples.get_samples_converted();
      _samples.reserve(converted.size());
      for (const auto& item : converted) {
        Sample s;
        s.t = item.first;
        s.value = item.second.first;
        s.blocked = item.second.second;
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

    if (_use_pod) {
      // Cannot return samples from POD storage
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

    const auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
      return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _times.end()) {
      size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
      _values[idx] = v;
      _blocked[idx] = 0;  // false = 0
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

} // namespace tinyusdz
