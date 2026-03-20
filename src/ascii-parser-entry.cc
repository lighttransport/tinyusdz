// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2022, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment, Inc.
//
// Entry-point functions split from ascii-parser.cc:
// ParseVariantSet, ParseBlock, Parse, and supporting utilities.

#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <atomic>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#if defined(__wasi__)
#else
#include <mutex>
#include <thread>
#endif
#include <unordered_map>
#include <vector>

#include "ascii-parser.hh"
#include "parser-timing.hh"
#include "path-util.hh"
#include "str-util.hh"
#include "tiny-format.hh"

#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external
#include "nonstd/expected.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "common-macros.inc"

#include "io-util.hh"
#include "pprint-enum.hh"
#include "core/prim-spec.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"

namespace tinyusdz {
namespace ascii {

extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<bool>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<int32_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::int2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::int3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::int4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<uint32_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::uint2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::uint3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::uint4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<int64_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<uint64_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::half>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::half2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::half3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::half4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<float>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::float2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::float3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::float4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<double>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::double2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::double3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::double4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::quath>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::quatf>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::quatd>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord2h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord2f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord2d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::texcoord3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::point3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::point3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::point3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::normal3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::normal3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::normal3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::vector3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::vector3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::vector3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color4h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color4f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::color4d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix2f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix4f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix2d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::matrix4d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::token>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::StringData>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<std::string>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<Reference>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<Payload>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<Path>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<value::AssetPath>> *result);

extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<bool> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<int32_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::int2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::int3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::int4> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<uint32_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::uint2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::uint3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::uint4> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<int64_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<uint64_t> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::half> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::half2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::half3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::half4> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<float> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::float2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::float3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::float4> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<double> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::double2> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::double3> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::double4> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::quath> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::quatf> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::quatd> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord2h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord2f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord2d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord3h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::texcoord3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::point3h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::point3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::point3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::normal3h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::normal3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::normal3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::vector3h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::vector3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::vector3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color3h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color4h> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color4f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::color4d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix2f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix3f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix4f> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix2d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix3d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::matrix4d> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::token> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::StringData> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<std::string> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<Reference> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<Payload> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<Path> *result);
extern template bool AsciiParser::ParseBasicTypeArray(
    std::vector<value::AssetPath> *result);


constexpr auto kAscii = "[ASCII]";

// Keyword database for fix suggestions (Priority 5)
// Contains common USD specifiers, types, and keywords
// Using C-style array to avoid static initialization requirements
static constexpr const char* g_usd_keywords[] = {
  // Specifiers
  "def", "over", "class",
  // Variability
  "uniform", "varying", "token",
  // Metadata indicators
  "custom", "documentation", "doc", "comment",
  // List edit qualifiers
  "add", "delete", "reorder", "append", "prepend",
  // Relationship indicators
  "rel", "relationship",
  // Attribute modifiers
  "timeVarying",
  // Common scalar types
  "bool", "byte", "ubyte", "int", "uint", "long", "ulong",
  "half", "float", "double", "string", "asset",
  // Vector types
  "int2", "int3", "int4",
  "uint2", "uint3", "uint4",
  "float2", "float3", "float4",
  "double2", "double3", "double4",
  "half2", "half3", "half4",
  // Color types
  "color3h", "color3f", "color3d",
  "color4h", "color4f", "color4d",
  // Matrix types
  "matrix2f", "matrix3f", "matrix4f",
  "matrix2d", "matrix3d", "matrix4d",
  // Geometric types
  "point3h", "point3f", "point3d",
  "vector3h", "vector3f", "vector3d",
  "normal3h", "normal3f", "normal3d",
  "texcoord2h", "texcoord2f", "texcoord2d",
  "texcoord3h", "texcoord3f", "texcoord3d",
  "quath", "quatf", "quatd",
  // Special types
  "path", "reference", "payload",
  // Prim types (common)
  "Mesh", "Sphere", "Cube", "Cylinder", "Cone",
  "Xform", "Scope", "Group", "Assembly",
  "Light", "SphereLight", "RectLight", "DomeLight",
  "Material", "Shader", "Texture",
  "BasisCurves", "PointInstancer", "Points",
  // Attributes (common)
  "points", "normals", "primvars", "indices",
  "extent", "visibility", "purpose", "kind",
  "interpolation", "faceVertexCounts", "faceVertexIndices",
  // Metadata field names
  "timeSamples", "connect", "customData",
  "subLayers", "defaultPrim", "upAxis",
  // Time/frame related
  "timeCodesPerSecond", "startTimeCode", "endTimeCode",
  "framesPerSecond", "metersPerUnit", "kilogramsPerUnit"
};

