// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Time Sample Interpolation
// Provides interpolation for USD time-sampled values

#pragma once

#include "value.hh"
#include <vector>
#include <utility>

namespace tinyusdz {
namespace next {

/// Time sample interpolation mode
/// Note: This is distinct from primvar Interpolation (Constant, Vertex, etc.)
enum class TimeInterpolation : uint8_t {
  Held = 0,   // Step function - use value from previous keyframe
  Linear = 1  // Linear interpolation between keyframes
};

/// Result of time sample lookup/interpolation
struct SampleResult {
  Value value;
  bool success = false;
  bool interpolated = false;  // True if value was interpolated, false if exact match
};

/// Time sample interpolator
/// Provides interpolation for time-sampled values
class TimeInterpolator {
public:
  /// Interpolate a value at the given time from time samples
  /// @param samples Vector of (time, value) pairs, sorted by time
  /// @param time The time to sample at
  /// @param mode Interpolation mode (Held or Linear)
  /// @return Interpolated value result
  static SampleResult Interpolate(
      const std::vector<std::pair<double, Value>>& samples,
      double time,
      TimeInterpolation mode = TimeInterpolation::Linear);

  /// Interpolate using time samples with offset indirection
  /// @param time_offsets Vector of (time, value_offset) pairs
  /// @param get_value Function to get Value from offset
  /// @param time The time to sample at
  /// @param mode Interpolation mode
  /// @return Interpolated value result
  template<typename GetValueFn>
  static SampleResult InterpolateWithOffsets(
      const std::vector<std::pair<double, uint32_t>>& time_offsets,
      GetValueFn get_value,
      double time,
      TimeInterpolation mode = TimeInterpolation::Linear);

  // ============================================================
  // Low-level interpolation functions
  // ============================================================

  /// Lerp two scalar values
  static float LerpFloat(float a, float b, float t);
  static double LerpDouble(double a, double b, double t);

  /// Lerp two vector values (float*)
  static void LerpFloatN(float* out, const float* a, const float* b, float t, size_t n);
  static void LerpDoubleN(double* out, const double* a, const double* b, double t, size_t n);

  /// Slerp two quaternion values (x, y, z, w order)
  static void SlerpQuatf(float* out, const float* a, const float* b, float t);
  static void SlerpQuatd(double* out, const double* a, const double* b, double t);

  /// Interpolate two Value objects
  /// Returns empty Value if types don't match or can't be interpolated
  static Value InterpolateValues(const Value& a, const Value& b, double t);

private:
  /// Find bracket indices for a given time
  /// Returns (lower_idx, upper_idx, is_exact_match)
  static std::tuple<size_t, size_t, bool> FindBracket(
      const std::vector<std::pair<double, uint32_t>>& time_offsets,
      double time);
};

// ============================================================
// Template implementation
// ============================================================

template<typename GetValueFn>
SampleResult TimeInterpolator::InterpolateWithOffsets(
    const std::vector<std::pair<double, uint32_t>>& time_offsets,
    GetValueFn get_value,
    double time,
    TimeInterpolation mode) {

  SampleResult result;

  if (time_offsets.empty()) {
    return result;
  }

  // Single sample case
  if (time_offsets.size() == 1) {
    const Value* v = get_value(time_offsets[0].second);
    if (v) {
      result.value = *v;
      result.success = true;
      result.interpolated = false;
    }
    return result;
  }

  // Find bracket
  auto [lower_idx, upper_idx, is_exact] = FindBracket(time_offsets, time);

  // Exact match - no interpolation needed
  if (is_exact) {
    const Value* v = get_value(time_offsets[lower_idx].second);
    if (v) {
      result.value = *v;
      result.success = true;
      result.interpolated = false;
    }
    return result;
  }

  // Before first sample - use first value
  if (lower_idx == upper_idx && time < time_offsets[0].first) {
    const Value* v = get_value(time_offsets[0].second);
    if (v) {
      result.value = *v;
      result.success = true;
      result.interpolated = false;
    }
    return result;
  }

  // After last sample - use last value
  if (lower_idx == upper_idx && time > time_offsets.back().first) {
    const Value* v = get_value(time_offsets.back().second);
    if (v) {
      result.value = *v;
      result.success = true;
      result.interpolated = false;
    }
    return result;
  }

  // Held interpolation - use lower value
  if (mode == TimeInterpolation::Held) {
    const Value* v = get_value(time_offsets[lower_idx].second);
    if (v) {
      result.value = *v;
      result.success = true;
      result.interpolated = false;
    }
    return result;
  }

  // Linear interpolation
  const Value* v_lower = get_value(time_offsets[lower_idx].second);
  const Value* v_upper = get_value(time_offsets[upper_idx].second);

  if (!v_lower || !v_upper) {
    return result;
  }

  double t_lower = time_offsets[lower_idx].first;
  double t_upper = time_offsets[upper_idx].first;
  double t = (time - t_lower) / (t_upper - t_lower);

  result.value = InterpolateValues(*v_lower, *v_upper, t);
  result.success = !result.value.is_empty();
  result.interpolated = result.success;

  return result;
}

}  // namespace next
}  // namespace tinyusdz
