// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

///
/// @file render-animation-converter.cc
/// @brief Animation conversion functions for RenderSceneConverter
///

#include "render-data.hh"

#include <cmath>
#include <cstring>
#include <functional>
#include <limits>

#include "common-utils.hh"
#include "pprinter.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "usdSkel.hh"
#include "value-types.hh"
#include "xform.hh"

#include "common-macros.inc"
#include "scene-access.hh"
#include "attribute-eval.hh"

// Helper macros for iterating over TypedTimeSamples in both AoS and SoA modes
#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA
#define FOREACH_TIMESAMPLES_BEGIN(ts, var_t, var_value, var_blocked) \
  { \
    const auto &_times = (ts).get_times(); \
    const auto &_values = (ts).get_values(); \
    const auto &_blocked = (ts).get_blocked(); \
    for (size_t _idx = 0; _idx < _times.size(); _idx++) { \
      const double var_t = _times[_idx]; \
      const auto &var_value = _values[_idx]; \
      const bool var_blocked = _blocked[_idx]; \
      if (!var_blocked) {

#define FOREACH_TIMESAMPLES_END() \
      } \
    } \
  }

#define TIMESAMPLES_EMPTY(ts) ((ts).size() == 0)

#else
#define FOREACH_TIMESAMPLES_BEGIN(ts, var_t, var_value, var_blocked) \
  for (const auto &_sample : (ts).get_samples()) { \
    const double var_t = _sample.t; \
    const auto &var_value = _sample.value; \
    const bool var_blocked = _sample.blocked; \
    if (!var_blocked) {

#define FOREACH_TIMESAMPLES_END() \
    } \
  }

#endif

