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

}  // namespace tusdview
