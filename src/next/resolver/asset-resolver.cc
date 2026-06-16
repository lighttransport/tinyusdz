// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Asset Resolution Implementation

#include "asset-resolver.hh"
#include <algorithm>
#include <fstream>
#include <cstring>

#ifdef _WIN32
#include <direct.h>
#define PATH_SEPARATOR '\\'
#define ALT_PATH_SEPARATOR '/'
#else
#include <unistd.h>
#include <sys/stat.h>
#define PATH_SEPARATOR '/'
#define ALT_PATH_SEPARATOR '\\'
#endif

namespace tinyusdz {
namespace next {

namespace {

// Global default resolver
std::unique_ptr<AssetResolver> g_default_resolver;

bool FileExistsImpl(const std::string& path) {
#if defined(__EMSCRIPTEN__)
  // wasm builds may have no filesystem at all (-sFILESYSTEM=0), where even a
  // stat syscall aborts. Resolution there goes through the custom resolver
  // (asset-cache backed); plain file paths never exist.
  (void)path;
  return false;
#elif defined(_WIN32)
  std::ifstream f(path);
  return f.good();
#else
  // Use lstat (not stat) to avoid following symlinks. Note: a TOCTOU
  // window still exists between this check and any subsequent open().
  struct stat st;
  return lstat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
#endif
}

bool DirectoryExistsImpl(const std::string& path) {
#if defined(__EMSCRIPTEN__)
  (void)path;
  return false;
#elif defined(_WIN32)
  std::ifstream f(path);
  return f.good();
#else
  struct stat st;
  return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string GetCurrentWorkingDirectory() {
#if defined(__EMSCRIPTEN__)
  // No filesystem (and the getcwd syscall ABORTS under -sFILESYSTEM=0).
  return ".";
#elif defined(_WIN32)
  char buffer[4096];
  if (_getcwd(buffer, sizeof(buffer))) {
    return std::string(buffer);
  }
#else
  char buffer[4096];
  if (getcwd(buffer, sizeof(buffer))) {
    return std::string(buffer);
  }
#endif
  return ".";
}

}  // namespace

// ============================================================
// AssetResolver
// ============================================================

AssetResolver::AssetResolver() {
  config_.working_directory = GetCurrentWorkingDirectory();
}

AssetResolver::AssetResolver(const ResolverConfig& config) : config_(config) {
  if (config_.working_directory.empty()) {
    config_.working_directory = GetCurrentWorkingDirectory();
  }
}

AssetResolver::~AssetResolver() = default;

void AssetResolver::SetWorkingDirectory(const std::string& dir) {
  config_.working_directory = dir;
}

void AssetResolver::AddSearchPath(const std::string& path) {
  config_.search_paths.push_back(path);
}

void AssetResolver::SetSearchPaths(const std::vector<std::string>& paths) {
  config_.search_paths = paths;
}

void AssetResolver::ClearSearchPaths() {
  config_.search_paths.clear();
}

void AssetResolver::SetCustomResolver(ResolverCallback callback) {
  custom_resolver_ = std::move(callback);
}

ResolvedAsset AssetResolver::Resolve(const std::string& asset_path,
                                      const std::string& anchor_path) const {
  return ResolveInternal(asset_path, anchor_path);
}

std::string AssetResolver::ResolvePath(const std::string& asset_path,
                                        const std::string& anchor_path) const {
  ResolvedAsset resolved = Resolve(asset_path, anchor_path);
  return resolved.resolved_path;
}

bool AssetResolver::Exists(const std::string& asset_path,
                           const std::string& anchor_path) const {
  ResolvedAsset resolved = Resolve(asset_path, anchor_path);
  return resolved.exists;
}

std::string AssetResolver::CreateIdentifier(const std::string& asset_path,
                                             const std::string& anchor_path) const {
  return ResolvePath(asset_path, anchor_path);
}

ResolvedAsset AssetResolver::ResolveInternal(const std::string& asset_path,
                                              const std::string& anchor_path) const {
  ResolvedAsset result;
  result.original_path = asset_path;

  if (asset_path.empty()) {
    return result;
  }

  // Check for package path (USDZ)
  if (IsPackagePath(asset_path)) {
    result.is_package = true;
    if (ParsePackagePath(asset_path, &result.package_path, &result.asset_in_package)) {
      // Resolve the package file itself
      ResolvedAsset pkg = ResolveInternal(result.package_path, anchor_path);
      if (pkg.exists) {
        result.resolved_path = asset_path;
        result.package_path = pkg.resolved_path;
        result.exists = true;  // Assume asset exists in package if package exists
      }
    }
    return result;
  }

  // Try custom resolver first
  if (custom_resolver_) {
    std::string custom_result = custom_resolver_(asset_path, anchor_path);
    if (!custom_result.empty()) {
      result.resolved_path = NormalizePath(custom_result);
      // A custom resolver returning a path asserts the asset resolved; its
      // result need not be a filesystem path (e.g. a wasm asset-cache key),
      // so don't second-guess it with a file probe.
      result.exists = true;
      return result;
    }
  }

  // Absolute path
  if (IsAbsolutePath(asset_path)) {
    if (config_.allow_absolute_paths) {
      result.resolved_path = NormalizePath(asset_path);
      result.exists = FileExists(result.resolved_path);
    }
    return result;
  }

  // Relative path - try relative to anchor first
  if (!anchor_path.empty()) {
    std::string anchor_dir = GetDirectory(anchor_path);
    std::string candidate = JoinPath(anchor_dir, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  // Try relative to working directory
  {
    std::string candidate = JoinPath(config_.working_directory, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  // Search in search paths
  for (const auto& search_path : config_.search_paths) {
    std::string candidate = JoinPath(search_path, asset_path);
    candidate = NormalizePath(candidate);
    if (FileExists(candidate)) {
      result.resolved_path = candidate;
      result.exists = true;
      return result;
    }
  }

  // Not found - return original path normalized
  result.resolved_path = NormalizePath(asset_path);
  result.exists = false;
  return result;
}

bool AssetResolver::FileExists(const std::string& path) const {
  return FileExistsImpl(path);
}

// ============================================================
// Static path utilities
// ============================================================

bool AssetResolver::IsAbsolutePath(const std::string& path) {
  if (path.empty()) return false;

#ifdef _WIN32
  // Windows: C:\ or \\network or /path
  if (path.length() >= 2 && path[1] == ':') return true;
  if (path.length() >= 2 && path[0] == '\\' && path[1] == '\\') return true;
#endif

  return path[0] == '/' || path[0] == '\\';
}

bool AssetResolver::IsPackagePath(const std::string& path) {
  // Package paths look like: /path/to/file.usdz[asset.usda]
  size_t bracket = path.find('[');
  if (bracket == std::string::npos) return false;
  return path.find(']', bracket) != std::string::npos;
}

bool AssetResolver::ParsePackagePath(const std::string& path,
                                      std::string* package_file,
                                      std::string* asset_in_package) {
  size_t bracket_start = path.find('[');
  if (bracket_start == std::string::npos) return false;

  size_t bracket_end = path.find(']', bracket_start);
  if (bracket_end == std::string::npos) return false;

  *package_file = path.substr(0, bracket_start);
  *asset_in_package = path.substr(bracket_start + 1, bracket_end - bracket_start - 1);
  return true;
}

std::string AssetResolver::JoinPath(const std::string& base, const std::string& path) {
  if (base.empty()) return path;
  if (path.empty()) return base;

  char last = base.back();
  if (last == '/' || last == '\\') {
    return base + path;
  }
  return base + PATH_SEPARATOR + path;
}

std::string AssetResolver::GetDirectory(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return ".";
  return path.substr(0, pos);
}

std::string AssetResolver::GetFilename(const std::string& path) {
  size_t pos = path.find_last_of("/\\");
  if (pos == std::string::npos) return path;
  return path.substr(pos + 1);
}

std::string AssetResolver::NormalizePath(const std::string& path) {
  if (path.empty()) return path;

  std::string result = path;

  // Normalize separators
  for (char& c : result) {
    if (c == ALT_PATH_SEPARATOR) {
      c = PATH_SEPARATOR;
    }
  }

  // Handle . and ..
  std::vector<std::string> parts;
  size_t start = 0;

  // Preserve leading slashes for absolute paths
  size_t prefix_len = 0;
  if (!result.empty() && result[0] == PATH_SEPARATOR) {
    prefix_len = 1;
#ifdef _WIN32
    if (result.length() >= 2 && result[1] == PATH_SEPARATOR) {
      prefix_len = 2;  // UNC path
    }
#endif
  }
#ifdef _WIN32
  // Handle drive letter
  if (result.length() >= 2 && result[1] == ':') {
    prefix_len = 2;
    if (result.length() >= 3 && result[2] == PATH_SEPARATOR) {
      prefix_len = 3;
    }
  }
#endif

  start = prefix_len;

  while (start < result.length()) {
    size_t end = result.find(PATH_SEPARATOR, start);
    if (end == std::string::npos) {
      end = result.length();
    }

    std::string part = result.substr(start, end - start);

    if (part == "..") {
      if (!parts.empty() && parts.back() != "..") {
        parts.pop_back();
      } else if (prefix_len == 0) {
        parts.push_back(part);
      }
    } else if (part != "." && !part.empty()) {
      parts.push_back(part);
    }

    start = end + 1;
  }

  // Reconstruct path
  std::string normalized = result.substr(0, prefix_len);
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0 || (prefix_len > 0 && normalized.back() != PATH_SEPARATOR)) {
      normalized += PATH_SEPARATOR;
    }
    normalized += parts[i];
  }

  if (normalized.empty()) {
    normalized = ".";
  }

  return normalized;
}

std::string AssetResolver::MakeRelative(const std::string& path, const std::string& base) {
  std::string norm_path = NormalizePath(path);
  std::string norm_base = NormalizePath(base);

  // Simple implementation - just check if path starts with base
  if (norm_path.find(norm_base) == 0) {
    size_t base_len = norm_base.length();
    if (base_len < norm_path.length()) {
      if (norm_path[base_len] == PATH_SEPARATOR) {
        return norm_path.substr(base_len + 1);
      }
    }
  }

  return path;
}

// ============================================================
// Global resolver
// ============================================================

AssetResolver& GetDefaultResolver() {
  if (!g_default_resolver) {
    g_default_resolver = std::make_unique<AssetResolver>();
  }
  return *g_default_resolver;
}

void SetDefaultResolver(std::unique_ptr<AssetResolver> resolver) {
  g_default_resolver = std::move(resolver);
}

}  // namespace next
}  // namespace tinyusdz