static constexpr size_t g_usd_keywords_count =
    sizeof(g_usd_keywords) / sizeof(g_usd_keywords[0]);


static void RegisterStageMetas(
    std::unordered_map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();
  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");
  metas["documentation"] =
      AsciiParser::VariableDef(value::kString, "doc");  // alias to 'doc'

  metas["comment"] = AsciiParser::VariableDef(value::kString, "comment");

  // TODO: both support float and double?
  metas["metersPerUnit"] =
      AsciiParser::VariableDef(value::kDouble, "metersPerUnit");
  metas["timeCodesPerSecond"] =
      AsciiParser::VariableDef(value::kDouble, "timeCodesPerSecond");
  metas["framesPerSecond"] =
      AsciiParser::VariableDef(value::kDouble, "framesPerSecond");

  metas["startTimeCode"] =
      AsciiParser::VariableDef(value::kDouble, "startTimeCode");
  metas["endTimeCode"] =
      AsciiParser::VariableDef(value::kDouble, "endTimeCode");

  metas["defaultPrim"] = AsciiParser::VariableDef(value::kToken, "defaultPrim");
  metas["upAxis"] = AsciiParser::VariableDef(value::kToken, "upAxis");
  metas["customLayerData"] =
      AsciiParser::VariableDef(value::kDictionary, "customLayerData");

  // Composition arc.
  // Type can be array. i.e. asset, asset[]
  metas["subLayers"] = AsciiParser::VariableDef(value::kAssetPath, "subLayers",
                                                /* allow array type */ true);

  // UsdPhysics
  metas["kilogramsPerUnit"] =
      AsciiParser::VariableDef(value::kDouble, "kilogramsPerUnit");

  // USDZ extension
  metas["autoPlay"] = AsciiParser::VariableDef(value::kBool, "autoPlay");
  metas["playbackMode"] =
      AsciiParser::VariableDef(value::kToken, "playbackMode");
}

static void RegisterPrimMetas(
    std::unordered_map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["kind"] = AsciiParser::VariableDef(value::kToken, "kind");
  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");

  //
  // Composition arcs -----------------------
  //

  // Type can be array. i.e. path, path[]
  metas["references"] = AsciiParser::VariableDef("Reference", "references",
                                                 /* allow array type */ true);

  // TODO: Use relatioship type?
  metas["inherits"] = AsciiParser::VariableDef(value::kPath, "inherits", true);
  metas["payload"] = AsciiParser::VariableDef("Payload", "payload", true);
  metas["specializes"] =
      AsciiParser::VariableDef(value::kPath, "specializes", true);

  // Use `string`
  metas["variantSets"] = AsciiParser::VariableDef(value::kString, "variantSets",
                                                  /* allow array type */ true);

  // Parse as dict. TODO: Use ParseVariants()
  metas["variants"] = AsciiParser::VariableDef(value::kDictionary, "variants");

  // ------------------------------------------

  metas["assetInfo"] =
      AsciiParser::VariableDef(value::kDictionary, "assetInfo");
  metas["customData"] =
      AsciiParser::VariableDef(value::kDictionary, "customData");

  metas["active"] = AsciiParser::VariableDef(value::kBool, "active");
  metas["hidden"] = AsciiParser::VariableDef(value::kBool, "hidden");
  metas["instanceable"] =
      AsciiParser::VariableDef(value::kBool, "instanceable");

  // ListOp
  metas["apiSchemas"] = AsciiParser::VariableDef(
      value::Add1DArraySuffix(value::kToken), "apiSchemas");

  // usdShade
  // NOTE: items are expected to be all string type.
  metas["sdrMetadata"] =
      AsciiParser::VariableDef(value::kDictionary, "sdrMetadata");

  metas["clips"] = AsciiParser::VariableDef(value::kDictionary, "clips");

  // USDZ extension
  metas["sceneName"] = AsciiParser::VariableDef(value::kString, "sceneName");

  // Builtin from pxrUSD 23.xx
  metas["displayName"] =
      AsciiParser::VariableDef(value::kString, "displayName");

}

