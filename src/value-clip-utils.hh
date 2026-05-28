// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// value-clip-utils.hh - Value clip template metadata expansion
//
// Implements AOUSD Core Spec section 12.3.4.1.3 (Template Metadata):
//   - Expand templateAssetPath patterns (###, ###.###) into asset paths
//   - Generate times and active metadata from template parameters
//
#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <sstream>
#include <iomanip>

namespace tinyusdz {

// Result of expanding template clip metadata
struct ExpandedClipMetadata {
  std::vector<std::string> assetPaths;        // expanded file paths
  std::vector<std::pair<double, double>> times;   // (stageTime, clipTime)
  std::vector<std::pair<double, int>> active;     // (stageTime, assetIndex)
};

///
/// Expand template clip metadata into explicit clip metadata.
///
/// Per AOUSD Core Spec 12.3.4.1.3:
///   templateAssetPath: e.g. "path/clipname.###.usd" or "path/clipname.###.###.usd"
///   templateStartTime: first time to substitute
///   templateEndTime: last time to substitute
///   templateStride: increment between times
///   templateActiveOffset: (optional) offset for active time calculation
///
/// @param[in] templateAssetPath  Pattern with ### placeholders
/// @param[in] templateStartTime Start time for file generation
/// @param[in] templateEndTime   End time for file generation
/// @param[in] templateStride    Time increment between clips
/// @param[in] templateActiveOffset Offset for active time (default 0)
/// @param[out] result Expanded metadata
/// @param[out] err Error message
/// @return true on success
///
inline bool ExpandTemplateClipMetadata(
    const std::string &templateAssetPath,
    double templateStartTime,
    double templateEndTime,
    double templateStride,
    double templateActiveOffset,
    ExpandedClipMetadata *result,
    std::string *err) {

  if (!result) return false;

  if (templateStride <= 0.0) {
    if (err) *err = "templateStride must be positive.";
    return false;
  }

  if (templateEndTime < templateStartTime) {
    if (err) *err = "templateEndTime must be >= templateStartTime.";
    return false;
  }

  // Find ### placeholder pattern in templateAssetPath
  // Spec: "exactly one or two groups of placeholders"
  // "path/clipname.###.usd" (integer) or "path/clipname.###.###.usd" (sub-integer)
  size_t firstHash = templateAssetPath.find('#');
  if (firstHash == std::string::npos) {
    if (err) *err = "templateAssetPath must contain ### placeholders.";
    return false;
  }

  // Count consecutive '#' in first group
  size_t firstGroupEnd = firstHash;
  while (firstGroupEnd < templateAssetPath.size() &&
         templateAssetPath[firstGroupEnd] == '#') {
    firstGroupEnd++;
  }
  size_t firstGroupLen = firstGroupEnd - firstHash;

  // Check for second group (separated by a dot)
  size_t secondGroupStart = 0;
  size_t secondGroupLen = 0;
  bool hasFractional = false;

  if (firstGroupEnd < templateAssetPath.size() &&
      templateAssetPath[firstGroupEnd] == '.') {
    size_t dotPos = firstGroupEnd;
    size_t nextPos = dotPos + 1;
    if (nextPos < templateAssetPath.size() && templateAssetPath[nextPos] == '#') {
      hasFractional = true;
      secondGroupStart = nextPos;
      size_t secondGroupEnd = nextPos;
      while (secondGroupEnd < templateAssetPath.size() &&
             templateAssetPath[secondGroupEnd] == '#') {
        secondGroupEnd++;
      }
      secondGroupLen = secondGroupEnd - secondGroupStart;
    }
  }

  // Prefix and suffix around the placeholder(s)
  std::string prefix = templateAssetPath.substr(0, firstHash);
  std::string suffix;
  if (hasFractional) {
    suffix = templateAssetPath.substr(secondGroupStart + secondGroupLen);
  } else {
    suffix = templateAssetPath.substr(firstGroupEnd);
  }

  // Generate clips
  result->assetPaths.clear();
  result->times.clear();
  result->active.clear();

  int assetIndex = 0;

  for (double t = templateStartTime; t <= templateEndTime + templateStride * 0.5;
       t += templateStride) {
    // Clamp to end
    double clampedT = std::min(t, templateEndTime);

    // Format the time value into the placeholder
    std::ostringstream pathss;
    pathss << prefix;

    if (hasFractional) {
      // Two groups: integer.fractional
      double intPart;
      double fracPart = std::modf(clampedT, &intPart);

      // Integer part: truncate if fractional start time
      int intVal = static_cast<int>(intPart);
      pathss << std::setfill('0') << std::setw(static_cast<int>(firstGroupLen))
             << intVal;
      pathss << ".";

      // Fractional part: scale to fit secondGroupLen digits
      double fracScaled = std::abs(fracPart) * std::pow(10.0, static_cast<double>(secondGroupLen));
      int fracVal = static_cast<int>(std::round(fracScaled));
      pathss << std::setfill('0') << std::setw(static_cast<int>(secondGroupLen))
             << fracVal;
    } else {
      // Single group: integer only (truncate fractional)
      int intVal = static_cast<int>(clampedT);
      pathss << std::setfill('0') << std::setw(static_cast<int>(firstGroupLen))
             << intVal;
    }

    pathss << suffix;
    result->assetPaths.push_back(pathss.str());

    // times: (stageTime, clipTime) -- identity mapping for templates
    result->times.emplace_back(clampedT, clampedT);

    // active: (stageTime + offset, assetIndex)
    double activeTime = clampedT + templateActiveOffset;
    result->active.emplace_back(activeTime, assetIndex);

    assetIndex++;

    if (clampedT >= templateEndTime) break;
  }

  // Per spec 12.3.4.1.3.5: two additional clip time knots at the ends
  // based on the absolute value of templateActiveOffset
  if (std::abs(templateActiveOffset) > 1e-15 && !result->times.empty()) {
    double startKnot = templateStartTime + templateActiveOffset;
    double endKnot = templateEndTime + templateActiveOffset;

    // Insert boundary knots if they differ from existing
    if (startKnot < result->times.front().first) {
      result->times.insert(result->times.begin(),
                           {startKnot, startKnot});
    }
    if (endKnot > result->times.back().first) {
      result->times.emplace_back(endKnot, endKnot);
    }
  }

  return true;
}

///
/// Find the active clip index for a given stage time.
///
/// Per Spec 12.3.4.3 (Active Clips):
///   A [stageTime, assetIndex] entry indicates that the clip at assetIndex
///   is active from that stageTime up to the next entry's stageTime.
///   The first clip is active for all earlier times, the last for all later.
///
/// @param[in] active Sorted list of (stageTime, assetIndex)
/// @param[in] stageTime The time to query
/// @return The asset index of the active clip (-1 if no clips)
///
inline int FindActiveClipIndex(
    const std::vector<std::pair<double, int>> &active,
    double stageTime) {
  if (active.empty()) return -1;

  // Before first entry: first clip is active
  if (stageTime < active.front().first) {
    return active.front().second;
  }

  // Find the last entry whose stageTime <= stageTime
  int result = active.front().second;
  for (const auto &entry : active) {
    if (entry.first <= stageTime) {
      result = entry.second;
    } else {
      break;
    }
  }
  return result;
}

///
/// Remap stage time to clip time using the times metadata.
///
/// Per Spec 12.3.4.4 (Stage Time and Clip Time):
///   The (stageTime, clipTime) pairs define a piecewise-linear timing curve.
///   Times between entries are linearly interpolated.
///
/// @param[in] times Sorted list of (stageTime, clipTime)
/// @param[in] stageTime The stage time to remap
/// @return The corresponding clip time
///
inline double RemapStageTimeToClipTime(
    const std::vector<std::pair<double, double>> &times,
    double stageTime) {
  if (times.empty()) return stageTime;

  // Before first entry: extrapolate using first segment's slope
  if (stageTime <= times.front().first) {
    if (times.size() == 1) return times.front().second;
    // Clamp to first clip time
    return times.front().second;
  }

  // After last entry: clamp to last clip time
  if (stageTime >= times.back().first) {
    return times.back().second;
  }

  // Find segment and linearly interpolate
  for (size_t i = 0; i + 1 < times.size(); i++) {
    if (stageTime >= times[i].first && stageTime < times[i + 1].first) {
      double dt_stage = times[i + 1].first - times[i].first;
      if (dt_stage <= 0.0) return times[i].second;
      double u = (stageTime - times[i].first) / dt_stage;
      return times[i].second + u * (times[i + 1].second - times[i].second);
    }
  }

  return times.back().second;
}

///
/// Resolve a value clip query: find which clip to load and at what time.
///
/// Combines FindActiveClipIndex and RemapStageTimeToClipTime.
///
/// @param[in] active Sorted (stageTime, assetIndex) list
/// @param[in] times Sorted (stageTime, clipTime) list
/// @param[in] assetPaths List of clip asset paths
/// @param[in] stageTime The time to query
/// @param[out] clipAssetPath The resolved clip file path
/// @param[out] clipTime The time to query within the clip
/// @return true if a clip was found, false if no clips defined
///
inline bool ResolveValueClipQuery(
    const std::vector<std::pair<double, int>> &active,
    const std::vector<std::pair<double, double>> &times,
    const std::vector<std::string> &assetPaths,
    double stageTime,
    std::string *clipAssetPath,
    double *clipTime) {
  if (active.empty() || assetPaths.empty()) return false;

  int idx = FindActiveClipIndex(active, stageTime);
  if (idx < 0 || idx >= static_cast<int>(assetPaths.size())) return false;

  if (clipAssetPath) *clipAssetPath = assetPaths[static_cast<size_t>(idx)];
  if (clipTime) *clipTime = RemapStageTimeToClipTime(times, stageTime);

  return true;
}

///
/// Parse a clips Dictionary to extract clip set metadata.
/// Result of parsing clip set metadata.
struct ClipSetMetadata {
  std::vector<std::string> assetPaths;
  std::vector<std::pair<double, double>> times;
  std::vector<std::pair<double, int>> active;
  std::string primPath;
  std::string manifestAssetPath;           // optional: lists attributes in clips
  bool interpolateMissingClipValues{false}; // Spec 12.3.4.6: interpolate across missing clips
};

///
/// @param[in] clips_dict The clips Dictionary from prim metadata
/// @param[out] assetPaths Extracted asset paths
/// @param[out] times Extracted (stageTime, clipTime) pairs
/// @param[out] active Extracted (stageTime, assetIndex) pairs
/// @param[out] primPath The prim path to query in clip layers
/// @param[out] err Error message
/// @return true if at least one clip set was found
///
inline bool ParseClipSetMetadata(
    const std::map<std::string, MetaVariable> &clips_dict,
    std::vector<std::string> *assetPaths,
    std::vector<std::pair<double, double>> *times,
    std::vector<std::pair<double, int>> *active,
    std::string *primPath,
    std::string *err) {

  // Iterate over clip set names (there may be multiple, use the first one)
  for (const auto &clipset_entry : clips_dict) {
    // Each clip set is a Dictionary stored as MetaVariable
    auto clipset_opt = clipset_entry.second.get_value<Dictionary>();
    if (!clipset_opt) continue;

    const Dictionary &d = clipset_opt.value();

    // Helper to extract double from dictionary
    auto get_double_from = [](const Dictionary &dict, const char *key, double *out) {
      auto it = dict.find(key);
      if (it != dict.end()) {
        auto v = it->second.get_value<double>();
        if (v) { *out = v.value(); }
      }
    };

    // Check for template metadata first
    auto tmpl_it = d.find("templateAssetPath");
    if (tmpl_it != d.end()) {
      // Extract template parameters
      std::string templateAssetPath;
      double templateStartTime = 0, templateEndTime = 0, templateStride = 1;
      double templateActiveOffset = 0;

      if (auto v = tmpl_it->second.get_value<value::AssetPath>()) {
        templateAssetPath = v.value().GetAssetPath();
      } else if (auto sv = tmpl_it->second.get_value<std::string>()) {
        templateAssetPath = sv.value();
      }

      get_double_from(d, "templateStartTime", &templateStartTime);
      get_double_from(d, "templateEndTime", &templateEndTime);
      get_double_from(d, "templateStride", &templateStride);
      get_double_from(d, "templateActiveOffset", &templateActiveOffset);

      ExpandedClipMetadata expanded;
      if (ExpandTemplateClipMetadata(templateAssetPath, templateStartTime,
                                      templateEndTime, templateStride,
                                      templateActiveOffset, &expanded, err)) {
        if (assetPaths) *assetPaths = expanded.assetPaths;
        if (times) *times = expanded.times;
        if (active) *active = expanded.active;
      }
    } else {
      // Explicit metadata
      auto ap_it = d.find("assetPaths");
      if (ap_it != d.end() && assetPaths) {
        auto v = ap_it->second.get_value<std::vector<value::AssetPath>>();
        if (v) {
          for (const auto &ap : v.value()) {
            assetPaths->push_back(ap.GetAssetPath());
          }
        }
      }

      auto times_it = d.find("times");
      if (times_it != d.end() && times) {
        auto v = times_it->second.get_value<std::vector<value::double2>>();
        if (v) {
          for (const auto &tv : v.value()) {
            times->emplace_back(tv[0], tv[1]);
          }
        }
      }

      auto active_it = d.find("active");
      if (active_it != d.end() && active) {
        auto v = active_it->second.get_value<std::vector<value::double2>>();
        if (v) {
          for (const auto &a : v.value()) {
            active->emplace_back(a[0], static_cast<int>(a[1]));
          }
        }
      }
    }

    // primPath
    auto pp_it = d.find("primPath");
    if (pp_it != d.end() && primPath) {
      auto v = pp_it->second.get_value<std::string>();
      if (v) {
        *primPath = v.value();
      } else {
        auto sv = pp_it->second.get_value<value::StringData>();
        if (sv) {
          *primPath = sv.value().value;
        }
      }
    }

    // Use first clip set only
    return true;
  }

  return false;
}

///
/// Parse clip set metadata into a ClipSetMetadata struct.
/// Extracts all fields including manifestAssetPath and interpolateMissingClipValues.
///
inline bool ParseClipSetMetadataFull(
    const std::map<std::string, MetaVariable> &clips_dict,
    ClipSetMetadata *result,
    std::string *err) {

  if (!result) return false;

  if (!ParseClipSetMetadata(clips_dict, &result->assetPaths, &result->times,
                             &result->active, &result->primPath, err)) {
    return false;
  }

  // Extract additional fields from the first clip set
  for (const auto &clipset_entry : clips_dict) {
    auto clipset_opt = clipset_entry.second.get_value<Dictionary>();
    if (!clipset_opt) continue;

    const Dictionary &d = clipset_opt.value();

    // manifestAssetPath
    auto manifest_it = d.find("manifestAssetPath");
    if (manifest_it != d.end()) {
      if (auto v = manifest_it->second.get_value<value::AssetPath>()) {
        result->manifestAssetPath = v.value().GetAssetPath();
      }
    }

    // interpolateMissingClipValues (Spec 12.3.4.6)
    auto interp_it = d.find("interpolateMissingClipValues");
    if (interp_it != d.end()) {
      if (auto v = interp_it->second.get_value<bool>()) {
        result->interpolateMissingClipValues = v.value();
      }
    }

    break;  // first clip set only
  }

  return true;
}

///
/// Discover attributes available in clip files from a manifest asset.
///
/// Per AOUSD Core Spec 12.3.4.2: If manifestAssetPath is provided, load
/// the manifest USD file and extract property names from the target primPath.
/// This avoids having to open every clip file to discover available attributes.
///
/// @param[in] manifestAssetPath Path to the manifest USD file
/// @param[in] primPath The prim path to inspect in the manifest
/// @param[out] attribute_names List of discovered attribute names
/// @return true if manifest was loaded and attributes discovered
///
/// Note: This function is declared inline but depends on LoadLayerFromFile
/// which is defined in tinyusdz.cc. In practice, use it from .cc files
/// that include tinyusdz.hh.
///
inline bool DiscoverClipAttributes(
    const std::string &manifestAssetPath,
    const std::string &primPath,
    std::vector<std::string> *attribute_names) {
  if (!attribute_names) return false;
  if (manifestAssetPath.empty()) return false;

  attribute_names->clear();

  // The manifest is a lightweight USD file that contains a prim at primPath
  // with attribute declarations (no values needed, just the property names).
  // Since we can't call LoadLayerFromFile from a header (circular dependency),
  // store the manifest path for the caller to load.
  //
  // The caller (EvaluateAttributeFromClips) can check:
  //   1. Load manifest layer
  //   2. find_primspec_at(primPath)
  //   3. Iterate props() to get attribute names
  //
  // For now, return false to indicate the caller should handle loading.
  (void)primPath;
  return false;
}

}  // namespace tinyusdz
