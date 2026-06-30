// SPDX-License-Identifier: Apache-2.0
// tusdview - startup JSON config loading.
#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace tusdview {

struct StartupConfig {
  std::optional<float> fontSizePx;
  std::optional<float> windowScale;
  std::optional<int> windowWidth;
  std::optional<int> windowHeight;
  std::optional<float> orbitSensitivity;
  std::optional<float> panSensitivity;
  std::optional<float> dollySensitivity;
  std::optional<bool> invertDolly;
  // USD composition: enable arc composition on load; payload policy
  // ("defer" = lazy load on demand, "load" = eager).
  std::optional<bool> composition;
  std::optional<std::string> payloadPolicy;  // "defer" | "load"
  // Most-recently-opened scene paths (newest first), persisted across sessions.
  std::vector<std::string> recentScenes;
};

enum class ConfigLoadStatus {
  NotFound,
  Loaded,
  Error,
};

struct ConfigLoadResult {
  ConfigLoadStatus status{ConfigLoadStatus::NotFound};
  std::filesystem::path path;
  StartupConfig config;
  std::vector<std::string> warnings;
  std::string error;
  bool explicitPath{false};
};

ConfigLoadResult LoadStartupConfig(const std::optional<std::string>& explicitPath);

// Persist the recently-opened scene list into the config file at `path`, merging
// into any existing JSON (other keys preserved). Creates parent dirs as needed.
// Returns false (with *err set) on failure.
bool SaveRecentScenes(const std::filesystem::path& path,
                      const std::vector<std::string>& recent, std::string* err);

}  // namespace tusdview
