// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
#include <iostream>

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "io-util.hh"
#include "value-pprint.hh"

namespace tinyusdz {

std::string AssetResolutionResolver::search_paths_str() const {
  std::string str;

  str += "[ ";
  for (size_t i = 0; i < _search_paths.size(); i++) {
    if (i > 0) {
      str += ", ";
    }
    // TODO: Escape character?
    str += _search_paths[i];
  }
  str += " ]";
  return str;
}

bool AssetResolutionResolver::find(const std::string &assetPath) const {
  DCOUT("search_paths = " << _search_paths);
  DCOUT("assetPath = " << assetPath);
  // TODO: Cache resolition.
  std::string fpath = io::FindFile(assetPath, _search_paths);
  return fpath.size();
}

std::string AssetResolutionResolver::resolve(
    const std::string &assetPath) const {
  DCOUT("search_paths = " << _search_paths);
  DCOUT("assetPath = " << assetPath);
  // TODO: Cache resolition.
  return io::FindFile(assetPath, _search_paths);
}

}  // namespace tinyusdz
