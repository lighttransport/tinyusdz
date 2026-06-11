// SPDX-License-Identifier: Apache-2.0
#include "config.hh"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <system_error>

#include "external/jsonhpp/nlohmann/json.hpp"

namespace tusdview {
namespace fs = std::filesystem;
namespace {

using json = nlohmann::json;

std::optional<fs::path> PathFromEnv(const char* name) {
  if (const char* value = std::getenv(name)) {
    if (*value) return fs::path(value);
  }
  return std::nullopt;
}

std::optional<fs::path> DefaultConfigPath() {
#if defined(_WIN32)
  if (auto base = PathFromEnv("APPDATA")) return *base / "tusdview" / "config.json";
  if (auto base = PathFromEnv("LOCALAPPDATA")) return *base / "tusdview" / "config.json";
  if (auto base = PathFromEnv("USERPROFILE")) return *base / "tusdview" / "config.json";
  return std::nullopt;
#elif defined(__APPLE__)
  if (auto home = PathFromEnv("HOME")) {
    return *home / "Library" / "Application Support" / "tusdview" / "config.json";
  }
  return std::nullopt;
#else
  if (auto base = PathFromEnv("XDG_CONFIG_HOME")) return *base / "tusdview" / "config.json";
  if (auto home = PathFromEnv("HOME")) return *home / ".config" / "tusdview" / "config.json";
  return std::nullopt;
#endif
}

const json* FindMember(const json& obj, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    auto it = obj.find(key);
    if (it != obj.end()) return &(*it);
  }
  return nullptr;
}

bool ParsePositiveFloat(const json& value, const char* label, float* out, std::string* err) {
  if (!value.is_number()) {
    *err = std::string(label) + " must be a positive number";
    return false;
  }

  const double v = value.get<double>();
  if (!(v > 0.0) || !std::isfinite(v) ||
      v > static_cast<double>(std::numeric_limits<float>::max())) {
    *err = std::string(label) + " must be a positive finite number";
    return false;
  }

  *out = static_cast<float>(v);
  return true;
}

bool ParsePositiveInt(const json& value, const char* label, int* out, std::string* err) {
  if (!value.is_number()) {
    *err = std::string(label) + " must be a positive integer";
    return false;
  }

  const double v = value.get<double>();
  if (!(v > 0.0) || !std::isfinite(v) || std::floor(v) != v ||
      v > static_cast<double>(std::numeric_limits<int>::max())) {
    *err = std::string(label) + " must be a positive integer";
    return false;
  }

  *out = static_cast<int>(v);
  return true;
}

bool ParseBool(const json& value, const char* label, bool* out, std::string* err) {
  if (!value.is_boolean()) {
    *err = std::string(label) + " must be a boolean";
    return false;
  }
  *out = value.get<bool>();
  return true;
}

bool ParseConfigFile(const fs::path& path, StartupConfig* cfg,
                     std::vector<std::string>* warnings, std::string* err) {
  std::ifstream ifs(path, std::ios::binary);
  if (!ifs) {
    *err = "failed to open config file";
    return false;
  }

  const std::string text((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
  json root = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (root.is_discarded() || !root.is_object()) {
    *err = "expected a top-level JSON object";
    return false;
  }

  if (const json* fontSize = FindMember(root, {"font_size", "font-size"})) {
    float px = 0.0f;
    if (!ParsePositiveFloat(*fontSize, "font_size", &px, err)) return false;
    cfg->fontSizePx = px;
  }

  if (const json* windowScale = FindMember(root, {"window_scale", "window-scale"})) {
    float scale = 0.0f;
    if (!ParsePositiveFloat(*windowScale, "window_scale", &scale, err)) return false;
    cfg->windowScale = scale;
  }

  if (const json* windowSize = FindMember(root, {"window_size", "window-size"})) {
    if (!windowSize->is_object()) {
      *err = "window_size must be an object with width and height";
      return false;
    }

    const json* width = FindMember(*windowSize, {"width"});
    const json* height = FindMember(*windowSize, {"height"});
    if (!width || !height) {
      *err = "window_size must contain width and height";
      return false;
    }

    int w = 0;
    int h = 0;
    if (!ParsePositiveInt(*width, "window_size.width", &w, err)) return false;
    if (!ParsePositiveInt(*height, "window_size.height", &h, err)) return false;
    cfg->windowWidth = w;
    cfg->windowHeight = h;

    if (cfg->windowScale) {
      warnings->push_back("window_size overrides window_scale.");
    }
  }

  const json* nav = FindMember(root, {"navigation"});
  const json* navObj = (nav && nav->is_object()) ? nav : &root;

  if (const json* orbit = FindMember(*navObj, {"orbit_sensitivity", "orbit-sensitivity"})) {
    float v = 0.0f;
    if (!ParsePositiveFloat(*orbit, "orbit_sensitivity", &v, err)) return false;
    cfg->orbitSensitivity = v;
  }
  if (const json* pan = FindMember(*navObj, {"pan_sensitivity", "pan-sensitivity"})) {
    float v = 0.0f;
    if (!ParsePositiveFloat(*pan, "pan_sensitivity", &v, err)) return false;
    cfg->panSensitivity = v;
  }
  if (const json* dolly = FindMember(*navObj, {"dolly_sensitivity", "dolly-sensitivity"})) {
    float v = 0.0f;
    if (!ParsePositiveFloat(*dolly, "dolly_sensitivity", &v, err)) return false;
    cfg->dollySensitivity = v;
  }
  if (const json* invert = FindMember(*navObj, {"invert_dolly", "invert-dolly"})) {
    bool on = false;
    if (!ParseBool(*invert, "invert_dolly", &on, err)) return false;
    cfg->invertDolly = on;
  }

  if (const json* comp = FindMember(root, {"composition"})) {
    bool on = false;
    if (!ParseBool(*comp, "composition", &on, err)) return false;
    cfg->composition = on;
  }
  if (const json* pl = FindMember(root, {"payload_policy", "payload-policy"})) {
    if (!pl->is_string()) {
      *err = "payload_policy must be \"defer\" or \"load\"";
      return false;
    }
    const std::string v = pl->get<std::string>();
    if (v != "defer" && v != "load") {
      *err = "payload_policy must be \"defer\" or \"load\"";
      return false;
    }
    cfg->payloadPolicy = v;
  }

  return true;
}

}  // namespace

ConfigLoadResult LoadStartupConfig(const std::optional<std::string>& explicitPath) {
  ConfigLoadResult result;
  result.explicitPath = explicitPath.has_value();

  std::optional<fs::path> path;
  if (explicitPath) {
    path = fs::path(*explicitPath);
  } else {
    path = DefaultConfigPath();
  }

  if (!path) return result;
  result.path = *path;

  std::error_code ec;
  const bool exists = fs::exists(result.path, ec);
  if (ec) {
    result.status = ConfigLoadStatus::Error;
    result.error = "failed to query config path";
    return result;
  }

  if (!exists) {
    if (result.explicitPath) {
      result.status = ConfigLoadStatus::Error;
      result.error = "config file not found";
    }
    return result;
  }

  if (!fs::is_regular_file(result.path, ec)) {
    result.status = ConfigLoadStatus::Error;
    result.error = ec ? "failed to inspect config path" : "config path is not a regular file";
    return result;
  }

  if (!ParseConfigFile(result.path, &result.config, &result.warnings, &result.error)) {
    result.status = ConfigLoadStatus::Error;
    return result;
  }

  result.status = ConfigLoadStatus::Loaded;
  return result;
}

}  // namespace tusdview