namespace tinyusdz {
namespace tydra {

namespace {

// Helper function: Quaternion multiplication
// q1 * q2
static value::quatf quat_mul(const value::quatf &q1, const value::quatf &q2) {
  value::quatf result;
  result[0] = q1[3] * q2[0] + q1[0] * q2[3] + q1[1] * q2[2] - q1[2] * q2[1];  // x
  result[1] = q1[3] * q2[1] - q1[0] * q2[2] + q1[1] * q2[3] + q1[2] * q2[0];  // y
  result[2] = q1[3] * q2[2] + q1[0] * q2[1] - q1[1] * q2[0] + q1[2] * q2[3];  // z
  result[3] = q1[3] * q2[3] - q1[0] * q2[0] - q1[1] * q2[1] - q1[2] * q2[2];  // w
  return result;
}

}  // namespace

bool RenderSceneConverter::ConvertSkelAnimation(const RenderSceneConverterEnv &env,
                                            const Path &abs_path,
                                            const SkelAnimation &skelAnim,
                                            int32_t skeleton_id,
                                            AnimationClip *anim_out) {
  // The spec says:
  // "An animation source is only valid if its translation, rotation, and scale components
  //  are all authored, storing arrays sized to the same size as the authored joints array."
  //
  // Convert USD SkelAnimation to glTF/Three.js compatible AnimationClip structure
  // with flat sampler arrays and channel bindings

  std::vector<value::token> joints;

  if (skelAnim.joints.authored()) {
    if (!EvaluateTypedAttribute(env.stage, skelAnim.joints, "joints", &joints, &_err)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to evaluate `joints` in SkelAnimation Prim : {}", abs_path));
    }

    if (!skelAnim.rotations.authored() ||
        !skelAnim.translations.authored() ||
        !skelAnim.scales.authored()) {

      PUSH_ERROR_AND_RETURN(fmt::format("`translations`, `rotations` and `scales` must be all authored for SkelAnimation Prim {}. authored flags: translations {}, rotations {}, scales {}", abs_path, skelAnim.translations.authored() ? "yes" : "no",
      skelAnim.rotations.authored() ? "yes" : "no",
      skelAnim.scales.authored() ? "yes" : "no"));
    }
  }

  // TODO: inbetweens BlendShape
  std::vector<value::token> blendShapes;
  if (skelAnim.blendShapes.authored()) {
    if (!EvaluateTypedAttribute(env.stage, skelAnim.blendShapes, "blendShapes", &blendShapes, &_err)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to evaluate `blendShapes` in SkelAnimation Prim : {}", abs_path));
    }

    if (!skelAnim.blendShapeWeights.authored()) {
      PUSH_ERROR_AND_RETURN(fmt::format("`blendShapeWeights` must be authored for SkelAnimation Prim {}", abs_path));
    }
  }

  // Setup basic metadata
  anim_out->abs_path = abs_path.full_path_name();
  anim_out->prim_name = skelAnim.name;
  anim_out->name = skelAnim.name;
  anim_out->display_name = skelAnim.metas().has_displayName() ? skelAnim.metas().get_displayName() : "";
  anim_out->duration = 0.0f;  // Will be computed below

  // Joint animations - convert to glTF-style flat arrays
  if (joints.size()) {
    Animatable<std::vector<value::float3>> translations;
    if (!skelAnim.translations.get_value(&translations)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `translations` attribute of SkelAnimation: {}", abs_path));
    }

    Animatable<std::vector<value::quatf>> rotations;
    if (!skelAnim.rotations.get_value(&rotations)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `rotations` attribute of SkelAnimation: {}", abs_path));
    }

    Animatable<std::vector<value::half3>> scales;
    if (!skelAnim.scales.get_value(&scales)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `scales` attribute of SkelAnimation: {}", abs_path));
    }

    // Extract timesamples for each animation type
    std::vector<double> translation_times, rotation_times, scale_times;
    std::vector<std::vector<value::float3>> translation_samples;
    std::vector<std::vector<value::quatf>> rotation_samples;
    std::vector<std::vector<value::half3>> scale_samples;

    if (translations.has_timesamples()) {
      const TypedTimeSamples<std::vector<value::float3>> &ts_txs = translations.get_timesamples();
      FOREACH_TIMESAMPLES_BEGIN(ts_txs, sample_t, sample_value, sample_blocked)
        if (sample_value.size() != joints.size()) {
          PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: translations[{}].size {} != joints.size {} at time {}",
            translation_times.size(), sample_value.size(), joints.size(), sample_t));
        }
        translation_times.push_back(sample_t);
        translation_samples.push_back(sample_value);
        if (float(sample_t) > anim_out->duration) anim_out->duration = float(sample_t);
      FOREACH_TIMESAMPLES_END()
    } else if (translations.has_value()) {
      // Handle static (non-time-sampled) values as a single keyframe at time 0.0
      std::vector<value::float3> default_value;
      if (!translations.get_scalar(&default_value)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default value for translations in SkelAnimation: {}", abs_path));
      }
      if (default_value.size() != joints.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: translations.size {} != joints.size {}",
          default_value.size(), joints.size()));
      }
      translation_times.push_back(0.0);
      translation_samples.push_back(default_value);
    }

    if (rotations.has_timesamples()) {
      const TypedTimeSamples<std::vector<value::quatf>> &ts_rots = rotations.get_timesamples();
      FOREACH_TIMESAMPLES_BEGIN(ts_rots, sample_t, sample_value, sample_blocked)
        if (sample_value.size() != joints.size()) {
          PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: rotations[{}].size {} != joints.size {} at time {}",
            rotation_times.size(), sample_value.size(), joints.size(), sample_t));
        }
        rotation_times.push_back(sample_t);
        rotation_samples.push_back(sample_value);
        if (float(sample_t) > anim_out->duration) anim_out->duration = float(sample_t);
      FOREACH_TIMESAMPLES_END()
    } else if (rotations.has_value()) {
      // Handle static (non-time-sampled) values as a single keyframe at time 0.0
      std::vector<value::quatf> default_value;
      if (!rotations.get_scalar(&default_value)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default value for rotations in SkelAnimation: {}", abs_path));
      }
      if (default_value.size() != joints.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: rotations.size {} != joints.size {}",
          default_value.size(), joints.size()));
      }
      rotation_times.push_back(0.0);
      rotation_samples.push_back(default_value);
    }

    if (scales.has_timesamples()) {
      const TypedTimeSamples<std::vector<value::half3>> &ts_scales = scales.get_timesamples();
      FOREACH_TIMESAMPLES_BEGIN(ts_scales, sample_t, sample_value, sample_blocked)
        if (sample_value.size() != joints.size()) {
          PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: scales[{}].size {} != joints.size {} at time {}",
            scale_times.size(), sample_value.size(), joints.size(), sample_t));
        }
        scale_times.push_back(sample_t);
        scale_samples.push_back(sample_value);
        if (float(sample_t) > anim_out->duration) anim_out->duration = float(sample_t);
      FOREACH_TIMESAMPLES_END()
    } else if (scales.has_value()) {
      // Handle static (non-time-sampled) values as a single keyframe at time 0.0
      std::vector<value::half3> default_value;
      if (!scales.get_scalar(&default_value)) {
        PUSH_ERROR_AND_RETURN(fmt::format("Failed to get default value for scales in SkelAnimation: {}", abs_path));
      }
      if (default_value.size() != joints.size()) {
        PUSH_ERROR_AND_RETURN(fmt::format("Array length mismatch: scales.size {} != joints.size {}",
          default_value.size(), joints.size()));
      }
      scale_times.push_back(0.0);
      scale_samples.push_back(default_value);
    }

    // Create glTF-style samplers and channels for each joint
    // Note: This creates one sampler per joint per property (not optimal but simple)
    // TODO: Optimize to share samplers when possible

    for (size_t joint_idx = 0; joint_idx < joints.size(); joint_idx++) {
      // Translation sampler and channel
      if (!translation_times.empty()) {
        KeyframeSampler trans_sampler;
        trans_sampler.times.reserve(translation_times.size());
        trans_sampler.values.reserve(translation_times.size() * 3);
        trans_sampler.interpolation = AnimationInterpolation::Linear;

        for (size_t t = 0; t < translation_times.size(); t++) {
          trans_sampler.times.push_back(float(translation_times[t]));
          const auto &v = translation_samples[t][joint_idx];
          trans_sampler.values.push_back(v[0]);
          trans_sampler.values.push_back(v[1]);
          trans_sampler.values.push_back(v[2]);
        }

        int32_t sampler_idx = int32_t(anim_out->samplers.size());
        anim_out->samplers.push_back(trans_sampler);

        AnimationChannel channel;
        channel.target_type = ChannelTargetType::SkeletonJoint;
        channel.path = AnimationPath::Translation;
        channel.skeleton_id = skeleton_id;
        channel.joint_id = int32_t(joint_idx);
        channel.sampler = sampler_idx;
        anim_out->channels.push_back(channel);
      }

      // Rotation sampler and channel
      if (!rotation_times.empty()) {
        KeyframeSampler rot_sampler;
        rot_sampler.times.reserve(rotation_times.size());
        rot_sampler.values.reserve(rotation_times.size() * 4);
        rot_sampler.interpolation = AnimationInterpolation::Linear;

        for (size_t t = 0; t < rotation_times.size(); t++) {
          rot_sampler.times.push_back(float(rotation_times[t]));
          const auto &q = rotation_samples[t][joint_idx];
          rot_sampler.values.push_back(q[0]);
          rot_sampler.values.push_back(q[1]);
          rot_sampler.values.push_back(q[2]);
          rot_sampler.values.push_back(q[3]);
        }

        int32_t sampler_idx = int32_t(anim_out->samplers.size());
        anim_out->samplers.push_back(rot_sampler);

        AnimationChannel channel;
        channel.target_type = ChannelTargetType::SkeletonJoint;
        channel.path = AnimationPath::Rotation;
        channel.skeleton_id = skeleton_id;
        channel.joint_id = int32_t(joint_idx);
        channel.sampler = sampler_idx;
        anim_out->channels.push_back(channel);
      }

      // Scale sampler and channel
      if (!scale_times.empty()) {
        KeyframeSampler scale_sampler;
        scale_sampler.times.reserve(scale_times.size());
        scale_sampler.values.reserve(scale_times.size() * 3);
        scale_sampler.interpolation = AnimationInterpolation::Linear;

        for (size_t t = 0; t < scale_times.size(); t++) {
          scale_sampler.times.push_back(float(scale_times[t]));
          const auto &v = scale_samples[t][joint_idx];
          scale_sampler.values.push_back(value::half_to_float(v[0]));
          scale_sampler.values.push_back(value::half_to_float(v[1]));
          scale_sampler.values.push_back(value::half_to_float(v[2]));
        }

        int32_t sampler_idx = int32_t(anim_out->samplers.size());
        anim_out->samplers.push_back(scale_sampler);

        AnimationChannel channel;
        channel.target_type = ChannelTargetType::SkeletonJoint;
        channel.path = AnimationPath::Scale;
        channel.skeleton_id = skeleton_id;
        channel.joint_id = int32_t(joint_idx);
        channel.sampler = sampler_idx;
        anim_out->channels.push_back(channel);
      }
    }
  }

  // BlendShape animations
  if (blendShapes.size()) {
    Animatable<std::vector<float>> weights;
    if (!skelAnim.blendShapeWeights.get_value(&weights)) {
      PUSH_ERROR_AND_RETURN(fmt::format("Failed to get `blendShapeWeights` attribute: {}", abs_path));
    }

    if (weights.has_timesamples()) {
      const TypedTimeSamples<std::vector<float>> &ts_weights = weights.get_timesamples();

      std::vector<double> weight_times;
      std::vector<std::vector<float>> weight_samples;

      FOREACH_TIMESAMPLES_BEGIN(ts_weights, sample_t, sample_value, sample_blocked)
        if (sample_value.size() != blendShapes.size()) {
          PUSH_ERROR_AND_RETURN(fmt::format("blendShapeWeights size mismatch at time {}: {} != {}",
            sample_t, sample_value.size(), blendShapes.size()));
        }
        weight_times.push_back(sample_t);
        weight_samples.push_back(sample_value);
        if (float(sample_t) > anim_out->duration) anim_out->duration = float(sample_t);
      FOREACH_TIMESAMPLES_END()

      // Create one sampler with all weights (glTF morph targets style)
      if (!weight_times.empty()) {
        KeyframeSampler weight_sampler;
        weight_sampler.times.reserve(weight_times.size());
        weight_sampler.values.reserve(weight_times.size() * blendShapes.size());
        weight_sampler.interpolation = AnimationInterpolation::Linear;

        for (size_t t = 0; t < weight_times.size(); t++) {
          weight_sampler.times.push_back(float(weight_times[t]));
          for (size_t w = 0; w < blendShapes.size(); w++) {
            weight_sampler.values.push_back(weight_samples[t][w]);
          }
        }

        int32_t sampler_idx = int32_t(anim_out->samplers.size());
        anim_out->samplers.push_back(weight_sampler);

        AnimationChannel channel;
        channel.path = AnimationPath::Weights;
        channel.target_node = -1;  // Weights target the mesh, not a specific node
        channel.sampler = sampler_idx;
        anim_out->channels.push_back(channel);
      }
    }
  }

  return true;
}

