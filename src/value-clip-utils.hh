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

}  // namespace tinyusdz
