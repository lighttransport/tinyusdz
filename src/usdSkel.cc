// SPDX-License-Identifier: MIT
// Copyright 2022 - Present, Syoyo Fujita.
//
// UsdSkel API implementations

#include "usdSkel.hh"

#include <sstream>

#include "common-macros.inc"
#include "prim-types.hh"

namespace tinyusdz {
namespace {}  // namespace

bool SkelAnimation::get_blendShapes(std::vector<value::token> *toks) {
  return blendShapes.get_value(toks);
}

bool SkelAnimation::get_joints(std::vector<value::token> *dst) {
  return joints.get_value(dst);
}

bool SkelAnimation::get_blendShapeWeights(
    std::vector<float> *vals, const double t,
    const TimeSampleInterpolationType tinterp) {
  Animatable<std::vector<float>> v;
  if (blendShapeWeights.get_value(&v)) {
    // Evaluate at time `tc` with `tinterp` interpolation
    return v.ts.get(vals, t, tinterp);
  }

  return false;
}

bool SkelAnimation::get_rotations(std::vector<value::quatf> *vals,
                                  const double t,
                                  const TimeSampleInterpolationType tinterp) {
  Animatable<std::vector<value::quatf>> v;
  if (rotations.get_value(&v)) {
    // Evaluate at time `tc` with `tinterp` interpolation
    return v.ts.get(vals, t, tinterp);
  }

  return false;
}

bool SkelAnimation::get_scales(std::vector<value::half3> *vals, const double t,
                               const TimeSampleInterpolationType tinterp) {
  Animatable<std::vector<value::half3>> v;
  if (scales.get_value(&v)) {
    // Evaluate at time `tc` with `tinterp` interpolation
    return v.ts.get(vals, t, tinterp);
  }

  return false;
}

bool SkelAnimation::get_translations(
    std::vector<value::float3> *vals, const double t,
    const TimeSampleInterpolationType tinterp) {
  Animatable<std::vector<value::float3>> v;
  if (translations.get_value(&v)) {
    // Evaluate at time `tc` with `tinterp` interpolation
    return v.ts.get(vals, t, tinterp);
  }

  return false;
}

}  // namespace tinyusdz

