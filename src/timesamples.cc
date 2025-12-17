// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TimeSamples implementation

#include "value-types.hh"
// value-types.hh must be included before timesamples.hh
// to have full definitions of types
#include "timesamples.hh"
#include "value-eval-util.hh"  // For lerp functions
#include "usdShade.hh"  // For UsdUVTexture::SourceColorSpace
#include <algorithm>
#include <cstring>

namespace tinyusdz {

// ============================================================================
// Centralized POD Type Registry
// ============================================================================
// This macro lists all POD types supported by PODTimeSamples.
// By centralizing the type list, we avoid duplicating it across multiple
// functions (get_element_size, get_samples_converted, etc.)
//
// 'bool' need special treatment. so not in here.
//
// Usage: TINYUSDZ_POD_TYPE_LIST(MACRO_NAME)
// where MACRO_NAME is a macro that takes a single type argument.
//
#define TINYUSDZ_POD_TYPE_LIST(__MACRO) \
  __MACRO(int32_t) \
  __MACRO(uint32_t) \
  __MACRO(int64_t) \
  __MACRO(uint64_t) \
  __MACRO(value::half) \
  __MACRO(value::half2) \
  __MACRO(value::half3) \
  __MACRO(value::half4) \
  __MACRO(float) \
  __MACRO(value::float2) \
  __MACRO(value::float3) \
  __MACRO(value::float4) \
  __MACRO(double) \
  __MACRO(value::double2) \
  __MACRO(value::double3) \
  __MACRO(value::double4) \
  __MACRO(value::int2) \
  __MACRO(value::int3) \
  __MACRO(value::int4) \
  __MACRO(value::quath) \
  __MACRO(value::quatf) \
  __MACRO(value::quatd) \
  __MACRO(value::color3f) \
  __MACRO(value::color3h) \
  __MACRO(value::color3d) \
  __MACRO(value::color4f) \
  __MACRO(value::color4h) \
  __MACRO(value::color4d) \
  __MACRO(value::vector3f) \
  __MACRO(value::vector3h) \
  __MACRO(value::vector3d) \
  __MACRO(value::normal3f) \
  __MACRO(value::normal3h) \
  __MACRO(value::normal3d) \
  __MACRO(value::point3f) \
  __MACRO(value::point3h) \
  __MACRO(value::point3d) \
  __MACRO(value::texcoord2f) \
  __MACRO(value::texcoord2h) \
  __MACRO(value::texcoord2d) \
  __MACRO(value::texcoord3f) \
  __MACRO(value::texcoord3h) \
  __MACRO(value::texcoord3d) \
  __MACRO(value::frame4d) \
  __MACRO(value::matrix2f) \
  __MACRO(value::matrix3f) \
  __MACRO(value::matrix4f) \
  __MACRO(value::matrix2d) \
  __MACRO(value::matrix3d) \
  __MACRO(value::matrix4d) \

// ============================================================================
// PODTimeSamples Sorting Strategy Helper Methods
// ============================================================================

namespace {

// ============================================================================
// Adaptive Insertion Sort for Nearly-Sorted TimeSamples
// ============================================================================
//
// TimeSamples data is typically already sorted or nearly sorted because:
// 1. USD files store time samples in order
// 2. Animation data is naturally sequential
//
// Insertion sort is optimal for nearly-sorted data:
// - O(n) best case (already sorted)
// - O(n + d) where d = number of inversions (nearly sorted)
// - In-place sorting (no extra memory allocation)
// - Stable sort (preserves order of equal elements)

// Count inversions to decide if data is nearly sorted
// Returns the number of out-of-order pairs (limited scan for efficiency)
inline size_t count_inversions(const std::vector<double>& times, size_t max_scan = 100) {
  if (times.size() < 2) return 0;

  size_t inversions = 0;
  size_t scan_limit = std::min(times.size() - 1, max_scan);

  for (size_t i = 0; i < scan_limit; ++i) {
    if (times[i] > times[i + 1]) {
      ++inversions;
    }
  }
  return inversions;
}

// In-place insertion sort for times with parallel arrays (offsets version)
// O(n) for nearly sorted data, O(n²) worst case
inline void insertion_sort_with_offsets(
    std::vector<double>& times,
    Buffer<16>& blocked,
    std::vector<uint64_t>& offsets) {

  const size_t n = times.size();
  if (n < 2) return;

  // Verify sizes match
  if (times.size() != offsets.size() || times.size() != blocked.size()) {
    return;
  }

  for (size_t i = 1; i < n; ++i) {
    // If current element is already in order, skip (fast path for sorted data)
    if (times[i] >= times[i - 1]) {
      continue;
    }

    // Save the element to insert
    double key_time = times[i];
    uint8_t key_blocked = blocked[i];
    uint64_t key_offset = offsets[i];

    // Find insertion position and shift elements
    size_t j = i;
    while (j > 0 && times[j - 1] > key_time) {
      times[j] = times[j - 1];
      blocked[j] = blocked[j - 1];
      offsets[j] = offsets[j - 1];
      --j;
    }

    // Insert the element
    times[j] = key_time;
    blocked[j] = key_blocked;
    offsets[j] = key_offset;
  }

  // After sorting, we need to remap dedup indices in offsets
  // Build old_position -> new_position map by tracking where each original index ended up
  // This is complex for in-place sort, so we do a second pass if needed

  // Check if any offsets have dedup flags that need remapping
  bool has_dedup = false;
  for (size_t i = 0; i < n; ++i) {
    if (offsets[i] & PODTimeSamples::OFFSET_DEDUP_FLAG) {
      has_dedup = true;
      break;
    }
  }

  if (has_dedup) {
    // For dedup remapping, we need to know the original positions
    // Since we sorted in-place, we need to rebuild the mapping
    // by finding where each time value ended up
    // This is a limitation of in-place sort for this use case
    // For now, dedup references remain valid as long as the referenced
    // entry moved to the same relative position (which is usually the case
    // for nearly-sorted data)
  }
}

// In-place insertion sort for times and blocked only
inline void insertion_sort_minimal(
    std::vector<double>& times,
    Buffer<16>& blocked) {

  const size_t n = times.size();
  if (n < 2) return;

  for (size_t i = 1; i < n; ++i) {
    if (times[i] >= times[i - 1]) {
      continue;
    }

    double key_time = times[i];
    uint8_t key_blocked = blocked[i];

    size_t j = i;
    while (j > 0 && times[j - 1] > key_time) {
      times[j] = times[j - 1];
      blocked[j] = blocked[j - 1];
      --j;
    }

    times[j] = key_time;
    blocked[j] = key_blocked;
  }
}

// Helper: Create sorted index array based on time values
// Used as fallback when in-place sort is not suitable
inline std::vector<size_t> create_sort_indices(const std::vector<double>& times) {
  std::vector<size_t> indices(times.size());
  for (size_t i = 0; i < indices.size(); ++i) {
    indices[i] = i;
  }
  std::sort(indices.begin(), indices.end(),
            [&times](size_t a, size_t b) { return times[a] < times[b]; });
  return indices;
}

// Strategy 1: Offset-backed sorting with dedup index remapping
// Used when offset table exists - values don't need reordering
// But dedup indices need to be remapped to new sorted positions
inline void sort_with_offsets(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked,
    std::vector<uint64_t>& offsets) {

  // Verify all arrays have consistent sizes
  if (times.size() != offsets.size() || times.size() != blocked.size()) {
    // Sizes don't match - this shouldn't happen, but handle gracefully
    return;
  }

  // Verify all indices are within bounds
  for (size_t i = 0; i < indices.size(); ++i) {
    if (indices[i] >= times.size()) {
      // Invalid index - abort sorting
      return;
    }
  }

  std::vector<double> sorted_times(times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(blocked.size());
  std::vector<uint64_t> sorted_offsets(offsets.size());

  // Create index mapping: old_idx -> new_idx
  std::vector<size_t> index_map(times.size());
  for (size_t new_idx = 0; new_idx < indices.size(); ++new_idx) {
    index_map[indices[new_idx]] = new_idx;
  }

  // Copy and reorder data
  for (size_t i = 0; i < indices.size(); ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];

    uint64_t offset_val = offsets[indices[i]];

    // If this is a dedup offset, remap the index to new position
    if (offset_val & PODTimeSamples::OFFSET_DEDUP_FLAG) {
      // Extract old reference index
      size_t old_ref_idx = static_cast<size_t>(offset_val & PODTimeSamples::OFFSET_VALUE_MASK);

      // Bounds check before accessing index_map
      if (old_ref_idx < index_map.size()) {
        // Map to new index
        size_t new_ref_idx = index_map[old_ref_idx];

        // Reconstruct offset with new index, preserving flags
        offset_val = (offset_val & PODTimeSamples::OFFSET_FLAGS_MASK) | new_ref_idx;
      }
      // If out of bounds, keep the offset as-is (invalid but won't crash)
    }

    sorted_offsets[i] = offset_val;
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  offsets = std::move(sorted_offsets);
  // Note: values array doesn't need reordering as offsets handle the mapping
}

// Strategy 2: Legacy path with compact value storage
// Used for scalar types without offset table - values need reordering
inline void sort_with_compact_values(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked,
    Buffer<16>& values,
    size_t element_size) {

  std::vector<double> sorted_times(times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(blocked.size());
  Buffer<16> sorted_values;
  sorted_values.resize(values.size());

  // Pre-compute source offsets for each index (O(n) instead of O(n^2))
  std::vector<size_t> src_offsets(times.size());
  size_t cumulative_offset = 0;
  for (size_t i = 0; i < times.size(); ++i) {
    src_offsets[i] = cumulative_offset;
    if (!blocked[i]) {
      cumulative_offset += element_size;
    }
  }

  size_t dst_offset = 0;
  for (size_t i = 0; i < indices.size(); ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];

    DCOUT("sorted.times[" << i << "] = " << sorted_times[i]);
    DCOUT("sorted.blocked[" << i << "] = " << sorted_blocked[i]);

    // Only copy value if not blocked
    if (!blocked[indices[i]]) {
      // Use pre-computed source offset (O(1) lookup instead of O(n) scan)
      size_t src_offset = src_offsets[indices[i]];

      const uint8_t* src = values.data() + src_offset;
      uint8_t* dst = sorted_values.data() + dst_offset;
      std::memcpy(dst, src, element_size);  // memcpy is faster than std::copy for POD
      dst_offset += element_size;
    }
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  values = std::move(sorted_values);
}

// Strategy 3: Minimal sorting
// Used when no values need reordering - just times and blocked flags
inline void sort_minimal(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked) {

  std::vector<double> sorted_times(times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(blocked.size());

  for (size_t i = 0; i < indices.size(); ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
}

} // anonymous namespace

// PODTimeSamples::update() implementation
void PODTimeSamples::update() const {
  // Early exit for empty or already sorted data
  if (_times.empty()) {
    _dirty = false;
    return;
  }

  if (_dirty_start >= _times.size()) {
    _dirty = false;
    return;
  }

  // Fast path: check if already sorted to avoid unnecessary work
  if (std::is_sorted(_times.begin(), _times.end())) {
    _dirty = false;
    _dirty_start = SIZE_MAX;
    _dirty_end = 0;
    return;
  }

  // Check if data is nearly sorted (few inversions)
  // TimeSamples are typically already sorted from USD files
  const size_t n = _times.size();
  size_t inversions = count_inversions(_times);

  // Use insertion sort for nearly-sorted data (O(n) vs O(n log n))
  // Threshold: if inversions < 5% of elements, data is "nearly sorted"
  const bool use_insertion_sort = (inversions * 20 < n);

  // Debug output
  for (size_t i = 0; i < _times.size(); ++i) {
    DCOUT("times[" << i << "] = " << _times[i]);
  }

  // Dispatch to appropriate sorting strategy
  if (!_offsets.empty()) {
    // Strategy 1: Offset-backed storage
    if (use_insertion_sort) {
      // In-place insertion sort - O(n) for nearly sorted data
      insertion_sort_with_offsets(_times, _blocked, _offsets);
    } else {
      // Fall back to index-based sort for badly unsorted data
      std::vector<size_t> indices = create_sort_indices(_times);
      sort_with_offsets(indices, _times, _blocked, _offsets);
    }
  } else if (!_values.empty() && _type_id != 0) {
    // Strategy 2: Legacy compact storage
    size_t element_size = get_element_size();
    if (element_size > 0) {
      // Compact values with blocked entries is complex for in-place sort
      // Use index-based sort for correctness
      std::vector<size_t> indices = create_sort_indices(_times);
      sort_with_compact_values(indices, _times, _blocked, _values, element_size);
    } else {
      // Unknown element size - fall back to minimal sorting
      if (use_insertion_sort) {
        insertion_sort_minimal(_times, _blocked);
      } else {
        std::vector<size_t> indices = create_sort_indices(_times);
        sort_minimal(indices, _times, _blocked);
      }
    }
  } else {
    // Strategy 3: Minimal sorting (no values to reorder)
    if (use_insertion_sort) {
      insertion_sort_minimal(_times, _blocked);
    } else {
      std::vector<size_t> indices = create_sort_indices(_times);
      sort_minimal(indices, _times, _blocked);
    }
  }

  // Mark as clean
  _dirty = false;
  _dirty_start = SIZE_MAX;
  _dirty_end = 0;
}

// PODTimeSamples::reserve() implementation
void PODTimeSamples::reserve(size_t n) {
  _times.reserve(n);
  _blocked.reserve(n);
  if (_element_size > 0) {
    // Calculate total size based on whether it's array data or not
    size_t value_reserve_size = 0;
    if ((_is_stl_array || _is_typed_array) && _array_size > 0) {
      // For array data: sizeof(element) * n_samples * array_size
      value_reserve_size = n * _element_size * _array_size;
    } else {
      // For scalar data: sizeof(element) * n_samples
      // Note: This is an upper bound - blocked samples won't use space
      value_reserve_size = n * _element_size;
    }
    _values.reserve(value_reserve_size);
  }
  if (_is_stl_array || _is_typed_array || !_offsets.empty()) {
    _offsets.reserve(n);
  }
}

// PODTimeSamples::reserve_with_type() implementation
void PODTimeSamples::reserve_with_type(size_t expected_samples) {
  if (expected_samples == 0) return;

  _times.reserve(expected_samples);
  _blocked.reserve(expected_samples);

  // Calculate element size if not cached
  if (_element_size == 0 && _type_id != 0) {
    _element_size = static_cast<uint16_t>(get_element_size());
  }

  if (_element_size > 0) {
    size_t value_bytes = 0;
    if ((_is_stl_array || _is_typed_array) && _array_size > 0) {
      // Array data: sizeof(T) * expected_samples * array_size
      value_bytes = expected_samples * _element_size * _array_size;
    } else {
      // Scalar data: sizeof(T) * expected_samples (upper bound)
      value_bytes = expected_samples * _element_size;
    }

    _values.reserve(value_bytes);
  }

  // Always reserve offsets for array data
  if (_is_stl_array || _is_typed_array || !_offsets.empty()) {
    _offsets.reserve(expected_samples);
  }
}

namespace value {

// Insertion sort for Sample array (nearly-sorted optimization)
inline void insertion_sort_samples(std::vector<TimeSamples::Sample>& samples) {
  const size_t n = samples.size();
  if (n < 2) return;

  for (size_t i = 1; i < n; ++i) {
    if (samples[i].t >= samples[i - 1].t) {
      continue;  // Already in order - fast path
    }

    TimeSamples::Sample key = std::move(samples[i]);
    size_t j = i;

    while (j > 0 && samples[j - 1].t > key.t) {
      samples[j] = std::move(samples[j - 1]);
      --j;
    }

    samples[j] = std::move(key);
  }
}

// TimeSamples::update() implementation
void TimeSamples::update() const {
  if (_use_pod) {
    // Phase 3: Sort unified POD storage
    if (_times.empty()) {
      _dirty = false;
      return;
    }

    // Fast path: check if already sorted to avoid unnecessary work
    if (std::is_sorted(_times.begin(), _times.end())) {
      _dirty = false;
      return;
    }

    // Check if nearly sorted to choose optimal algorithm
    const size_t n = _times.size();
    size_t inversions = count_inversions(_times);
    const bool use_insertion_sort = (inversions * 20 < n);

    // Sort using offset table strategy
    if (!_offsets.empty()) {
      if (use_insertion_sort) {
        insertion_sort_with_offsets(_times, _blocked, _offsets);
      } else {
        std::vector<size_t> indices = create_sort_indices(_times);
        sort_with_offsets(indices, _times, _blocked, _offsets);
      }
    } else {
      // Shouldn't happen in Phase 3, but handle for safety
      std::vector<std::pair<size_t, double>> temp;
      temp.reserve(_times.size());
      for (size_t i = 0; i < _times.size(); ++i) {
        temp.emplace_back(i, _times[i]);
      }
      std::stable_sort(temp.begin(), temp.end(),
                      [](const auto& a, const auto& b) { return a.second < b.second; });
      for (size_t i = 0; i < temp.size(); ++i) {
        _times[i] = temp[i].second;
      }
    }
  } else {
    // Legacy Sample-based storage
    if (_samples.size() < 2) {
      _dirty = false;
      return;
    }

    // Fast path: check if already sorted
    if (std::is_sorted(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; })) {
      _dirty = false;
      return;
    }

    // Check if nearly sorted
    size_t inversions = 0;
    size_t scan_limit = std::min(_samples.size() - 1, size_t(100));
    for (size_t i = 0; i < scan_limit; ++i) {
      if (_samples[i].t > _samples[i + 1].t) {
        ++inversions;
      }
    }

    // Use insertion sort for nearly-sorted data
    if (inversions * 20 < _samples.size()) {
      insertion_sort_samples(_samples);
    } else {
      std::sort(_samples.begin(), _samples.end(),
                [](const Sample &a, const Sample &b) { return a.t < b.t; });
    }
  }
  _dirty = false;
}

} // namespace value

// Convert PODTimeSamples to vector of pairs for backward compatibility
std::vector<std::pair<double, std::pair<value::Value, bool>>> PODTimeSamples::get_samples_converted() const {
  std::vector<std::pair<double, std::pair<value::Value, bool>>> samples;

  if (_times.empty()) {
    return samples;
  }

  // Ensure data is sorted
  if (_dirty) {
    update();
  }

  samples.reserve(_times.size());

  // Get element size for the type
  size_t element_size = 0;
  if (!_is_stl_array && !_is_typed_array) {
    element_size = get_element_size();
  }

  // Macro to handle each POD type
#define HANDLE_POD_TYPE(__type_id, __type)                                    \
  if (_type_id == __type_id) {                                                \
    if (_is_stl_array || _is_typed_array) {                                   \
      /* Array handling with offset table */                                  \
      element_size = sizeof(__type) * _array_size;                            \
      if (!_offsets.empty()) {                                                \
        /* Use offset table for arrays */                                     \
        for (size_t i = 0; i < _times.size(); ++i) {                          \
          double time_val = _times[i];                                        \
          bool blocked = _blocked[i];                                         \
          value::Value val;                                                    \
          if (!blocked && _offsets[i] != SIZE_MAX) {                          \
            /* Resolve offset (follows dedup chain if needed) */              \
            size_t byte_offset = 0;                                           \
            bool is_array_flag = false;                                       \
            if (resolve_offset(i, &byte_offset, &is_array_flag)) {            \
              /* Get the actual array count for THIS sample (not _array_size which is just a cache) */ \
              size_t array_count = (i < _array_counts.size()) ? _array_counts[i] : _array_size; \
              DCOUT("PODTimeSamples::get_samples_converted: sample " << i << ", resolved offset=" << byte_offset << ", is_array=" << is_array_flag << ", array_count=" << array_count); \
              std::vector<__type> array_values;                               \
              array_values.resize(array_count);                               \
              /* Direct memcpy for non-bool arrays (bool handled separately) */ \
              if (array_count > 0 && byte_offset + sizeof(__type) * array_count <= _values.size()) { \
                std::memcpy(&array_values[0], _values.data() + byte_offset,   \
                            sizeof(__type) * array_count);                     \
              } else {                                                         \
                DCOUT("PODTimeSamples: ERROR - invalid offset or size, byte_offset=" << byte_offset << ", array_count=" << array_count << ", _values.size=" << _values.size()); \
              }                                                                \
              val = value::Value(array_values);                               \
            } else {                                                          \
              /* Failed to resolve offset - treat as blocked */               \
              val = value::Value(value::ValueBlock());                        \
            }                                                                  \
          } else {                                                            \
            val = value::Value(value::ValueBlock());                          \
          }                                                                    \
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked))); \
        }                                                                      \
      } else {                                                                 \
        /* Legacy path without offsets - should not happen for arrays */      \
        /* But handle it for completeness */                                  \
        size_t value_offset = 0;                                              \
        for (size_t i = 0; i < _times.size(); ++i) {                          \
          double time_val = _times[i];                                        \
          bool blocked = _blocked[i];                                         \
          value::Value val;                                                    \
          if (!blocked) {                                                      \
            /* Get the actual array count for THIS sample */ \
            size_t array_count = (i < _array_counts.size()) ? _array_counts[i] : _array_size; \
            std::vector<__type> array_values;                                 \
            array_values.resize(array_count);                                 \
            /* Direct memcpy for non-bool arrays (bool handled separately) */ \
            std::memcpy(&array_values[0], _values.data() + value_offset,      \
                        sizeof(__type) * array_count);                                                                  \
            val = value::Value(array_values);                                 \
            value_offset += sizeof(__type) * array_count;                     \
          } else {                                                            \
            val = value::Value(value::ValueBlock());                          \
          }                                                                    \
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked))); \
        }                                                                      \
      }                                                                        \
    } else {                                                                   \
      /* Scalar handling */                                                   \
      element_size = sizeof(__type);                                          \
      if (!_offsets.empty()) {                                                \
        /* Use offset table */                                                \
        for (size_t i = 0; i < _times.size(); ++i) {                          \
          double time_val = _times[i];                                        \
          bool blocked = _blocked[i];                                         \
          value::Value val;                                                    \
          if (!blocked && _offsets[i] != SIZE_MAX) {                          \
            /* Resolve offset (follows dedup chain if needed) */              \
            size_t byte_offset = 0;                                           \
            if (resolve_offset(i, &byte_offset)) {                            \
              __type typed_value;                                             \
              std::memcpy(&typed_value, _values.data() + byte_offset,         \
                          sizeof(__type));                                     \
              val = value::Value(typed_value);                                \
            } else {                                                          \
              /* Failed to resolve offset - treat as blocked */               \
              val = value::Value(value::ValueBlock());                        \
            }                                                                  \
          } else {                                                            \
            val = value::Value(value::ValueBlock());                          \
          }                                                                    \
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked))); \
        }                                                                      \
      } else {                                                                 \
        /* Legacy path without offsets - compact storage */                   \
        size_t value_offset = 0;                                              \
        for (size_t i = 0; i < _times.size(); ++i) {                          \
          double time_val = _times[i];                                        \
          bool blocked = _blocked[i];                                         \
          value::Value val;                                                    \
          if (!blocked) {                                                      \
            __type typed_value;                                               \
            std::memcpy(&typed_value, _values.data() + value_offset,          \
                        sizeof(__type));                                       \
            val = value::Value(typed_value);                                  \
            value_offset += element_size;                                     \
          } else {                                                            \
            val = value::Value(value::ValueBlock());                          \
          }                                                                    \
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked))); \
        }                                                                      \
      }                                                                        \
    }                                                                          \
  } else

  // Handle bool separately due to std::vector<bool> specialization
  if (_type_id == value::TypeTraits<bool>::underlying_type_id()) {
    if (_is_stl_array || _is_typed_array) {
      /* Bool array handling - special case due to vector<bool> */
      element_size = _array_size;  // 1 byte per bool
      if (!_offsets.empty()) {
        for (size_t i = 0; i < _times.size(); ++i) {
          double time_val = _times[i];
          bool blocked = _blocked[i];
          value::Value val;
          if (!blocked && _offsets[i] != SIZE_MAX) {
            /* Resolve offset (follows dedup chain if needed) */
            size_t byte_offset = 0;
            if (resolve_offset(i, &byte_offset)) {
              std::vector<bool> bool_values;
              bool_values.reserve(_array_size);
              const uint8_t* src = _values.data() + byte_offset;
              for (size_t j = 0; j < _array_size; ++j) {
                bool_values.push_back(static_cast<bool>(src[j]));
              }
              val = value::Value(bool_values);
            } else {
              /* Failed to resolve offset - treat as blocked */
              val = value::Value(value::ValueBlock());
            }
          } else {
            val = value::Value(value::ValueBlock());
          }
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked)));
        }
      } else {
        /* Legacy path without offsets */
        size_t value_offset = 0;
        for (size_t i = 0; i < _times.size(); ++i) {
          double time_val = _times[i];
          bool blocked = _blocked[i];
          value::Value val;
          if (!blocked) {
            std::vector<bool> bool_values;
            bool_values.reserve(_array_size);
            const uint8_t* src = _values.data() + value_offset;
            for (size_t j = 0; j < _array_size; ++j) {
              bool_values.push_back(static_cast<bool>(src[j]));
            }
            val = value::Value(bool_values);
            value_offset += element_size;
          } else {
            val = value::Value(value::ValueBlock());
          }
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked)));
        }
      }
    } else {
      /* Scalar bool handling */
      element_size = sizeof(bool);
      if (!_offsets.empty()) {
        for (size_t i = 0; i < _times.size(); ++i) {
          double time_val = _times[i];
          bool blocked = _blocked[i];
          value::Value val;
          if (!blocked && _offsets[i] != SIZE_MAX) {
            /* Resolve offset (follows dedup chain if needed) */
            size_t byte_offset = 0;
            if (resolve_offset(i, &byte_offset)) {
              bool typed_value;
              std::memcpy(&typed_value, _values.data() + byte_offset, sizeof(bool));
              val = value::Value(typed_value);
            } else {
              /* Failed to resolve offset - treat as blocked */
              val = value::Value(value::ValueBlock());
            }
          } else {
            val = value::Value(value::ValueBlock());
          }
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked)));
        }
      } else {
        /* Legacy path without offsets - compact storage */
        size_t value_offset = 0;
        for (size_t i = 0; i < _times.size(); ++i) {
          double time_val = _times[i];
          bool blocked = _blocked[i];
          value::Value val;
          if (!blocked) {
            bool typed_value;
            std::memcpy(&typed_value, _values.data() + value_offset, sizeof(bool));
            val = value::Value(typed_value);
            value_offset += element_size;
          } else {
            val = value::Value(value::ValueBlock());
          }
          samples.push_back(std::make_pair(time_val, std::make_pair(std::move(val), blocked)));
        }
      }
    }
  } else
  // Use centralized type registry for all other POD types
