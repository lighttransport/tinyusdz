// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
#include <cassert>
#include <iostream>

#include "asset-resolution.hh"
#include "common-macros.inc"
#include "io-util.hh"
#include "value-pprint.hh"
#include "str-util.hh"

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

  std::string ext = io::GetFileExtension(assetPath);

  if (_asset_resolution_handlers.count(ext)) {
    if (_asset_resolution_handlers.at(ext).resolve_fun && _asset_resolution_handlers.at(ext).size_fun) {
      std::string resolvedPath;
      std::string err;

      // Use custom handler's userdata
      void *userdata = _asset_resolution_handlers.at(ext).userdata;

      int ret = _asset_resolution_handlers.at(ext).resolve_fun(assetPath.c_str(), _search_paths, &resolvedPath, &err, userdata);
      if (ret != 0) {
        return false;
      }

      uint64_t sz{0};
      ret = _asset_resolution_handlers.at(ext).size_fun(resolvedPath.c_str(), &sz, &err, userdata);
      if (ret != 0) {
        return false;
      }

      return sz > 0;

    } else {
      DCOUT("Either Resolve function or Size function is nullptr. Fallback to built-in file handler.");
    }
  }

  // TODO: Cache resolition.
  std::string fpath = io::FindFile(assetPath, _search_paths);
  return fpath.size();
}

std::string AssetResolutionResolver::resolve(
    const std::string &assetPath) const {

  std::string ext = io::GetFileExtension(assetPath);

  if (_asset_resolution_handlers.count(ext)) {
    if (_asset_resolution_handlers.at(ext).resolve_fun) {
      std::string resolvedPath;
      std::string err;

      // Use custom handler's userdata
      void *userdata = _asset_resolution_handlers.at(ext).userdata;

      int ret = _asset_resolution_handlers.at(ext).resolve_fun(assetPath.c_str(), _search_paths, &resolvedPath, &err, userdata);
      if (ret != 0) {
        return std::string();
      }

      return resolvedPath;

    } else {
      DCOUT("Resolve function is nullptr. Fallback to built-in file handler.");
    }
  }

  DCOUT("search_paths = " << _search_paths);
  DCOUT("assetPath = " << assetPath);
  // TODO: Cache resolition.
  return io::FindFile(assetPath, _search_paths);
}

bool AssetResolutionResolver::open_asset(const std::string &resolvedPath, const std::string &assetPath,
                  Asset *asset_out, std::string *warn, std::string *err) {


  if (!asset_out) {
    if (err) {
      (*err) = "`asset` arg is nullptr.";
    }
    return false;
  }

  (void)assetPath;
  (void)warn;

  std::string ext = io::GetFileExtension(resolvedPath);

  if (_asset_resolution_handlers.count(ext)) {
    if (_asset_resolution_handlers.at(ext).size_fun && _asset_resolution_handlers.at(ext).read_fun) {

      // Use custom handler's userdata
      void *userdata = _asset_resolution_handlers.at(ext).userdata;

      // Get asset size.
      uint64_t sz{0};
      int ret = _asset_resolution_handlers.at(ext).size_fun(resolvedPath.c_str(), &sz, err, userdata);
      if (ret != 0) {
        return false;
      }

      tinyusdz::Asset asset;
      asset.resize(sz);

      uint64_t read_size{0};

      ret = _asset_resolution_handlers.at(ext).read_fun(resolvedPath.c_str(), /* req_size */asset.size(), asset.data(), &read_size, err, userdata);
      if (ret != 0) {
        return false;
      }

      if (read_size < sz) {
        asset.resize(read_size);
        // May optimize memory usage
        asset.shrink_to_fit();
      }

      (*asset_out) = std::move(asset);

      return true;
    } else {
      DCOUT("Resolve function is nullptr. Fallback to built-in file handler.");
    }
  }

  std::vector<uint8_t> data;
  size_t max_bytes = 1024 * 1024 * _max_asset_bytes_in_mb;
  if (!io::ReadWholeFile(&data, err, resolvedPath, max_bytes,
                           /* userdata */ nullptr)) {
      return false;
  }

  asset_out->set_data(std::move(data));

  return true;
}

}  // namespace tinyusdz
