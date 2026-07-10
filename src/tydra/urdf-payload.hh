// SPDX-License-Identifier: Apache 2.0
// Copyright 2026 - Present Light Transport Entertainment Inc.

#pragma once

#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {
namespace detail {

// Schema-neutral, validated view of the JSON exchanged by the browser URDF
// and MJCF frontends. Both legacy and next-core authors consume this object so
// section defaults and source-format detection cannot drift.
struct URDFPayload {
  nlohmann::json root;
  nlohmann::json empty_array = nlohmann::json::array();
  bool mjcf_source{false};

  const nlohmann::json &Array(const char *name) const {
    if (root.contains(name) && root.at(name).is_array()) {
      return root.at(name);
    }
    return empty_array;
  }

  static bool Parse(const std::string &text, URDFPayload *out,
                    std::string *err) {
    if (!out) {
      if (err) *err = "URDF payload output is null";
      return false;
    }
    out->root = nlohmann::json::parse(text, nullptr, false);
    if (out->root.is_discarded() || !out->root.is_object()) {
      if (err) *err = "URDF export JSON parse failed";
      return false;
    }
    auto string_value = [&](const char *key) -> std::string {
      if (!out->root.contains(key) || !out->root.at(key).is_string()) {
        return std::string();
      }
      return out->root.at(key).get<std::string>();
    };
    const std::string source_format = string_value("sourceFormat");
    const std::string input_format = string_value("inputFormat");
    out->mjcf_source = source_format == "mjcf" || source_format == "MJCF" ||
                       input_format == "mjcf" || input_format == "MJCF";
    if (out->Array("links").empty()) {
      if (err) *err = "URDF export JSON has no links";
      return false;
    }
    return true;
  }
};

}  // namespace detail
}  // namespace tydra
}  // namespace tinyusdz