bool RenderSceneConverter::ExtractXformOpAnimation(
    const RenderSceneConverterEnv &env,
    const Path &abs_path,
    const std::string &prim_name,
    const Xformable &xformable,
    int32_t target_node_index,
    AnimationClip *anim_out) {

  (void)env;  // Unused parameter

  if (!anim_out) {
    PUSH_ERROR_AND_RETURN("anim_out is nullptr");
  }

  // Check if xformable has any animated xformOps
  if (!xformable.has_timesamples()) {
    return false;  // No animation data
  }

  // Setup basic metadata
  anim_out->abs_path = abs_path.full_path_name();
  anim_out->prim_name = prim_name;
  anim_out->name = prim_name + "_xform";
  anim_out->duration = 0.0f;  // Will be computed below

  // Process each xformOp that has time samples
  for (size_t xform_idx = 0; xform_idx < xformable.xformOps.size(); xform_idx++) {
    const XformOp &xformOp = xformable.xformOps[xform_idx];

    if (xformOp.op_type == XformOp::OpType::ResetXformStack) {
      continue;  // Skip reset operations
    }

    if (!xformOp.has_timesamples()) {
      continue;  // Skip non-animated ops
    }

    // Get the time samples
    auto ts_opt = xformOp.get_timesamples();
    if (!ts_opt) {
      continue;
    }

    const value::TimeSamples &ts = ts_opt.value();
    if (ts.size() == 0) {
      continue;
    }

    // Determine the animation path based on xformOp type
    AnimationPath anim_path = AnimationPath::Translation;  // Default initialization
    bool is_supported = false;

    switch (xformOp.op_type) {
      case XformOp::OpType::Translate:
        anim_path = AnimationPath::Translation;
        is_supported = true;
        break;

      case XformOp::OpType::Scale:
        anim_path = AnimationPath::Scale;
        is_supported = true;
        break;

      case XformOp::OpType::Orient:
        anim_path = AnimationPath::Rotation;
        is_supported = true;
        break;

      case XformOp::OpType::RotateX:
      case XformOp::OpType::RotateY:
      case XformOp::OpType::RotateZ:
      case XformOp::OpType::RotateXYZ:
      case XformOp::OpType::RotateXZY:
      case XformOp::OpType::RotateYXZ:
      case XformOp::OpType::RotateYZX:
      case XformOp::OpType::RotateZXY:
      case XformOp::OpType::RotateZYX:
        anim_path = AnimationPath::Rotation;
        is_supported = true;
        break;

      case XformOp::OpType::Transform:
        // Full matrix transform - decompose into TRS
        // We'll handle this specially below since it produces multiple animation channels
        is_supported = true;
        break;

      case XformOp::OpType::ResetXformStack:
        // Not animatable - skip
        is_supported = false;
        break;
    }

    if (!is_supported) {
      continue;
    }

    // Build sampler data
    KeyframeSampler sampler;
    sampler.interpolation = AnimationInterpolation::Linear;

    if (xformOp.op_type == XformOp::OpType::Translate ||
        xformOp.op_type == XformOp::OpType::Scale) {
      // Extract vec3 values
      std::vector<double> times;
      std::vector<value::float3> values;

      FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
        if (sample_blocked) {
          continue;
        }

        value::float3 v;
        bool got_value = false;

        if (auto vf = sample_value.as<value::float3>()) {
          v = *vf;
          got_value = true;
        } else if (auto vd = sample_value.as<value::double3>()) {
          v[0] = float((*vd)[0]);
          v[1] = float((*vd)[1]);
          v[2] = float((*vd)[2]);
          got_value = true;
        } else if (auto vh = sample_value.as<value::half3>()) {
          v[0] = value::half_to_float((*vh)[0]);
          v[1] = value::half_to_float((*vh)[1]);
          v[2] = value::half_to_float((*vh)[2]);
          got_value = true;
        }

        if (got_value) {
          times.push_back(sample_t);
          values.push_back(v);
          if (float(sample_t) > anim_out->duration) {
            anim_out->duration = float(sample_t);
          }
        }
      FOREACH_TIMESAMPLES_END()

      // Build sampler data
      sampler.times.reserve(times.size());
      sampler.values.reserve(times.size() * 3);

      for (size_t i = 0; i < times.size(); i++) {
        sampler.times.push_back(float(times[i]));
        sampler.values.push_back(values[i][0]);
        sampler.values.push_back(values[i][1]);
        sampler.values.push_back(values[i][2]);
      }

    } else if (xformOp.op_type == XformOp::OpType::Orient) {
      // Extract quaternion values directly
      std::vector<double> times;
      std::vector<value::quatf> values;

      FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
        if (sample_blocked) {
          continue;
        }

        value::quatf q;
        bool got_value = false;

        if (auto qf = sample_value.as<value::quatf>()) {
          q = *qf;
          got_value = true;
        } else if (auto qd = sample_value.as<value::quatd>()) {
          q[0] = float((*qd)[0]);
          q[1] = float((*qd)[1]);
          q[2] = float((*qd)[2]);
          q[3] = float((*qd)[3]);
          got_value = true;
        } else if (auto qh = sample_value.as<value::quath>()) {
          q[0] = value::half_to_float((*qh)[0]);
          q[1] = value::half_to_float((*qh)[1]);
          q[2] = value::half_to_float((*qh)[2]);
          q[3] = value::half_to_float((*qh)[3]);
          got_value = true;
        }

        if (got_value) {
          times.push_back(sample_t);
          values.push_back(q);
          if (float(sample_t) > anim_out->duration) {
            anim_out->duration = float(sample_t);
          }
        }
      FOREACH_TIMESAMPLES_END()

      // Build sampler data for quaternions
      sampler.times.reserve(times.size());
      sampler.values.reserve(times.size() * 4);

      for (size_t i = 0; i < times.size(); i++) {
        sampler.times.push_back(float(times[i]));
        sampler.values.push_back(values[i][0]);
        sampler.values.push_back(values[i][1]);
        sampler.values.push_back(values[i][2]);
        sampler.values.push_back(values[i][3]);
      }

    } else if (xformOp.op_type >= XformOp::OpType::RotateX &&
               xformOp.op_type <= XformOp::OpType::RotateZYX) {
      // Rotation operations - need to convert to quaternions
      std::vector<double> times;
      std::vector<value::quatf> values;

      // For single-axis rotations (RotateX, RotateY, RotateZ)
      if (xformOp.op_type == XformOp::OpType::RotateX ||
          xformOp.op_type == XformOp::OpType::RotateY ||
          xformOp.op_type == XformOp::OpType::RotateZ) {

        value::double3 axis{0.0, 0.0, 0.0};
        if (xformOp.op_type == XformOp::OpType::RotateX) axis[0] = 1.0;
        else if (xformOp.op_type == XformOp::OpType::RotateY) axis[1] = 1.0;
        else axis[2] = 1.0;

        std::vector<double> angle_times;
        std::vector<double> angle_values;

        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }

          double angle = 0.0;
          bool got_value = false;

          if (auto vf = sample_value.as<float>()) {
            angle = double(*vf);
            got_value = true;
          } else if (auto vd = sample_value.as<double>()) {
            angle = *vd;
            got_value = true;
          } else if (auto vh = sample_value.as<value::half>()) {
            angle = double(value::half_to_float(*vh));
            got_value = true;
          }

          if (got_value) {
            angle_times.push_back(sample_t);
            angle_values.push_back(angle);
            if (float(sample_t) > anim_out->duration) {
              anim_out->duration = float(sample_t);
            }
          }
        FOREACH_TIMESAMPLES_END()

        for (size_t i = 0; i < angle_times.size(); i++) {
          times.push_back(angle_times[i]);
          values.push_back(to_quaternion(value::float3{float(axis[0]), float(axis[1]), float(axis[2])},
                                        float(angle_values[i])));
        }

      } else {
        // Multi-axis rotation (vec3 of angles)
        // For RotateXYZ and similar, we need to compute the combined quaternion
        std::vector<double> angle_times;
        std::vector<value::double3> euler_angles;

        FOREACH_TIMESAMPLES_BEGIN(ts, sample_t, sample_value, sample_blocked)
          if (sample_blocked) {
            continue;
          }

          value::double3 angles;
          bool got_value = false;

          if (auto v = sample_value.as<value::float3>()) {
            angles[0] = double((*v)[0]);
            angles[1] = double((*v)[1]);
            angles[2] = double((*v)[2]);
            got_value = true;
          } else if (auto vd = sample_value.as<value::double3>()) {
            angles = *vd;
            got_value = true;
          } else if (auto vh = sample_value.as<value::half3>()) {
            angles[0] = double(value::half_to_float((*vh)[0]));
            angles[1] = double(value::half_to_float((*vh)[1]));
            angles[2] = double(value::half_to_float((*vh)[2]));
            got_value = true;
          }

          if (got_value) {
            angle_times.push_back(sample_t);
            euler_angles.push_back(angles);
            if (float(sample_t) > anim_out->duration) {
              anim_out->duration = float(sample_t);
            }
          }
        FOREACH_TIMESAMPLES_END()

        // Convert Euler angles to quaternions based on rotation order
        // Note: This is a simplified conversion; proper implementation would use matrix composition
        for (size_t i = 0; i < angle_times.size(); i++) {
          times.push_back(angle_times[i]);

          // For now, convert XYZ order (most common)
          // TODO: Support other rotation orders properly
          const auto &angles = euler_angles[i];
          value::quatf qx = to_quaternion(value::float3{1.0f, 0.0f, 0.0f}, float(angles[0]));
          value::quatf qy = to_quaternion(value::float3{0.0f, 1.0f, 0.0f}, float(angles[1]));
          value::quatf qz = to_quaternion(value::float3{0.0f, 0.0f, 1.0f}, float(angles[2]));

          // Combine quaternions based on rotation order
          // For XYZ: qz * qy * qx
          value::quatf combined = quat_mul(quat_mul(qz, qy), qx);
          values.push_back(combined);
        }
      }

      // Build sampler data for rotations (quaternions)
      if (!times.empty()) {
        sampler.times.reserve(times.size());
        sampler.values.reserve(times.size() * 4);

        for (size_t i = 0; i < times.size(); i++) {
          sampler.times.push_back(float(times[i]));
          sampler.values.push_back(values[i][0]);
          sampler.values.push_back(values[i][1]);
          sampler.values.push_back(values[i][2]);
          sampler.values.push_back(values[i][3]);
        }
      }
    }

    // Only add if we have valid sampler data
    if (!sampler.times.empty()) {
      int32_t sampler_idx = int32_t(anim_out->samplers.size());
      anim_out->samplers.push_back(sampler);

      AnimationChannel channel;
      channel.target_type = ChannelTargetType::SceneNode;
      channel.path = anim_path;
      channel.target_node = target_node_index;
      channel.sampler = sampler_idx;
      anim_out->channels.push_back(channel);
    }
  }

  // Return true if we extracted any animation data
  return !anim_out->channels.empty();
}