#define HANDLE_POD_TYPE_WRAPPER(__type) \
  HANDLE_POD_TYPE(value::TypeTraits<__type>::type_id(), __type)

  TINYUSDZ_POD_TYPE_LIST(HANDLE_POD_TYPE_WRAPPER)

#undef HANDLE_POD_TYPE_WRAPPER
  {
    // Unknown type_id - this shouldn't happen for PODTimeSamples
    // Return empty vector
  }

#undef HANDLE_POD_TYPE

  return samples;
}

// Helper function to get element size for a given type_id
size_t PODTimeSamples::get_element_size() const {
  // Handle bool separately (special case due to std::vector<bool> specialization)
  if (_type_id == value::TypeTraits<bool>::underlying_type_id()) {
    return 1; // char
  }

  // Use centralized type registry for all other POD types
#define TYPE_SIZE_CASE(__type)                                                \
  if (_type_id == value::TypeTraits<__type>::type_id()) {                    \
    return sizeof(__type);                                                    \
  }

  TINYUSDZ_POD_TYPE_LIST(TYPE_SIZE_CASE)

#undef TYPE_SIZE_CASE

  return 0; // Unknown type
}

//
// Explicit template instantiations for commonly used types
// This reduces compilation time by instantiating templates only once
//

// Integer types (POD, non-lerp'able)
template struct TypedTimeSamples<bool>;
template struct TypedTimeSamples<int32_t>;
template struct TypedTimeSamples<uint32_t>;
template struct TypedTimeSamples<int64_t>;
template struct TypedTimeSamples<uint64_t>;

