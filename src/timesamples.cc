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
// TimeSamples Sorting Strategy Helper Methods
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
    std::vector<uint64_t>& offsets,
    std::vector<size_t>* array_counts = nullptr) {

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
    size_t key_array_count = 0;
    if (array_counts && i < array_counts->size()) {
      key_array_count = (*array_counts)[i];
    }

    // Find insertion position and shift elements
    size_t j = i;
    while (j > 0 && times[j - 1] > key_time) {
      times[j] = times[j - 1];
      blocked[j] = blocked[j - 1];
      offsets[j] = offsets[j - 1];
      if (array_counts && j < array_counts->size()) {
        (*array_counts)[j] = (*array_counts)[j - 1];
      }
      --j;
    }

    // Insert the element
    times[j] = key_time;
    blocked[j] = key_blocked;
    offsets[j] = key_offset;
    if (array_counts && j < array_counts->size()) {
      (*array_counts)[j] = key_array_count;
    }
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
    std::vector<uint64_t>& offsets,
    std::vector<size_t>* array_counts = nullptr) {

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
  std::vector<size_t> sorted_array_counts;
  if (array_counts && array_counts->size() == times.size()) {
    sorted_array_counts.resize(array_counts->size());
  }

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
    if (offset_val & value::TimeSamples::OFFSET_DEDUP_FLAG) {
      // Extract old reference index
      size_t old_ref_idx = static_cast<size_t>(offset_val & value::TimeSamples::OFFSET_VALUE_MASK);

      // Bounds check before accessing index_map
      if (old_ref_idx < index_map.size()) {
        // Map to new index
        size_t new_ref_idx = index_map[old_ref_idx];

        // Reconstruct offset with new index, preserving flags
        offset_val = (offset_val & value::TimeSamples::OFFSET_FLAGS_MASK) | new_ref_idx;
      }
      // If out of bounds, keep the offset as-is (invalid but won't crash)
    }

    sorted_offsets[i] = offset_val;
    if (!sorted_array_counts.empty()) {
      sorted_array_counts[i] = (*array_counts)[indices[i]];
    }
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  offsets = std::move(sorted_offsets);
  if (!sorted_array_counts.empty()) {
    (*array_counts) = std::move(sorted_array_counts);
  }
  // Note: values array doesn't need reordering as offsets handle the mapping
}