bool RenderSceneConverter::ConvertSkeletonImpl(const RenderSceneConverterEnv &env, const tinyusdz::GeomMesh &mesh,
                       int32_t skeleton_id,
                       SkelHierarchy *out_skel, nonstd::optional<AnimationClip> *out_anim) {

  if (!out_skel) {
    return false;
  }

  Path skelPath;

  // Get skeleton path from mesh.skeleton relationship if available
  if (mesh.skeleton.authored()) {
    if (mesh.skeleton.relationship().is_path()) {
      skelPath = mesh.skeleton.relationship().targetPath;
    } else if (mesh.skeleton.relationship().is_pathvector()) {
      // Use the first one
      if (mesh.skeleton.relationship().targetPathVector.size()) {
        skelPath = mesh.skeleton.relationship().targetPathVector[0];
      } else {
        PUSH_WARN("`skel:skeleton` has invalid definition.");
      }
    } else {
      PUSH_WARN("`skel:skeleton` has invalid definition.");
    }
  }

  // If no skeleton path from relationship, return false (caller should use overload with explicit path)
  if (!skelPath.is_valid()) {
    PUSH_ERROR_AND_RETURN("No valid skeleton path found. Use ConvertSkeletonImplWithPath for ancestor-discovered skeletons.");
  }

  return ConvertSkeletonImplWithPath(env, skelPath, skeleton_id, out_skel, out_anim);
}