static void RegisterPropMetas(
    std::unordered_map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");
  metas["active"] = AsciiParser::VariableDef(value::kBool, "active");
  metas["hidden"] = AsciiParser::VariableDef(value::kBool, "hidden");
  metas["customData"] =
      AsciiParser::VariableDef(value::kDictionary, "customData");

  // for sparse primvars
  metas["unauthoredValuesIndex"] =
      AsciiParser::VariableDef(value::kInt, "unauthoredValuesIndex");

  // usdSkel
  metas["elementSize"] = AsciiParser::VariableDef(value::kInt, "elementSize");

  // usdSkel inbetween BlendShape
  // use Double in TinyUSDZ. its float type in pxrUSD.
  metas["weight"] = AsciiParser::VariableDef(value::kDouble, "weight");

  // usdShade?
  metas["colorSpace"] = AsciiParser::VariableDef(value::kToken, "colorSpace");

  metas["interpolation"] =
      AsciiParser::VariableDef(value::kToken, "interpolation");

  // usdShade
  metas["bindMaterialAs"] =
      AsciiParser::VariableDef(value::kToken, "bindMaterialAs");
  metas["connectability"] =
      AsciiParser::VariableDef(value::kToken, "connectability");
  metas["renderType"] = AsciiParser::VariableDef(value::kToken, "renderType");
  metas["outputName"] = AsciiParser::VariableDef(value::kToken, "outputName");
  metas["sdrMetadata"] =
      AsciiParser::VariableDef(value::kDictionary, "sdrMetadata");

  // Builtin from pxrUSD 23.xx
  metas["displayName"] =
      AsciiParser::VariableDef(value::kString, "displayName");

  // Builtin from pxrUSD 24.xx?
  metas["displayGroup"] =
      AsciiParser::VariableDef(value::kString, "displayGroup");
}

// Shared implementation lives in value-types.hh:
// value::RegisterPrimAttrTypes<SetType>(d, include_variant_set)

static void RegisterPrimTypes(std::unordered_set<std::string> &d) {
  d.insert("Xform");
  d.insert("Sphere");
  d.insert("Cube");
  d.insert("Cone");
  d.insert("Cylinder");
  d.insert("Capsule");
  d.insert("BasisCurves");
  d.insert("Mesh");
  d.insert("Points");
  d.insert("GeomSubset");
  d.insert("Scope");
  d.insert("Material");
  d.insert("NodeGraph");
  d.insert("Shader");
  d.insert("SphereLight");
  d.insert("DomeLight");
  d.insert("DiskLight");
  d.insert("DistantLight");
  d.insert("RectLight");
  d.insert("CylinderLight");
  d.insert("GeometryLight");
  d.insert("PortalLight");
  d.insert("Camera");
  d.insert("SkelRoot");
  d.insert("Skeleton");
  d.insert("SkelAnimation");
  d.insert("BlendShape");

  d.insert("GPrim");
}

// TinyUSDZ does not allow user-defined API schema at the moment
// (Primarily for security reason, secondary it requires re-design of Prim
// classes to support user-defined API schema)
static void RegisterAPISchemas(std::unordered_set<std::string> &d) {
  d.insert("MaterialBindingAPI");
  d.insert("SkelBindingAPI");

  // d.insert("PhysicsCollisionAPI");
  // d.insert("PhysicsRigidBodyAPI");

  // TODO: Support Multi-apply API(`CollectionAPI`)
  // d.insert("PhysicsLimitAPI");
  // d.insert("PhysicsDriveAPI");
  // d.insert("CollectionAPI");
}

std::string AsciiParser::GetCurrentPrimPath() {
  if (_path_stack.empty()) {
    return "/";
  }

  return _path_stack.top();
}

//
// -- ctor, dtor
//

AsciiParser::AsciiParser() { Setup(); }

AsciiParser::AsciiParser(StreamReader *sr) : _sr(sr) { Setup(); }

