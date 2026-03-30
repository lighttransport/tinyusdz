// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Time Sample Interpolation Implementation

#include "interpolation.hh"
#include "type-id.hh"
#include <cmath>
#include <cstring>

namespace tinyusdz {
namespace next {

// ============================================================
// Scalar interpolation
// ============================================================

float TimeInterpolator::LerpFloat(float a, float b, float t) {
  return a + t * (b - a);
}

double TimeInterpolator::LerpDouble(double a, double b, double t) {
  return a + t * (b - a);
}

// ============================================================
// Vector interpolation
// ============================================================

void TimeInterpolator::LerpFloatN(float* out, const float* a, const float* b, float t, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    out[i] = a[i] + t * (b[i] - a[i]);
  }
}

void TimeInterpolator::LerpDoubleN(double* out, const double* a, const double* b, double t, size_t n) {
  for (size_t i = 0; i < n; ++i) {
    out[i] = a[i] + t * (b[i] - a[i]);
  }
}

// ============================================================
// Quaternion interpolation (Spherical Linear Interpolation)
// ============================================================

void TimeInterpolator::SlerpQuatf(float* out, const float* a, const float* b, float t) {
  // Quaternion format: x, y, z, w

  // Compute dot product
  float dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

  // If dot is negative, negate one quaternion to take shorter path
  float b_sign = 1.0f;
  if (dot < 0.0f) {
    dot = -dot;
    b_sign = -1.0f;
  }

  float s0, s1;

  // If quaternions are very close, use linear interpolation
  if (dot > 0.9995f) {
    s0 = 1.0f - t;
    s1 = t * b_sign;
  } else {
    float theta = std::acos(dot);
    float sin_theta = std::sin(theta);
    s0 = std::sin((1.0f - t) * theta) / sin_theta;
    s1 = std::sin(t * theta) / sin_theta * b_sign;
  }

  out[0] = s0 * a[0] + s1 * b[0];
  out[1] = s0 * a[1] + s1 * b[1];
  out[2] = s0 * a[2] + s1 * b[2];
  out[3] = s0 * a[3] + s1 * b[3];

  // Normalize result
  float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
  if (len > 0.0f) {
    float inv_len = 1.0f / len;
    out[0] *= inv_len;
    out[1] *= inv_len;
    out[2] *= inv_len;
    out[3] *= inv_len;
  }
}

void TimeInterpolator::SlerpQuatd(double* out, const double* a, const double* b, double t) {
  // Quaternion format: x, y, z, w

  // Compute dot product
  double dot = a[0]*b[0] + a[1]*b[1] + a[2]*b[2] + a[3]*b[3];

  // If dot is negative, negate one quaternion to take shorter path
  double b_sign = 1.0;
  if (dot < 0.0) {
    dot = -dot;
    b_sign = -1.0;
  }

  double s0, s1;

  // If quaternions are very close, use linear interpolation
  if (dot > 0.9995) {
    s0 = 1.0 - t;
    s1 = t * b_sign;
  } else {
    double theta = std::acos(dot);
    double sin_theta = std::sin(theta);
    s0 = std::sin((1.0 - t) * theta) / sin_theta;
    s1 = std::sin(t * theta) / sin_theta * b_sign;
  }

  out[0] = s0 * a[0] + s1 * b[0];
  out[1] = s0 * a[1] + s1 * b[1];
  out[2] = s0 * a[2] + s1 * b[2];
  out[3] = s0 * a[3] + s1 * b[3];

  // Normalize result
  double len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2] + out[3]*out[3]);
  if (len > 0.0) {
    double inv_len = 1.0 / len;
    out[0] *= inv_len;
    out[1] *= inv_len;
    out[2] *= inv_len;
    out[3] *= inv_len;
  }
}

// ============================================================
// Value interpolation
// ============================================================

