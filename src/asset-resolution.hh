// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - Present, Light Transport Entertainment, Inc.
//
// Asset Resolution utilities
// https://graphics.pixar.com/usd/release/api/ar_page_front.html
//
// To avoid a confusion with AR(Argumented Reality), we doesn't use abberation
// `ar`, `Ar` and `AR`. ;-)
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "nonstd/optional.hpp"
#include "value-types.hh"

namespace tinyusdz {

///
/// Abstract class for asset(e.g. file, memory, uri, ...)
/// Similar to ArAsset in pxrUSD.
///
class Asset {
 public:
  size_t size() const { return buf_.size(); }

  const uint8_t *data() const { return buf_.data(); }

  uint8_t *data() { return buf_.data(); }

#if 0
  ///
  /// Read asset data to `buffer`
  ///
  /// @param[out] buffer Buffer address. Must have enough memory allocated.
  /// @param[in] nbytes The number of bytes to read.
  /// @param[in] byte_offset Byte offset.
  ///
  /// @return The number of bytes read.

  size_t read(uint8_t *buffer, size_t nbytes, size_t byte_offset) const;
#endif

 private:
  std::vector<uint8_t> buf_;
};

struct ResolverAssetInfo {
  std::string version;
  std::string assetName;
  // std::string repoPath;  deprecated in pxrUSD Ar 2.0

  value::Value resolverInfo;
};

///
/// For easier language bindings(e.g. for C), we use simple callback function
/// approach.
///

// @param[in] asset_name Asset name or filepath
// @param[out] nbytes Bytes of this asset.
// @param[out] err Error message.
// @param[inout] userdata Userdata.
// 
// @return 0 upon success. negative value = error
typedef int (*FSSizeData)(const char *asset_name, uint64_t *nbytes,
                          std::string *err, void *userdata);

// @param[in] asset_name Asset name or filepath
// @param[in] req_nbytes Required bytes for output buffer.
// @param[out] out_buf Output buffer. Memory should be allocated before calling this functione(`req_nbytes` or more)
// @param[out] nbytes Read bytes. 0 <= nbytes <= `req_nbytes`
// @param[out] err Error message.
// @param[inout] userdata Userdata.
//
// @return 0 upon success. negative value = error
typedef int (*FSReadData)(const char *asset_name, uint64_t req_nbytes, uint8_t *out_buf,
                          uint64_t *nbytes, std::string *err, void *userdata);

// @param[in] asset_name Asset name or filepath
// @param[in] buffer Data.
// @param[in] nbytes Data bytes.
// @param[out] err Error message.
// @param[inout] userdata Userdata.
// 
// @return 0 upon success. negative value = error
typedef int (*FSWriteData)(const char *asset_name, const uint8_t *buffer,
                           const uint64_t nbytes, std::string *err, void *userdata);

struct FileSystemHandler {
  FSSizeData size_fun{nullptr};
  FSReadData read_fun{nullptr};
  FSWriteData write_fun{nullptr};
  void *userdata{nullptr};
};

///
/// @param[in] path Path string to be resolved.
/// @param[in] assetInfo nullptr when no `assetInfo` assigned to this path.
/// @param[inout] userdata Userdata pointer passed by callee. could be nullptr
/// @param[out] resolvedPath Resolved Path string.
/// @param[out] err Error message.
//
typedef bool (*ResolvePathHandler)(const std::string &path,
                                   const ResolverAssetInfo *assetInfo,
                                   void *userdata, std::string *resolvedPath,
                                   std::string *err);

class AssetResolutionResolver {
 public:
  AssetResolutionResolver() = default;
  ~AssetResolutionResolver() {}

  AssetResolutionResolver(const AssetResolutionResolver &rhs) {
    if (this != &rhs) {
      _resolve_path_handler = rhs._resolve_path_handler;
      _userdata = rhs._userdata;
      _search_paths = rhs._search_paths;
    }
  }

  AssetResolutionResolver &operator=(const AssetResolutionResolver &rhs) {
    if (this != &rhs) {
      _resolve_path_handler = rhs._resolve_path_handler;
      _userdata = rhs._userdata;
      _search_paths = rhs._search_paths;
    }
    return (*this);
  }

  AssetResolutionResolver &operator=(AssetResolutionResolver &&rhs) {
    if (this != &rhs) {
      _resolve_path_handler = rhs._resolve_path_handler;
      _userdata = rhs._userdata;
      _search_paths = std::move(rhs._search_paths);
    }
    return (*this);
  }

  // TinyUSDZ does not provide global search paths at the moment.
  // static void SetDefaultSearchPath(const std::vector<std::string> &p);

  void set_search_paths(const std::vector<std::string> &paths) {
    // TODO: Validate input paths.
    _search_paths = paths;
  }

  void add_seartch_path(const std::string &path) {
    _search_paths.push_back(path);
  }

  const std::vector<std::string> &search_paths() const { return _search_paths; }

  std::string search_paths_str() const;

  ///
  /// Register user defined filesystem handler.
  /// Default = use file handler(FILE/ifstream)
  ///
  void register_filesystem_handler(FileSystemHandler handler) {
    _filesystem_handler = handler;
  }

  void unregister_filesystem_handler() { _filesystem_handler.reset(); }

  ///
  /// Register user defined asset path resolver.
  /// Default = find file from search paths.
  ///
  void register_resolve_path_handler(ResolvePathHandler handler) {
    _resolve_path_handler = handler;
  }

  void unregister_resolve_path_handler() { _resolve_path_handler = nullptr; }

  ///
  /// Check if input asset exists(do asset resolution inside the function).
  ///
  /// @param[in] assetPath Asset path string(e.g. "bora.png",
  /// "/mnt/c/sphere.usd")
  ///
  bool find(const std::string &assetPath) const;

  ///
  /// Resolve asset path and returns resolved path as string.
  /// Returns empty string when the asset does not exit.
  ///
  std::string resolve(const std::string &assetPath) const;

  ///
  /// Open asset from the resolved Path.
  ///
  /// @param[in] resolvedPath Resolved path(through `resolve()`)
  /// @param[in] assetPath Asset path of resolved path.
  /// @param[out] asset Asset.
  /// @param[out] warn Warning.
  /// @param[out] err Error message.
  ///
  /// @return true upon success.
  ///
  bool open_asset(const std::string &resolvedPath, const std::string &assetPath,
                  Asset *asset, std::string *warn, std::string *err);

  void set_userdata(void *userdata) { _userdata = userdata; }
  void *get_userdata() { return _userdata; }
  const void *get_userdata() const { return _userdata; }

 private:
  ResolvePathHandler _resolve_path_handler{nullptr};
  void *_userdata{nullptr};
  std::vector<std::string> _search_paths;

  nonstd::optional<FileSystemHandler> _filesystem_handler;

  // TODO: Cache resolution result
  // mutable _dirty{true};
  // mutable std::map<std::string, std::string> _cached_resolved_paths;
};

}  // namespace tinyusdz