inline void sort_with_small_values(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked,
    std::vector<uint64_t>& small_values) {
  if (times.size() != blocked.size() || times.size() != small_values.size()) {
    return;
  }

  std::vector<double> sorted_times(times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(blocked.size());
  std::vector<uint64_t> sorted_small_values(small_values.size());

  for (size_t i = 0; i < indices.size(); ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];
    sorted_small_values[i] = small_values[indices[i]];
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  small_values = std::move(sorted_small_values);
}

inline void sort_with_value_array_refs(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked,
    std::vector<uint64_t>& refs,
    std::vector<size_t>* array_counts = nullptr) {
  if (times.size() != refs.size() || times.size() != blocked.size()) {
    return;
  }

  std::vector<double> sorted_times(times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(blocked.size());
  std::vector<uint64_t> sorted_refs(refs.size());
  std::vector<size_t> sorted_array_counts;
  if (array_counts && array_counts->size() == times.size()) {
    sorted_array_counts.resize(array_counts->size());
  }

  for (size_t i = 0; i < indices.size(); ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];
    sorted_refs[i] = refs[indices[i]];
    if (!sorted_array_counts.empty()) {
      sorted_array_counts[i] = (*array_counts)[indices[i]];
    }
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  refs = std::move(sorted_refs);
  if (!sorted_array_counts.empty()) {
    (*array_counts) = std::move(sorted_array_counts);
  }
}

inline void sort_times_and_blocked(
    const std::vector<size_t>& indices,
    std::vector<double>& times,
    Buffer<16>& blocked) {
  if (times.size() != blocked.size()) {
    return;
  }

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
  // Check which storage is in use
  if (!_times.empty()) {
    // Unified binary storage (new approach)
    // Fast path: check if already sorted to avoid unnecessary work
    if (_times.size() < 2 || std::is_sorted(_times.begin(), _times.end())) {
      _dirty = false;
      return;
    }

    if (!_offsets.empty()) {
      const bool has_dedup = std::any_of(
          _offsets.begin(), _offsets.end(), [](uint64_t offset) {
            return (offset & value::TimeSamples::OFFSET_DEDUP_FLAG) != 0;
          });

      if (!has_dedup && count_inversions(_times) * 20 < _times.size()) {
        insertion_sort_with_offsets(_times, _blocked, _offsets, &_array_counts);
      } else {
        std::vector<size_t> indices = create_sort_indices(_times);
        sort_with_offsets(indices, _times, _blocked, _offsets, &_array_counts);
      }
    } else {
      std::vector<size_t> indices = create_sort_indices(_times);

      if (_storage.uses_value_array()) {
        sort_with_value_array_refs(indices, _times, _blocked, _value_array_refs,
                                   &_array_counts);
      } else if (!_small_values.empty()) {
        sort_with_small_values(indices, _times, _blocked, _small_values);
      } else {
        sort_times_and_blocked(indices, _times, _blocked);
      }
    }
  } else if (!_samples.empty()) {
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

// ============================================================================
// reconstruct_binary_sample / reconstruct_unified_sample
// Moved from timesamples.hh to reduce header compilation cost.
// These expand 80+ macro-generated type dispatch cases.
// ============================================================================

bool TimeSamples::reconstruct_binary_sample(size_t idx, Sample* sample) const {
    if (!sample || idx >= _times.size() || idx >= _blocked.size()) {
      return false;
    }

    sample->t = _times[idx];
    sample->blocked = (_blocked[idx] != 0);
    sample->value = value::Value();

    if (sample->blocked) {
      return true;
    }

    // Array sample reconstruction
    if (_storage.is_array_backend()) {
      if (_storage.uses_value_array()) {
        return false;  // Handled by reconstruct_value_array_sample
      }

      const size_t array_count = get_array_count(idx);

      // Reconstruct from array-specific storage
      if (idx < _offsets.size()) {
        const uint64_t encoded_offset = _offsets[idx];

        // Check dedup flag
        if (encoded_offset & OFFSET_DEDUP_FLAG) {
          const size_t ref_idx = static_cast<size_t>(encoded_offset & OFFSET_VALUE_MASK);
          if (ref_idx < _offsets.size() && ref_idx != idx) {
            Sample ref_sample;
            if (reconstruct_binary_sample(ref_idx, &ref_sample)) {
              sample->value = ref_sample.value;
              return true;
            }
          }
          return true;
        }

        // Check array buffer flag
        if (encoded_offset & OFFSET_ARRAY_BUFFER_FLAG) {
          const size_t buf_idx = static_cast<size_t>(encoded_offset & OFFSET_VALUE_MASK);
          if (buf_idx < _array_values.size() && _array_values[buf_idx]) {
            const auto& buf = *_array_values[buf_idx];

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

            (void)buf;  // Used indirectly via reconstruct_unified_array_value
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
        }

        // Inline array from _values buffer
        const size_t byte_offset = static_cast<size_t>(encoded_offset & OFFSET_VALUE_MASK);
        if (array_count > 0) {
          // Reconstruct typed vector from inline buffer
          std::vector<uint8_t> raw(array_count * _storage.array.element_size);
          if (byte_offset + raw.size() <= _values.size()) {
            std::memcpy(raw.data(), _values.data() + byte_offset, raw.size());
          }
          // Store as raw bytes — type reconstruction happens at query time
          sample->value = value::Value(std::move(raw));
        }
      }

      return true;
    }

    // Scalar sample reconstruction
    const uint32_t type_id = _type_id;
    if (idx < _small_values.size()) {
      uint64_t stored = _small_values[idx];

#define RECONSTRUCT_SMALL_VALUE(TYPE) \
      case value::TypeTraits<TYPE>::type_id(): { \
        TYPE val; \
        std::memcpy(&val, &stored, sizeof(TYPE)); \
        sample->value = value::Value(val); \
        break; \
      }

      switch (type_id) {
        RECONSTRUCT_SMALL_VALUE(value::half)
        RECONSTRUCT_SMALL_VALUE(float)
        RECONSTRUCT_SMALL_VALUE(double)
        RECONSTRUCT_SMALL_VALUE(int32_t)
        RECONSTRUCT_SMALL_VALUE(uint32_t)
        RECONSTRUCT_SMALL_VALUE(int64_t)
        RECONSTRUCT_SMALL_VALUE(uint64_t)
        case value::TypeTraits<bool>::type_id(): {
          const bool bval = (stored != 0);
          sample->value = value::Value(bval);
          break;
        }
        default:
          break;
      }
#undef RECONSTRUCT_SMALL_VALUE
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

bool TimeSamples::reconstruct_unified_sample(size_t idx, Sample* sample) const {
    if (_storage.uses_value_array()) {
      return reconstruct_value_array_sample(idx, sample);
    }

    return reconstruct_binary_sample(idx, sample);
}

bool TimeSamples::add_sample(const Sample &s, std::string *err) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    if (!is_initialized() && !s.value.is_none()) {
      init(s.value.type_id());
    } else if (!s.value.is_none() && is_initialized()) {
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

    _samples.push_back(s);
    _dirty = true;
    return true;
}

bool TimeSamples::add_sample(double t, const value::Value &v, std::string *err) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    if (!is_initialized() && !v.is_none()) {
      init(v.type_id());
    } else if (!v.is_none() && is_initialized()) {
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

    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = v.is_none();
    _samples.push_back(s);
    _dirty = true;
    return true;
}

bool TimeSamples::add_blocked_sample(double t, const value::Value &v, std::string *err) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_blocked_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    if (!is_initialized() && !v.is_none() && v.type_id() != 1) {
      init(v.type_id());
    } else if (!v.is_none() && is_initialized() && v.type_id() != 1) {
      if (v.type_id() != _type_id) {
        if (err) {
          (*err) += "Type mismatch in TimeSamples (blocked sample): expected type_id " +
                    std::to_string(_type_id) + " but got " +
                    std::to_string(v.type_id()) + ".\n";
        }
        return false;
      }
    }

    Sample s;
    s.t = t;
    s.value = v;
    s.blocked = true;
    _samples.emplace_back(s);
    _dirty = true;
    return true;
}

bool TimeSamples::add_value_array_sample(double t, value::Value &&v, std::string *err) {
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

    size_t storage_index = _value_array_storage.size();
    _value_array_storage.push_back(std::move(v));

    _times.push_back(t);
    _blocked.push_back(0);
    _value_array_refs.push_back(make_value_array_ref(storage_index, false));
    _array_counts.push_back(_value_array_storage.back().array_size());

    invalidate_reconstructed_samples_cache();
    _dirty = true;
    return true;
}

size_t TimeSamples::estimate_memory_usage() const {
    size_t total = sizeof(TimeSamples);

    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity();
    total += _values.capacity();
    total += _offsets.capacity() * sizeof(uint64_t);
    total += _small_values.capacity() * sizeof(uint64_t);

    total += _array_values.capacity() * sizeof(std::unique_ptr<Buffer<16>>);
    for (const auto& buf : _array_values) {
      if (buf) {
        total += sizeof(Buffer<16>) + buf->capacity();
      }
    }

    total += _value_array_storage.capacity() * sizeof(value::Value);
    for (const auto& val : _value_array_storage) {
      total += val.estimate_memory_usage();
    }
    total += _value_array_refs.capacity() * sizeof(uint64_t);
    total += _array_counts.capacity() * sizeof(size_t);

    total += _samples.capacity() * sizeof(Sample);
    for (const auto &sample : _samples) {
      total += sample.value.estimate_memory_usage();
    }

    return total;
}

bool TimeSamples::add_dedup_sample(double t, size_t ref_index, std::string *err) {
    // Check if using value array storage
    if (_storage.uses_value_array()) {
      if (ref_index >= _times.size()) {
        if (err) {
          (*err) += "Invalid ref_index in add_dedup_sample: " +
                    std::to_string(ref_index) + " >= " + std::to_string(_times.size()) + ".\n";
        }
        return false;
      }

      uint64_t ref_entry = _value_array_refs[ref_index];
      size_t storage_index = get_value_array_index(ref_entry);

      if (is_value_array_dedup(ref_entry)) {
        if (err) {
          (*err) += "Cannot deduplicate from already deduplicated sample.\n";
        }
        return false;
      }

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

    Sample s;
    s.t = t;
    s.value = _samples[ref_index].value;
    s.blocked = _samples[ref_index].blocked;
    _samples.push_back(s);
    _dirty = true;
    return true;
}

std::vector<TimeSamples::Sample> &TimeSamples::samples() {
    if (!_times.empty() && _samples.empty()) {
      (void)get_samples();
    }
    if (_dirty) {
      update();
    }
    return _samples;
}

const std::vector<TimeSamples::Sample> &TimeSamples::get_samples() const {
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

} // namespace value

//
// Explicit template instantiations for commonly used types
// This reduces compilation time by instantiating templates only once
//

// Integer types (binary-serializable, non-lerp'able)
template struct TypedTimeSamples<bool>;
template struct TypedTimeSamples<int32_t>;
template struct TypedTimeSamples<uint32_t>;
template struct TypedTimeSamples<int64_t>;
template struct TypedTimeSamples<uint64_t>;

// Floating point scalar types (binary-serializable, lerp'able)
template struct TypedTimeSamples<value::half>;
template struct TypedTimeSamples<float>;
template struct TypedTimeSamples<double>;

// Vector types (binary-serializable, lerp'able)
template struct TypedTimeSamples<value::half2>;
template struct TypedTimeSamples<value::half3>;
template struct TypedTimeSamples<value::half4>;
template struct TypedTimeSamples<value::float2>;
template struct TypedTimeSamples<value::float3>;
template struct TypedTimeSamples<value::float4>;
template struct TypedTimeSamples<value::double2>;
template struct TypedTimeSamples<value::double3>;
template struct TypedTimeSamples<value::double4>;

// Integer vector types (binary-serializable, non-lerp'able)
template struct TypedTimeSamples<value::int2>;
template struct TypedTimeSamples<value::int3>;
template struct TypedTimeSamples<value::int4>;

// Quaternion types (binary-serializable, lerp'able)
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

// Role types (binary-serializable, lerp'able)
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
    // Return the first non-blocked sample.
    for (const auto &s : _samples) {
      if (!s.blocked) {
        (*dst) = s.value;
        return true;
      }
    }
    return false;  // All samples are blocked.
  } else {

    if (_samples.size() == 1) {
      if (_samples[0].blocked) return false;
      (*dst) = _samples[0].value;
      return true;
    }

    // Held = nearest preceding value for a given time.
    auto it = std::upper_bound(
      _samples.begin(), _samples.end(), t,
      [](double tval, const Sample &a) { return tval < a.t; });

    const auto it_held = (it == _samples.begin()) ? _samples.begin() : (it - 1);

    if (it_held->blocked) {
      return false;  // Nearest preceding sample is blocked (USD "None").
    }
    (*dst) = it_held->value;
    return true;
  }
#else
  // SoA layout implementation
  if (value::TimeCode(t).is_default()) {
    for (size_t i = 0; i < _times.size(); ++i) {
      if (!_blocked[i]) {
        (*dst) = _values[i];
        return true;
      }
    }
    return false;
  } else {

    if (_times.size() == 1) {
      if (_blocked[0]) return false;
      (*dst) = _values[0];
      return true;
    }

    // Held = nearest preceding value for a given time.
    auto it = std::upper_bound(_times.begin(), _times.end(), t);
    size_t idx = (it == _times.begin()) ? 0 : static_cast<size_t>(std::distance(_times.begin(), it) - 1);

    if (_blocked[idx]) {
      return false;
    }
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
    // Return the first non-blocked sample.
    for (const auto &s : _samples) {
      if (!s.blocked) {
        (*dst) = s.value;
        return true;
      }
    }
    return false;
  } else {

    if (_samples.size() == 1) {
      if (_samples[0].blocked) return false;
      (*dst) = _samples[0].value;
      return true;
    }

    if (interp == value::TimeSampleInterpolationType::Linear) {

      auto it = std::lower_bound(
        _samples.begin(), _samples.end(), t,
        [](const Sample &a, double tval) { return a.t < tval; });

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

      // If either endpoint is blocked, fall back to held interpolation
      // at the non-blocked endpoint.
      if (_samples[idx0].blocked && _samples[idx1].blocked) {
        return false;
      }
      if (_samples[idx0].blocked) {
        (*dst) = _samples[idx1].value;
        return true;
      }
      if (_samples[idx1].blocked) {
        (*dst) = _samples[idx0].value;
        return true;
      }

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
      // Held interpolation
      auto held_it = std::upper_bound(
          _samples.begin(), _samples.end(), t,
          [](double tval, const Sample &a) { return tval < a.t; });

      const auto it_held =
          (held_it == _samples.begin()) ? _samples.begin() : (held_it - 1);

      if (it_held->blocked) {
        return false;
      }
      (*dst) = it_held->value;
      return true;
    }
  }
#else
  // SoA layout implementation
  if (value::TimeCode(t).is_default()) {
    for (size_t i = 0; i < _times.size(); ++i) {
      if (!_blocked[i]) {
        (*dst) = _values[i];
        return true;
      }
    }
    return false;
  } else {

    if (_times.size() == 1) {
      if (_blocked[0]) return false;
      (*dst) = _values[0];
      return true;
    }

    if (interp == value::TimeSampleInterpolationType::Linear) {

      auto it = std::lower_bound(_times.begin(), _times.end(), t);

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

      if (_blocked[idx0] && _blocked[idx1]) {
        return false;
      }
      if (_blocked[idx0]) {
        (*dst) = _values[idx1];
        return true;
      }
      if (_blocked[idx1]) {
        (*dst) = _values[idx0];
        return true;
      }

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
      // Held interpolation
      auto held_it = std::upper_bound(_times.begin(), _times.end(), t);
      size_t idx =
          (held_it == _times.begin())
              ? 0
              : static_cast<size_t>(std::distance(_times.begin(), held_it) - 1);

      if (_blocked[idx]) {
        return false;
      }
      (*dst) = _values[idx];
      return true;
    }
  }
#endif
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
      _small_values(std::move(other._small_values)),
      _offsets(std::move(other._offsets)),
      _array_values(std::move(other._array_values)),
      _blocked(std::move(other._blocked)),
      _values(std::move(other._values)),
      _value_array_storage(std::move(other._value_array_storage)),
      _value_array_refs(std::move(other._value_array_refs)),
      _array_counts(std::move(other._array_counts)),
      _storage(other._storage),
      _blocked_count(other._blocked_count),
      _dirty_start(other._dirty_start),
      _dirty_end(other._dirty_end),
      _type_id(other._type_id),
      _dirty(other._dirty) {
  // Reset moved-from object to valid empty state
  other._type_id = 0;
  other._storage.clear();
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
    _small_values = std::move(other._small_values);
    _value_array_storage = std::move(other._value_array_storage);
    _value_array_refs = std::move(other._value_array_refs);
    _type_id = other._type_id;
    _storage = other._storage;
    _blocked_count = other._blocked_count;
    _array_counts = std::move(other._array_counts);
    _dirty = other._dirty;
    _dirty_start = other._dirty_start;
    _dirty_end = other._dirty_end;

    // Reset moved-from object to valid empty state
    other._type_id = 0;
    other._storage.clear();
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
      _small_values(other._small_values),
      _offsets(other._offsets),
      // _array_values deep-copied in body
      _blocked(other._blocked),
      _values(other._values),
      // _value_array_storage copied in body to avoid TypeTraits issues
      _value_array_refs(other._value_array_refs),
      _array_counts(other._array_counts),
      _storage(other._storage),
      _blocked_count(other._blocked_count),
      _dirty_start(other._dirty_start),
      _dirty_end(other._dirty_end),
      _type_id(other._type_id),
      _dirty(other._dirty) {
  // Copy value array storage in body to avoid TypeTraits instantiation issues
  _value_array_storage.reserve(other._value_array_storage.size());
  for (const auto& v : other._value_array_storage) {
    _value_array_storage.push_back(v);
  }
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
    _storage = other._storage;
    _blocked_count = other._blocked_count;
    _array_counts = other._array_counts;
    _dirty = other._dirty;
    _dirty_start = other._dirty_start;
    _dirty_end = other._dirty_end;
    _small_values = other._small_values;
    _value_array_refs = other._value_array_refs;
    // Copy value array storage element by element to avoid TypeTraits instantiation issues
    _value_array_storage.clear();
    _value_array_storage.reserve(other._value_array_storage.size());
    for (const auto& v : other._value_array_storage) {
      _value_array_storage.push_back(v);
    }

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
  _array_values.clear();
  _offsets.clear();
  _small_values.clear();
  _value_array_storage.clear();
  _value_array_refs.clear();
  _array_counts.clear();
  _type_id = 0;
  _storage.clear();
  _blocked_count = 0;
  _dirty = true;
  _dirty_start = 0;
  _dirty_end = 0;
}


// init() method
bool TimeSamples::init(uint32_t type_id) {
  if (type_id == 0) {
    return false;
  }

  if (_type_id != 0) {
    return _type_id == type_id;
  }

  _type_id = type_id;

  return true;
}

namespace {

// Helper function to get underlying type_id from a type_id
// For role types (color3f, point3f, etc.), returns the base type's type_id
// For non-role types, returns the same type_id
uint32_t GetUnderlyingTypeIdFromTypeId(uint32_t tyid) {
  // Strip array bit if present
  bool is_array = (tyid & TYPE_ID_1D_ARRAY_BIT) != 0;
  uint32_t base_tyid = tyid & (~TYPE_ID_1D_ARRAY_BIT);

  // Map role types to their underlying types
  // This is needed because we don't have TypeTraits access at runtime
#define MAP_ROLE_TO_UNDERLYING(role_id, underlying_id) \
  if (base_tyid == role_id) return is_array ? (underlying_id | TYPE_ID_1D_ARRAY_BIT) : underlying_id;

  // Texcoord types
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD2H, TYPE_ID_HALF2)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD2F, TYPE_ID_FLOAT2)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD2D, TYPE_ID_DOUBLE2)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD3H, TYPE_ID_HALF3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD3F, TYPE_ID_FLOAT3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_TEXCOORD3D, TYPE_ID_DOUBLE3)

  // Normal types
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_NORMAL3H, TYPE_ID_HALF3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_NORMAL3F, TYPE_ID_FLOAT3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_NORMAL3D, TYPE_ID_DOUBLE3)

  // Vector types
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_VECTOR3H, TYPE_ID_HALF3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_VECTOR3F, TYPE_ID_FLOAT3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_VECTOR3D, TYPE_ID_DOUBLE3)

  // Point types
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_POINT3H, TYPE_ID_HALF3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_POINT3F, TYPE_ID_FLOAT3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_POINT3D, TYPE_ID_DOUBLE3)

  // Color types
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR3H, TYPE_ID_HALF3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR3F, TYPE_ID_FLOAT3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR3D, TYPE_ID_DOUBLE3)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR4H, TYPE_ID_HALF4)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR4F, TYPE_ID_FLOAT4)
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_COLOR4D, TYPE_ID_DOUBLE4)

  // Frame type
  MAP_ROLE_TO_UNDERLYING(TYPE_ID_FRAME4D, TYPE_ID_MATRIX4D)

#undef MAP_ROLE_TO_UNDERLYING

  // Not a role type, return as-is
  return tyid;
}

}  // namespace

bool TimeSamples::cast_to_role_type(uint32_t role_type_id) {
  if (_type_id == 0) {
    return false;  // Not initialized
  }

  // If already the target type, nothing to do
  if (_type_id == role_type_id) {
    return true;
  }

  // Get underlying type_ids for both current and target types
  uint32_t current_underlying = GetUnderlyingTypeIdFromTypeId(_type_id);
  uint32_t target_underlying = GetUnderlyingTypeIdFromTypeId(role_type_id);

  // Check if the underlying types match
  if (current_underlying != target_underlying) {
    return false;  // Incompatible types
  }

  // Safe to cast - just update the type_id
  _type_id = role_type_id;
  return true;
}

} // namespace value
} // namespace tinyusdz
