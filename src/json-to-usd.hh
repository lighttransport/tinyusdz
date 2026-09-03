// SPDX-License-Identifier: Apache 2.0
// Experimental JSON to USD converter

#include <string>

#include "lightusd.hh"
#include "usdGeom.hh"  // GeomMesh etc. (no longer re-exported by lightusd.hh)

namespace lightusd {

///
/// Convert JSON string to USD Stage
///
///
bool JSONToStage(const std::string &json_string, lightusd::Stage *stage, std::string *warn, std::string *err);

///
/// Convert JSON string to USD Prim
///
///
bool JSONToStage(const std::string &json_string, lightusd::Prim *prim, std::string *warn, std::string *err);

///
/// Convert JSON string to USD Layer
///
///
bool JSONToLayer(const std::string &json_string, lightusd::Layer *layer, std::string *warn, std::string *err);

///
/// Convert JSON string to PrimSpec
///
///
bool JSONToPrimSpec(const std::string &json_string, lightusd::PrimSpec *ps, std::string *warn, std::string *err);

///
/// Convert JSON object to GeomMesh
///
///
bool JSONToGeomMesh(const std::string &json_string, lightusd::GeomMesh *mesh, std::string *warn, std::string *err);

} // namespace lightusd