std::string AsciiParser::GenerateSuggestion(const std::string& invalid_token) {
  // Only generate suggestions if feature is enabled and token is not empty
  if (!TINYUSDZ_ENABLE_SUGGEST_FIX || invalid_token.empty()) {
    return "";
  }

  // Convert C-style keyword array to vector for string similarity matching
  std::vector<std::string> keywords(g_usd_keywords,
                                     g_usd_keywords + g_usd_keywords_count);

  // Find closest matching keyword
  std::string best_match = string_similarity::FindClosestMatch(
      invalid_token, keywords, 0.6);  // 0.6 = 60% similarity threshold

  if (!best_match.empty()) {
    return fmt::format("Did you mean '{}'?", best_match);
  }

  return "";
}

void AsciiParser::Setup() {
  RegisterStageMetas(_supported_stage_metas);
  RegisterPrimMetas(_supported_prim_metas);
  RegisterPropMetas(_supported_prop_metas);
  value::RegisterPrimAttrTypes(_supported_prim_attr_types, /* include_variant_set */ true);
  RegisterPrimTypes(_supported_prim_types);
  RegisterAPISchemas(_supported_api_schemas);
}

bool AsciiParser::ReportProgress() {
  // Check if callback exists and is callable
  if (!_progress_callback) {
    return true;  // No callback, continue parsing
  }

  if (!_sr) {
    return true;  // No stream reader, can't compute progress
  }

  // Calculate progress based on current position in stream
  uint64_t current_pos = _sr->tell();
  uint64_t total_size = _sr->size();

  float progress = 0.0f;
  if (total_size > 0) {
    progress = static_cast<float>(current_pos) / static_cast<float>(total_size);
    // Clamp to [0, 1] range
    if (progress > 1.0f) progress = 1.0f;
    if (progress < 0.0f) progress = 0.0f;
  }

  // Call the callback and return its result
  return _progress_callback(progress, _progress_userptr);
}

AsciiParser::~AsciiParser() {}

bool AsciiParser::CheckHeader() { return ParseMagicHeader(); }

bool AsciiParser::IsRegisteredPrimMeta(const std::string &name) {
  return _supported_prim_metas.count(name) ? true : false;
}

bool AsciiParser::IsStageMeta(const std::string &name) {
  return _supported_stage_metas.count(name) ? true : false;
}