Value TimeInterpolator::InterpolateValues(const Value& a, const Value& b, double t) {
  // Types must match
  if (a.type_id() != b.type_id()) {
    return Value();
  }

  // Both must be same array status
  if (a.is_array() != b.is_array()) {
    return Value();
  }

  TypeId type = a.type_id();
  float tf = static_cast<float>(t);

  // Handle array interpolation
  if (a.is_array()) {
    // Array sizes must match
    if (a.array_size() != b.array_size()) {
      return Value();
    }

    // Handle float arrays
    if (type == TypeId::Float || type == TypeId::Float3 || type == TypeId::Point3f ||
        type == TypeId::Vector3f || type == TypeId::Normal3f || type == TypeId::Color3f) {
      const std::vector<float>* arr_a = a.as_float_array();
      const std::vector<float>* arr_b = b.as_float_array();
      if (arr_a && arr_b && arr_a->size() == arr_b->size()) {
        std::vector<float> result(arr_a->size());
        LerpFloatN(result.data(), arr_a->data(), arr_b->data(), tf, arr_a->size());
        return Value::MakeFloatArray(std::move(result));
      }
    }

    // Handle int arrays - use held interpolation (can't lerp integers meaningfully)
    if (type == TypeId::Int) {
      const std::vector<int32_t>* arr_a = a.as_int_array();
      if (arr_a) {
        return Value::MakeIntArray(*arr_a);
      }
    }

    return Value();
  }

  // Handle scalar/vector types
  switch (type) {
    // Scalar float types
    case TypeId::Float: {
      const float* va = a.as_float();
      const float* vb = b.as_float();
      if (va && vb) {
        return Value(LerpFloat(*va, *vb, tf));
      }
      break;
    }

    case TypeId::Double: {
      const double* va = a.as_double();
      const double* vb = b.as_double();
      if (va && vb) {
        return Value(LerpDouble(*va, *vb, t));
      }
      break;
    }

    // Integer types - use held (can't interpolate meaningfully)
    case TypeId::Int:
    case TypeId::UInt:
    case TypeId::Int64:
    case TypeId::UInt64:
    case TypeId::Bool:
      return Value(a);  // Return copy of 'a' (held)

    // Float2
    case TypeId::Float2:
    case TypeId::Texcoord2f: {
      const float* va = a.as_float2();
      const float* vb = b.as_float2();
      if (va && vb) {
        float result[2];
        LerpFloatN(result, va, vb, tf, 2);
        return Value::MakeFloat2(result[0], result[1]);
      }
      break;
    }

    // Float3
    case TypeId::Float3:
    case TypeId::Point3f:
    case TypeId::Vector3f:
    case TypeId::Normal3f:
    case TypeId::Color3f: {
      const float* va = a.as_float3();
      const float* vb = b.as_float3();
      if (va && vb) {
        float result[3];
        LerpFloatN(result, va, vb, tf, 3);
        if (type == TypeId::Point3f) {
          return Value::MakePoint3f(result[0], result[1], result[2]);
        } else if (type == TypeId::Vector3f) {
          return Value::MakeVector3f(result[0], result[1], result[2]);
        } else if (type == TypeId::Normal3f) {
          return Value::MakeNormal3f(result[0], result[1], result[2]);
        } else if (type == TypeId::Color3f) {
          return Value::MakeColor3f(result[0], result[1], result[2]);
        }
        return Value::MakeFloat3(result[0], result[1], result[2]);
      }
      break;
    }

    // Float4
    case TypeId::Float4:
    case TypeId::Color4f: {
      const float* va = a.as_float4();
      const float* vb = b.as_float4();
      if (va && vb) {
        float result[4];
        LerpFloatN(result, va, vb, tf, 4);
        if (type == TypeId::Color4f) {
          return Value::MakeColor4f(result[0], result[1], result[2], result[3]);
        }
        return Value::MakeFloat4(result[0], result[1], result[2], result[3]);
      }
      break;
    }

    // Double2
    case TypeId::Double2:
    case TypeId::Texcoord2d: {
      const double* va = a.as_double2();
      const double* vb = b.as_double2();
      if (va && vb) {
        double result[2];
        LerpDoubleN(result, va, vb, t, 2);
        return Value::MakeDouble2(result[0], result[1]);
      }
      break;
    }

    // Double3
    case TypeId::Double3:
    case TypeId::Point3d:
    case TypeId::Vector3d:
    case TypeId::Normal3d: {
      const double* va = a.as_double3();
      const double* vb = b.as_double3();
      if (va && vb) {
        double result[3];
        LerpDoubleN(result, va, vb, t, 3);
        if (type == TypeId::Point3d) {
          return Value::MakePoint3d(result[0], result[1], result[2]);
        } else if (type == TypeId::Vector3d) {
          return Value::MakeVector3d(result[0], result[1], result[2]);
        } else if (type == TypeId::Normal3d) {
          return Value::MakeNormal3d(result[0], result[1], result[2]);
        }
        return Value::MakeDouble3(result[0], result[1], result[2]);
      }
      break;
    }

    // Double4
    case TypeId::Double4: {
      const double* va = a.as_double4();
      const double* vb = b.as_double4();
      if (va && vb) {
        double result[4];
        LerpDoubleN(result, va, vb, t, 4);
        return Value::MakeDouble4(result[0], result[1], result[2], result[3]);
      }
      break;
    }

    // Quaternions - use slerp
    case TypeId::Quatf: {
      const float* va = a.as_float4();  // Quat stored as 4 floats
      const float* vb = b.as_float4();
      if (va && vb) {
        float result[4];
        SlerpQuatf(result, va, vb, tf);
        return Value::MakeQuatf(result[0], result[1], result[2], result[3]);
      }
      break;
    }

    case TypeId::Quatd: {
      const double* va = a.as_double4();  // Quat stored as 4 doubles
      const double* vb = b.as_double4();
      if (va && vb) {
        double result[4];
        SlerpQuatd(result, va, vb, t);
        return Value::MakeQuatd(result[0], result[1], result[2], result[3]);
      }
      break;
    }

    // Matrix4f - linear interpolation of elements
    // Note: For proper rotation interpolation, should decompose to TRS
    case TypeId::Matrix4f: {
      const float* va = a.as_matrix4f();
      const float* vb = b.as_matrix4f();
      if (va && vb) {
        float result[16];
        LerpFloatN(result, va, vb, tf, 16);
        return Value::MakeMatrix4f(result);
      }
      break;
    }

    // Matrix4d
    case TypeId::Matrix4d: {
      const double* va = a.as_matrix4d();
      const double* vb = b.as_matrix4d();
      if (va && vb) {
        double result[16];
        LerpDoubleN(result, va, vb, t, 16);
        return Value::MakeMatrix4d(result);
      }
      break;
    }

    // Matrix3f
    case TypeId::Matrix3f: {
      const float* va = a.as_matrix3f();
      const float* vb = b.as_matrix3f();
      if (va && vb) {
        float result[9];
        LerpFloatN(result, va, vb, tf, 9);
        return Value::MakeMatrix3f(result);
      }
      break;
    }

    // Matrix3d
    case TypeId::Matrix3d: {
      const double* va = a.as_matrix3d();
      const double* vb = b.as_matrix3d();
      if (va && vb) {
        double result[9];
        LerpDoubleN(result, va, vb, t, 9);
        return Value::MakeMatrix3d(result);
      }
      break;
    }

    // String types - use held (can't interpolate)
    case TypeId::String:
    case TypeId::Token:
    case TypeId::AssetPath:
      return Value(a);

    default:
      break;
  }

  return Value();
}

