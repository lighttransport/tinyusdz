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

// PODTimeSamples::update() implementation
void PODTimeSamples::update() const {
  if (_times.empty()) {
    _dirty = false;
    return;
  }

  // Lazy sorting optimization: only sort if there's a dirty range
  if (_dirty_start >= _times.size()) {
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

  // HACK
  for (size_t i = 0; i < indices.size(); ++i) {
    DCOUT("indices[" << i << "] = " << indices[i]);
    DCOUT("times[" << i << "] = " << _times[i]);
  }

  // Reorder arrays based on sorted indices
  std::vector<double> sorted_times(_times.size());
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(_blocked.size());

  // If using offsets, sort them too
  if (!_offsets.empty()) {
    std::vector<size_t> sorted_offsets(_offsets.size());
    for (size_t i = 0; i < indices.size(); ++i) {
      sorted_times[i] = _times[indices[i]];
      sorted_blocked[i] = _blocked[indices[i]];
      sorted_offsets[i] = _offsets[indices[i]];

    }
    _times = std::move(sorted_times);
    _blocked = std::move(sorted_blocked);
    _offsets = std::move(sorted_offsets);
    // Note: _values array doesn't need reordering as offsets handle the mapping
  } else if (!_values.empty() && _type_id != 0) {
    // For non-array scalar types without offsets (legacy path)
    // Calculate element size from stored element size or type
    size_t element_size = get_element_size();
    if (element_size > 0) {
      Buffer<16> sorted_values;
      sorted_values.resize(_values.size());

      size_t dst_offset = 0;
      for (size_t i = 0; i < indices.size(); ++i) {
        sorted_times[i] = _times[indices[i]];
        sorted_blocked[i] = _blocked[indices[i]];
        DCOUT("sorted.times[" << i << "] = " << sorted_times[i]);
        DCOUT("sorted.blocked[" << i << "] = " << sorted_blocked[i]);

        // Only copy value if not blocked
        if (!_blocked[indices[i]]) {
          // Find source offset by counting non-blocked entries before indices[i]
          size_t src_offset = 0;
          for (size_t j = 0; j < indices[i]; ++j) {
            if (!_blocked[j]) {
              src_offset += element_size;
            }
          }

          const uint8_t* src = _values.data() + src_offset;
          uint8_t* dst = sorted_values.data() + dst_offset;
          std::copy(src, src + element_size, dst);
          dst_offset += element_size;
        }
      }

      _times = std::move(sorted_times);
      _blocked = std::move(sorted_blocked);
      _values = std::move(sorted_values);
    } else {
      _times = std::move(sorted_times);
      _blocked = std::move(sorted_blocked);
    }
  } else {
    // Just sort times and blocked flags
    for (size_t i = 0; i < indices.size(); ++i) {
      sorted_times[i] = _times[indices[i]];
      sorted_blocked[i] = _blocked[indices[i]];
    }
    _times = std::move(sorted_times);
    _blocked = std::move(sorted_blocked);
  }

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

// TimeSamples::update() implementation
void TimeSamples::update() const {
  if (_use_pod) {
    _pod_samples.update();
  } else {
    std::sort(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; });
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
            std::vector<__type> array_values;                                 \
            array_values.resize(_array_size);                                 \
            /* Direct memcpy for non-bool arrays (bool handled separately) */ \
            std::memcpy(&array_values[0], _values.data() + _offsets[i],       \
                        sizeof(__type) * _array_size);                                                                  \
            val = value::Value(array_values);                                 \
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
            std::vector<__type> array_values;                                 \
            array_values.resize(_array_size);                                 \
            /* Direct memcpy for non-bool arrays (bool handled separately) */ \
            std::memcpy(&array_values[0], _values.data() + value_offset,      \
                        sizeof(__type) * _array_size);                                                                  \
            val = value::Value(array_values);                                 \
            value_offset += element_size;                                     \
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
            __type typed_value;                                               \
            std::memcpy(&typed_value, _values.data() + _offsets[i],           \
                        sizeof(__type));                                       \
            val = value::Value(typed_value);                                  \
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
  if (_type_id == value::TypeTraits<bool>::type_id()) {
    if (_is_stl_array || _is_typed_array) {
      /* Bool array handling - special case due to vector<bool> */
      element_size = _array_size;  // 1 byte per bool
      if (!_offsets.empty()) {
        for (size_t i = 0; i < _times.size(); ++i) {
          double time_val = _times[i];
          bool blocked = _blocked[i];
          value::Value val;
          if (!blocked && _offsets[i] != SIZE_MAX) {
            std::vector<bool> bool_values;
            bool_values.reserve(_array_size);
            const uint8_t* src = _values.data() + _offsets[i];
            for (size_t j = 0; j < _array_size; ++j) {
              bool_values.push_back(static_cast<bool>(src[j]));
            }
            val = value::Value(bool_values);
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
            bool typed_value;
            std::memcpy(&typed_value, _values.data() + _offsets[i], sizeof(bool));
            val = value::Value(typed_value);
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
  // Handle all other POD types
  HANDLE_POD_TYPE(value::TypeTraits<int32_t>::type_id(), int32_t)
  HANDLE_POD_TYPE(value::TypeTraits<uint32_t>::type_id(), uint32_t)
  HANDLE_POD_TYPE(value::TypeTraits<int64_t>::type_id(), int64_t)
  HANDLE_POD_TYPE(value::TypeTraits<uint64_t>::type_id(), uint64_t)
  HANDLE_POD_TYPE(value::TypeTraits<value::half>::type_id(), value::half)
  HANDLE_POD_TYPE(value::TypeTraits<value::half2>::type_id(), value::half2)
  HANDLE_POD_TYPE(value::TypeTraits<value::half3>::type_id(), value::half3)
  HANDLE_POD_TYPE(value::TypeTraits<value::half4>::type_id(), value::half4)
  HANDLE_POD_TYPE(value::TypeTraits<float>::type_id(), float)
  HANDLE_POD_TYPE(value::TypeTraits<value::float2>::type_id(), value::float2)
  HANDLE_POD_TYPE(value::TypeTraits<value::float3>::type_id(), value::float3)
  HANDLE_POD_TYPE(value::TypeTraits<value::float4>::type_id(), value::float4)
  HANDLE_POD_TYPE(value::TypeTraits<double>::type_id(), double)
  HANDLE_POD_TYPE(value::TypeTraits<value::double2>::type_id(), value::double2)
  HANDLE_POD_TYPE(value::TypeTraits<value::double3>::type_id(), value::double3)
  HANDLE_POD_TYPE(value::TypeTraits<value::double4>::type_id(), value::double4)
  HANDLE_POD_TYPE(value::TypeTraits<value::int2>::type_id(), value::int2)
  HANDLE_POD_TYPE(value::TypeTraits<value::int3>::type_id(), value::int3)
  HANDLE_POD_TYPE(value::TypeTraits<value::int4>::type_id(), value::int4)
  HANDLE_POD_TYPE(value::TypeTraits<value::quath>::type_id(), value::quath)
  HANDLE_POD_TYPE(value::TypeTraits<value::quatf>::type_id(), value::quatf)
  HANDLE_POD_TYPE(value::TypeTraits<value::quatd>::type_id(), value::quatd)
  HANDLE_POD_TYPE(value::TypeTraits<value::color3f>::type_id(), value::color3f)
  HANDLE_POD_TYPE(value::TypeTraits<value::color3h>::type_id(), value::color3h)
  HANDLE_POD_TYPE(value::TypeTraits<value::color3d>::type_id(), value::color3d)
  HANDLE_POD_TYPE(value::TypeTraits<value::color4f>::type_id(), value::color4f)
  HANDLE_POD_TYPE(value::TypeTraits<value::color4h>::type_id(), value::color4h)
  HANDLE_POD_TYPE(value::TypeTraits<value::color4d>::type_id(), value::color4d)
  HANDLE_POD_TYPE(value::TypeTraits<value::vector3f>::type_id(), value::vector3f)
  HANDLE_POD_TYPE(value::TypeTraits<value::vector3h>::type_id(), value::vector3h)
  HANDLE_POD_TYPE(value::TypeTraits<value::vector3d>::type_id(), value::vector3d)
  HANDLE_POD_TYPE(value::TypeTraits<value::normal3f>::type_id(), value::normal3f)
  HANDLE_POD_TYPE(value::TypeTraits<value::normal3h>::type_id(), value::normal3h)
  HANDLE_POD_TYPE(value::TypeTraits<value::normal3d>::type_id(), value::normal3d)
  HANDLE_POD_TYPE(value::TypeTraits<value::point3f>::type_id(), value::point3f)
  HANDLE_POD_TYPE(value::TypeTraits<value::point3h>::type_id(), value::point3h)
  HANDLE_POD_TYPE(value::TypeTraits<value::point3d>::type_id(), value::point3d)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord2f>::type_id(), value::texcoord2f)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord2h>::type_id(), value::texcoord2h)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord2d>::type_id(), value::texcoord2d)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord3f>::type_id(), value::texcoord3f)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord3h>::type_id(), value::texcoord3h)
  HANDLE_POD_TYPE(value::TypeTraits<value::texcoord3d>::type_id(), value::texcoord3d)
  // frame4d is also POD
  HANDLE_POD_TYPE(value::TypeTraits<value::frame4d>::type_id(), value::frame4d)
  {
    // Unknown type_id - this shouldn't happen for PODTimeSamples
    // Return empty vector
  }

#undef HANDLE_POD_TYPE

  return samples;
}

// Helper function to get element size for a given type_id
size_t PODTimeSamples::get_element_size() const {
  // Calculate element size based on type_id
#define TYPE_SIZE_CASE(__type)                                                \
  if (_type_id == value::TypeTraits<__type>::type_id()) {                    \
    return sizeof(__type);                                                    \
  }

  TYPE_SIZE_CASE(bool)
  TYPE_SIZE_CASE(int32_t)
  TYPE_SIZE_CASE(uint32_t)
  TYPE_SIZE_CASE(int64_t)
  TYPE_SIZE_CASE(uint64_t)
  TYPE_SIZE_CASE(value::half)
  TYPE_SIZE_CASE(value::half2)
  TYPE_SIZE_CASE(value::half3)
  TYPE_SIZE_CASE(value::half4)
  TYPE_SIZE_CASE(float)
  TYPE_SIZE_CASE(value::float2)
  TYPE_SIZE_CASE(value::float3)
  TYPE_SIZE_CASE(value::float4)
  TYPE_SIZE_CASE(double)
  TYPE_SIZE_CASE(value::double2)
  TYPE_SIZE_CASE(value::double3)
  TYPE_SIZE_CASE(value::double4)
  TYPE_SIZE_CASE(value::int2)
  TYPE_SIZE_CASE(value::int3)
  TYPE_SIZE_CASE(value::int4)
  TYPE_SIZE_CASE(value::quath)
  TYPE_SIZE_CASE(value::quatf)
  TYPE_SIZE_CASE(value::quatd)
  TYPE_SIZE_CASE(value::color3f)
  TYPE_SIZE_CASE(value::color3h)
  TYPE_SIZE_CASE(value::color3d)
  TYPE_SIZE_CASE(value::color4f)
  TYPE_SIZE_CASE(value::color4h)
  TYPE_SIZE_CASE(value::color4d)
  TYPE_SIZE_CASE(value::vector3f)
  TYPE_SIZE_CASE(value::vector3h)
  TYPE_SIZE_CASE(value::vector3d)
  TYPE_SIZE_CASE(value::normal3f)
  TYPE_SIZE_CASE(value::normal3h)
  TYPE_SIZE_CASE(value::normal3d)
  TYPE_SIZE_CASE(value::point3f)
  TYPE_SIZE_CASE(value::point3h)
  TYPE_SIZE_CASE(value::point3d)
  TYPE_SIZE_CASE(value::texcoord2f)
  TYPE_SIZE_CASE(value::texcoord2h)
  TYPE_SIZE_CASE(value::texcoord2d)
  TYPE_SIZE_CASE(value::texcoord3f)
  TYPE_SIZE_CASE(value::texcoord3h)
  TYPE_SIZE_CASE(value::texcoord3d)
  TYPE_SIZE_CASE(value::frame4d)

#undef TYPE_SIZE_CASE

#if 0
  if (_type_id == value::TYPE_ID_TYPED_TIMESAMPLE_VALUE) {
    return sizeof(uint64_t);
  }

  if (_type_id == value::TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE) {
    return sizeof(uint64_t);
  }
#endif

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

// Explicit instantiations for PODTimeSamples::add_sample
// Integer types
template bool PODTimeSamples::add_sample<bool>(double, const bool&, std::string*, size_t);
template bool PODTimeSamples::add_sample<int32_t>(double, const int32_t&, std::string*, size_t);
template bool PODTimeSamples::add_sample<uint32_t>(double, const uint32_t&, std::string*, size_t);
template bool PODTimeSamples::add_sample<int64_t>(double, const int64_t&, std::string*, size_t);
template bool PODTimeSamples::add_sample<uint64_t>(double, const uint64_t&, std::string*, size_t);

// Float types
template bool PODTimeSamples::add_sample<value::half>(double, const value::half&, std::string*, size_t);
template bool PODTimeSamples::add_sample<float>(double, const float&, std::string*, size_t);
template bool PODTimeSamples::add_sample<double>(double, const double&, std::string*, size_t);

// Vector types - removed typedef versions, kept in std::array section below

// Quaternion types
template bool PODTimeSamples::add_sample<value::quath>(double, const value::quath&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::quatf>(double, const value::quatf&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::quatd>(double, const value::quatd&, std::string*, size_t);

// Note: Matrix types are not trivial (don't satisfy std::is_trivial) so they cannot use PODTimeSamples::add_sample

// Role types
template bool PODTimeSamples::add_sample<value::vector3h>(double, const value::vector3h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::vector3f>(double, const value::vector3f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::vector3d>(double, const value::vector3d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::normal3h>(double, const value::normal3h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::normal3f>(double, const value::normal3f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::normal3d>(double, const value::normal3d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::point3h>(double, const value::point3h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::point3f>(double, const value::point3f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::point3d>(double, const value::point3d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color3h>(double, const value::color3h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color3f>(double, const value::color3f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color3d>(double, const value::color3d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color4h>(double, const value::color4h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color4f>(double, const value::color4f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::color4d>(double, const value::color4d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord2h>(double, const value::texcoord2h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord2f>(double, const value::texcoord2f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord2d>(double, const value::texcoord2d&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord3h>(double, const value::texcoord3h&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord3f>(double, const value::texcoord3f&, std::string*, size_t);
template bool PODTimeSamples::add_sample<value::texcoord3d>(double, const value::texcoord3d&, std::string*, size_t);

// Other types
template bool PODTimeSamples::add_sample<value::timecode>(double, const value::timecode&, std::string*, size_t);
// Note: frame4d is not trivial (doesn't satisfy std::is_trivial) so it cannot use PODTimeSamples::add_sample

// std::array versions - needed when code uses std::array directly instead of typedefs
// These are the same types as the typedef versions above, but we need both to handle
// cases where the code explicitly uses std::array
template bool PODTimeSamples::add_sample<std::array<value::half, 2>>(double, const std::array<value::half, 2>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<value::half, 3>>(double, const std::array<value::half, 3>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<value::half, 4>>(double, const std::array<value::half, 4>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<float, 2>>(double, const std::array<float, 2>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<float, 3>>(double, const std::array<float, 3>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<float, 4>>(double, const std::array<float, 4>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<double, 2>>(double, const std::array<double, 2>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<double, 3>>(double, const std::array<double, 3>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<double, 4>>(double, const std::array<double, 4>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<int, 2>>(double, const std::array<int, 2>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<int, 3>>(double, const std::array<int, 3>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<int, 4>>(double, const std::array<int, 4>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<uint32_t, 2>>(double, const std::array<uint32_t, 2>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<uint32_t, 3>>(double, const std::array<uint32_t, 3>&, std::string*, size_t);
template bool PODTimeSamples::add_sample<std::array<uint32_t, 4>>(double, const std::array<uint32_t, 4>&, std::string*, size_t);

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

// Explicit instantiations for PODTimeSamples::add_typed_array_sample
// Common types that use TypedArray
template bool PODTimeSamples::add_typed_array_sample<float>(double, const TypedArray<float>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<double>(double, const TypedArray<double>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<int32_t>(double, const TypedArray<int32_t>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<uint32_t>(double, const TypedArray<uint32_t>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<int64_t>(double, const TypedArray<int64_t>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<uint64_t>(double, const TypedArray<uint64_t>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::float2>(double, const TypedArray<value::float2>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::float3>(double, const TypedArray<value::float3>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::float4>(double, const TypedArray<value::float4>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::double2>(double, const TypedArray<value::double2>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::double3>(double, const TypedArray<value::double3>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::double4>(double, const TypedArray<value::double4>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::int2>(double, const TypedArray<value::int2>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::int3>(double, const TypedArray<value::int3>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::int4>(double, const TypedArray<value::int4>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix2f>(double, const TypedArray<value::matrix2f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix3f>(double, const TypedArray<value::matrix3f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix4f>(double, const TypedArray<value::matrix4f>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix2d>(double, const TypedArray<value::matrix2d>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix3d>(double, const TypedArray<value::matrix3d>&, std::string*, size_t);
template bool PODTimeSamples::add_typed_array_sample<value::matrix4d>(double, const TypedArray<value::matrix4d>&, std::string*, size_t);

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

} // namespace tinyusdz