bool AsciiParser::ParseVariantSet(
    const int64_t primIdx, const int64_t parentPrimIdx, const uint32_t depth,
    VariantSetContent *variantSetContentOut) {

  if (depth > 512) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "[InternalError] VariantSet nesting too deep (> 512).");
  }

  if (!variantSetContentOut) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii,
                              "[InternalError] variantSetContentOut arg is nullptr.");
  }

  // variantSet =
  // {
  //   "variantName0" ( metas ) { ... }
  //   "variantName1" ( metas ) { ... }
  //   ...
  // }
  if (!Expect('{')) {
    return false;
  }

  if (!SkipCommentAndWhitespaceAndNewline()) {
    return false;
  }

  VariantSetContent variantSetContent;

  // for each variantStatement
  while (!Eof()) {
    {
      char c;
      if (!Char1(&c)) {
        return false;
      }

      if (c == '}') {
        // end
        break;
      }

      Rewind(1);
    }

    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    // string
    std::string variantName;
    if (!ReadBasicType(&variantName)) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Failed to parse variant name for `variantSet` statement.");
    }

    if (!SkipWhitespace()) {
      return false;
    }

    // Optional: PrimSpec meta
    PrimMetaMap metas;
    {
      char mc;
      if (!LookChar1(&mc)) {
        return false;
      }

      if (mc == '(') {
        if (!ParsePrimMetas(&metas)) {
          PUSH_ERROR_AND_RETURN_TAG(
              kAscii, "Failed to parse PrimSpec metas in variant statement.");
        }
      }
    }

    if (!Expect('{')) {
      return false;
    }

    VariantContent variantContent;

    int64_t variantPrimIdx = _prim_idx_assign_fun(parentPrimIdx);
    //variantContent.variantPrimIdx = variantPrimIdx;
    DCOUT("primIdx for variant = " << variantPrimIdx);

    while (!Eof()) {

      if (!SkipCommentAndWhitespaceAndNewline()) {
        return false;
      }

      {
        char c;
        if (!Char1(&c)) {
          return false;
        }

        if (c == '}') {
          DCOUT("End block in variantSet stmt.");
          // end block
          break;
        }
      }

      if (!Rewind(1)) {
        return false;
      }

      DCOUT("Read first token in VariantSet stmt");
      Identifier tok;
      if (!ReadBasicType(&tok)) {
        PUSH_ERROR_AND_RETURN(
            "Failed to parse an identifier in variantSet block statement.");
      }

      if (tok == "variantSet") {

        if (!SkipWhitespace()) {
          return false;
        }

        std::string childVariantName;
        if (!ReadBasicType(&childVariantName)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `variantSet` statement.");
        }

        DCOUT("childVariantName = " << childVariantName);

        if (!SkipWhitespace()) {
          return false;
        }

        if (!Expect('=')) {
          return false;
        }

        if (!SkipWhitespace()) {
          return false;
        }


        VariantSetContent child_vmap;
        if (!ParseVariantSet(variantPrimIdx, primIdx, depth+1, &child_vmap)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `variantSet` statement.");
        }

        variantContent.variantSets[childVariantName] = child_vmap;

      } else {

        if (!Rewind(tok.size())) {
          return false;
        }

        if (!SkipWhitespace()) {
          return false;
        }

        Specifier child_spec{Specifier::Invalid};
        if (tok == "def") {
          child_spec = Specifier::Def;
        } else if (tok == "over") {
          child_spec = Specifier::Over;
        } else if (tok == "class") {
          child_spec = Specifier::Class;
        }

        // No specifier => Assume properties only.
        // Has specifier => Prim
        if (child_spec != Specifier::Invalid) {
          int64_t idx = _prim_idx_assign_fun(parentPrimIdx);
          DCOUT("enter parseBlock in variantSet. spec = " << to_string(child_spec) << ", idx = "
                                          << idx << ", rootIdx = " << primIdx);

          // recusive call
          if (!ParseBlock(child_spec, idx, primIdx, depth + 1, /* in_variantStmt */true)) {
            PUSH_ERROR_AND_RETURN(
                fmt::format("`{}` block parse failed.", to_string(child_spec)));
          }
          DCOUT(fmt::format("Done parse `{}` block.", to_string(child_spec)));

          DCOUT(fmt::format("Add primIdx {} to variant {}", idx, variantName));
          variantContent.primIndices.push_back(idx);

        } else {
          DCOUT("Enter ParsePrimProps.");
          if (!ParsePrimProps(&variantContent.props, &variantContent.properties)) {
            PUSH_ERROR_AND_RETURN("Failed to parse Prim attribute.");
          }
          DCOUT(fmt::format("Done parse ParsePrimProps."));
        }

        if (!SkipCommentAndWhitespaceAndNewline()) {
          return false;
        }
      }
    }

    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    DCOUT(fmt::format("variantSet item {} parsed.", variantName));



    variantContent.metas = metas;
    variantSetContent.variantSets[variantName] = variantContent;
  }

  variantSetContent.variantPrimIdx = primIdx;

  (*variantSetContentOut) = std::move(variantSetContent);

  return true;
}

