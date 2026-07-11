// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.

#pragma once

#include "../stage/stage.hh"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace tinyusdz {
namespace next {

struct ValueClipSet {
  std::string name;
  std::vector<std::string> asset_paths;
  std::vector<std::pair<double, double>> times;
  std::vector<std::pair<double, int>> active;
  std::string prim_path;
  std::string manifest_asset_path;
  bool interpolate_missing = false;
};

using ValueClipStageLoader = std::function<bool(
    const std::string& asset_path, Stage* stage, std::string* warn,
    std::string* err)>;

bool ParseValueClipSets(const UsdPrim& prim, std::vector<ValueClipSet>* out,
                        std::string* error = nullptr);

/// Resolve one numeric-time query through the prim's composed clip metadata.
/// Ordinary timeSamples/default precedence is handled by AttributeEval before
/// this function is called.
bool ResolveValueClip(const UsdPrim& prim, const std::string& property,
                      double stage_time, const ValueClipStageLoader& loader,
                      Value* out, std::string* source_asset = nullptr,
                      std::string* error = nullptr);

}  // namespace next
}  // namespace tinyusdz
