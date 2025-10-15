// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
// TimeSamples implementation

#include "value-types.hh"
// value-types.hh must be included before timesamples.hh
// to have full definitions of types
#include <cstring>

namespace tinyusdz {

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

  if (_type_id == value::TYPE_ID_TYPED_TIMESAMPLE_VALUE) {
    return sizeof(uint64_t);
  }

  if (_type_id == value::TYPE_ID_TYPED_ARRAY_TIMESAMPLE_VALUE) {
    return sizeof(uint64_t);
  }

  return 0; // Unknown type
}

} // namespace tinyusdz

