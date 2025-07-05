// SPDX-License-Identifier: Apache 2.0
// Experimental USD to JSON converter

#include <string>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "nonstd/expected.hpp"
#include "external/jsonhpp/nlohmann/json.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "tinyusdz.hh"

namespace tinyusdz {

///
/// Convert USD Stage to JSON
///
/// @returns JSON string or error message(std::string) when failed to convert.
///
nonstd::expected<std::string, std::string> ToJSON(const tinyusdz::Stage &stage);

///
/// Convert USD Layer to JSON (nlohmann::json object)
///
nlohmann::json ToJSON(const tinyusdz::Layer &layer);

///
/// Convert USD Layer to JSON
///
bool to_json_string(const tinyusdz::Layer &layer, std::string *json_str, std::string *warn, std::string *err);

} // namespace tinyusd