// ============================================================
// Bracket finding
// ============================================================

std::tuple<size_t, size_t, bool> TimeInterpolator::FindBracket(
    const std::vector<std::pair<double, uint32_t>>& time_offsets,
    double time) {

  if (time_offsets.empty()) {
    return {0, 0, false};
  }

  // Check if before first sample
  if (time <= time_offsets.front().first) {
    bool exact = (time == time_offsets.front().first);
    return {0, 0, exact};
  }

  // Check if after last sample
  if (time >= time_offsets.back().first) {
    size_t last = time_offsets.size() - 1;
    bool exact = (time == time_offsets.back().first);
    return {last, last, exact};
  }

  // Binary search for bracket
  size_t low = 0;
  size_t high = time_offsets.size() - 1;

  while (low < high - 1) {
    size_t mid = (low + high) / 2;
    double mid_time = time_offsets[mid].first;

    if (time == mid_time) {
      return {mid, mid, true};
    } else if (time < mid_time) {
      high = mid;
    } else {
      low = mid;
    }
  }

  // Check for exact match at boundaries
  if (time == time_offsets[low].first) {
    return {low, low, true};
  }
  if (time == time_offsets[high].first) {
    return {high, high, true};
  }

  return {low, high, false};
}

// ============================================================
// High-level interpolation with direct Value vector
// ============================================================

SampleResult TimeInterpolator::Interpolate(
    const std::vector<std::pair<double, Value>>& samples,
    double time,
    TimeInterpolation mode) {

  SampleResult result;

  if (samples.empty()) {
    return result;
  }

  // Single sample
  if (samples.size() == 1) {
    result.value = samples[0].second;
    result.success = true;
    result.interpolated = false;
    return result;
  }

  // Find bracket
  size_t low = 0;
  size_t high = samples.size() - 1;

  // Before first
  if (time <= samples.front().first) {
    result.value = samples.front().second;
    result.success = true;
    result.interpolated = false;
    return result;
  }

  // After last
  if (time >= samples.back().first) {
    result.value = samples.back().second;
    result.success = true;
    result.interpolated = false;
    return result;
  }

  // Binary search
  while (low < high - 1) {
    size_t mid = (low + high) / 2;
    if (time == samples[mid].first) {
      result.value = samples[mid].second;
      result.success = true;
      result.interpolated = false;
      return result;
    } else if (time < samples[mid].first) {
      high = mid;
    } else {
      low = mid;
    }
  }

  // Held interpolation
  if (mode == TimeInterpolation::Held) {
    result.value = samples[low].second;
    result.success = true;
    result.interpolated = false;
    return result;
  }

  // Linear interpolation
  double t_low = samples[low].first;
  double t_high = samples[high].first;
  double t = (time - t_low) / (t_high - t_low);

  result.value = InterpolateValues(samples[low].second, samples[high].second, t);
  result.success = !result.value.is_empty();
  result.interpolated = result.success;

  return result;
}

}  // namespace next
}  // namespace tinyusdz
