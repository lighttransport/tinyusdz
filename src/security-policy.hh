// SPDX-License-Identifier: Apache 2.0
// Centralized security limits/policies used across loaders and converters.
#pragma once

#include <algorithm>
#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace tinyusdz {
namespace security_policy {

// MCP limits
constexpr size_t kMCPMaxRequestBodyBytes = 16 * 1024 * 1024;
constexpr size_t kMCPMaxBase64InputBytes = 64 * 1024 * 1024;
constexpr size_t kMCPMaxBase64DecodedBytes = 48 * 1024 * 1024;

// JSON->USD conversion limits
constexpr size_t kJSONMaxBase64InputChars = 96 * 1024 * 1024;
constexpr size_t kJSONMaxDecodedBytes = 64 * 1024 * 1024;

// Raw asset reads through resolver paths (render/material/texture conversions).
constexpr size_t kResolverMaxAssetReadBytes = 512 * 1024 * 1024;

inline bool EstimateBase64DecodedSize(const std::string &data,
                                      size_t *decoded_size) {
  if (!decoded_size) {
    return false;
  }
  if ((data.size() % 4) != 0) {
    return false;
  }

  size_t padding = 0;
  if (!data.empty() && data[data.size() - 1] == '=') {
    padding++;
    if ((data.size() > 1) && (data[data.size() - 2] == '=')) {
      padding++;
    }
  }

  size_t q = data.size() / 4;
  if (q > (std::numeric_limits<size_t>::max() / 3)) {
    return false;
  }
  size_t raw = q * 3;
  if (raw < padding) {
    return false;
  }

  *decoded_size = raw - padding;
  return true;
}

// Validates an asset path as "relative and contained":
//   - must not be empty
//   - must not start with '/' or a Windows drive (e.g. "C:")
//   - must not contain any '..' segment
// On success, writes the slash-normalized path (backslashes -> '/', empty
// and '.' segments removed) into *out and returns true.
inline bool ValidateAndNormalizeAssetPath(const std::string &path,
                                          std::string *out) {
  if (!out) {
    return false;
  }
  if (path.empty()) {
    return false;
  }

  std::string p = path;
  std::replace(p.begin(), p.end(), '\\', '/');

  if (!p.empty() && p[0] == '/') {
    return false;
  }

  if (p.size() >= 2 &&
      ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
      p[1] == ':') {
    return false;
  }

  std::stringstream ss(p);
  std::string part;
  std::vector<std::string> parts;
  while (std::getline(ss, part, '/')) {
    if (part.empty() || part == ".") {
      continue;
    }
    if (part == "..") {
      return false;
    }
    parts.push_back(std::move(part));
  }

  if (parts.empty()) {
    return false;
  }

  std::string result;
  for (size_t i = 0; i < parts.size(); i++) {
    if (i > 0) {
      result.push_back('/');
    }
    result += parts[i];
  }

  *out = std::move(result);
  return true;
}

// bool-only convenience wrapper.
inline bool IsSafeRelativeAssetPath(const std::string &path) {
  std::string discard;
  return ValidateAndNormalizeAssetPath(path, &discard);
}

} // namespace security_policy
} // namespace tinyusdz