///
/// Parse block.
///
/// block = spec prim_type? token metas? { ... }
/// metas = '(' args ')'
///
/// spec = `def`, `over` or `class`
///
///
bool AsciiParser::ParseBlock(const Specifier spec, const int64_t primIdx,
                             const int64_t parentPrimIdx, const uint32_t depth,
                             const bool in_variantStaement) {
  (void)in_variantStaement;

  DCOUT("ParseBlock");

  if (depth > 512) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "[InternalError] Prim definition nesting too deep (> 512).");
  }

  // Report progress and check for cancellation
  if (!ReportProgress()) {
    PUSH_ERROR_AND_RETURN("Parsing cancelled by progress callback.");
  }

  if (!SkipCommentAndWhitespaceAndNewline()) {
    DCOUT("SkipCommentAndWhitespaceAndNewline failed");
    return false;
  }

  Identifier def;
  if (!ReadIdentifier(&def)) {
    DCOUT("ReadIdentifier failed");
    return false;
  }
  DCOUT("spec = " << def);

  if ((def == "def") || (def == "over") || (def == "class")) {
    // ok
  } else {
    PUSH_ERROR_AND_RETURN("Invalid specifier.");
  }

  // Ensure spec and def is same.
  if (def == "def") {
    if (spec != Specifier::Def) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Internal error. Invalid Specifier token combination. def = "
                      << def << ", spec = " << to_string(spec));
    }
  } else if (def == "over") {
    if (spec != Specifier::Over) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Internal error. Invalid Specifier token combination. def = "
                      << def << ", spec = " << to_string(spec));
    }
  } else if (def == "class") {
    if (spec != Specifier::Class) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Internal error. Invalid Specifier token combination. def = "
                      << def << ", spec = " << to_string(spec));
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  // look ahead
  bool has_primtype = false;
  {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (!Rewind(1)) {
      return false;
    }

    if (c == '"') {
      // token
      has_primtype = false;
    } else {
      has_primtype = true;
    }
  }

  Identifier prim_type;

  DCOUT("has_primtype = " << has_primtype);

  if (has_primtype) {
    if (!ReadIdentifier(&prim_type)) {
      return false;
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::string prim_name;
  if (!ReadBasicType(&prim_name)) {
    return false;
  }

  DCOUT("prim name = " << prim_name);
  if (!ValidatePrimElementName(prim_name)) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "Prim name contains invalid chacracter.");
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  PrimMetaMap in_metas;
  {
    // look ahead
    char c;
    if (!LookChar1(&c)) {
      return false;
    }

    if (c == '(') {
      // meta

      if (!ParsePrimMetas(&in_metas)) {
        DCOUT("Parse Prim metas failed.");
        return false;
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }
  }

  if (!SkipCommentAndWhitespaceAndNewline()) {
    return false;
  }

  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::map<std::string, Property> props;
  std::vector<value::token> propNames;
  VariantSetList variantSetList;

  {
    std::string full_path = GetCurrentPrimPath();
    if (full_path == "/") {
      full_path += prim_name;
    } else {
      full_path += "/" + prim_name;
    }
    PushPrimPath(full_path);
  }

  // expect = '}'
  //        | def_block
  //        | prim_attr+
  //        | variantSet '{' ... '}'
  while (!Eof()) {
    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == '}') {
      // end block
      break;
    } else {
      if (!Rewind(1)) {
        return false;
      }

      DCOUT("Read stmt token");
      Identifier tok;
      if (!ReadBasicType(&tok)) {
        // maybe ';'?

        if (LookChar1(&c)) {
          if (c == ';') {
            PUSH_ERROR_AND_RETURN(
                "Semicolon is not allowd in `def` block statement.");
          }
        }
        PUSH_ERROR_AND_RETURN(
            "Failed to parse an identifier in `def` block statement.");
      }

      if (tok == "variantSet") {
        if (!SkipWhitespace()) {
          return false;
        }

        std::string variantName;
        if (!ReadBasicType(&variantName)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `variantSet` statement.");
        }

        DCOUT("variantName = " << variantName);

        if (!SkipWhitespace()) {
          return false;
        }

        if (!Expect('=')) {
          return false;
        }

        if (!SkipWhitespace()) {
          return false;
        }

        int64_t variantPrimIdx = _prim_idx_assign_fun(parentPrimIdx);

        VariantSetContent vs;
        if (!ParseVariantSet(variantPrimIdx, primIdx, depth, &vs)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `variantSet` statement.");
        }

        vs.variantPrimIdx = variantPrimIdx;
        variantSetList[variantName] = vs;

        continue;
      }

      if (!Rewind(tok.size())) {
        return false;
      }

      Specifier child_spec{Specifier::Invalid};
      if (tok == "def") {
        child_spec = Specifier::Def;
      } else if (tok == "over") {
        child_spec = Specifier::Over;
      } else if (tok == "class") {
        child_spec = Specifier::Class;
      }

      if (child_spec != Specifier::Invalid) {
        int64_t idx = _prim_idx_assign_fun(parentPrimIdx);
        DCOUT("enter parseDef. spec = " << to_string(child_spec) << ", idx = "
                                        << idx << ", rootIdx = " << primIdx);

        // recusive call
        if (!ParseBlock(child_spec, idx, primIdx, depth + 1)) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("`{}` block parse failed.", to_string(child_spec)));
        }
        DCOUT(fmt::format("Done parse `{}` block.", to_string(child_spec)));
      } else {
        DCOUT("Enter ParsePrimProps.");
        // Assume PrimAttr
        if (!ParsePrimProps(&props, &propNames)) {
          PUSH_ERROR_AND_RETURN("Failed to parse Prim attribute.");
        }
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }
  }

  std::string pTy = prim_type;

  if (_primspec_mode) {
    // Load scene as PrimSpec tree
    if (_primspec_fun) {
      Path fullpath(GetCurrentPrimPath(), "");
      Path pname(prim_name, "");

      // pass prim_type as is(empty = empty string)
      nonstd::expected<bool, std::string> ret =
          _primspec_fun(fullpath, spec, prim_type, pname, primIdx,
                        parentPrimIdx, props, in_metas, variantSetList);

      if (!ret) {
        // construction failed.
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Constructing PrimSpec typeName `{}`, elementName `{}` failed: {}",
            prim_type, prim_name, ret.error()));
      }
    } else {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "[Internal Error] PrimSpec handler is not found.");
    }

  } else {
    // Create typed Prim.

    if (prim_type.empty()) {
      // No Prim type specified. Treat it as Model

      pTy = "Model";
    }

    auto it = _prim_construct_fun_map.find(pTy);
    if (it == _prim_construct_fun_map.end()) {
      if (_option.allow_unknown_prim) {
        // Unknown Prim type specified. Treat it as Model
        // Prim's type name will be storead in Model::prim_type_name
        pTy = "Model";
        it = _prim_construct_fun_map.find(pTy);
      }
    }

    if (it != _prim_construct_fun_map.end()) {
      auto construct_fun = it->second;

      Path fullpath(GetCurrentPrimPath(), "");
      Path pname(prim_name, "");
      nonstd::expected<bool, std::string> ret =
          construct_fun(fullpath, spec, prim_type, pname, primIdx,
                        parentPrimIdx, props, in_metas, variantSetList);

      if (!ret) {
        // construction failed.
        PUSH_ERROR_AND_RETURN("Constructing Prim type `" + pTy +
                              "` failed: " + ret.error());
      }

    } else {
      PUSH_WARN(fmt::format(
          "TODO: Unsupported/Unimplemented Prim type: `{}`. Skipping parsing.",
          pTy));
    }
  }

  PopPrimPath();

  return true;
}