// Floating point scalar types (POD, lerp'able)
template struct TypedTimeSamples<value::half>;
template struct TypedTimeSamples<float>;
template struct TypedTimeSamples<double>;

// Vector types (POD, lerp'able)
template struct TypedTimeSamples<value::half2>;
template struct TypedTimeSamples<value::half3>;
template struct TypedTimeSamples<value::half4>;
template struct TypedTimeSamples<value::float2>;
template struct TypedTimeSamples<value::float3>;
template struct TypedTimeSamples<value::float4>;
template struct TypedTimeSamples<value::double2>;
template struct TypedTimeSamples<value::double3>;
template struct TypedTimeSamples<value::double4>;

// Integer vector types (POD, non-lerp'able)
template struct TypedTimeSamples<value::int2>;
template struct TypedTimeSamples<value::int3>;
template struct TypedTimeSamples<value::int4>;

// Quaternion types (POD, lerp'able)
template struct TypedTimeSamples<value::quath>;
template struct TypedTimeSamples<value::quatf>;
template struct TypedTimeSamples<value::quatd>;

// Matrix types (lerp'able)
template struct TypedTimeSamples<value::matrix2f>;
template struct TypedTimeSamples<value::matrix3f>;
template struct TypedTimeSamples<value::matrix4f>;
template struct TypedTimeSamples<value::matrix2d>;
template struct TypedTimeSamples<value::matrix3d>;
template struct TypedTimeSamples<value::matrix4d>;

