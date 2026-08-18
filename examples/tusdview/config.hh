// SPDX-License-Identifier: Apache-2.0
// tusdview - startup JSON config loading.
#pragma once

#include <filesystem>
#include <map>
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
  std::optional<bool> invertYaw;
  std::optional<bool> invertDolly;
  std::optional<std::string> cameraConform;
  std::optional<bool> adaptiveQuality;
  std::optional<float> targetRenderFps;
  std::optional<float> minRenderScale;
  // USD composition: enable arc composition on load; payload policy
  // ("defer" = lazy load on demand, "load" = eager).
  std::optional<bool> composition;
  std::optional<std::string> payloadPolicy;  // "defer" | "load"
  // Vulkan physical device selector: index ("0") or case-insensitive substring
  // of device/driver text ("nvidia", "rtx", "llvmpipe").
  std::optional<std::string> vulkanDevice;
  std::optional<int> subdivisionLevel;
  std::optional<bool> subdivisionAuto;
  std::optional<int> subdivisionAutoMaxLevel;
  std::map<std::string, int> subdivisionPrimLevels;
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

// Default per-user ImGui state path, colocated with tusdview config.json.
std::optional<std::filesystem::path> DefaultImGuiIniPath();

// Persist the recently-opened scene list into the config file at `path`, merging
// into any existing JSON (other keys preserved). Creates parent dirs as needed.
// Returns false (with *err set) on failure.
bool SaveRecentScenes(const std::filesystem::path& path,
                      const std::vector<std::string>& recent, std::string* err);

}  // namespace tusdview