///
/// Parser entry point
/// TODO: Refactor and use unified code path regardless of LoadState.
///
bool AsciiParser::Parse(const uint32_t load_states,
                        const AsciiParserOption &parser_option) {
  TINYUSDZ_PROFILE_FUNCTION("ascii-parser");

  _toplevel = (load_states & static_cast<uint32_t>(LoadState::Toplevel));
  _sub_layered = (load_states & static_cast<uint32_t>(LoadState::Sublayer));
  _referenced = (load_states & static_cast<uint32_t>(LoadState::Reference));
  _payloaded = (load_states & static_cast<uint32_t>(LoadState::Payload));
  _option = parser_option;

  bool header_ok;
  {
    TINYUSDZ_PROFILE_SCOPE("ascii-parser", "ParseMagicHeader");
    header_ok = ParseMagicHeader();
  }
  if (!header_ok) {
    PUSH_ERROR_AND_RETURN("Failed to parse USDA magic header.\n");
  }

  SkipCommentAndWhitespaceAndNewline();

  if (Eof()) {
    // Empty USDA
    return true;
  }

  {
    char c;
    if (!LookChar1(&c)) {
      return false;
    }

    if (c == '(') {
      // stage meta.
      TINYUSDZ_PROFILE_SCOPE("ascii-parser", "ParseStageMetas");
      if (!ParseStageMetas()) {
        PUSH_ERROR_AND_RETURN("Failed to parse Stage metas.");
      }
    }
  }

  if (_stage_meta_process_fun) {
    DCOUT("Invoke StageMeta callback.");
    bool ret = _stage_meta_process_fun(_stage_metas);
    if (!ret) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage metas.");
    }
  } else {
    // TODO: Report error when StageMeta callback is not set?
    PUSH_WARN("Stage metadata processing callback is not set.");
  }

  PushPrimPath("/");

  // parse blocks
  {
    TINYUSDZ_PROFILE_SCOPE("ascii-parser", "ParseBlocks");
    while (!Eof()) {
      if (!SkipCommentAndWhitespaceAndNewline()) {
        return false;
      }

      if (Eof()) {
        // Whitespaces in the end of line.
        break;
      }

      // Look ahead token
      auto curr_loc = _sr->tell();

      Identifier tok;
      if (!ReadBasicType(&tok)) {
        PUSH_ERROR_AND_RETURN("Identifier expected.\n");
      }

      // Rewind
      if (!SeekTo(curr_loc)) {
        return false;
      }

      Specifier spec{Specifier::Invalid};
      if (tok == "def") {
        spec = Specifier::Def;
      } else if (tok == "over") {
        spec = Specifier::Over;
      } else if (tok == "class") {
        spec = Specifier::Class;
      } else {
        // Generate suggestion for invalid specifier using string similarity (Priority 5)
        std::string suggestion = GenerateSuggestion(tok);
        std::string error_msg = "Invalid specifier token '" + tok + "'";
        PushError(error_msg, ErrorType::SyntaxError, ErrorRecoveryHint::NoHint, suggestion);
        return false;
      }

      int64_t primIdx = _prim_idx_assign_fun(-1);
      DCOUT("Enter parseDef. primIdx = " << primIdx
                                         << ", parentPrimIdx = root(-1)");
      bool block_ok;
      {
        TINYUSDZ_PROFILE_SCOPE("ascii-parser", "ParseBlock");
        block_ok = ParseBlock(spec, primIdx, /* parent */ -1, /* depth */ 0,
                             /* in_variantStmt */ false);
      }
      if (!block_ok) {
        PUSH_ERROR_AND_RETURN("Failed to parse `def` block.");
      }
    }
  }

  return true;
}