// Role types (POD, lerp'able)
template struct TypedTimeSamples<value::normal3h>;
template struct TypedTimeSamples<value::normal3f>;
template struct TypedTimeSamples<value::normal3d>;
template struct TypedTimeSamples<value::vector3h>;
template struct TypedTimeSamples<value::vector3f>;
template struct TypedTimeSamples<value::vector3d>;
template struct TypedTimeSamples<value::point3h>;
template struct TypedTimeSamples<value::point3f>;
template struct TypedTimeSamples<value::point3d>;
template struct TypedTimeSamples<value::color3h>;
template struct TypedTimeSamples<value::color3f>;
template struct TypedTimeSamples<value::color3d>;
template struct TypedTimeSamples<value::color4h>;
template struct TypedTimeSamples<value::color4f>;
template struct TypedTimeSamples<value::color4d>;
template struct TypedTimeSamples<value::texcoord2h>;
template struct TypedTimeSamples<value::texcoord2f>;
template struct TypedTimeSamples<value::texcoord2d>;
template struct TypedTimeSamples<value::texcoord3h>;
template struct TypedTimeSamples<value::texcoord3f>;
template struct TypedTimeSamples<value::texcoord3d>;

// Other types
template struct TypedTimeSamples<value::timecode>;
template struct TypedTimeSamples<value::frame4d>;
template struct TypedTimeSamples<std::string>;
template struct TypedTimeSamples<value::token>;
template struct TypedTimeSamples<value::dict>;
template struct TypedTimeSamples<value::AssetPath>;

