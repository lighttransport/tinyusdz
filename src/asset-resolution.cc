// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
#include <iostream>

#include "asset-resolution.hh"
#include "io-util.hh"
#include "common-macros.inc"
#include "value-pprint.hh"

namespace tinyusdz {

bool AssetResolutionResolver::find(const std::string &assetPath) {
  DCOUT("search_paths = " << _search_paths);
  DCOUT("assetPath = " << assetPath);
  std::string fpath = io::FindFile(assetPath, _search_paths);
  return fpath.size();
}

std::string AssetResolutionResolver::resolve(const std::string &assetPath) {
  DCOUT("search_paths = " << _search_paths);
  DCOUT("assetPath = " << assetPath);
  return io::FindFile(assetPath, _search_paths);
}

} // namespace tinyusdz