bool ParseUnregistredValue(const std::string &_typeName, const std::string &str,
                           value::Value *value, std::string *err) {
  if (!value) {
    if (err) {
      (*err) += "`value` argument is nullptr.\n";
    }
    return false;
  }

  bool array_qual = false;
  std::string typeName = _typeName;
  if (endsWith(typeName, "[]")) {
    typeName = removeSuffix(typeName, "[]");
    array_qual = true;
  }

  nonstd::optional<uint32_t> typeId = value::TryGetTypeId(typeName);

  if (!typeId) {
    if (err) {
      (*err) += "Unsupported type: " + typeName + "\n";
    }
    return false;
  }

  tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t *>(str.data()),
                            str.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

#define PARSE_BASE_TYPE(__ty)                                            \
  case value::TypeTraits<__ty>::type_id(): {                             \
    if (array_qual) {                                                    \
      std::vector<__ty> vss;                                             \
      if (!parser.ParseBasicTypeArray(&vss)) {                           \
        if (err) {                                                       \
          (*err) = fmt::format("Failed to parse a value of type `{}[]`", \
                               value::TypeTraits<__ty>::type_name());    \
        }                                                                \
        return false;                                                    \
      }                                                                  \
      dst = vss;                                                         \
    } else {                                                             \
      __ty val;                                                          \
      if (!parser.ReadBasicType(&val)) {                                 \
        if (err) {                                                       \
          (*err) = fmt::format("Failed to parse a value of type `{}`",   \
                               value::TypeTraits<__ty>::type_name());    \
        }                                                                \
        return false;                                                    \
      }                                                                  \
      dst = val;                                                         \
    }                                                                    \
    break;                                                               \
  }

  value::Value dst;

  switch (typeId.value()) {
    PARSE_BASE_TYPE(value::uint2)
    PARSE_BASE_TYPE(value::uint3)
    PARSE_BASE_TYPE(value::uint4)
    default: {
      if (err) {
        (*err) =
            fmt::format("Unsupported or unimplemeneted type `{}`", typeName);
      }
      return false;
    }
  }

  (*value) = std::move(dst);

  return true;
}

}  // namespace ascii
}  // namespace tinyusdz

#endif  // !TINYUSDZ_DISABLE_MODULE_USDA_READER