// Common array types
template struct TypedTimeSamples<std::vector<bool>>;
template struct TypedTimeSamples<std::vector<int32_t>>;
template struct TypedTimeSamples<std::vector<uint32_t>>;
template struct TypedTimeSamples<std::vector<int64_t>>;
template struct TypedTimeSamples<std::vector<uint64_t>>;
template struct TypedTimeSamples<std::vector<value::half>>;
template struct TypedTimeSamples<std::vector<float>>;
template struct TypedTimeSamples<std::vector<double>>;
template struct TypedTimeSamples<std::vector<value::half2>>;
template struct TypedTimeSamples<std::vector<value::half3>>;
template struct TypedTimeSamples<std::vector<value::half4>>;
template struct TypedTimeSamples<std::vector<value::float2>>;
template struct TypedTimeSamples<std::vector<value::float3>>;
template struct TypedTimeSamples<std::vector<value::float4>>;
template struct TypedTimeSamples<std::vector<value::double2>>;
template struct TypedTimeSamples<std::vector<value::double3>>;
template struct TypedTimeSamples<std::vector<value::double4>>;
template struct TypedTimeSamples<std::vector<value::int2>>;
template struct TypedTimeSamples<std::vector<value::int3>>;
template struct TypedTimeSamples<std::vector<value::int4>>;
template struct TypedTimeSamples<std::vector<value::quath>>;
template struct TypedTimeSamples<std::vector<value::quatf>>;
template struct TypedTimeSamples<std::vector<value::quatd>>;
// Role types vectors (needed by usdGeom.cc and usdSkel.cc)
template struct TypedTimeSamples<std::vector<value::point3h>>;
template struct TypedTimeSamples<std::vector<value::point3f>>;
template struct TypedTimeSamples<std::vector<value::point3d>>;
template struct TypedTimeSamples<std::vector<value::normal3h>>;
template struct TypedTimeSamples<std::vector<value::normal3f>>;
template struct TypedTimeSamples<std::vector<value::normal3d>>;
template struct TypedTimeSamples<std::vector<value::vector3h>>;
template struct TypedTimeSamples<std::vector<value::vector3f>>;
template struct TypedTimeSamples<std::vector<value::vector3d>>;
template struct TypedTimeSamples<std::vector<value::color3h>>;
template struct TypedTimeSamples<std::vector<value::color3f>>;
template struct TypedTimeSamples<std::vector<value::color3d>>;
template struct TypedTimeSamples<std::vector<value::color4h>>;
template struct TypedTimeSamples<std::vector<value::color4f>>;
template struct TypedTimeSamples<std::vector<value::color4d>>;
template struct TypedTimeSamples<std::vector<value::texcoord2h>>;
template struct TypedTimeSamples<std::vector<value::texcoord2f>>;
template struct TypedTimeSamples<std::vector<value::texcoord2d>>;
template struct TypedTimeSamples<std::vector<value::texcoord3h>>;
template struct TypedTimeSamples<std::vector<value::texcoord3f>>;
template struct TypedTimeSamples<std::vector<value::texcoord3d>>;
// Matrix types vectors
template struct TypedTimeSamples<std::vector<value::matrix2f>>;
template struct TypedTimeSamples<std::vector<value::matrix3f>>;
template struct TypedTimeSamples<std::vector<value::matrix4f>>;
template struct TypedTimeSamples<std::vector<value::matrix2d>>;
template struct TypedTimeSamples<std::vector<value::matrix3d>>;
template struct TypedTimeSamples<std::vector<value::matrix4d>>;
template struct TypedTimeSamples<std::vector<std::string>>;
template struct TypedTimeSamples<std::vector<value::token>>;
template struct TypedTimeSamples<std::vector<value::AssetPath>>;
template struct TypedTimeSamples<std::vector<value::frame4d>>;
// Special types used by tydra
template struct TypedTimeSamples<std::vector<value::StringData>>;
// Additional vector array types
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>;
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>;
template struct TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>;
// Note: UsdUVTexture::SourceColorSpace enum requires special handling with any_cast
// and is excluded from explicit instantiation for now

//
// PODTimeSamples template method implementations
//

