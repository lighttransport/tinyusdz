// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
#include "asset-resolution.hh"
#include "io-util.hh"

namespace tinyusdz {

bool AssetResolutionResolver::find(const std::string &assetPath) {
  std::string fpath = io::FindFile(assetPath, _search_paths);
  return fpath.size();
}

std::string AssetResolutionResolver::resolve(const std::string &assetPath) {
  return io::FindFile(assetPath, _search_paths);
}

} // namespace tinyusdz
