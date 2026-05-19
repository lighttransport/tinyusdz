#pragma once

#include <string>
#include <vector>

#include "../value-types.hh"
#include "../core/prim-metas.hh"

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json_fwd.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {
namespace tydra {

/// Convert a value::Value to structured JSON representation.
///
/// Format:
///   Scalar:   {"type": "float", "value": 1.5}
///   Compound: {"type": "float3", "value": [1, 2, 3]}
///   Array:    {"type": "float3[]", "value": [[1,2,3], [4,5,6]]}
///   Matrix:   {"type": "matrix4d", "value": [[1,0,0,0],...]}
///   Quat:     {"type": "quatf", "value": [x, y, z, w]}
///   Asset:    {"type": "asset", "value": {"assetPath":"...", "resolvedPath":"..."}}
///   Dict:     {"type": "dictionary", "value": {k: v, ...}}
///   Blocked:  {"type": "None"}
///   TimeSamples: {"type": "timesamples", "value": {t: {type, value}, ...}}
nlohmann::json ValueToJSON(const value::Value &val);

/// Convert JSON back to value::Value.
nonstd::optional<value::Value> JSONToValue(const nlohmann::json &j,
                                           std::string *err = nullptr);

/// Convert to plain JSON (no type wrapper) for convenience.
nlohmann::json ValueToPlainJSON(const value::Value &val);

/// Get a JSON Schema description for a USD type name.
nlohmann::json ValueTypeToJSONSchema(const std::string &type_name);

/// Convert PrimMeta to JSON
nlohmann::json PrimMetaToJSON(const PrimMeta &meta);

/// List all known role type names
std::vector<std::string> GetRoleTypeNames();

/// List all known prim type names
std::vector<std::string> GetPrimTypeNames();

} // namespace tydra
} // namespace tinyusdz