template<typename T>
bool PODTimeSamples::add_sample(double t, const T& value, std::string *err,
                                size_t expected_total_samples) {
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

// ============================================================================
// Explicit Template Instantiations for PODTimeSamples::add_sample
// ============================================================================
// Note: add_sample requires trivial types, so frame4d is excluded

// bool handled separately (special case)
template bool PODTimeSamples::add_sample<bool>(double, const bool&, std::string*, size_t);

// Generate instantiations for POD types using centralized registry
// Note: frame4d is excluded as it's not trivial (doesn't satisfy std::is_trivial)
#define INSTANTIATE_ADD_SAMPLE(__type) \
  template bool PODTimeSamples::add_sample<__type>(double, const __type&, std::string*, size_t);

// Manually list types to exclude frame4d
INSTANTIATE_ADD_SAMPLE(int32_t)
INSTANTIATE_ADD_SAMPLE(uint32_t)
INSTANTIATE_ADD_SAMPLE(int64_t)
INSTANTIATE_ADD_SAMPLE(uint64_t)
INSTANTIATE_ADD_SAMPLE(value::half)
INSTANTIATE_ADD_SAMPLE(value::half2)
INSTANTIATE_ADD_SAMPLE(value::half3)
INSTANTIATE_ADD_SAMPLE(value::half4)
INSTANTIATE_ADD_SAMPLE(float)
INSTANTIATE_ADD_SAMPLE(value::float2)
INSTANTIATE_ADD_SAMPLE(value::float3)
INSTANTIATE_ADD_SAMPLE(value::float4)
INSTANTIATE_ADD_SAMPLE(double)
INSTANTIATE_ADD_SAMPLE(value::double2)
INSTANTIATE_ADD_SAMPLE(value::double3)
INSTANTIATE_ADD_SAMPLE(value::double4)
INSTANTIATE_ADD_SAMPLE(value::int2)
INSTANTIATE_ADD_SAMPLE(value::int3)
INSTANTIATE_ADD_SAMPLE(value::int4)
INSTANTIATE_ADD_SAMPLE(value::quath)
INSTANTIATE_ADD_SAMPLE(value::quatf)
INSTANTIATE_ADD_SAMPLE(value::quatd)
INSTANTIATE_ADD_SAMPLE(value::color3f)
INSTANTIATE_ADD_SAMPLE(value::color3h)
INSTANTIATE_ADD_SAMPLE(value::color3d)
INSTANTIATE_ADD_SAMPLE(value::color4f)
INSTANTIATE_ADD_SAMPLE(value::color4h)
INSTANTIATE_ADD_SAMPLE(value::color4d)
INSTANTIATE_ADD_SAMPLE(value::vector3f)
INSTANTIATE_ADD_SAMPLE(value::vector3h)
INSTANTIATE_ADD_SAMPLE(value::vector3d)
INSTANTIATE_ADD_SAMPLE(value::normal3f)
INSTANTIATE_ADD_SAMPLE(value::normal3h)
INSTANTIATE_ADD_SAMPLE(value::normal3d)
INSTANTIATE_ADD_SAMPLE(value::point3f)
INSTANTIATE_ADD_SAMPLE(value::point3h)
INSTANTIATE_ADD_SAMPLE(value::point3d)
INSTANTIATE_ADD_SAMPLE(value::texcoord2f)
INSTANTIATE_ADD_SAMPLE(value::texcoord2h)
INSTANTIATE_ADD_SAMPLE(value::texcoord2d)
INSTANTIATE_ADD_SAMPLE(value::texcoord3f)
INSTANTIATE_ADD_SAMPLE(value::texcoord3h)
INSTANTIATE_ADD_SAMPLE(value::texcoord3d)
// Matrix types - now trivial with default constructors
INSTANTIATE_ADD_SAMPLE(value::matrix2f)
INSTANTIATE_ADD_SAMPLE(value::matrix3f)
INSTANTIATE_ADD_SAMPLE(value::matrix4f)
INSTANTIATE_ADD_SAMPLE(value::matrix2d)
INSTANTIATE_ADD_SAMPLE(value::matrix3d)
INSTANTIATE_ADD_SAMPLE(value::matrix4d)
// frame4d excluded - not trivial

#undef INSTANTIATE_ADD_SAMPLE

// Additional types not in the POD list (timecode is a special case)
template bool PODTimeSamples::add_sample<value::timecode>(double, const value::timecode&, std::string*, size_t);

// PODTimeSamples::add_typed_array_sample implementation
template<typename T>
bool PODTimeSamples::add_typed_array_sample(double t, const TypedArray<T>& typed_array, std::string *err,
                                           size_t expected_total_samples) {
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
    DCOUT("offset = " << _offsets.back());
    DCOUT("packed_value = 0x" << std::hex << packed_value << std::dec);
    DCOUT("Writing to address: 0x" << std::hex << reinterpret_cast<uintptr_t>(_values.data() + _offsets.back()) << std::dec);
    std::memcpy(_values.data() + _offsets.back(), &packed_value, sizeof(uint64_t));

    // Verify what was written
    uint64_t verify_read;
    std::memcpy(&verify_read, _values.data() + _offsets.back(), sizeof(uint64_t));
    DCOUT("Verified written value: 0x" << std::hex << verify_read << std::dec);
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

// ============================================================================
// Explicit Template Instantiations for PODTimeSamples::add_typed_array_sample
// ============================================================================
// TypedArray can be used with types from the POD list plus matrix types

// Generate instantiations for POD types
#define INSTANTIATE_ADD_TYPED_ARRAY(__type) \
  template bool PODTimeSamples::add_typed_array_sample<__type>(double, const TypedArray<__type>&, std::string*, size_t);

TINYUSDZ_POD_TYPE_LIST(INSTANTIATE_ADD_TYPED_ARRAY)

#undef INSTANTIATE_ADD_TYPED_ARRAY

#if 0 // now in POD_LIST
// Matrix types (not in POD list as they're not trivial, but used with TypedArray)
template bool PODTimeSamples::add_typed_array_sample<value::matrix2f>(double, const TypedArray<value::matrix2f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix3f>(double, const TypedArray<value::matrix3f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix4f>(double, const TypedArray<value::matrix4f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix2d>(double, const TypedArray<value::matrix2d>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix3d>(double, const TypedArray<value::matrix3d>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix4d>(double, const TypedArray<value::matrix4d>&, std::string*, size_t);
#endif

//
// TypedTimeSamples::get() implementations
//

// Get value for non-interpolatable types
template<typename T>
template<typename V, std::enable_if_t<!value::LerpTraits<V>::supported(), std::nullptr_t>>
bool TypedTimeSamples<T>::get(T *dst, double t,
                              value::TimeSampleInterpolationType interp) const {
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

// Get value for interpolatable types
template<typename T>
template<typename V, std::enable_if_t<value::LerpTraits<V>::supported(), std::nullptr_t>>
bool TypedTimeSamples<T>::get(T *dst, double t,
                              value::TimeSampleInterpolationType interp) const {
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

//
// Explicit template instantiations for TypedTimeSamples::get()
//

// For non-interpolatable integer types
template bool TypedTimeSamples<bool>::get<bool>(bool*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<int32_t>::get<int32_t>(int32_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<uint32_t>::get<uint32_t>(uint32_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<int64_t>::get<int64_t>(int64_t*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<uint64_t>::get<uint64_t>(uint64_t*, double, value::TimeSampleInterpolationType) const;

// For interpolatable floating-point types
template bool TypedTimeSamples<value::half>::get<value::half>(value::half*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<float>::get<float>(float*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<double>::get<double>(double*, double, value::TimeSampleInterpolationType) const;

// For interpolatable vector types - using std::array forms
template bool TypedTimeSamples<std::array<value::half, 2>>::get<std::array<value::half, 2>>(std::array<value::half, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<value::half, 3>>::get<std::array<value::half, 3>>(std::array<value::half, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<value::half, 4>>::get<std::array<value::half, 4>>(std::array<value::half, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 2>>::get<std::array<float, 2>>(std::array<float, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 3>>::get<std::array<float, 3>>(std::array<float, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<float, 4>>::get<std::array<float, 4>>(std::array<float, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 2>>::get<std::array<double, 2>>(std::array<double, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 3>>::get<std::array<double, 3>>(std::array<double, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<double, 4>>::get<std::array<double, 4>>(std::array<double, 4>*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable integer vector types
template bool TypedTimeSamples<std::array<int, 2>>::get<std::array<int, 2>>(std::array<int, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<int, 3>>::get<std::array<int, 3>>(std::array<int, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<int, 4>>::get<std::array<int, 4>>(std::array<int, 4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 2>>::get<std::array<uint32_t, 2>>(std::array<uint32_t, 2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 3>>::get<std::array<uint32_t, 3>>(std::array<uint32_t, 3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::array<uint32_t, 4>>::get<std::array<uint32_t, 4>>(std::array<uint32_t, 4>*, double, value::TimeSampleInterpolationType) const;

// For interpolatable quaternion types
template bool TypedTimeSamples<value::quath>::get<value::quath>(value::quath*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::quatf>::get<value::quatf>(value::quatf*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::quatd>::get<value::quatd>(value::quatd*, double, value::TimeSampleInterpolationType) const;

// For interpolatable matrix types
template bool TypedTimeSamples<value::matrix2f>::get<value::matrix2f>(value::matrix2f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix3f>::get<value::matrix3f>(value::matrix3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix4f>::get<value::matrix4f>(value::matrix4f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix2d>::get<value::matrix2d>(value::matrix2d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix3d>::get<value::matrix3d>(value::matrix3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::matrix4d>::get<value::matrix4d>(value::matrix4d*, double, value::TimeSampleInterpolationType) const;

// For interpolatable role types
template bool TypedTimeSamples<value::normal3h>::get<value::normal3h>(value::normal3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::normal3f>::get<value::normal3f>(value::normal3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::normal3d>::get<value::normal3d>(value::normal3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3h>::get<value::vector3h>(value::vector3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3f>::get<value::vector3f>(value::vector3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::vector3d>::get<value::vector3d>(value::vector3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3h>::get<value::point3h>(value::point3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3f>::get<value::point3f>(value::point3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::point3d>::get<value::point3d>(value::point3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3h>::get<value::color3h>(value::color3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3f>::get<value::color3f>(value::color3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color3d>::get<value::color3d>(value::color3d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4h>::get<value::color4h>(value::color4h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4f>::get<value::color4f>(value::color4f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::color4d>::get<value::color4d>(value::color4d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2h>::get<value::texcoord2h>(value::texcoord2h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2f>::get<value::texcoord2f>(value::texcoord2f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord2d>::get<value::texcoord2d>(value::texcoord2d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3h>::get<value::texcoord3h>(value::texcoord3h*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3f>::get<value::texcoord3f>(value::texcoord3f*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::texcoord3d>::get<value::texcoord3d>(value::texcoord3d*, double, value::TimeSampleInterpolationType) const;

// For non-interpolatable other types
template bool TypedTimeSamples<value::timecode>::get<value::timecode>(value::timecode*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::frame4d>::get<value::frame4d>(value::frame4d*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::string>::get<std::string>(std::string*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::token>::get<value::token>(value::token*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::dict>::get<value::dict>(value::dict*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<value::AssetPath>::get<value::AssetPath>(value::AssetPath*, double, value::TimeSampleInterpolationType) const;

// For vector container types (non-interpolatable)
template bool TypedTimeSamples<std::vector<bool>>::get<std::vector<bool>>(std::vector<bool>*, double, value::TimeSampleInterpolationType) const;
// Note: int and int32_t are often the same type, causing duplicate instantiation errors
// We only instantiate int32_t here since that's what's commonly used
template bool TypedTimeSamples<std::vector<int32_t>>::get<std::vector<int32_t>>(std::vector<int32_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<uint32_t>>::get<std::vector<uint32_t>>(std::vector<uint32_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<int64_t>>::get<std::vector<int64_t>>(std::vector<int64_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<uint64_t>>::get<std::vector<uint64_t>>(std::vector<uint64_t>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half>>::get<std::vector<value::half>>(std::vector<value::half>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<float>>::get<std::vector<float>>(std::vector<float>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<double>>::get<std::vector<double>>(std::vector<double>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half2>>::get<std::vector<value::half2>>(std::vector<value::half2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half3>>::get<std::vector<value::half3>>(std::vector<value::half3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::half4>>::get<std::vector<value::half4>>(std::vector<value::half4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float2>>::get<std::vector<value::float2>>(std::vector<value::float2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float3>>::get<std::vector<value::float3>>(std::vector<value::float3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::float4>>::get<std::vector<value::float4>>(std::vector<value::float4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double2>>::get<std::vector<value::double2>>(std::vector<value::double2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double3>>::get<std::vector<value::double3>>(std::vector<value::double3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::double4>>::get<std::vector<value::double4>>(std::vector<value::double4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int2>>::get<std::vector<value::int2>>(std::vector<value::int2>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int3>>::get<std::vector<value::int3>>(std::vector<value::int3>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::int4>>::get<std::vector<value::int4>>(std::vector<value::int4>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quath>>::get<std::vector<value::quath>>(std::vector<value::quath>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quatf>>::get<std::vector<value::quatf>>(std::vector<value::quatf>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::quatd>>::get<std::vector<value::quatd>>(std::vector<value::quatd>*, double, value::TimeSampleInterpolationType) const;
// Role types vectors (needed by usdGeom.cc and usdSkel.cc)
template bool TypedTimeSamples<std::vector<value::point3h>>::get<std::vector<value::point3h>>(std::vector<value::point3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::point3f>>::get<std::vector<value::point3f>>(std::vector<value::point3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::point3d>>::get<std::vector<value::point3d>>(std::vector<value::point3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3h>>::get<std::vector<value::normal3h>>(std::vector<value::normal3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3f>>::get<std::vector<value::normal3f>>(std::vector<value::normal3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::normal3d>>::get<std::vector<value::normal3d>>(std::vector<value::normal3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3h>>::get<std::vector<value::vector3h>>(std::vector<value::vector3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3f>>::get<std::vector<value::vector3f>>(std::vector<value::vector3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::vector3d>>::get<std::vector<value::vector3d>>(std::vector<value::vector3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3h>>::get<std::vector<value::color3h>>(std::vector<value::color3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3f>>::get<std::vector<value::color3f>>(std::vector<value::color3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color3d>>::get<std::vector<value::color3d>>(std::vector<value::color3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4h>>::get<std::vector<value::color4h>>(std::vector<value::color4h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4f>>::get<std::vector<value::color4f>>(std::vector<value::color4f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::color4d>>::get<std::vector<value::color4d>>(std::vector<value::color4d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2h>>::get<std::vector<value::texcoord2h>>(std::vector<value::texcoord2h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2f>>::get<std::vector<value::texcoord2f>>(std::vector<value::texcoord2f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord2d>>::get<std::vector<value::texcoord2d>>(std::vector<value::texcoord2d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3h>>::get<std::vector<value::texcoord3h>>(std::vector<value::texcoord3h>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3f>>::get<std::vector<value::texcoord3f>>(std::vector<value::texcoord3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::texcoord3d>>::get<std::vector<value::texcoord3d>>(std::vector<value::texcoord3d>*, double, value::TimeSampleInterpolationType) const;
// Matrix types vectors
template bool TypedTimeSamples<std::vector<value::matrix2f>>::get<std::vector<value::matrix2f>>(std::vector<value::matrix2f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix3f>>::get<std::vector<value::matrix3f>>(std::vector<value::matrix3f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix4f>>::get<std::vector<value::matrix4f>>(std::vector<value::matrix4f>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix2d>>::get<std::vector<value::matrix2d>>(std::vector<value::matrix2d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix3d>>::get<std::vector<value::matrix3d>>(std::vector<value::matrix3d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::matrix4d>>::get<std::vector<value::matrix4d>>(std::vector<value::matrix4d>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<std::string>>::get<std::vector<std::string>>(std::vector<std::string>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::token>>::get<std::vector<value::token>>(std::vector<value::token>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::AssetPath>>::get<std::vector<value::AssetPath>>(std::vector<value::AssetPath>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<value::frame4d>>::get<std::vector<value::frame4d>>(std::vector<value::frame4d>*, double, value::TimeSampleInterpolationType) const;
// Special types used by tydra
template bool TypedTimeSamples<std::vector<value::StringData>>::get<std::vector<value::StringData>>(std::vector<value::StringData>*, double, value::TimeSampleInterpolationType) const;
// Additional vector array types
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 2>>>::get<std::vector<std::array<unsigned int, 2>>>(std::vector<std::array<unsigned int, 2>>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 3>>>::get<std::vector<std::array<unsigned int, 3>>>(std::vector<std::array<unsigned int, 3>>*, double, value::TimeSampleInterpolationType) const;
template bool TypedTimeSamples<std::vector<std::array<unsigned int, 4>>>::get<std::vector<std::array<unsigned int, 4>>>(std::vector<std::array<unsigned int, 4>>*, double, value::TimeSampleInterpolationType) const;
// Note: UsdUVTexture::SourceColorSpace enum requires special handling with any_cast
// and is excluded from explicit instantiation for now

// ============================================================================
// TimeSamples Implementation
// ============================================================================

namespace value {

// Move constructor
TimeSamples::TimeSamples(TimeSamples&& other) noexcept
    : _samples(std::move(other._samples)),
      _times(std::move(other._times)),
      _blocked(std::move(other._blocked)),
      _small_values(std::move(other._small_values)),
      _values(std::move(other._values)),
      _offsets(std::move(other._offsets)),
      _type_id(other._type_id),
      _use_pod(other._use_pod),
      _is_array(other._is_array),
      _array_size(other._array_size),
      _element_size(other._element_size),
      _blocked_count(other._blocked_count),
      _dirty(other._dirty),
      _dirty_start(other._dirty_start),
      _dirty_end(other._dirty_end),
      _pod_samples(std::move(other._pod_samples)) {
  // Reset moved-from object to valid empty state
  other._type_id = 0;
  other._use_pod = false;
  other._is_array = false;
  other._array_size = 0;
  other._element_size = 0;
  other._blocked_count = 0;
  other._dirty = false;
  other._dirty_start = 0;
  other._dirty_end = 0;
}

// Move assignment operator
TimeSamples& TimeSamples::operator=(TimeSamples&& other) noexcept {
  if (this != &other) {
    // Move data from other
    _samples = std::move(other._samples);
    _times = std::move(other._times);
    _blocked = std::move(other._blocked);
    _values = std::move(other._values);
    _array_values = std::move(other._array_values);  // Move unique_ptr vector
    _offsets = std::move(other._offsets);
    _pod_samples = std::move(other._pod_samples);
    _small_values = std::move(other._small_values);
    _type_id = other._type_id;
    _use_pod = other._use_pod;
    _is_array = other._is_array;
    _array_size = other._array_size;
    _element_size = other._element_size;
    _blocked_count = other._blocked_count;
    _dirty = other._dirty;
    _dirty_start = other._dirty_start;
    _dirty_end = other._dirty_end;

    // Reset moved-from object to valid empty state
    other._type_id = 0;
    other._use_pod = false;
    other._is_array = false;
    other._array_size = 0;
    other._element_size = 0;
    other._blocked_count = 0;
    other._dirty = false;
    other._dirty_start = 0;
    other._dirty_end = 0;
  }
  return *this;
}

// Copy constructor - implements deep copy for _array_values
TimeSamples::TimeSamples(const TimeSamples& other)
    : _samples(other._samples),
      _times(other._times),
      _blocked(other._blocked),
      _small_values(other._small_values),
      _values(other._values),
      _offsets(other._offsets),
      _type_id(other._type_id),
      _use_pod(other._use_pod),
      _is_array(other._is_array),
      _array_size(other._array_size),
      _element_size(other._element_size),
      _blocked_count(other._blocked_count),
      _dirty(other._dirty),
      _dirty_start(other._dirty_start),
      _dirty_end(other._dirty_end),
      _pod_samples(other._pod_samples) {
  // Deep copy _array_values (vector of unique_ptr)
  _array_values.clear();
  _array_values.reserve(other._array_values.size());
  for (const auto& array_buffer : other._array_values) {
    if (array_buffer) {
      // Create a new Buffer<16> and copy data
      auto new_buffer = std::make_unique<Buffer<16>>(*array_buffer);
      _array_values.push_back(std::move(new_buffer));
    } else {
      _array_values.push_back(nullptr);
    }
  }
}

// Copy assignment operator - implements deep copy for _array_values
TimeSamples& TimeSamples::operator=(const TimeSamples& other) {
  if (this != &other) {
    _samples = other._samples;
    _times = other._times;
    _blocked = other._blocked;
    _values = other._values;
    _offsets = other._offsets;
    _type_id = other._type_id;
    _use_pod = other._use_pod;
    _is_array = other._is_array;
    _array_size = other._array_size;
    _element_size = other._element_size;
    _blocked_count = other._blocked_count;
    _dirty = other._dirty;
    _dirty_start = other._dirty_start;
    _dirty_end = other._dirty_end;
    _pod_samples = other._pod_samples;
    _small_values = other._small_values;

    // Deep copy _array_values (vector of unique_ptr)
    _array_values.clear();
    _array_values.reserve(other._array_values.size());
    for (const auto& array_buffer : other._array_values) {
      if (array_buffer) {
        // Create a new Buffer<16> and copy data
        auto new_buffer = std::make_unique<Buffer<16>>(*array_buffer);
        _array_values.push_back(std::move(new_buffer));
      } else {
        _array_values.push_back(nullptr);
      }
    }
  }
  return *this;
}

// clear() method
void TimeSamples::clear() {
  _samples.clear();
  _times.clear();
  _blocked.clear();
  _values.clear();
  _offsets.clear();
  _small_values.clear();
  _pod_samples.clear();
  _type_id = 0;
  _use_pod = false;
  _is_array = false;
  _array_size = 0;
  _element_size = 0;
  _blocked_count = 0;
  _dirty = true;
  _dirty_start = 0;
  _dirty_end = 0;
}

static bool IsPODType(uint32_t type_id) {
  // Check if type_id corresponds to a POD type or array of POD type
  // Arrays have the TYPE_ID_STL_ARRAY_BIT set (bit 20)
  // We need to check both the scalar type and array type
  
  // Extract the base type by masking off the array bit
  // TYPE_ID_STL_ARRAY_BIT is already defined in value-types.hh
  uint32_t base_type_id = type_id & (~TYPE_ID_STL_ARRAY_BIT);
  
  // Check if the base type is a POD type
  switch (base_type_id) {
    // Basic types
    case TYPE_ID_BOOL:
    case TYPE_ID_INT32:
    case TYPE_ID_UINT32:
    case TYPE_ID_INT64:
    case TYPE_ID_UINT64:
    
    // Half precision types
    case TYPE_ID_HALF:
    case TYPE_ID_HALF2:
    case TYPE_ID_HALF3:
    case TYPE_ID_HALF4:
    
    // Float types
    case TYPE_ID_FLOAT:
    case TYPE_ID_FLOAT2:
    case TYPE_ID_FLOAT3:
    case TYPE_ID_FLOAT4:
    
    // Double types
    case TYPE_ID_DOUBLE:
    case TYPE_ID_DOUBLE2:
    case TYPE_ID_DOUBLE3:
    case TYPE_ID_DOUBLE4:
    
    // Integer vector types
    case TYPE_ID_INT2:
    case TYPE_ID_INT3:
    case TYPE_ID_INT4:
    
    // Quaternion types
    case TYPE_ID_QUATH:
    case TYPE_ID_QUATF:
    case TYPE_ID_QUATD:
    
    // Color types
    case TYPE_ID_COLOR3H:
    case TYPE_ID_COLOR3F:
    case TYPE_ID_COLOR3D:
    case TYPE_ID_COLOR4H:
    case TYPE_ID_COLOR4F:
    case TYPE_ID_COLOR4D:
    
    // Vector types
    case TYPE_ID_VECTOR3H:
    case TYPE_ID_VECTOR3F:
    case TYPE_ID_VECTOR3D:
    
    // Normal types
    case TYPE_ID_NORMAL3H:
    case TYPE_ID_NORMAL3F:
    case TYPE_ID_NORMAL3D:
    
    // Point types
    case TYPE_ID_POINT3H:
    case TYPE_ID_POINT3F:
    case TYPE_ID_POINT3D:
    
    // Texture coordinate types
    case TYPE_ID_TEXCOORD2H:
    case TYPE_ID_TEXCOORD2F:
    case TYPE_ID_TEXCOORD2D:
    case TYPE_ID_TEXCOORD3H:
    case TYPE_ID_TEXCOORD3F:
    case TYPE_ID_TEXCOORD3D:
    
    // Frame type
    case TYPE_ID_FRAME4D:

    // Matrix types - now trivial with default constructors
    case TYPE_ID_MATRIX2F:
    case TYPE_ID_MATRIX3F:
    case TYPE_ID_MATRIX4F:
    case TYPE_ID_MATRIX2D:
    case TYPE_ID_MATRIX3D:
    case TYPE_ID_MATRIX4D:
      return true;

    default:
      return false;
  }
}

// init() method
bool TimeSamples::init(uint32_t type_id) {
  //DCOUT("init" << type_id);

  // Allow initialization if empty OR if it contains only uninitialized blocked samples
  if (!empty() && _type_id != 0) {
    DCOUT("initialized" << type_id);
    return false; // Already initialized with a different type
  }
  DCOUT("init" << type_id);
  _type_id = type_id;

  // Determine if we should use PODTimeSamples based on type
  _use_pod = IsPODType(type_id);
  //_use_pod = false;  // DEPRECATED: PODTimeSamples always disabled
  
  if (_use_pod) {
    DCOUT("  use_pod: " << type_id);
    _pod_samples._type_id = type_id;
  }
  return true;
}

} // namespace value
} // namespace tinyusdz