// Helper function that takes skeleton pointer directly (used for ancestor-discovered skeletons)
bool RenderSceneConverter::ConvertSkeletonFromPtr(const RenderSceneConverterEnv &env,
                       const Path &skelPath,
                       const Skeleton &skel,
                       const std::string &primName,
                       int32_t skeleton_id,
                       SkelHierarchy *out_skel, nonstd::optional<AnimationClip> *out_anim) {

  if (!out_skel) {
    return false;
  }

  SkelHierarchy dst;
  SkelNode root;
  if (!BuildSkelHierarchy(skel, root, &_err)) {
    return false;
  }
  dst.abs_path = skelPath.prim_part();
  dst.prim_name = primName;
  dst.display_name = skel.metas().has_displayName() ? skel.metas().get_displayName() : "";
  dst.root_node = root;

  // Handle animation source
  if (skel.animationSource.authored()) {
    DCOUT("skel:animationSource (from ptr)");

    const Relationship &animSourceRel = skel.animationSource.relationship();

    Path animSourcePath;

    if (animSourceRel.is_path()) {
      animSourcePath = animSourceRel.targetPath;
    } else if (animSourceRel.is_pathvector()) {
      if (animSourceRel.targetPathVector.size()) {
        animSourcePath = animSourceRel.targetPathVector[0];
      } else {
        PUSH_ERROR_AND_RETURN("`skel:animationSource` has invalid definition.");
      }
    } else {
      PUSH_ERROR_AND_RETURN("`skel:animationSource` has invalid definition.");
    }

    const Prim *animSourcePrim{nullptr};
    if (!env.stage.find_prim_at_path(animSourcePath, animSourcePrim, &_err)) {
      return false;
    }

    if (const auto panim = animSourcePrim->as<SkelAnimation>()) {
      DCOUT("Convert SkelAnimation (from ptr)");
      AnimationClip anim;
      if (!ConvertSkelAnimation(env, animSourcePath, *panim, skeleton_id, &anim)) {
        return false;
      }

      DCOUT("Converted SkelAnimation (from ptr)");
      (*out_anim) = anim;

    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Target Prim of `skel:animationSource` must be `SkelAnimation` Prim, but got `{}`.", animSourcePrim->prim_type_name()));
    }
  }

  (*out_skel) = dst;
  return true;
}

