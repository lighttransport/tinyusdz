// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
// 
// Asset Resolution utilities
// https://graphics.pixar.com/usd/release/api/ar_page_front.html
// 
// To avoid a confusion with AR(Argumented Reality), we doesn't use abberation `ar`, `Ar` and `AR`. ;-)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "value-types.hh"

namespace tinyusdz {

struct ResolverAssetInfo {
  std::string version;
  std::string assetName;
  // std::string repoPath;  deprecated in pxrUSD Ar 2.0

  value::Value resolverInfo;
};

///
/// For easier language bindings(e.g. for C), we use simple callback function approach.
///
typedef int (*FSReadFile)(const char *filename, uint8_t **bytes, uint64_t *size, std::string *err);

///
/// @param[in] path Path string to be resolved.
/// @param[in] assetInfo nullptr when no `assetInfo` assigned to this path.
/// @param[inout] userdata Userdata pointer passed by callee. could be nullptr
/// @param[out] resolvedPath Resolved Path string.
/// @param[out] err Error message.
//
typedef bool (*ResolvePathHandler)(const std::string &path, const ResolverAssetInfo *assetInfo, void *userdata, std::string *resolvedPath, std::string *err);

class AssetResolutionResolver
{
 public:
  static void SetDefaultSearchPath(const std::string &p);

  ///
  /// Register user defined filesystem handler.
  ///
  void register_filesystem_handler(FSReadFile readFn);

  void register_resolve_path_handler(ResolvePathHandler handler) {
    _resolve_path_handler = handler;
  }

  void set_userdata(void *userdata) { _userdata = userdata; }
  void *get_userdata() { return _userdata; }

 private:

  ResolvePathHandler _resolve_path_handler{nullptr};
  void *_userdata{nullptr};
  std::vector<std::string> search_paths;
  static std::vector<std::string> _default_search_paths;
  
};


} // namespace tinyusdz 

