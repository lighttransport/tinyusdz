// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// PCP Time Sample Evaluation - Animation Timeline Support
//

#pragma once

#include <map>
#include <vector>
#include <memory>
#include <cmath>
#include "pcp-types.hh"

namespace tinyusdz {
namespace tydra {
namespace pcp {

// Forward declarations
class PrimIndex;
class TimelineFrame;

/// Floating point time representation (in seconds)
using TimeCode = double;

/// Keyframe interpolation mode
enum class InterpolationMode {
  LINEAR,      // Linear interpolation between keyframes
  STEP,        // Step function (no interpolation)
  CUBIC,       // Cubic spline interpolation
  BEZIER,      // Bezier curve interpolation
  CONSTANT,    // Hold value until next keyframe
};

/// A single time sample in a composition timeline
struct TimeSample {
  TimeCode time;                           // Time in seconds
  std::shared_ptr<PrimIndex> composition;  // Composition at this time
  InterpolationMode interpolation;         // How to interpolate to next frame
  float interpolation_parameter;           // 0.0 to 1.0 for curve shape
};

/// Animation timeline for a single prim or property
class AnimationTimeline {
 public:
  AnimationTimeline(const Path& prim_path);
  ~AnimationTimeline() = default;

  /// Add a keyframe at specific time
  /// @param[in] time Time in seconds
  /// @param[in] composition Composition at this time
  /// @param[in] interpolation Interpolation mode to next keyframe
  void AddKeyframe(TimeCode time,
                   std::shared_ptr<PrimIndex> composition,
                   InterpolationMode interpolation = InterpolationMode::LINEAR);

  /// Get composition at specific time (with interpolation)
  /// @param[in] time Time to evaluate
  /// @param[out] composition Evaluated composition
  /// @return true if found, false if time is outside range
  bool EvaluateAt(TimeCode time, std::shared_ptr<PrimIndex>& composition);

  /// Get composition with optional caching
  /// @param[in] time Time to evaluate
  /// @param[in] use_cache Whether to cache results
  std::shared_ptr<PrimIndex> GetAt(TimeCode time, bool use_cache = true);

  /// Get all keyframe times
  std::vector<TimeCode> GetKeyframeTimes() const;

  /// Get keyframe at index
  /// @param[in] index Keyframe index
  /// @return TimeSample if valid, nullptr otherwise
  std::shared_ptr<TimeSample> GetKeyframeAt(size_t index) const;

  /// Get number of keyframes
  size_t GetKeyframeCount() const { return keyframes_.size(); }

  /// Get animation time range
  /// @param[out] start_time Earliest keyframe
  /// @param[out] end_time Latest keyframe
  /// @return true if keyframes exist
  bool GetTimeRange(TimeCode& start_time, TimeCode& end_time) const;

  /// Check if time is within animation range
  bool IsTimeInRange(TimeCode time) const;

  /// Clear all keyframes
  void Clear() { keyframes_.clear(); }

  /// Get path of animated prim
  const Path& GetPrimPath() const { return prim_path_; }

 private:
  std::shared_ptr<PrimIndex> InterpolateLinear(TimeCode time);
  std::shared_ptr<PrimIndex> InterpolateCubic(TimeCode time);
  std::shared_ptr<PrimIndex> InterpolateBezier(TimeCode time);
  size_t FindKeyframeIndex(TimeCode time) const;

  Path prim_path_;
  std::vector<std::shared_ptr<TimeSample>> keyframes_;
  std::map<TimeCode, std::shared_ptr<PrimIndex>> cache_;
  bool cache_enabled_ = true;
};

/// Timeline management for entire composition
class CompositionTimeline {
 public:
  CompositionTimeline();
  ~CompositionTimeline() = default;

  /// Add animation timeline for a prim
  /// @param[in] prim_path Path to animated prim
  /// @param[in] timeline Animation timeline
  void AddTimeline(const Path& prim_path,
                   std::shared_ptr<AnimationTimeline> timeline);

  /// Get timeline for prim
  /// @param[in] prim_path Path to prim
  /// @return Timeline if exists, nullptr otherwise
  std::shared_ptr<AnimationTimeline> GetTimeline(const Path& prim_path) const;

  /// Evaluate entire composition at specific time
  /// @param[in] time Time in seconds
  /// @param[out] frame Frame composition
  /// @return true on success
  bool EvaluateFrame(TimeCode time, std::shared_ptr<PrimIndex>& frame);

  /// Get overall timeline range
  /// @param[out] start_time Earliest keyframe
  /// @param[out] end_time Latest keyframe
  bool GetTimeRange(TimeCode& start_time, TimeCode& end_time) const;

  /// Get number of animated timelines
  size_t GetTimelineCount() const { return timelines_.size(); }

  /// Get all animated prim paths
  std::vector<Path> GetAnimatedPrims() const;

  /// Check if prim has animation
  bool HasAnimation(const Path& prim_path) const;

  /// Clear all timelines
  void Clear() { timelines_.clear(); }

  /// Set FPS for frame-based timing
  void SetFramesPerSecond(double fps) {
    fps_ = fps;
    frame_duration_ = (fps > 0.0) ? 1.0 / fps : 0.0;
  }

  /// Convert frame number to time
  TimeCode FrameToTime(int frame_number) const {
    return static_cast<TimeCode>(frame_number) * frame_duration_;
  }

  /// Convert time to frame number
  int TimeToFrame(TimeCode time) const {
    if (frame_duration_ == 0.0) return 0;
    return static_cast<int>(std::round(time / frame_duration_));
  }

 private:
  std::map<Path, std::shared_ptr<AnimationTimeline>> timelines_;
  double fps_ = 24.0;              // Default 24 FPS
  double frame_duration_ = 1.0 / 24.0;
};

/// Time-aware composition evaluator
class TimeBasedCompositionEvaluator {
 public:
  explicit TimeBasedCompositionEvaluator(Cache* cache);
  ~TimeBasedCompositionEvaluator() = default;

  /// Evaluate composition at specific time
  /// @param[in] prim_path Path to evaluate
  /// @param[in] time Time in seconds
  /// @param[out] errors Errors encountered
  /// @return Evaluated PrimIndex
  std::shared_ptr<PrimIndex> EvaluateAtTime(
      const Path& prim_path,
      TimeCode time,
      std::vector<Error>* errors = nullptr);

  /// Get composition timeline
  CompositionTimeline* GetTimeline() { return &timeline_; }

  /// Evaluate animation sequence
  /// @param[in] prim_path Path to evaluate
  /// @param[in] start_time Start time
  /// @param[in] end_time End time
  /// @param[in] frame_step Step between frames
  /// @return Vector of compositions for each frame
  std::vector<std::pair<TimeCode, std::shared_ptr<PrimIndex>>>
  EvaluateSequence(const Path& prim_path,
                   TimeCode start_time,
                   TimeCode end_time,
                   TimeCode frame_step);

  /// Cache composition at time
  /// @param[in] time Time in seconds
  /// @param[in] composition Composition to cache
  void CacheCompositionAt(TimeCode time,
                          std::shared_ptr<PrimIndex> composition);

 private:
  Cache* cache_;
  CompositionTimeline timeline_;
};

}  // namespace pcp
}  // namespace tydra
}  // namespace tinyusdz
