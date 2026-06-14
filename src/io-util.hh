// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
// 
#pragma once

#include <cstddef>
#include <string>
#include <vector>
#include <algorithm>
#include <map>
#include <cstdint>

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif

namespace tinyusdz {
namespace io {

// TODO: Move texture-utils.hh or somewhere, not here.
//
// <UDIM> : 1001 ~ 1100
// <UVTILE> : u1_v1 ~ u10_v10
//
struct UDIMAsset
{
  // up to 10x10 tiles
  uint32_t index{1001}; // [1001, 1100]

  std::string asset_identifier; // usually filename or URI
};

struct UDIMAssetTiles
{
  std::map<uint32_t, UDIMAsset> tiles;

  // tile u, v : 0-based

  static uint32_t UDIMIndex(uint32_t u, uint32_t v) {
    uint32_t uu = (std::min)(9u, u);
    uint32_t vv = (std::min)(9u, v);

    return 1001 + uu + vv * 10;
  }

  static std::string UVTILEIndex(uint32_t u, uint32_t v) {
    uint32_t uu = (std::min)(9u, u);
    uint32_t vv = (std::min)(9u, v);

    return "u" + std::to_string(uu+1) + "_" + std::to_string(vv+1);
  }

  bool IsValidTile(uint32_t u, uint32_t v) {
    if (u > 9) return false;
    if (v > 9) return false;

    return true;
  }

  bool has_tile(uint32_t u, uint32_t v) {
    if (u > 9) return false;
    if (v > 9) return false;

    uint32_t tid = UDIMIndex(u, v);
    return tiles.count(tid);
  }

  bool set(uint32_t u, uint32_t v, const UDIMAsset &asset) {
    if (!IsValidTile(u, v)) {
      return false;
    }

    tiles.emplace(UDIMIndex(u, v), asset);

    return true;
  }

  bool erase(uint32_t u, uint32_t v) {
    if (!IsValidTile(u, v)) {
      return false;
    }

    tiles.erase(UDIMIndex(u, v));

    return true;
  }

};

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
extern AAssetManager *asset_manager;
#endif

#ifdef _WIN32
std::wstring UTF8ToWchar(const std::string &str);
std::string WcharToUTF8(const std::wstring &wstr);
#endif

std::string ExpandFilePath(const std::string &filepath,
                           void *userdata = nullptr);

bool FileExists(const std::string &filepath, void *userdata = nullptr);

///
/// Find file from search paths.
/// Returns empty string if a file is not found.
/// TODO: Filesystem callback.
///
std::string FindFile(const std::string &filepath, const std::vector<std::string> &search_paths);

///
/// Generate fallback path candidates for an asset path that failed literal
/// resolution, by stripping its un-anchorable prefix (Windows drive, leading
/// '/', './' and '../' runs) and then dropping leading directory components
/// one at a time (longest suffix first, down to the basename).
///
/// e.g. "../../../../../USD_Exports/Scene/Assets/SM_Ppe3.usd" ->
///   ["USD_Exports/Scene/Assets/SM_Ppe3.usd", "Scene/Assets/SM_Ppe3.usd",
///    "Assets/SM_Ppe3.usd", "SM_Ppe3.usd"]
///
/// The input path itself is never included. Used to rebase composition arcs
/// authored against another machine's directory layout (e.g. UnrealEngine USD
/// exports) onto the local scene root.
///
std::vector<std::string> AssetPathSuffixCandidates(const std::string &asset_path);

bool ReadWholeFile(std::vector<uint8_t> *out, std::string *err,
                   const std::string &filepath, size_t filesize_max = 0,
                   void *userdata = nullptr);

///
/// Read first N bytes from a file.
/// Example is for detect file formats.
///
bool ReadFileHeader(std::vector<uint8_t> *out, std::string *err,
                   const std::string &filepath, uint32_t max_read_bytes = 128,
                   void *userdata = nullptr);


///
/// @return true when the system supports mmap. 
///
bool IsMMapSupported(); 

// Simple mmap file handle struct
struct MMapFileHandle
{
  std::string filename;
#if defined(WIN32)
  std::wstring unicode_filename;
  void *hFile = nullptr;
#endif
  bool writable{false};
  uint8_t *addr{nullptr};
  uint64_t size{0};
};

///
/// memory-map file.
///
/// @param[in] filepath UTF8 filepath.
///
/// Returns false when file is not found, invalid, or mmap feature is not available.
/// err = warning message when the API returns true.
///
bool MMapFile(const std::string &filepath, MMapFileHandle *handle, bool writable, std::string *err);

#ifdef _WIN32
// Unicode(UTF16LE) version
bool MMapFile(const std::wstring &filepath, MMapFileHandle *handle, bool writable, std::string *err);
#endif

///
/// err = warning message when the API returns true.
///
bool UnmapFile(const MMapFileHandle &handle, std::string *err);


/// 
/// Write data to file(UTF8 filepath)
/// 
bool WriteWholeFile(const std::string &filepath,
                    const unsigned char *contents, size_t content_bytes, std::string *err);

#ifdef _WIN32
bool WriteWholeFile(const std::wstring &filepath,
                    const unsigned char *contents, size_t content_bytes, std::string *err);
#endif

std::string GetBaseDir(const std::string &filepath);
std::string GetBaseFilename(const std::string &filepath);
std::string GetFileExtension(const std::string &filepath);

std::string JoinPath(const std::string &dir, const std::string &filename);
bool IsAbsPath(const std::string &filepath);

// Collapse `.` and `..` segments in a '/'-separated path (lexical
// normalization; no filesystem access). Preserves a leading '/' (absolute) and,
// for relative paths, any leading `..` that cannot be collapsed. `..` at the
// root of an absolute path is dropped. Intended for forward-slash USD asset
// paths; callers should skip URI/scheme paths.
std::string NormalizePath(const std::string &path);

bool IsUDIMPath(const std::string &filepath);


bool USDFileExists(const std::string &filepath);

//
// diffuse.<UDIM>.png => "diffuse.", ".png"
//
bool SplitUDIMPath(const std::string &filepath, std::string *pre,
                   std::string *post);

///
/// Parse a packaged resource path per AOUSD Core Spec 9.7.
///
/// Syntax: "archive.usdz[internal/path/file.usdc]"
///
/// @param[in] path The full path string
/// @param[out] archive_path The archive file path (e.g. "archive.usdz")
/// @param[out] internal_path The path inside the archive (e.g. "internal/path/file.usdc")
/// @return true if the path contains bracket notation, false if it's a plain path
///
inline bool ParsePackagedResourcePath(const std::string &path,
                                       std::string *archive_path,
                                       std::string *internal_path) {
  auto bracket_open = path.find('[');
  if (bracket_open == std::string::npos) {
    return false;  // Not a packaged path
  }

  auto bracket_close = path.find(']', bracket_open);
  if (bracket_close == std::string::npos) {
    return false;  // Malformed
  }

  if (archive_path) {
    *archive_path = path.substr(0, bracket_open);
  }
  if (internal_path) {
    *internal_path = path.substr(bracket_open + 1,
                                  bracket_close - bracket_open - 1);
  }

  return true;
}

}  // namespace io
}  // namespace tinyusdz
