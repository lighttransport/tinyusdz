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

#if 0 // not used anymore

// Helper function to check if a type_id represents a POD type
// POD types are numeric types that are trivial and standard layout
inline bool is_pod_type_id(uint32_t type_id) {
  // POD types: bool, numeric types (char, int, uint, float, double, half),
  // and their vector variants (float2, float3, etc.)
  
  // turn off 1D array flag
  uint32_t tid = type_id & (~TYPE_ID_1D_ARRAY_BIT);
  
  return (tid >= uint32_t(TYPE_ID_BOOL) && tid <= uint32_t(TYPE_ID_TIMECODE));
}

#endif

} // namespace value

///
/// POD (Plain Old Data) time samples container with SoA layout
///
/// @deprecated Use TimeSamples with new unified storage (_array_values) instead.
/// This class is kept for backward compatibility but will be removed in a future version.
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
  size_t _array_size{0}; // Number of elements per array (DEPRECATED: use _array_counts for variable-sized arrays)
  mutable bool _dirty{false};

  // Dirty range tracking for lazy sorting optimization
  mutable size_t _dirty_start{SIZE_MAX};
  mutable size_t _dirty_end{0};

  // Cold data (less frequently accessed)
  mutable std::vector<double> _times;
  mutable Buffer<16> _blocked; // ValueBlock flags with 16-byte alignment
  mutable Buffer<16> _values; // Raw byte storage: compact storage without blocked values
  mutable std::vector<uint64_t> _offsets; // Offset table with dedup/array flags (bit 63=dedup, bit 62=array, bits 61-0=index/offset)
  mutable std::vector<size_t> _array_counts; // Per-sample array element counts (for variable-sized arrays)
  mutable size_t _blocked_count{0}; // Count of blocked samples for O(1) queries

  // Offset encoding constants
  static constexpr uint64_t OFFSET_DEDUP_FLAG = 0x8000000000000000ULL;        // Bit 63: dedup flag
  static constexpr uint64_t OFFSET_ARRAY_FLAG = 0x4000000000000000ULL;        // Bit 62: array data flag
  static constexpr uint64_t OFFSET_ARRAY_BUFFER_FLAG = 0x2000000000000000ULL; // Bit 61: array data in _array_values buffer (vs _values)
  static constexpr uint64_t OFFSET_VALUE_MASK = 0x1FFFFFFFFFFFFFFFULL;        // Bits 60-0: index/offset value
  static constexpr uint64_t OFFSET_FLAGS_MASK = 0xE000000000000000ULL;        // Bits 63-61: flags

  // Storage optimization: no offset entry needed for small scalars (stored directly in _small_values)

  // Offset manipulation helpers
  /// Create offset for scalar or array data in _values buffer
  static constexpr uint64_t make_offset(size_t byte_offset, bool is_array) {
    return (is_array ? OFFSET_ARRAY_FLAG : 0ULL) | (byte_offset & OFFSET_VALUE_MASK);
  }

  /// Create offset for array data in _array_values (unique_ptr array)
  /// @param array_index Index into _array_values vector
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
  /// Static version that works with any offsets vector (for TimeSamples Phase 2)
  /// @param offsets The offset vector to resolve from
  /// @param sample_idx The sample index to resolve
  /// @param out_byte_offset Output: the resolved byte offset in the appropriate buffer
  /// @param out_is_array Output: whether this is array data
  /// @param out_use_array_buffer Output: whether data is in _array_values (vs _values)
  /// @param max_depth Maximum dedup chain depth to prevent infinite loops (default 100)
  /// @return true on success, false if circular reference detected or max depth exceeded
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

  /// Resolve offset value to actual byte offset, following dedup chain if necessary
  /// Instance method - delegates to static version
  /// @param sample_idx The sample index to resolve
  /// @param out_byte_offset Output: the resolved byte offset in the appropriate buffer
  /// @param out_is_array Output: whether this is array data
  /// @param out_use_array_buffer Output: whether data is in _array_values (vs _values)
  /// @param max_depth Maximum dedup chain depth to prevent infinite loops (default 100)
  /// @param out_resolved_idx Output: the sample index that contains the actual data (after following dedup chain)
  /// @return true on success, false if circular reference detected or max depth exceeded
  bool resolve_offset(size_t sample_idx, size_t* out_byte_offset, bool* out_is_array = nullptr,
                      bool* out_use_array_buffer = nullptr, size_t max_depth = 100,
                      size_t* out_resolved_idx = nullptr) const {
    return resolve_offset_static(_offsets, sample_idx, out_byte_offset, out_is_array, out_use_array_buffer, max_depth, out_resolved_idx);
  }

  /// Validate that a dedup reference is valid (no circular refs, references non-dedup data)
  /// @param ref_index The index being referenced
  /// @param new_sample_idx The new sample index that will reference ref_index
  /// @return true if valid, false if would create circular reference
  bool validate_dedup_reference(size_t ref_index, size_t new_sample_idx) const {
    // Check bounds
    if (ref_index >= _times.size()) {
      return false;
    }

    // Self-reference check
    if (ref_index == new_sample_idx) {
      return false;
    }

    // Check that ref_index is not blocked
    if (_offsets[ref_index] == SIZE_MAX) {
      return false;
    }

    // Verify ref_index doesn't point to dedup data (must be original)
    uint64_t ref_offset = _offsets[ref_index];
    if (is_dedup(ref_offset)) {
      return false; // Cannot dedup from dedup - must reference original data only
    }

    return true;
  }

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
    _array_counts.clear();
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
        _array_counts(std::move(other._array_counts)),
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
      _array_counts = std::move(other._array_counts);
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
                  size_t expected_total_samples = 0);

  /// Specialized add_sample for TypedArray<T> - stores only the packed 64-bit pointer
  /// This reduces storage to 8 bytes per sample instead of full Value object
  /// @param t Time value
  /// @param typed_array The TypedArray to add (stores its packed pointer value)
  /// @param err Optional error string
  /// @param expected_total_samples Optional: if this is the first sample, pre-allocate for this many samples
  template<typename T>
  bool add_typed_array_sample(double t, const TypedArray<T>& typed_array, std::string *err = nullptr,
                              size_t expected_total_samples = 0);

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
      _array_size = count;  // Store first sample size for backward compatibility
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
      // Note: USD allows variable-sized arrays, so we don't enforce size consistency
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0
    _array_counts.push_back(count);  // Store per-sample array size

    // Store offset with array flag and append array data
    size_t byte_offset = _values.size();
    uint64_t encoded_offset = make_offset(byte_offset, true);  // is_array=true
    _offsets.push_back(encoded_offset);

    size_t byte_size = sizeof(T) * count;
    _values.resize(_values.size() + byte_size);
    std::memcpy(_values.data() + byte_offset, values, byte_size);

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
      _array_size = count;  // Store first sample size for backward compatibility
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
      // Note: USD allows variable-sized arrays, so we don't enforce size consistency
    }

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(0);  // false = 0
    _array_counts.push_back(count);  // Store per-sample array size

    // Store offset with array flag and append array data
    size_t byte_offset = _values.size();
    uint64_t encoded_offset = make_offset(byte_offset, true);  // is_array=true
    _offsets.push_back(encoded_offset);

    size_t byte_size = sizeof(T) * count;
    _values.resize(_values.size() + byte_size);
    std::memcpy(_values.data() + byte_offset, values, byte_size);

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

    size_t new_idx = _times.size();

    // Validate dedup reference (checks bounds, self-reference, circular refs, non-dedup source)
    if (!validate_dedup_reference(ref_index, new_idx)) {
      if (err) {
        if (ref_index >= _times.size()) {
          (*err) += "Invalid ref_index in add_dedup_matrix_array_sample: " +
                    std::to_string(ref_index) + " >= " +
                    std::to_string(_times.size()) + ".\n";
        } else if (ref_index == new_idx) {
          (*err) += "Self-reference detected in add_dedup_matrix_array_sample: " +
                    std::to_string(ref_index) + ".\n";
        } else if (is_dedup(_offsets[ref_index])) {
          (*err) += "Cannot deduplicate from deduplicated sample at index " +
                    std::to_string(ref_index) + ". Must reference original data only.\n";
        } else if (_offsets[ref_index] == SIZE_MAX) {
          (*err) += "Cannot deduplicate from blocked sample at index " +
                    std::to_string(ref_index) + ".\n";
        } else {
          (*err) += "Invalid dedup reference in add_dedup_matrix_array_sample.\n";
        }
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

    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Copy array count from the referenced sample
    size_t ref_array_count = (ref_index < _array_counts.size()) ? _array_counts[ref_index] : _array_size;
    _array_counts.push_back(ref_array_count);

    // Create dedup offset: bit 63=1 (dedup), bit 62=1 (array), bits 61-0=ref_index
    uint64_t dedup_offset = make_dedup_offset(ref_index, true);
    _offsets.push_back(dedup_offset);
    // NOTE: No data appended to _values - we reference existing data via index

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

    size_t new_idx = _times.size();

    // Validate dedup reference (checks bounds, self-reference, circular refs, non-dedup source)
    if (!validate_dedup_reference(ref_index, new_idx)) {
      if (err) {
        if (ref_index >= _times.size()) {
          (*err) += "Invalid ref_index in add_dedup_array_sample: " +
                    std::to_string(ref_index) + " >= " +
                    std::to_string(_times.size()) + ".\n";
        } else if (ref_index == new_idx) {
          (*err) += "Self-reference detected in add_dedup_array_sample: " +
                    std::to_string(ref_index) + ".\n";
        } else if (is_dedup(_offsets[ref_index])) {
          (*err) += "Cannot deduplicate from deduplicated sample at index " +
                    std::to_string(ref_index) + ". Must reference original data only.\n";
        } else if (_offsets[ref_index] == SIZE_MAX) {
          (*err) += "Cannot deduplicate from blocked sample at index " +
                    std::to_string(ref_index) + ".\n";
        } else {
          (*err) += "Invalid dedup reference in add_dedup_array_sample.\n";
        }
      }
      return false;
    }

    // Debug logging
    DCOUT("PODTimeSamples::add_dedup_array_sample called, ref_index=" << ref_index << ", _array_size=" << _array_size);

    // If _array_size not yet set, determine it from the referenced sample
    if (_array_size == 0) {
      DCOUT("PODTimeSamples: _array_size is 0, trying to determine from ref_index " << ref_index);
      // The array count is stored in _array_counts, NOT in the buffer!
      // The _values buffer contains only raw element data (no count prefix)
      if (ref_index < _array_counts.size()) {
        _array_size = _array_counts[ref_index];
        DCOUT("PODTimeSamples: read _array_size=" << _array_size << " from _array_counts[" << ref_index << "]");

        // Also set type info if not yet initialized
        _type_id = value::TypeTraits<T>::underlying_type_id();
        _is_stl_array = true;  // Using std::vector-based array
        _is_typed_array = false;  // Not using TypedArray
        _element_size = sizeof(T);  // Cache element size
      } else {
        DCOUT("PODTimeSamples: ref_index " << ref_index << " out of bounds for _array_counts (size=" << _array_counts.size() << ")");
      }
    }

    // Verify we're in array mode with correct type
    if (!_is_stl_array || _type_id != value::TypeTraits<T>::underlying_type_id()) {
      if (err) {
        (*err) += "Type mismatch in add_dedup_array_sample: not in array mode or wrong type.\n";
      }
      return false;
    }

    _times.push_back(t);
    _blocked.push_back(0);  // false = 0

    // Copy array count from the referenced sample
    size_t ref_array_count = (ref_index < _array_counts.size()) ? _array_counts[ref_index] : _array_size;
    _array_counts.push_back(ref_array_count);

    // Create dedup offset: bit 63=1 (dedup), bit 62=1 (array), bits 61-0=ref_index
    uint64_t dedup_offset = make_dedup_offset(ref_index, true);
    _offsets.push_back(dedup_offset);
    // NOTE: No data appended to _values - we reference existing data via index

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
      size_t byte_offset = 0;
      size_t element_size = get_element_size();
      for (size_t i = 0; i < _times.size() - 1; ++i) {  // -1 because we just added this time
        if (!_blocked[i]) {
          // Encode offset with flags (scalar = is_array false)
          uint64_t encoded_offset = make_offset(byte_offset, false);
          _offsets.push_back(encoded_offset);
          byte_offset += element_size;
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
    (void)err;  // Unused since we removed size validation
    // Initialize array info on first sample
    if (_times.empty()) {
      _is_stl_array = true;  // Assume STL array for blocked array samples
      _is_typed_array = false;
      _array_size = count;  // Store first sample size for backward compatibility
      // type_id will be set when first non-blocked sample is added

      // Pre-allocate if requested (note: blocked samples don't use value storage)
      if (expected_total_samples > 0) {
        // We can at least reserve times and blocked arrays
        _times.reserve(expected_total_samples);
        _blocked.reserve(expected_total_samples);
        _offsets.reserve(expected_total_samples);
      }
    }
    // Note: USD allows variable-sized arrays, so we don't enforce size consistency

    size_t new_idx = _times.size();
    _times.push_back(t);
    _blocked.push_back(1);  // true = 1
    _blocked_count++;
    _array_counts.push_back(count);  // Store per-sample array size

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

    // Resolve offset following dedup chain if necessary
    if (!_offsets.empty()) {
      // Using offset table with dedup support
      if (_offsets[idx] == SIZE_MAX) {
        // This is a blocked value (shouldn't happen as we checked above, but be safe)
        if (blocked) {
          *blocked = true;
        }
        *value = T{};
        return true;
      }

      size_t byte_offset = 0;
      if (!resolve_offset(idx, &byte_offset)) {
        // Failed to resolve offset (circular reference or invalid dedup chain)
        return false;
      }

      const uint8_t* src = _values.data() + byte_offset;
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

    DCOUT("PODTimeSamples::get_typed_array_at idx=" << idx << " packed_value=0x" << std::hex << packed_value << std::dec);

    // Reconstruct TypedArray from packed value
    // Note: This creates a shallow copy - the underlying TypedArrayImpl is shared
    // and marked as dedup to prevent deletion
    TypedArrayImpl<T>* ptr = nullptr;

    // Extract pointer from packed value
    uint64_t ptr_bits = packed_value & 0x0000FFFFFFFFFFFFULL;  // Lower 48 bits
    // Note: dedup flag is in bit 63 but we always mark as dedup when retrieving

    DCOUT("PODTimeSamples::get_typed_array_at after mask: ptr_bits=0x" << std::hex << ptr_bits << std::dec);

    // Sign-extend from 48 bits to 64 bits for canonical address
    if (ptr_bits & (1ULL << 47)) {
      ptr_bits |= 0xFFFF000000000000ULL;
      DCOUT("PODTimeSamples::get_typed_array_at sign-extended: ptr_bits=0x" << std::hex << ptr_bits << std::dec);
    }

    ptr = reinterpret_cast<TypedArrayImpl<T>*>(ptr_bits);
    DCOUT("PODTimeSamples::get_typed_array_at ptr=" << std::hex << ptr << " size=" << std::dec << (ptr ? ptr->size() : 0));

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
    if ((_is_stl_array || _is_typed_array) && (!_array_counts.empty() || _array_size > 0)) {
      // Resolve offset following dedup chain if necessary
      if (!_offsets.empty()) {
        if (_offsets[idx] == SIZE_MAX) {
          // Blocked value
          return TypedArrayView<const T>();
        }

        size_t byte_offset = 0;
        size_t resolved_idx = idx;  // Track which sample index we resolved to
        if (!resolve_offset(idx, &byte_offset, nullptr, nullptr, 100, &resolved_idx)) {
          // Failed to resolve offset (circular reference or invalid dedup chain)
          return TypedArrayView<const T>();
        }

        // Use per-sample array size from the resolved index (with fallback to global _array_size)
        size_t array_count = (resolved_idx < _array_counts.size()) ? _array_counts[resolved_idx] : _array_size;
        const T* src = reinterpret_cast<const T*>(_values.data() + byte_offset);
        return TypedArrayView<const T>(src, array_count);
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

  size_t estimate_memory_usage() const {
    size_t total = sizeof(PODTimeSamples);
    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity();  // Buffer already stores bytes
    total += _values.capacity();   // Buffer already stores bytes
    total += _offsets.capacity() * sizeof(uint64_t);  // Include offset table
    total += _array_counts.capacity() * sizeof(size_t);  // Per-sample array counts
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
    // Check if we're using unified storage (Phase 3) or legacy storage
    if (!_times.empty()) {
      // Using unified storage
      return false;
    }
    return _use_pod ? _pod_samples.empty() : _samples.empty();
  }

  size_t size() const {
    if (_dirty) {
      update();
    }
    // Check if we're using unified storage (Phase 3) or legacy storage
    if (!_times.empty()) {
      // Using unified storage - return _times.size()
      return _times.size();
    }
    return _use_pod ? _pod_samples.size() : _samples.size();
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
  /// Initialize TimeSamples with a specific type_id
  /// This determines whether to use POD optimization or regular storage
  bool init(uint32_t type_id);

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
      // Phase 3: Use unified storage directly
      if (idx >= _times.size()) {
        return nonstd::nullopt;
      }
      if (_dirty) {
        update();
      }
      return _times[idx];
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
    // Check if using POD storage
    if (_use_pod) {
      // Get type name from type_id when using POD storage
      if (_type_id != 0) {
        return value::GetTypeName(_type_id);
      } else {
        return std::string();
      }
    }

    // Original path for non-POD storage
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

  /// Add an array sample using value::Value storage with dedup support
  /// This stores the value::Value in _value_array_storage and records the index
  /// @param t Time value for this sample
  /// @param v The array value to add (will be moved)
  /// @param err Optional error string
  bool add_value_array_sample(double t, value::Value &&v, std::string *err = nullptr) {
    (void)err;  // Currently unused, reserved for future error reporting
    // Auto-initialize on first sample
    if (_times.empty() && !_use_value_array) {
      _type_id = v.type_id();
      _use_value_array = true;
      _is_array = true;
      _use_pod = false;  // Value array storage is not POD storage
    }

    // Store in value array storage
    size_t storage_index = _value_array_storage.size();
    _value_array_storage.push_back(std::move(v));

    // Record time and reference
    _times.push_back(t);
    _value_array_refs.push_back(make_value_array_ref(storage_index, false));

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
    if (_use_value_array) {
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
      _value_array_refs.push_back(make_value_array_ref(storage_index, true));

      _dirty = true;
      return true;
    }

    // Fallback to old _samples based storage
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
  typename std::enable_if<!std::is_same<T, bool>::value, bool>::type
  add_array_sample_pod(double t, const std::vector<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {
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

  // Specialization for std::vector<bool> since it doesn't have data() member
  template<typename T>
  typename std::enable_if<std::is_same<T, bool>::value, bool>::type
  add_array_sample_pod(double t, const std::vector<T>& value, std::string *err = nullptr, size_t expected_total_samples = 0) {
    // Auto-initialize on first sample
    if (empty()) {
      init(value::TypeTraits<std::vector<T>>::type_id());
    }

    if (_use_pod) {
      // Convert std::vector<bool> to std::vector<uint8_t> for storage
      std::vector<uint8_t> byte_array;
      byte_array.reserve(value.size());
      for (bool b : value) {
        byte_array.push_back(b ? 1 : 0);
      }

      // Manually initialize PODTimeSamples with bool type_id if this is the first sample
      if (_pod_samples.empty()) {
        // Initialize with bool type_id, not uint8_t
        _pod_samples.init(value::TypeTraits<bool>::underlying_type_id(), true, sizeof(uint8_t), value.size(), expected_total_samples);
      }

      // Store as uint8_t array internally, but type remains bool[]
      // Note: We need to bypass the type_id setting in add_array_sample
      // by checking if already initialized
      bool result;
      if (_pod_samples.type_id() == value::TypeTraits<bool>::underlying_type_id()) {
        // PODTimeSamples already initialized with bool type, just add the data
        _pod_samples._times.push_back(t);
        _pod_samples._blocked.push_back(0);  // false = 0
        _pod_samples._array_counts.push_back(value.size());  // Store per-sample array size

        // Store offset with array flag and append array data
        size_t byte_offset = _pod_samples._values.size();
        uint64_t encoded_offset = PODTimeSamples::make_offset(byte_offset, true);  // is_array=true
        _pod_samples._offsets.push_back(encoded_offset);

        size_t byte_size = sizeof(uint8_t) * byte_array.size();
        _pod_samples._values.resize(_pod_samples._values.size() + byte_size);
        std::memcpy(_pod_samples._values.data() + byte_offset, byte_array.data(), byte_size);

        _pod_samples._dirty = true;
        result = true;
      } else {
        // Fallback to normal add_array_sample (shouldn't happen)
        result = _pod_samples.add_array_sample<uint8_t>(t, byte_array.data(), byte_array.size(), err, expected_total_samples);
      }
      _dirty = true;
      return result;
    }

    if (err) {
      (*err) += "Not using POD storage for type bool[].\n";
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

    DCOUT("is dedup? " << value.is_dedup());
    DCOUT("_use_pod? " << _use_pod);

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

  /// Add a deduplicated bool array sample - reuses data from an existing sample
  /// Special handling for bool since std::vector<bool> doesn't satisfy POD requirements
  /// @param t Time value for this sample
  /// @param ref_index Index of the existing sample whose data/offset to reuse
  /// @param err Optional error string
  bool add_dedup_bool_array_sample_pod(double t, size_t ref_index, std::string *err = nullptr) {
    if (_use_pod) {
      // Bool arrays are stored internally as uint8_t, but with bool type_id
      // The dedup mechanism works the same way - just reference the existing sample
      size_t new_idx = _pod_samples._times.size();

      // Validate reference
      if (ref_index >= new_idx) {
        if (err) {
          (*err) += "Invalid ref_index in add_dedup_bool_array_sample_pod: " +
                    std::to_string(ref_index) + " >= " + std::to_string(new_idx) + ".\n";
        }
        return false;
      }

      if (PODTimeSamples::is_dedup(_pod_samples._offsets[ref_index])) {
        if (err) {
          (*err) += "Cannot deduplicate from deduplicated sample.\n";
        }
        return false;
      }

      _pod_samples._times.push_back(t);
      _pod_samples._blocked.push_back(0);  // false = 0

      // Copy array count from the referenced sample
      size_t ref_array_count = (ref_index < _pod_samples._array_counts.size())
                                   ? _pod_samples._array_counts[ref_index]
                                   : _pod_samples._array_size;
      _pod_samples._array_counts.push_back(ref_array_count);

      // Create dedup offset: bit 63=1 (dedup), bit 62=1 (array), bits 61-0=ref_index
      uint64_t dedup_offset = PODTimeSamples::make_dedup_offset(ref_index, true);
      _pod_samples._offsets.push_back(dedup_offset);

      _pod_samples._dirty = true;
      _dirty = true;
      return true;
    }

    if (err) {
      (*err) += "Not using POD storage for dedup bool array.\n";
    }
    return false;
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

    // If already using _pod_samples (backward compat), delegate to it
    if (!_pod_samples.empty()) {
      bool result = _pod_samples.add_typed_array_sample<T>(t, typed_array, err, expected_total_samples);
      _dirty = true;
      return result;
    }

    // Use new unified storage with _array_values
    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Allocate new buffer for this array sample
    auto array_buffer = std::make_unique<Buffer<16>>();
    size_t data_size = sizeof(T) * typed_array.size();
    array_buffer->resize(data_size);
    if (typed_array.data() && typed_array.size() > 0) {
      std::memcpy(array_buffer->data(), typed_array.data(), data_size);
    }

    // Store the index of the newly allocated buffer
    size_t array_index = _array_values.size();
    _array_values.push_back(std::move(array_buffer));

    // Create encoded offset with array buffer flag and index
    uint64_t encoded_offset = PODTimeSamples::make_array_buffer_offset(array_index);
    _offsets.push_back(encoded_offset);

    // Update array metadata
    _is_array = true;
    _array_size = typed_array.size();
    _element_size = sizeof(T);

    _dirty = true;
    return true;
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

    // Phase 2: Check if using unified storage (non-POD samples but has POD data)
    if (!_pod_samples.empty()) {
      // Delegate to PODTimeSamples (backward compat during transition)
      return _pod_samples.get_typed_array_view_at<T>(idx);
    }

    // Phase 2: Use unified storage directly
    if (!_times.empty() && _is_array) {
      if (idx >= _times.size()) {
        return TypedArrayView<const T>(nullptr, 0);
      }

      // Check if blocked
      if (_blocked[idx]) {
        return TypedArrayView<const T>(nullptr, 0);
      }

      // Resolve offset - now checks both _values and _array_values buffers
      size_t byte_offset = 0;
      bool use_array_buffer = false;
      if (!PODTimeSamples::resolve_offset_static(_offsets, idx, &byte_offset, nullptr, &use_array_buffer)) {
        return TypedArrayView<const T>(nullptr, 0);
      }

      const T* data;
      if (use_array_buffer) {
        // Array data is in _array_values (unique_ptr vector)
        if (byte_offset >= _array_values.size() || !_array_values[byte_offset]) {
          return TypedArrayView<const T>(nullptr, 0);
        }
        data = reinterpret_cast<const T*>(_array_values[byte_offset]->data());
      } else {
        // Scalar array data is in _values buffer
        data = reinterpret_cast<const T*>(_values.data() + byte_offset);
      }
      return TypedArrayView<const T>(data, _array_size);
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

    // If _use_pod = false but unified storage has data, convert to generic samples
    // Skip if using value array storage - that's handled below
    if (!_use_value_array && !_times.empty() && _samples.empty()) {
      if (_dirty) {
        update();
      }

      // Convert unified POD storage to generic samples
      // This handles the case where ParseTypedTimeSamples stored data in unified storage
      // but _use_pod is disabled (for deprecation)
      _samples.clear();
      _samples.reserve(_times.size());

      uint32_t type_id = _type_id;
      for (size_t i = 0; i < _times.size(); ++i) {
        Sample s;
        s.t = _times[i];
        s.blocked = (_blocked[i] != 0);

        if (s.blocked) {
          // Blocked sample
          s.value = value::Value();  // None value
        } else {
          // Reconstruct value from storage based on type_id
          // For POD types, values are stored in _small_values (up to 8 bytes) or _values (larger)
          // First check if this is a small value (<= 8 bytes) stored in _small_values
          if (i < _small_values.size()) {
            uint64_t stored = _small_values[i];
            // Reconstruct typed value based on type_id
            switch (type_id) {
              case value::TypeTraits<float>::type_id(): {
                float fval = 0.0f;
                std::memcpy(&fval, &stored, sizeof(float));
                s.value = value::Value(fval);
                break;
              }
              case value::TypeTraits<double>::type_id(): {
                double dval = 0.0;
                std::memcpy(&dval, &stored, sizeof(double));
                s.value = value::Value(dval);
                break;
              }
              case value::TypeTraits<int32_t>::type_id(): {
                int32_t ival = 0;
                std::memcpy(&ival, &stored, sizeof(int32_t));
                s.value = value::Value(ival);
                break;
              }
              case value::TypeTraits<uint32_t>::type_id(): {
                uint32_t uval = 0;
                std::memcpy(&uval, &stored, sizeof(uint32_t));
                s.value = value::Value(uval);
                break;
              }
              case value::TypeTraits<int64_t>::type_id(): {
                int64_t lval = 0;
                std::memcpy(&lval, &stored, sizeof(int64_t));
                s.value = value::Value(lval);
                break;
              }
              case value::TypeTraits<uint64_t>::type_id(): {
                uint64_t ulval = 0;
                std::memcpy(&ulval, &stored, sizeof(uint64_t));
                s.value = value::Value(ulval);
                break;
              }
              case value::TypeTraits<bool>::type_id(): {
                bool bval = (stored != 0);
                s.value = value::Value(bval);
                break;
              }
              default:
                s.value = value::Value();  // Fallback for types not yet supported
                break;
            }
          } else if (i < _offsets.size()) {
            // Larger types (> 8 bytes) are stored in _values with offsets
            uint64_t encoded_offset = _offsets[i];
            size_t byte_offset = static_cast<size_t>(encoded_offset & PODTimeSamples::OFFSET_VALUE_MASK);

            // Reconstruct value based on type_id
            switch (type_id) {
              case value::TypeTraits<value::float3>::type_id(): {
                if (byte_offset + sizeof(value::float3) <= _values.size()) {
                  value::float3 f3val;
                  std::memcpy(&f3val, _values.data() + byte_offset, sizeof(value::float3));
                  s.value = value::Value(f3val);
                } else {
                  s.value = value::Value();  // Invalid offset
                }
                break;
              }
              case value::TypeTraits<value::point3f>::type_id(): {
                if (byte_offset + sizeof(value::point3f) <= _values.size()) {
                  value::point3f p3val;
                  std::memcpy(&p3val, _values.data() + byte_offset, sizeof(value::point3f));
                  s.value = value::Value(p3val);
                } else {
                  s.value = value::Value();  // Invalid offset
                }
                break;
              }
              case value::TypeTraits<value::color3f>::type_id(): {
                if (byte_offset + sizeof(value::color3f) <= _values.size()) {
                  value::color3f c3val;
                  std::memcpy(&c3val, _values.data() + byte_offset, sizeof(value::color3f));
                  s.value = value::Value(c3val);
                } else {
                  s.value = value::Value();  // Invalid offset
                }
                break;
              }
              default:
                s.value = value::Value();  // Fallback for types not yet supported
                break;
            }
          } else {
            s.value = value::Value();  // Fallback
          }
        }

        _samples.push_back(s);
      }
      return _samples;
    }

    // Handle value array storage (from add_value_array_sample)
    if (_use_value_array && !_times.empty() && _samples.empty()) {
      if (_dirty) {
        update();
      }

      _samples.clear();
      _samples.reserve(_times.size());

      for (size_t i = 0; i < _times.size(); ++i) {
        Sample s;
        s.t = _times[i];

        // Check dedup flag from _value_array_refs
        if (i < _value_array_refs.size()) {
          size_t storage_idx = get_value_array_index(_value_array_refs[i]);
          if (storage_idx < _value_array_storage.size()) {
            s.value = _value_array_storage[storage_idx];
            s.blocked = false;
          } else {
            s.value = value::Value();  // Invalid index
            s.blocked = true;
          }
        } else {
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
      if (!_samples.empty()) {
        if (const auto pv = _samples[0].value.as<T>()) {
          (*dst) = *pv;
          return true;
        }
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
      // Also account for unified storage if in use
      total += _times.capacity() * sizeof(double);
      total += _blocked.capacity();
      total += _values.capacity();
      total += _offsets.capacity() * sizeof(uint64_t);
    } else {
      for (const auto &sample : _samples) {
        total += sizeof(Sample);
        total += sample.value.estimate_memory_usage();
      }
    }

    return total;
  }

  //
  // Phase 2: Unified array methods (work directly with TimeSamples storage)
  //

  /// Add array sample using unified storage (Phase 2 path)
  /// This bypasses _pod_samples and uses TimeSamples members directly
  /// Array data is stored in _array_values using unique_ptr for proper ownership semantics
  template<typename T>
  bool add_array_sample(double t, const T* values, size_t count, std::string* err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_array_sample requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      if (!init(value::TypeTraits<T>::type_id())) {
        if (err) *err = "Failed to initialize TimeSamples";
        return false;
      }
    }

    // If already using _pod_samples (backward compat), delegate to it
    if (!_pod_samples.empty()) {
      return _pod_samples.add_array_sample<T>(t, values, count, err);
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
    uint64_t encoded_offset = PODTimeSamples::make_array_buffer_offset(array_index);
    _offsets.push_back(encoded_offset);

    // Update array metadata
    _is_array = true;
    _array_size = count;
    _element_size = sizeof(T);

    _dirty = true;
    return true;
  }

  /// Add deduplicated array sample (Phase 2 path)
  template<typename T>
  bool add_dedup_array_sample(double t, size_t ref_index, std::string* err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_dedup_array_sample requires POD types");

    // If using _pod_samples, delegate
    if (!_pod_samples.empty()) {
      return _pod_samples.add_dedup_array_sample<T>(t, ref_index, err);
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

    if (PODTimeSamples::is_dedup(_offsets[ref_index])) {
      if (err) *err = "Cannot deduplicate from deduplicated sample";
      return false;
    }

    // Add dedup sample
    _times.push_back(t);
    _blocked.push_back(0);

    uint64_t dedup_offset = PODTimeSamples::make_dedup_offset(ref_index, true);
    _offsets.push_back(dedup_offset);

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

  //
  // Phase 3: POD scalar sample methods
  //

  /// Add POD scalar sample using unified storage (Phase 3 path)
  /// This is for single POD values (not arrays)
  /// Small types (sizeof(T) <= 8) - stored directly in _small_values
  template<typename T>
  typename std::enable_if<(sizeof(T) <= 8), bool>::type
  add_pod_sample(double t, const T& value, std::string* err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_pod_sample requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      if (!init(value::TypeTraits<T>::type_id())) {
        if (err) *err = "Failed to initialize TimeSamples";
        return false;
      }
    }

    // If already using _pod_samples (backward compat), delegate to it
    if (!_pod_samples.empty()) {
      return _pod_samples.add_sample<T>(t, value, err);
    }

    // Use unified storage for scalar POD
    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Direct storage for small scalars - no offset entry needed
    uint64_t small_value = 0;
    std::memcpy(&small_value, &value, sizeof(T));
    _small_values.push_back(small_value);

    // Update metadata (scalar, not array)
    _is_array = false;
    _array_size = 1;
    _element_size = sizeof(T);

    _dirty = true;
    return true;
  }

  /// Add POD scalar sample using unified storage (Phase 3 path)
  /// Large types (sizeof(T) > 8) - stored in _values with offset table
  template<typename T>
  typename std::enable_if<(sizeof(T) > 8), bool>::type
  add_pod_sample(double t, const T& value, std::string* err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_pod_sample requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      if (!init(value::TypeTraits<T>::type_id())) {
        if (err) *err = "Failed to initialize TimeSamples";
        return false;
      }
    }

    // If already using _pod_samples (backward compat), delegate to it
    if (!_pod_samples.empty()) {
      return _pod_samples.add_sample<T>(t, value, err);
    }

    // Use unified storage for scalar POD
    _times.push_back(t);
    _blocked.push_back(0);  // Not blocked

    // Offset-based storage for large scalars
    size_t byte_offset = _values.size();
    _values.resize(byte_offset + sizeof(T));
    std::memcpy(_values.data() + byte_offset, &value, sizeof(T));

    // Create encoded offset (not array)
    uint64_t encoded_offset = PODTimeSamples::make_offset(byte_offset, false);
    _offsets.push_back(encoded_offset);

    // Update metadata (scalar, not array)
    _is_array = false;
    _array_size = 1;
    _element_size = sizeof(T);

    _dirty = true;
    return true;
  }

  /// Add blocked POD sample (Phase 3 path)
  template<typename T>
  bool add_pod_blocked_sample(double t, std::string* err = nullptr) {
    static_assert(std::is_trivial<T>::value && std::is_standard_layout<T>::value,
                  "add_pod_blocked_sample requires POD types");

    // Auto-initialize on first sample
    if (empty()) {
      if (!init(value::TypeTraits<T>::type_id())) {
        if (err) *err = "Failed to initialize TimeSamples";
        return false;
      }
    }

    // If already using _pod_samples (backward compat), delegate to it
    if (!_pod_samples.empty()) {
      return _pod_samples.add_blocked_sample<T>(t, err);
    }

    // Add blocked sample to unified storage
    _times.push_back(t);
    _blocked.push_back(1);  // Blocked

    // For small types (sizeof <= 8), don't use offsets - just rely on _blocked flag
    // For large types (sizeof > 8), need offset table entry
    if (sizeof(T) > 8) {
      _offsets.push_back(SIZE_MAX);  // Special marker for blocked
    }
    // Note: No entry in _small_values for blocked samples (for small types)

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
  /// Works with both unified POD storage and generic Value storage
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

    // Check if using _pod_samples (backward compat)
    if (!_pod_samples.empty()) {
      // Delegate to PODTimeSamples
      auto view = _pod_samples.get_typed_array_view_at<T>(idx);
      if (view.size() == 0) {
        if (out_blocked) {
          // Check if it's actually blocked or just empty
          *out_blocked = (idx < _pod_samples._times.size() &&
                         _pod_samples._blocked[idx]);
        }
        return false;
      }
      out_vec->assign(view.data(), view.data() + view.size());
      if (out_blocked) *out_blocked = false;
      return true;
    }

    // Check unified POD storage
    if (!_times.empty() && _is_array) {
      if (idx >= _times.size()) {
        return false;
      }

      // Check if blocked
      if (_blocked[idx]) {
        if (out_blocked) *out_blocked = true;
        return false;
      }

      // Resolve offset - now checks both _values and _array_values buffers
      size_t byte_offset = 0;
      bool use_array_buffer = false;
      if (!PODTimeSamples::resolve_offset_static(_offsets, idx, &byte_offset, nullptr, &use_array_buffer)) {
        return false;
      }

      const T* data;
      if (use_array_buffer) {
        // Array data is in _array_values (unique_ptr vector)
        if (byte_offset >= _array_values.size() || !_array_values[byte_offset]) {
          return false;
        }
        data = reinterpret_cast<const T*>(_array_values[byte_offset]->data());
      } else {
        // Scalar array data is in _values buffer
        data = reinterpret_cast<const T*>(_values.data() + byte_offset);
      }
      out_vec->assign(data, data + _array_size);
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

    // Check if using _pod_samples (backward compat)
    if (!_pod_samples.empty()) {
      auto view = _pod_samples.get_typed_array_view_at_time<T>(t);
      if (view.size() == 0) {
        return false;
      }
      out_vec->assign(view.data(), view.data() + view.size());
      if (out_blocked) *out_blocked = false;
      return true;
    }

    // Check unified POD storage
    if (!_times.empty()) {
      // Find index for time
      auto it = std::find_if(_times.begin(), _times.end(), [&t](double sample_t) {
        return std::fabs(t - sample_t) < std::numeric_limits<double>::epsilon();
      });

      if (it != _times.end()) {
        size_t idx = static_cast<size_t>(std::distance(_times.begin(), it));
        return get_vector_at<T>(idx, out_vec, out_blocked);
      }
      return false;  // Time not found
    }

    // Check generic Value storage
    auto it = std::find_if(_samples.begin(), _samples.end(), [&t](const Sample& s) {
      return std::fabs(t - s.t) < std::numeric_limits<double>::epsilon();
    });

    if (it != _samples.end()) {
      size_t idx = static_cast<size_t>(std::distance(_samples.begin(), it));
      return get_vector_at<T>(idx, out_vec, out_blocked);
    }

    return false;  // Time not found
  }

  //
  // Phase 3: Accessor methods for unified storage
  // These provide read-only access to internal POD storage for utilities like pprint
  //

  const std::vector<double>& get_times() const {
    if (_dirty) {
      update();
    }
    // Check if using unified storage (Phase 3) or _pod_samples storage
    if (!_times.empty()) {
      // Using unified storage directly on TimeSamples
      return _times;
    }
    // Delegate to _pod_samples if not using unified storage
    if (_use_pod) {
      return _pod_samples._times;
    }
    return _times;
  }

  const Buffer<16>& get_blocked() const {
    if (_dirty) {
      update();
    }
    // Check if using unified storage
    if (!_times.empty()) {
      return _blocked;
    }
    if (_use_pod) {
      return _pod_samples._blocked;
    }
    return _blocked;
  }

  const Buffer<16>& get_values() const {
    if (_dirty) {
      update();
    }
    // Check if using unified storage
    if (!_times.empty()) {
      return _values;
    }
    if (_use_pod) {
      return _pod_samples._values;
    }
    return _values;
  }

  const std::vector<uint64_t>& get_offsets() const {
    if (_dirty) {
      update();
    }
    // Check if using unified storage
    if (!_times.empty()) {
      return _offsets;
    }
    if (_use_pod) {
      return _pod_samples._offsets;
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
    return _is_array;
  }

  size_t get_array_size() const {
    return _array_size;
  }

  const std::vector<size_t>& get_array_counts() const {
    // Check if using unified storage
    if (!_times.empty()) {
      return _array_counts;
    }
    if (_use_pod) {
      return _pod_samples._array_counts;
    }
    return _array_counts;
  }

 private:
  // Generic path storage (for non-POD types: string, token, dict, etc.)
  mutable std::vector<Sample> _samples;

  // POD path storage (moved from PODTimeSamples for Phase 2 unification)
  mutable std::vector<double> _times;
  mutable Buffer<16> _blocked;
  mutable std::vector<uint64_t> _small_values;                       // Direct storage for small scalar POD types (sizeof(T) <= 8 bytes), stored as uint64
  mutable Buffer<16> _values;                                        // Raw byte storage for large scalar POD types and arrays
  mutable std::vector<std::unique_ptr<Buffer<16>>> _array_values;    // Array data storage: each entry is a separate allocated buffer for one array sample
  mutable std::vector<uint64_t> _offsets;                            // Offset table for large types and arrays with dedup/array/buffer flags

  // value::Value array storage with dedup support (for non-POD array types)
  // Stores unique value::Value objects; _value_array_refs contains indices or dedup references
  mutable std::vector<value::Value> _value_array_storage;  // Stores unique array values
  mutable std::vector<uint64_t> _value_array_refs;         // bit 63 = dedup flag, bits 0-62 = storage index or ref index

  // Type information
  uint32_t _type_id{0};
  bool _use_pod{false};  // True = use POD path, False = use generic path
  bool _use_value_array{false};  // True = use _value_array_storage for array samples

  // Array type information (for POD arrays)
  bool _is_array{false};
  size_t _array_size{0};
  size_t _element_size{0};
  mutable size_t _blocked_count{0};
  mutable std::vector<size_t> _array_counts; // Per-sample array element counts (for variable-sized arrays)

  mutable bool _dirty{false};
  mutable size_t _dirty_start{0};
  mutable size_t _dirty_end{0};

  // Keep _pod_samples for now (will be removed in final step)
  mutable tinyusdz::PODTimeSamples _pod_samples;

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
// Extern template declarations for PODTimeSamples methods
//

// PODTimeSamples::add_sample
extern template bool PODTimeSamples::add_sample<bool>(double, const bool&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<int32_t>(double, const int32_t&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<uint32_t>(double, const uint32_t&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<int64_t>(double, const int64_t&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<uint64_t>(double, const uint64_t&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::half>(double, const value::half&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<float>(double, const float&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<double>(double, const double&, std::string*, size_t);
// Vector types - using std::array versions
extern template bool PODTimeSamples::add_sample<std::array<value::half, 2>>(double, const std::array<value::half, 2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<value::half, 3>>(double, const std::array<value::half, 3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<value::half, 4>>(double, const std::array<value::half, 4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<float, 2>>(double, const std::array<float, 2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<float, 3>>(double, const std::array<float, 3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<float, 4>>(double, const std::array<float, 4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<double, 2>>(double, const std::array<double, 2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<double, 3>>(double, const std::array<double, 3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<double, 4>>(double, const std::array<double, 4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<int, 2>>(double, const std::array<int, 2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<int, 3>>(double, const std::array<int, 3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<int, 4>>(double, const std::array<int, 4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<uint32_t, 2>>(double, const std::array<uint32_t, 2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<uint32_t, 3>>(double, const std::array<uint32_t, 3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<std::array<uint32_t, 4>>(double, const std::array<uint32_t, 4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::quath>(double, const value::quath&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::quatf>(double, const value::quatf&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::quatd>(double, const value::quatd&, std::string*, size_t);
// Matrix types - now trivial with default constructors
extern template bool PODTimeSamples::add_sample<value::matrix2f>(double, const value::matrix2f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::matrix3f>(double, const value::matrix3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::matrix4f>(double, const value::matrix4f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::matrix2d>(double, const value::matrix2d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::matrix3d>(double, const value::matrix3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::matrix4d>(double, const value::matrix4d&, std::string*, size_t);
// Note: frame4d is not trivial (doesn't satisfy std::is_trivial) so it cannot use PODTimeSamples::add_sample
extern template bool PODTimeSamples::add_sample<value::vector3h>(double, const value::vector3h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::vector3f>(double, const value::vector3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::vector3d>(double, const value::vector3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::normal3h>(double, const value::normal3h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::normal3f>(double, const value::normal3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::normal3d>(double, const value::normal3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::point3h>(double, const value::point3h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::point3f>(double, const value::point3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::point3d>(double, const value::point3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color3h>(double, const value::color3h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color3f>(double, const value::color3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color3d>(double, const value::color3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color4h>(double, const value::color4h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color4f>(double, const value::color4f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::color4d>(double, const value::color4d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord2h>(double, const value::texcoord2h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord2f>(double, const value::texcoord2f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord2d>(double, const value::texcoord2d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord3h>(double, const value::texcoord3h&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord3f>(double, const value::texcoord3f&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::texcoord3d>(double, const value::texcoord3d&, std::string*, size_t);
extern template bool PODTimeSamples::add_sample<value::timecode>(double, const value::timecode&, std::string*, size_t);
// Note: frame4d is not trivial (doesn't satisfy std::is_trivial) so it cannot use PODTimeSamples::add_sample

// PODTimeSamples::add_typed_array_sample
extern template bool PODTimeSamples::add_typed_array_sample<float>(double, const TypedArray<float>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<double>(double, const TypedArray<double>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<int32_t>(double, const TypedArray<int32_t>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<uint32_t>(double, const TypedArray<uint32_t>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<int64_t>(double, const TypedArray<int64_t>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<uint64_t>(double, const TypedArray<uint64_t>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::float2>(double, const TypedArray<value::float2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::float3>(double, const TypedArray<value::float3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::float4>(double, const TypedArray<value::float4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::double2>(double, const TypedArray<value::double2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::double3>(double, const TypedArray<value::double3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::double4>(double, const TypedArray<value::double4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::int2>(double, const TypedArray<value::int2>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::int3>(double, const TypedArray<value::int3>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::int4>(double, const TypedArray<value::int4>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix2f>(double, const TypedArray<value::matrix2f>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix3f>(double, const TypedArray<value::matrix3f>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix4f>(double, const TypedArray<value::matrix4f>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix2d>(double, const TypedArray<value::matrix2d>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix3d>(double, const TypedArray<value::matrix3d>&, std::string*, size_t);
extern template bool PODTimeSamples::add_typed_array_sample<value::matrix4d>(double, const TypedArray<value::matrix4d>&, std::string*, size_t);

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
