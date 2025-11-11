// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 Light Transport Entertainment Inc.
//
// PCP Time Sample Evaluation Implementation
//

#include "pcp-timesample.hh"
#include <algorithm>
#include <cmath>

namespace tinyusdz {
namespace tydra {
namespace pcp {

/// AnimationTimeline Implementation

AnimationTimeline::AnimationTimeline(const Path& prim_path)
    : prim_path_(prim_path) {}

void AnimationTimeline::AddKeyframe(TimeCode time,
                                    std::shared_ptr<PrimIndex> composition,
                                    InterpolationMode interpolation) {
  if (!composition) {
    return;
  }

  // Check if keyframe at this time already exists
  for (auto& kf : keyframes_) {
    if (std::abs(kf->time - time) < 1e-9) {
      // Update existing keyframe
      kf->composition = composition;
      kf->interpolation = interpolation;
      return;
    }
  }

  // Add new keyframe
  auto sample = std::make_shared<TimeSample>();
  sample->time = time;
  sample->composition = composition;
  sample->interpolation = interpolation;
  sample->interpolation_parameter = 0.5f;

  keyframes_.push_back(sample);

  // Keep sorted by time
  std::sort(keyframes_.begin(), keyframes_.end(),
            [](const auto& a, const auto& b) { return a->time < b->time; });

  // Invalidate cache on new keyframe
  cache_.clear();
}

bool AnimationTimeline::EvaluateAt(TimeCode time,
                                    std::shared_ptr<PrimIndex>& composition) {
  if (keyframes_.empty()) {
    return false;
  }

  // Check cache first
  auto cache_it = cache_.find(time);
  if (cache_it != cache_.end()) {
    composition = cache_it->second;
    return true;
  }

  TimeCode start_time, end_time;
  if (!GetTimeRange(start_time, end_time)) {
    return false;
  }

  // Clamp time to range
  time = std::max(start_time, std::min(time, end_time));

  // Find keyframe index
  size_t idx = FindKeyframeIndex(time);

  // Exact match or before first keyframe
  if (idx == keyframes_.size()) {
    composition = keyframes_.back()->composition;
  } else if (time <= keyframes_[idx]->time) {
    composition = keyframes_[idx]->composition;
  } else if (idx + 1 < keyframes_.size()) {
    // Interpolate between keyframes
    const auto& current = keyframes_[idx];
    const auto& next = keyframes_[idx + 1];

    switch (current->interpolation) {
      case InterpolationMode::LINEAR:
        composition = InterpolateLinear(time);
        break;
      case InterpolationMode::CUBIC:
        composition = InterpolateCubic(time);
        break;
      case InterpolationMode::BEZIER:
        composition = InterpolateBezier(time);
        break;
      case InterpolationMode::STEP:
      case InterpolationMode::CONSTANT:
      default:
        composition = current->composition;
        break;
    }
  } else {
    composition = keyframes_.back()->composition;
  }

  // Cache result
  if (cache_enabled_) {
    cache_[time] = composition;
  }

  return composition != nullptr;
}

bool AnimationTimeline::GetTimeRange(TimeCode& start_time,
                                      TimeCode& end_time) const {
  if (keyframes_.empty()) {
    return false;
  }

  start_time = keyframes_.front()->time;
  end_time = keyframes_.back()->time;
  return true;
}

std::shared_ptr<PrimIndex> AnimationTimeline::GetAt(TimeCode time,
                                                     bool use_cache) {
  bool prev_cache = cache_enabled_;
  cache_enabled_ = use_cache;

  std::shared_ptr<PrimIndex> result;
  EvaluateAt(time, result);

  cache_enabled_ = prev_cache;
  return result;
}

std::vector<TimeCode> AnimationTimeline::GetKeyframeTimes() const {
  std::vector<TimeCode> times;
  for (const auto& kf : keyframes_) {
    times.push_back(kf->time);
  }
  return times;
}

std::shared_ptr<TimeSample> AnimationTimeline::GetKeyframeAt(
    size_t index) const {
  if (index < keyframes_.size()) {
    return keyframes_[index];
  }
  return nullptr;
}

bool AnimationTimeline::IsTimeInRange(TimeCode time) const {
  TimeCode start, end;
  if (!GetTimeRange(start, end)) {
    return false;
  }
  return time >= start && time <= end;
}

std::shared_ptr<PrimIndex> AnimationTimeline::InterpolateLinear(
    TimeCode time) {
  size_t idx = FindKeyframeIndex(time);
  if (idx >= keyframes_.size()) {
    return keyframes_.back()->composition;
  }

  const auto& current = keyframes_[idx];
  const auto& next = keyframes_[idx + 1];

  // Linear interpolation factor (0 to 1)
  TimeCode time_range = next->time - current->time;
  if (time_range < 1e-9) {
    return current->composition;
  }

  float t = static_cast<float>((time - current->time) / time_range);
  t = std::max(0.0f, std::min(1.0f, t));

  // For now, use step (blend is complex for composition trees)
  // In production, would implement proper blending
  return (t < 0.5f) ? current->composition : next->composition;
}

std::shared_ptr<PrimIndex> AnimationTimeline::InterpolateCubic(
    TimeCode time) {
  // Simplified cubic interpolation - uses keyframe values with easing
  size_t idx = FindKeyframeIndex(time);
  if (idx >= keyframes_.size()) {
    return keyframes_.back()->composition;
  }

  const auto& current = keyframes_[idx];
  const auto& next = keyframes_[idx + 1];

  TimeCode time_range = next->time - current->time;
  if (time_range < 1e-9) {
    return current->composition;
  }

  float t = static_cast<float>((time - current->time) / time_range);
  t = std::max(0.0f, std::min(1.0f, t));

  // Cubic easing: t^3
  float eased_t = t * t * t;

  return (eased_t < 0.5f) ? current->composition : next->composition;
}

std::shared_ptr<PrimIndex> AnimationTimeline::InterpolateBezier(
    TimeCode time) {
  // Bezier interpolation using interpolation_parameter as control point
  size_t idx = FindKeyframeIndex(time);
  if (idx >= keyframes_.size()) {
    return keyframes_.back()->composition;
  }

  const auto& current = keyframes_[idx];
  const auto& next = keyframes_[idx + 1];

  TimeCode time_range = next->time - current->time;
  if (time_range < 1e-9) {
    return current->composition;
  }

  float t = static_cast<float>((time - current->time) / time_range);
  t = std::max(0.0f, std::min(1.0f, t));

  // Simplified Bezier: (1-t)^2*P0 + 2(1-t)t*P_control + t^2*P1
  float p = current->interpolation_parameter;
  float bezier_t = (1 - t) * (1 - t) * 0.0f + 2 * (1 - t) * t * p +
                   t * t * 1.0f;

  return (bezier_t < 0.5f) ? current->composition : next->composition;
}

size_t AnimationTimeline::FindKeyframeIndex(TimeCode time) const {
  // Binary search for keyframe before or at time
  size_t left = 0, right = keyframes_.size();

  while (left < right) {
    size_t mid = left + (right - left) / 2;
    if (keyframes_[mid]->time <= time) {
      left = mid + 1;
    } else {
      right = mid;
    }
  }

  return left > 0 ? left - 1 : 0;
}

/// CompositionTimeline Implementation

CompositionTimeline::CompositionTimeline() {}

void CompositionTimeline::AddTimeline(
    const Path& prim_path,
    std::shared_ptr<AnimationTimeline> timeline) {
  if (timeline) {
    timelines_[prim_path] = timeline;
  }
}

std::shared_ptr<AnimationTimeline> CompositionTimeline::GetTimeline(
    const Path& prim_path) const {
  auto it = timelines_.find(prim_path);
  if (it != timelines_.end()) {
    return it->second;
  }
  return nullptr;
}

bool CompositionTimeline::EvaluateFrame(TimeCode time,
                                        std::shared_ptr<PrimIndex>& frame) {
  if (timelines_.empty()) {
    return false;
  }

  // For now, return first timeline's evaluation
  // In production, would blend all timelines
  auto it = timelines_.begin();
  return it->second->EvaluateAt(time, frame);
}

bool CompositionTimeline::GetTimeRange(TimeCode& start_time,
                                       TimeCode& end_time) const {
  if (timelines_.empty()) {
    return false;
  }

  start_time = 1e10;
  end_time = -1e10;

  for (const auto& [path, timeline] : timelines_) {
    TimeCode s, e;
    if (timeline->GetTimeRange(s, e)) {
      start_time = std::min(start_time, s);
      end_time = std::max(end_time, e);
    }
  }

  return start_time <= end_time;
}

std::vector<Path> CompositionTimeline::GetAnimatedPrims() const {
  std::vector<Path> prims;
  for (const auto& [path, timeline] : timelines_) {
    prims.push_back(path);
  }
  return prims;
}

bool CompositionTimeline::HasAnimation(const Path& prim_path) const {
  return timelines_.find(prim_path) != timelines_.end();
}

/// TimeBasedCompositionEvaluator Implementation

TimeBasedCompositionEvaluator::TimeBasedCompositionEvaluator(Cache* cache)
    : cache_(cache) {}

std::shared_ptr<PrimIndex> TimeBasedCompositionEvaluator::EvaluateAtTime(
    const Path& prim_path,
    TimeCode time,
    std::vector<Error>* errors) {
  if (!cache_) {
    return nullptr;
  }

  auto timeline = timeline_.GetTimeline(prim_path);
  if (!timeline) {
    if (errors) {
      errors->push_back(Error("No animation timeline for prim: " +
                              prim_path.ToString()));
    }
    return nullptr;
  }

  std::shared_ptr<PrimIndex> composition;
  if (timeline->EvaluateAt(time, composition)) {
    return composition;
  }

  if (errors) {
    errors->push_back(Error("Failed to evaluate timeline at time: " +
                            std::to_string(time)));
  }

  return nullptr;
}

std::vector<std::pair<TimeCode, std::shared_ptr<PrimIndex>>>
TimeBasedCompositionEvaluator::EvaluateSequence(
    const Path& prim_path,
    TimeCode start_time,
    TimeCode end_time,
    TimeCode frame_step) {
  std::vector<std::pair<TimeCode, std::shared_ptr<PrimIndex>>> sequence;

  if (frame_step <= 0.0) {
    frame_step = timeline_.frame_duration_;
  }

  for (TimeCode time = start_time; time <= end_time; time += frame_step) {
    auto composition = EvaluateAtTime(prim_path, time);
    if (composition) {
      sequence.push_back({time, composition});
    }
  }

  return sequence;
}

void TimeBasedCompositionEvaluator::CacheCompositionAt(
    TimeCode time,
    std::shared_ptr<PrimIndex> composition) {
  // Composition is already cached in timeline
}

}  // namespace pcp
}  // namespace tydra
}  // namespace tinyusdz
