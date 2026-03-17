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

namespace {

// Insertion sort for Sample array (nearly-sorted optimization)
inline void insertion_sort_samples(std::vector<value::TimeSamples::Sample>& samples) {
  const size_t n = samples.size();
  if (n < 2) return;

  for (size_t i = 1; i < n; ++i) {
    if (samples[i].t >= samples[i - 1].t) {
      continue;
    }

    value::TimeSamples::Sample key = std::move(samples[i]);
    size_t j = i;

    while (j > 0 && samples[j - 1].t > key.t) {
      samples[j] = std::move(samples[j - 1]);
      --j;
    }

    samples[j] = std::move(key);
  }
}

// Sort flat binary storage by permuting parallel arrays
inline void sort_flat_storage(
    std::vector<double>& times,
    Buffer<16>& blocked,
    std::vector<uint32_t>& data_offsets,
    std::vector<uint32_t>* array_counts) {

  const size_t n = times.size();
  if (n < 2) return;

  // Create index array
  std::vector<size_t> indices(n);
  for (size_t i = 0; i < n; ++i) indices[i] = i;
  std::sort(indices.begin(), indices.end(),
            [&times](size_t a, size_t b) { return times[a] < times[b]; });

  std::vector<double> sorted_times(n);
  Buffer<16> sorted_blocked;
  sorted_blocked.resize(n);
  std::vector<uint32_t> sorted_offsets(data_offsets.size());
  std::vector<uint32_t> sorted_counts;
  if (array_counts && array_counts->size() == n) {
    sorted_counts.resize(n);
  }

  for (size_t i = 0; i < n; ++i) {
    sorted_times[i] = times[indices[i]];
    sorted_blocked[i] = blocked[indices[i]];
    if (indices[i] < data_offsets.size()) {
      sorted_offsets[i] = data_offsets[indices[i]];
    }
    if (!sorted_counts.empty()) {
      sorted_counts[i] = (*array_counts)[indices[i]];
    }
  }

  times = std::move(sorted_times);
  blocked = std::move(sorted_blocked);
  data_offsets = std::move(sorted_offsets);
  if (!sorted_counts.empty()) {
    (*array_counts) = std::move(sorted_counts);
  }
}

} // anonymous namespace

