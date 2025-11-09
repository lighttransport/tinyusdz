// SPDX-License-Identifier: Apache 2.0
// Experimental JSON to USD converter

#include <string>

#include "tinyusdz.hh"

namespace tinyusdz {

///
/// Convert JSON string to USD Stage
///
///
bool JSONToStage(const std::string &json_string, tinyusdz::Stage *stage, std::string *warn, std::string *err);

///
/// Convert JSON string to USD Prim
///
///
bool JSONToStage(const std::string &json_string, tinyusdz::Prim *prim, std::string *warn, std::string *err);

///
/// Convert JSON string to USD Layer
///
///
bool JSONToLayer(const std::string &json_string, tinyusdz::Layer *layer, std::string *warn, std::string *err);

///
/// Convert JSON string to PrimSpec
///
///
bool JSONToPrimSpec(const std::string &json_string, tinyusdz::PrimSpec *ps, std::string *warn, std::string *err);

///
/// Convert JSON object to GeomMesh
///
///
bool JSONToGeomMesh(const std::string &json_string, tinyusdz::GeomMesh *mesh, std::string *warn, std::string *err);

} // namespace tinyusd