bool RenderSceneConverter::ConvertSkeletonImplWithPath(const RenderSceneConverterEnv &env, const Path &skelPath,
                       int32_t skeleton_id,
                       SkelHierarchy *out_skel, nonstd::optional<AnimationClip> *out_anim) {

  if (!out_skel) {
    return false;
  }

  if (skelPath.is_valid()) {
    const Prim *skelPrim{nullptr};
    if (!env.stage.find_prim_at_path(skelPath, skelPrim, &_err)) {
      return false;
    }

    SkelHierarchy dst;
    if (const auto pskel = skelPrim->as<Skeleton>()) {
      SkelNode root;
      if (!BuildSkelHierarchy((*pskel), root, &_err)) {
        return false;
      }
      dst.abs_path = skelPath.prim_part();
      dst.prim_name = skelPrim->element_name();
      dst.display_name = pskel->metas().has_displayName() ? pskel->metas().get_displayName() : "";
      dst.root_node = root;

      if (pskel->animationSource.authored()) {
        DCOUT("skel:animationSource");

        const Relationship &animSourceRel = pskel->animationSource.relationship();

        Path animSourcePath;

        if (animSourceRel.is_path()) {
          animSourcePath = animSourceRel.targetPath;
        } else if (animSourceRel.is_pathvector()) {
          // Use the first one
          if (animSourceRel.targetPathVector.size()) {
            animSourcePath = animSourceRel.targetPathVector[0];
          } else {
            PUSH_ERROR_AND_RETURN("`skel:animationSource` has invalid definition.");
          }
        } else {
          PUSH_ERROR_AND_RETURN("`skel:animationSource` has invalid definition.");
        }

        const Prim *animSourcePrim{nullptr};
        if (!env.stage.find_prim_at_path(animSourcePath, animSourcePrim, &_err)) {
          return false;
        }

        if (const auto panim = animSourcePrim->as<SkelAnimation>()) {
          DCOUT("Convert SkelAnimation");
          AnimationClip anim;
          if (!ConvertSkelAnimation(env, animSourcePath, *panim, skeleton_id, &anim)) {
            return false;
          }

          DCOUT("Converted SkelAnimation");
          (*out_anim) = anim;

        } else {
          PUSH_ERROR_AND_RETURN(fmt::format("Target Prim of `skel:animationSource` must be `SkelAnimation` Prim, but got `{}`.", animSourcePrim->prim_type_name()));
        }


      }
    } else {
      PUSH_ERROR_AND_RETURN("Prim is not Skeleton.");
    }

    (*out_skel) = dst;
    return true;
  }

  PUSH_ERROR_AND_RETURN("`skel:skeleton` path is invalid.");
}

}  // namespace tydra
}  // namespace tinyusdz