namespace value {

// TimeSamples::update() implementation
void TimeSamples::update() const {
  if (!_times.empty()) {
    // Flat binary storage
    if (_times.size() < 2 || std::is_sorted(_times.begin(), _times.end())) {
      _dirty = false;
      return;
    }

    sort_flat_storage(_times, _blocked, _data_offsets,
                      _is_array ? &_array_counts : nullptr);
  } else if (!_samples.empty()) {
    if (_samples.size() < 2) {
      _dirty = false;
      return;
    }

    if (std::is_sorted(_samples.begin(), _samples.end(),
              [](const Sample &a, const Sample &b) { return a.t < b.t; })) {
      _dirty = false;
      return;
    }

    size_t inversions = 0;
    size_t scan_limit = std::min(_samples.size() - 1, size_t(100));
    for (size_t i = 0; i < scan_limit; ++i) {
      if (_samples[i].t > _samples[i + 1].t) {
        ++inversions;
      }
    }

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
// reconstruct_binary_sample — reconstructs value::Value from flat _data buffer
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

    if (idx >= _data_offsets.size()) {
      return true;
    }

    const uint32_t byte_offset = _data_offsets[idx];
    if (byte_offset == BLOCKED_OFFSET) {
      sample->blocked = true;
      return true;
    }

    // Array reconstruction
    if (_is_array) {
      const size_t count = get_array_count(idx);

#define RECONSTRUCT_ARRAY(TYPE)                                                   \
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

      (void)count;
      RECONSTRUCT_ARRAY(float)
      RECONSTRUCT_ARRAY(double)
      RECONSTRUCT_ARRAY(int32_t)
      RECONSTRUCT_ARRAY(uint32_t)
      RECONSTRUCT_ARRAY(int64_t)
      RECONSTRUCT_ARRAY(uint64_t)
      RECONSTRUCT_ARRAY(value::half)
      RECONSTRUCT_ARRAY(value::half2)
      RECONSTRUCT_ARRAY(value::half3)
      RECONSTRUCT_ARRAY(value::half4)
      RECONSTRUCT_ARRAY(value::float2)
      RECONSTRUCT_ARRAY(value::float3)
      RECONSTRUCT_ARRAY(value::float4)
      RECONSTRUCT_ARRAY(value::double2)
      RECONSTRUCT_ARRAY(value::double3)
      RECONSTRUCT_ARRAY(value::double4)
      RECONSTRUCT_ARRAY(value::int2)
      RECONSTRUCT_ARRAY(value::int3)
      RECONSTRUCT_ARRAY(value::int4)
      RECONSTRUCT_ARRAY(value::quath)
      RECONSTRUCT_ARRAY(value::quatf)
      RECONSTRUCT_ARRAY(value::quatd)
      RECONSTRUCT_ARRAY(value::point3f)
      RECONSTRUCT_ARRAY(value::point3d)
      RECONSTRUCT_ARRAY(value::normal3f)
      RECONSTRUCT_ARRAY(value::normal3d)
      RECONSTRUCT_ARRAY(value::vector3f)
      RECONSTRUCT_ARRAY(value::vector3d)
      RECONSTRUCT_ARRAY(value::color3f)
      RECONSTRUCT_ARRAY(value::color3d)
      RECONSTRUCT_ARRAY(value::color4f)
      RECONSTRUCT_ARRAY(value::color4d)
      RECONSTRUCT_ARRAY(value::texcoord2f)
      RECONSTRUCT_ARRAY(value::texcoord2d)
      RECONSTRUCT_ARRAY(value::texcoord3f)
      RECONSTRUCT_ARRAY(value::texcoord3d)
      RECONSTRUCT_ARRAY(value::matrix2f)
      RECONSTRUCT_ARRAY(value::matrix2d)
      RECONSTRUCT_ARRAY(value::matrix3f)
      RECONSTRUCT_ARRAY(value::matrix3d)
      RECONSTRUCT_ARRAY(value::matrix4f)
      RECONSTRUCT_ARRAY(value::matrix4d)
#undef RECONSTRUCT_ARRAY
      return true;
    }

    // Scalar reconstruction from flat _data buffer
    const uint32_t tid = _type_id;

#define RECONSTRUCT_SCALAR(TYPE) \
    case value::TypeTraits<TYPE>::type_id(): { \
      if (static_cast<size_t>(byte_offset) + sizeof(TYPE) <= _data.size()) { \
        TYPE val; \
        std::memcpy(&val, _data.data() + byte_offset, sizeof(TYPE)); \
        sample->value = value::Value(val); \
      } \
      break; \
    }

    switch (tid) {
      RECONSTRUCT_SCALAR(value::half)
      RECONSTRUCT_SCALAR(float)
      RECONSTRUCT_SCALAR(double)
      RECONSTRUCT_SCALAR(int32_t)
      RECONSTRUCT_SCALAR(uint32_t)
      RECONSTRUCT_SCALAR(int64_t)
      RECONSTRUCT_SCALAR(uint64_t)
      case value::TypeTraits<bool>::type_id(): {
        if (static_cast<size_t>(byte_offset) + 1 <= _data.size()) {
          bool bval = (_data[byte_offset] != 0);
          sample->value = value::Value(bval);
        }
        break;
      }
      RECONSTRUCT_SCALAR(value::float2)
      RECONSTRUCT_SCALAR(value::float3)
      RECONSTRUCT_SCALAR(value::float4)
      RECONSTRUCT_SCALAR(value::double2)
      RECONSTRUCT_SCALAR(value::double3)
      RECONSTRUCT_SCALAR(value::double4)
      RECONSTRUCT_SCALAR(value::int2)
      RECONSTRUCT_SCALAR(value::int3)
      RECONSTRUCT_SCALAR(value::int4)
      RECONSTRUCT_SCALAR(value::half2)
      RECONSTRUCT_SCALAR(value::half3)
      RECONSTRUCT_SCALAR(value::half4)
      RECONSTRUCT_SCALAR(value::quath)
      RECONSTRUCT_SCALAR(value::quatf)
      RECONSTRUCT_SCALAR(value::quatd)
      RECONSTRUCT_SCALAR(value::point3f)
      RECONSTRUCT_SCALAR(value::point3d)
      RECONSTRUCT_SCALAR(value::normal3f)
      RECONSTRUCT_SCALAR(value::normal3d)
      RECONSTRUCT_SCALAR(value::vector3f)
      RECONSTRUCT_SCALAR(value::vector3d)
      RECONSTRUCT_SCALAR(value::color3f)
      RECONSTRUCT_SCALAR(value::color3d)
      RECONSTRUCT_SCALAR(value::color4f)
      RECONSTRUCT_SCALAR(value::color4d)
      RECONSTRUCT_SCALAR(value::texcoord2f)
      RECONSTRUCT_SCALAR(value::texcoord2d)
      RECONSTRUCT_SCALAR(value::texcoord3f)
      RECONSTRUCT_SCALAR(value::texcoord3d)
      RECONSTRUCT_SCALAR(value::matrix2f)
      RECONSTRUCT_SCALAR(value::matrix2d)
      RECONSTRUCT_SCALAR(value::matrix3f)
      RECONSTRUCT_SCALAR(value::matrix3d)
      RECONSTRUCT_SCALAR(value::matrix4f)
      RECONSTRUCT_SCALAR(value::matrix4d)
      default:
        break;
    }
#undef RECONSTRUCT_SCALAR
    return true;
}

bool TimeSamples::add_sample(const Sample &s, std::string *err) {
    if (has_unified_samples()) {
      if (err) {
        (*err) += "add_sample cannot append generic Value samples after unified storage samples.\n";
      }
      return false;
    }

    if (!s.value.is_none()) {
      if (!is_initialized()) {
        set_type_id(s.value.type_id());
      } else if (s.value.type_id() != _type_id) {
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

    if (!v.is_none()) {
      if (!is_initialized()) {
        set_type_id(v.type_id());
      } else if (v.type_id() != _type_id) {
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

    if (!v.is_none() && v.type_id() != 1) {
      if (!is_initialized()) {
        set_type_id(v.type_id());
      } else if (v.type_id() != _type_id) {
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

size_t TimeSamples::estimate_memory_usage() const {
    size_t total = sizeof(TimeSamples);

    total += _times.capacity() * sizeof(double);
    total += _blocked.capacity();
    total += _data.capacity();
    total += _data_offsets.capacity() * sizeof(uint32_t);
    total += _array_counts.capacity() * sizeof(uint32_t);

    total += _samples.capacity() * sizeof(Sample);
    for (const auto &sample : _samples) {
      total += sample.value.estimate_memory_usage();
    }

    return total;
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
        if (!reconstruct_binary_sample(i, &s)) {
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
      _data(std::move(other._data)),
      _data_offsets(std::move(other._data_offsets)),
      _array_counts(std::move(other._array_counts)),
      _type_id(other._type_id),
      _element_size(other._element_size),
      _dirty(other._dirty),
      _is_array(other._is_array) {
  other._type_id = 0;
  other._element_size = 0;
  other._dirty = false;
  other._is_array = false;
}

// Move assignment operator
TimeSamples& TimeSamples::operator=(TimeSamples&& other) noexcept {
  if (this != &other) {
    _samples = std::move(other._samples);
    _times = std::move(other._times);
    _blocked = std::move(other._blocked);
    _data = std::move(other._data);
    _data_offsets = std::move(other._data_offsets);
    _array_counts = std::move(other._array_counts);
    _type_id = other._type_id;
    _element_size = other._element_size;
    _dirty = other._dirty;
    _is_array = other._is_array;

    other._type_id = 0;
    other._element_size = 0;
    other._dirty = false;
    other._is_array = false;
  }
  return *this;
}

// Copy constructor
TimeSamples::TimeSamples(const TimeSamples& other)
    : _samples(other._samples),
      _times(other._times),
      _blocked(other._blocked),
      _data(other._data),
      _data_offsets(other._data_offsets),
      _array_counts(other._array_counts),
      _type_id(other._type_id),
      _element_size(other._element_size),
      _dirty(other._dirty),
      _is_array(other._is_array) {
}

// Copy assignment operator
TimeSamples& TimeSamples::operator=(const TimeSamples& other) {
  if (this != &other) {
    _samples = other._samples;
    _times = other._times;
    _blocked = other._blocked;
    _data = other._data;
    _data_offsets = other._data_offsets;
    _array_counts = other._array_counts;
    _type_id = other._type_id;
    _element_size = other._element_size;
    _dirty = other._dirty;
    _is_array = other._is_array;
  }
  return *this;
}

// clear() method
void TimeSamples::clear() {
  _samples.clear();
  _times.clear();
  _blocked.clear();
  _data.clear();
  _data_offsets.clear();
  _array_counts.clear();
  _type_id = 0;
  _element_size = 0;
  _dirty = true;
  _is_array = false;
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
