// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// To reduce compilation time and sections generated in .obj(object file),
// We split implementaion to multiple of .cc for ascii-parser.hh

#include <cstdio>
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <atomic>
//#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#if defined(__wasi__)
#else
#include <mutex>
#include <thread>
#endif
#include <vector>

#include "ascii-parser.hh"
#include "str-util.hh"
#include "tiny-format.hh"

//
#if !defined(TINYUSDZ_DISABLE_MODULE_USDA_READER)

//

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external

//#include "external/fast_float/include/fast_float/fast_float.h"
//#include "external/jsteemann/atoi.h"
//#include "external/simple_match/include/simple_match/simple_match.hpp"
#include "nonstd/expected.hpp"

//

#ifdef __clang__
#pragma clang diagnostic pop
#endif

//

// Tentative
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include "io-util.hh"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "value-pprint.hh"
#include "value-types.hh"

#if 0
// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                     \
  do {                                                               \
    std::ostringstream ss_e;                                         \
    ss_e << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_e << s;                                                       \
    PushError(ss_e.str());                                           \
    return false;                                                    \
  } while (0)

#define PUSH_WARN(s)                                                 \
  do {                                                               \
    std::ostringstream ss_w;                                         \
    ss_w << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_w << s;                                                       \
    PushWarn(ss_w.str());                                            \
  } while (0)
#endif

#include "common-macros.inc"

namespace tinyusdz {

namespace ascii {

constexpr auto kRel = "rel";
constexpr auto kTimeSamplesSuffix = ".timeSamples";
constexpr auto kConnectSuffix = ".connect";

constexpr auto kAscii = "[ASCII]";

extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<bool>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<int32_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<uint32_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<int64_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<uint64_t>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::half>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::half2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::half3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::half4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<float>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::float2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::float3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::float4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<double>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::double2>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::double3>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::double4>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord2h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord2f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord2d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::texcoord3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::point3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::point3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::point3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::normal3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::normal3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::normal3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::vector3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::vector3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::vector3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color3h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color3f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color4h>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color4f>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::color4d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::matrix2d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::matrix3d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::matrix4d>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::token>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<StringData>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<std::string>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<Reference>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<Path>> *result);
extern template bool AsciiParser::ParseBasicTypeArray(std::vector<nonstd::optional<value::AssetPath>> *result);

extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<bool> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<int32_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<uint32_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<int64_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<uint64_t> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::half4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<float> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::float4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<double> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double2> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double3> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::double4> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord2d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::texcoord3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::point3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::normal3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::vector3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4h> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4f> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::color4d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix2d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix3d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::matrix4d> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::token> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<StringData> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<std::string> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<Reference> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<Path> *result);
extern  template bool AsciiParser::ParseBasicTypeArray(std::vector<value::AssetPath> *result);

static void RegisterStageMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
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
}

static void RegisterPrimMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["kind"] = AsciiParser::VariableDef(value::kToken, "kind");
  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");

  //
  // Composition arcs -----------------------
  //

  // Type can be array. i.e. path, path[]
  metas["references"] = AsciiParser::VariableDef("Reference", "references",
                                                 /* allow array type */ true);
  metas["inherits"] = AsciiParser::VariableDef(value::kPath, "inherits", true);
  metas["payload"] = AsciiParser::VariableDef("Reference", "payload", true);
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

  // ListOp
  metas["apiSchemas"] = AsciiParser::VariableDef(
      value::Add1DArraySuffix(value::kToken), "apiSchemas");
}

static void RegisterPropMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");
  metas["active"] = AsciiParser::VariableDef(value::kBool, "active");
  metas["hidden"] = AsciiParser::VariableDef(value::kBool, "hidden");
  metas["customData"] =
      AsciiParser::VariableDef(value::kDictionary, "customData");

  // usdSkel
  metas["elementSize"] = AsciiParser::VariableDef(value::kInt, "elementSize");

  // usdSkel?
  metas["weight"] = AsciiParser::VariableDef(value::kDouble, "weight");

  // usdShade?
  metas["colorSpace"] = AsciiParser::VariableDef(value::kInt, "colorSpace");

  metas["interpolation"] = AsciiParser::VariableDef(value::kToken, "interpolation");
}


static void RegisterPrimAttrTypes(std::set<std::string> &d) {
  d.clear();

  d.insert(value::kBool);

  d.insert(value::kInt);
  d.insert(value::kInt2);
  d.insert(value::kInt3);
  d.insert(value::kInt4);

  d.insert(value::kFloat);
  d.insert(value::kFloat2);
  d.insert(value::kFloat3);
  d.insert(value::kFloat4);

  d.insert(value::kDouble);
  d.insert(value::kDouble2);
  d.insert(value::kDouble3);
  d.insert(value::kDouble4);

  d.insert(value::kHalf);
  d.insert(value::kHalf2);
  d.insert(value::kHalf3);
  d.insert(value::kHalf4);

  d.insert(value::kQuath);
  d.insert(value::kQuatf);
  d.insert(value::kQuatd);

  d.insert(value::kNormal3f);
  d.insert(value::kPoint3f);
  d.insert(value::kTexCoord2h);
  d.insert(value::kTexCoord3h);
  d.insert(value::kTexCoord4h);
  d.insert(value::kTexCoord2f);
  d.insert(value::kTexCoord3f);
  d.insert(value::kTexCoord4f);
  d.insert(value::kTexCoord2d);
  d.insert(value::kTexCoord3d);
  d.insert(value::kTexCoord4d);
  d.insert(value::kVector3f);
  d.insert(value::kVector4f);
  d.insert(value::kColor3h);
  d.insert(value::kColor3f);
  d.insert(value::kColor3d);
  d.insert(value::kColor4h);
  d.insert(value::kColor4f);
  d.insert(value::kColor4d);

  // It looks no `matrixNf` type for USDA
  // d.insert(value::kMatrix2f);
  // d.insert(value::kMatrix3f);
  // d.insert(value::kMatrix4f);

  d.insert(value::kMatrix2d);
  d.insert(value::kMatrix3d);
  d.insert(value::kMatrix4d);

  d.insert(value::kToken);
  d.insert(value::kString);

  d.insert(value::kRelationship);
  d.insert(value::kAssetPath);

  d.insert(value::kDictionary);

  // variantSet. Require special treatment.
  d.insert("variantSet");

  // TODO: Add more types...
}

static void RegisterPrimTypes(std::set<std::string> &d) {
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
  d.insert("CylinderLight");
  // d.insert("PortalLight");
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
static void RegisterAPISchemas(std::set<std::string> &d) {
  d.insert("MaterialBindingAPI");
  d.insert("SkelBindingAPI");

  // TODO:
  // d.insett("PhysicsCollisionAPI");
  // d.insett("PhysicsRigidBodyAPI");

  // TODO: Support Multi-apply API(`CollectionAPI`)
  // d.insett("PhysicsLimitAPI");
  // d.insett("PhysicsDriveAPI");
  // d.insett("CollectionAPI");
}

namespace {

#if 0
// parseInt
// 0 = success
// -1 = bad input
// -2 = overflow
// -3 = underflow
int parseInt(const std::string &s, int *out_result) {
  size_t n = s.size();
  const char *c = s.c_str();

  if ((c == nullptr) || (*c) == '\0') {
    return -1;
  }

  size_t idx = 0;
  bool negative = c[0] == '-';
  if ((c[0] == '+') | (c[0] == '-')) {
    idx = 1;
    if (n == 1) {
      // sign char only
      return -1;
    }
  }

  int64_t result = 0;

  // allow zero-padded digits(e.g. "003")
  while (idx < n) {
    if ((c[idx] >= '0') && (c[idx] <= '9')) {
      int digit = int(c[idx] - '0');
      result = result * 10 + digit;
    } else {
      // bad input
      return -1;
    }

    if (negative) {
      if ((-result) < (std::numeric_limits<int>::min)()) {
        return -3;
      }
    } else {
      if (result > (std::numeric_limits<int>::max)()) {
        return -2;
      }
    }

    idx++;
  }

  if (negative) {
    (*out_result) = -int(result);
  } else {
    (*out_result) = int(result);
  }

  return 0;  // OK
}
#endif

}  // namespace


namespace {

using ReferenceList = std::vector<std::pair<ListEditQual, Reference>>;

// https://www.techiedelight.com/trim-string-cpp-remove-leading-trailing-spaces/
std::string TrimString(const std::string &str) {
  const std::string WHITESPACE = " \n\r\t\f\v";

  // remove leading and trailing whitespaces
  std::string s = str;
  {
    size_t start = s.find_first_not_of(WHITESPACE);
    s = (start == std::string::npos) ? "" : s.substr(start);
  }

  {
    size_t end = s.find_last_not_of(WHITESPACE);
    s = (end == std::string::npos) ? "" : s.substr(0, end + 1);
  }

  return s;
}

}  // namespace

inline bool isChar(char c) { return std::isalpha(int(c)); }

inline bool hasConnect(const std::string &str) {
  return endsWith(str, ".connect");
}

inline bool hasInputs(const std::string &str) {
  return startsWith(str, "inputs:");
}

inline bool hasOutputs(const std::string &str) {
  return startsWith(str, "outputs:");
}

inline bool is_digit(char x) {
  return (static_cast<unsigned int>((x) - '0') < static_cast<unsigned int>(10));
}

#if 0
static nonstd::expected<float, std::string> ParseFloat(const std::string &s) {
  // Parse with fast_float
  float result;
  auto ans = fast_float::from_chars(s.data(), s.data() + s.size(), result);
  if (ans.ec != std::errc()) {
    // Current `fast_float` implementation does not report detailed parsing err.
    return nonstd::make_unexpected("Parse failed.");
  }

  return result;
}

static nonstd::expected<double, std::string> ParseDouble(const std::string &s) {
  // Parse with fast_float
  double result;
  auto ans = fast_float::from_chars(s.data(), s.data() + s.size(), result);
  if (ans.ec != std::errc()) {
    // Current `fast_float` implementation does not report detailed parsing err.
    return nonstd::make_unexpected("Parse failed.");
  }

  return result;
}
#endif

void AsciiParser::SetBaseDir(const std::string &str) { _base_dir = str; }

void AsciiParser::SetStream(StreamReader *sr) { _sr = sr; }

std::string AsciiParser::GetError() {
  if (err_stack.empty()) {
    return std::string();
  }

  std::stringstream ss;
  while (!err_stack.empty()) {
    ErrorDiagnositc diag = err_stack.top();

    ss << "Near line " << (diag.cursor.row + 1) << ", col "
       << (diag.cursor.col + 1) << ": ";
    ss << diag.err << "\n";

    err_stack.pop();
  }

  return ss.str();
}

std::string AsciiParser::GetWarning() {
  if (warn_stack.empty()) {
    return std::string();
  }

  std::stringstream ss;
  while (!warn_stack.empty()) {
    ErrorDiagnositc diag = warn_stack.top();

    ss << "Near line " << (diag.cursor.row + 1) << ", col "
       << (diag.cursor.col + 1) << ": ";
    ss << diag.err << "\n";

    warn_stack.pop();
  }

  return ss.str();
}

// -- end basic

bool AsciiParser::ParseDictElement(std::string *out_key,
                                   MetaVariable *out_var) {
  (void)out_key;
  (void)out_var;

  // dict_element: type (array_qual?) name '=' value
  //           ;

  std::string type_name;

  if (!ReadIdentifier(&type_name)) {
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  if (!IsSupportedPrimAttrType(type_name)) {
    PUSH_ERROR_AND_RETURN("Unknown or unsupported type `" + type_name + "`\n");
  }

  // Has array qualifier? `[]`
  bool array_qual = false;
  {
    char c0, c1;
    if (!Char1(&c0)) {
      return false;
    }

    if (c0 == '[') {
      if (!Char1(&c1)) {
        return false;
      }

      if (c1 == ']') {
        array_qual = true;
      } else {
        // Invalid syntax
        PushError("Invalid syntax found.\n");
        return false;
      }

    } else {
      if (!Rewind(1)) {
        return false;
      }
    }
  }

  if (!SkipWhitespace()) {
    return false;
  }

  std::string key_name;
  if (!ReadIdentifier(&key_name)) {
    // string literal is also supported. e.g. "0"
    if (ReadStringLiteral(&key_name)) {
      // ok
    } else {
      PushError("Failed to parse dictionary key identifier.\n");
      return false;
    }
  }

  if (!SkipWhitespace()) {
    return false;
  }

  if (!Expect('=')) {
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  //
  // Supports limited types for customData/Dictionary.
  // TODO: support more types?
  //
  MetaVariable var;
  if (type_name == value::kBool) {
    bool val;
    if (!ReadBasicType(&val)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `bool`");
    }
    var.set(val);
  } else if (type_name == value::kInt) {
    if (array_qual) {
      std::vector<int32_t> vss;
      if (!ParseBasicTypeArray(&vss)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `int[]`");
      }
      var.set(vss);
    } else {
      int32_t val;
      if (!ReadBasicType(&val)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `int`");
      }
      var.set(val);
    }
  } else if (type_name == value::kUInt) {
    if (array_qual) {
      std::vector<uint32_t> vss;
      if (!ParseBasicTypeArray(&vss)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `uint[]`");
      }
      var.set(vss);
    } else {
      uint32_t val;
      if (!ReadBasicType(&val)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `uint`");
      }
      var.set(val);
    }
  } else if (type_name == value::kFloat) {
    if (array_qual) {
      std::vector<float> vss;
      if (!ParseBasicTypeArray(&vss)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `float[]`");
      }
      var.set(vss);
    } else {
      float val;
      if (!ReadBasicType(&val)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `float`");
      }
      var.set(val);
    }
  } else if (type_name == value::kString) {
    if (array_qual) {
      std::vector<StringData> strs;
      if (!ParseBasicTypeArray(&strs)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `string[]`");
      }
      var.set(strs);
    } else {
      StringData str;
      if (!ReadBasicType(&str)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `string`");
      }
      var.set(str);
    }
  } else if (type_name == "token") {
    if (array_qual) {
      std::vector<value::token> toks;
      if (!ParseBasicTypeArray(&toks)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `token[]`");
      }
      var.set(toks);
    } else {
      value::token tok;
      if (!ReadBasicType(&tok)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `token`");
      }
      var.set(tok);
    }
  } else if (type_name == "dictionary") {
    CustomDataType dict;

    DCOUT("Parse dictionary");
    if (!ParseDict(&dict)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `dictionary`");
    }
    var.set(dict);
  } else {
    PUSH_ERROR_AND_RETURN("TODO: type = " + type_name);
  }

  var.type = type_name;
  if (array_qual) {
    // TODO: 2D array
    var.type += "[]";
  }
  var.name = key_name;

  DCOUT("key: " << key_name << ", type: " << type_name);

  (*out_key) = key_name;
  (*out_var) = var;

  return true;
}

bool AsciiParser::MaybeCustom() {
  std::string tok;

  auto loc = CurrLoc();
  bool ok = ReadIdentifier(&tok);

  if (!ok) {
    // revert
    SeekTo(loc);
    return false;
  }

  if (tok == "custom") {
    // cosume `custom` token.
    return true;
  }

  // revert
  SeekTo(loc);
  return false;
}

bool AsciiParser::ParseDict(std::map<std::string, MetaVariable> *out_dict) {
  // '{' (type name '=' value)+ '}'
  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == '}') {
      break;
    } else {
      if (!Rewind(1)) {
        return false;
      }

      std::string key;
      MetaVariable var;
      if (!ParseDictElement(&key, &var)) {
        PUSH_ERROR_AND_RETURN("Failed to parse dict element.");
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      if (!var.is_valid()) {
        PUSH_ERROR_AND_RETURN("Invalid Dict element(probably internal issue).");
      }

      DCOUT("Add to dict: " << key);
      (*out_dict)[key] = var;
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  return true;
}

bool AsciiParser::ParseVariantsElement(std::string *out_key,
                                       std::string *out_var) {
  // variants_element: string name '=' value
  //           ;

  std::string type_name;

  if (!ReadIdentifier(&type_name)) {
    return false;
  }

  // must be `string`
  if (type_name != value::kString) {
    PUSH_ERROR_AND_RETURN(
        "TinyUSDZ only accepts type `string` for `variants` element.");
  }

  if (!SkipWhitespace()) {
    return false;
  }

  std::string key_name;
  if (!ReadIdentifier(&key_name)) {
    // string literal is also supported. e.g. "0"
    if (ReadStringLiteral(&key_name)) {
      // ok
    } else {
      PushError("Failed to parse dictionary key identifier.\n");
      return false;
    }
  }

  if (!SkipWhitespace()) {
    return false;
  }

  if (!Expect('=')) {
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  std::string var;
  if (!ReadBasicType(&var)) {
    PUSH_ERROR_AND_RETURN("Failed to parse `string`");
  }

  DCOUT("key: " << key_name << ", value: " << var);

  (*out_key) = key_name;
  (*out_var) = var;

  return true;
}

bool AsciiParser::ParseVariants(VariantSelectionMap *out_map) {
  // '{' (string name '=' value)+ '}'
  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == '}') {
      break;
    } else {
      if (!Rewind(1)) {
        return false;
      }

      std::string key;
      std::string var;
      if (!ParseVariantsElement(&key, &var)) {
        PUSH_ERROR_AND_RETURN("Failed to parse an element of `variants`.");
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      DCOUT("Add to variants: " << key);
      (*out_map)[key] = var;
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  return true;
}

// 'None'
bool AsciiParser::MaybeNone() {
  std::vector<char> buf;

  auto loc = CurrLoc();

  if (!CharN(4, &buf)) {
    SeekTo(loc);
    return false;
  }

  if ((buf[0] == 'N') && (buf[1] == 'o') && (buf[2] == 'n') &&
      (buf[3] == 'e')) {
    // got it
    return true;
  }

  SeekTo(loc);

  return false;
}

bool AsciiParser::MaybeListEditQual(tinyusdz::ListEditQual *qual) {
  if (!SkipWhitespace()) {
    return false;
  }

  std::string tok;

  auto loc = CurrLoc();
  if (!ReadIdentifier(&tok)) {
    SeekTo(loc);
    return false;
  }

  if (tok == "prepend") {
    DCOUT("`prepend` list edit qualifier.");
    (*qual) = tinyusdz::ListEditQual::Prepend;
  } else if (tok == "append") {
    DCOUT("`append` list edit qualifier.");
    (*qual) = tinyusdz::ListEditQual::Append;
  } else if (tok == "add") {
    DCOUT("`add` list edit qualifier.");
    (*qual) = tinyusdz::ListEditQual::Add;
  } else if (tok == "delete") {
    DCOUT("`delete` list edit qualifier.");
    (*qual) = tinyusdz::ListEditQual::Delete;
  } else {
    DCOUT("No ListEdit qualifier.");
    // unqualified
    // rewind
    SeekTo(loc);
    (*qual) = tinyusdz::ListEditQual::ResetToExplicit;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  return true;
}

bool AsciiParser::IsSupportedPrimType(const std::string &ty) {
  return _supported_prim_types.count(ty);
}


bool AsciiParser::IsSupportedPrimAttrType(const std::string &ty) {
  return _supported_prim_attr_types.count(ty);
}

bool AsciiParser::IsSupportedAPISchema(const std::string &ty) {
  return _supported_api_schemas.count(ty);
}

bool AsciiParser::ReadStringLiteral(std::string *literal) {
  std::stringstream ss;

  char c0;
  if (!Char1(&c0)) {
    return false;
  }

  // TODO: Allow triple-quotated string?

  bool single_quote{false};

  if (c0 == '"') {
    // ok
  } else if (c0 == '\'') {
    // ok
    single_quote = true;
  } else {
    DCOUT("c0 = " << c0);
    PUSH_ERROR_AND_RETURN(
        "String or Token literal expected but it does not start with \" or '");
  }

  bool end_with_quotation{false};

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if ((c == '\n') || (c == '\r')) {
      PUSH_ERROR_AND_RETURN("New line in string literal.");
    }

    if (single_quote) {
      if (c == '\'') {
        end_with_quotation = true;
        break;
      }
    } else if (c == '"') {
      end_with_quotation = true;
      break;
    }

    ss << c;
  }

  if (!end_with_quotation) {
    PUSH_ERROR_AND_RETURN(
        fmt::format("String literal expected but it does not end with {}.",
                    single_quote ? "'" : "\""));
  }

  (*literal) = ss.str();

  _curr_cursor.col += int(literal->size() + 2);  // +2 for quotation chars

  return true;
}

bool AsciiParser::MaybeString(StringData *str) {
  std::stringstream ss;

  if (!str) {
    return false;
  }

  auto loc = CurrLoc();
  auto start_cursor = _curr_cursor;

  char c0;
  if (!Char1(&c0)) {
    SeekTo(loc);
    return false;
  }

  if (c0 != '"') {
    SeekTo(loc);
    return false;
  }

  bool end_with_quotation{false};

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      SeekTo(loc);
      return false;
    }

    if ((c == '\n') || (c == '\r')) {
      SeekTo(loc);
      return false;
    }

    if (c == '"') {
      end_with_quotation = true;
      break;
    }

    ss << c;
  }

  if (!end_with_quotation) {
    SeekTo(loc);
    return false;
  }

  DCOUT("Single quoted string found. col " << start_cursor.col << ", row "
                                           << start_cursor.row);

  str->value = ss.str();
  str->line_col = start_cursor.col;
  str->line_row = start_cursor.row;
  str->is_triple_quoted = false;

  _curr_cursor.col += int(str->value.size() + 2);  // +2 for quotation chars

  return true;
}

bool AsciiParser::MaybeTripleQuotedString(StringData *str) {
  std::stringstream ss;

  auto loc = CurrLoc();
  auto start_cursor = _curr_cursor;

  std::vector<char> triple_quote;
  if (!CharN(3, &triple_quote)) {
    SeekTo(loc);
    return false;
  }

  if (triple_quote.size() != 3) {
    SeekTo(loc);
    return false;
  }

  bool single_quote = false;

  if (triple_quote[0] == '"' && triple_quote[1] == '"' &&
      triple_quote[2] == '"') {
    // ok
  } else if (triple_quote[0] == '\'' && triple_quote[1] == '\'' &&
             triple_quote[2] == '\'') {
    // ok
    single_quote = true;
  } else {
    SeekTo(loc);
    return false;
  }

  // Read until next triple-quote `"""`
  std::stringstream str_buf;

  auto locinfo = _curr_cursor;

  int single_quote_count = 0;  // '
  int double_quote_count = 0;  // "

  bool got_triple_quote{false};

  while (!Eof()) {
    char c;

    if (!Char1(&c)) {
      SeekTo(loc);
      return false;
    }

    str_buf << c;

    if (c == '"') {
      double_quote_count++;
      single_quote_count = 0;
    } else if (c == '\'') {
      double_quote_count = 0;
      single_quote_count++;
    } else {
      double_quote_count = 0;
      single_quote_count = 0;
    }

    // Update loc info
    locinfo.col++;
    if (c == '\n') {
      locinfo.col = 0;
      locinfo.row++;
    } else if (c == '\r') {
      // CRLF?
      if (_sr->tell() < (_sr->size() - 1)) {
        char d;
        if (!Char1(&d)) {
          // this should not happen.
          SeekTo(loc);
          return false;
        }

        if (d == '\n') {
          // CRLF
          str_buf << d;
        } else {
          // unwind 1 char
          if (!_sr->seek_from_current(-1)) {
            // this should not happen.
            SeekTo(loc);
            return false;
          }
        }
      }
      locinfo.col = 0;
      locinfo.row++;
    }

    if (double_quote_count == 3) {
      // got '"""'
      got_triple_quote = true;
      break;
    }
    if (single_quote_count == 3) {
      // got '''
      got_triple_quote = true;
      break;
    }
  }

  if (!got_triple_quote) {
    SeekTo(loc);
    return false;
  }

  DCOUT("single_quote = " << single_quote);
  DCOUT("Triple quoted string found. col " << start_cursor.col << ", row "
                                           << start_cursor.row);

  // remove last '"""' or '''
  str->single_quote = single_quote;
  std::string s = str_buf.str();
  if (s.size() > 3) {  // just in case
    s.erase(s.size() - 3);
  }
  str->value = s;
  str->line_col = start_cursor.col;
  str->line_row = start_cursor.row;
  str->is_triple_quoted = true;

  _curr_cursor = locinfo;

  return true;
}

bool AsciiParser::ReadPrimAttrIdentifier(std::string *token) {
  // Example:
  // - xformOp:transform
  // - primvars:uvmap1

  std::stringstream ss;

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '_') {
      // ok
    } else if (c == ':') {  // namespace
      // ':' must lie in the middle of string literal
      if (ss.str().size() == 0) {
        PushError("PrimAttr name must not starts with `:`\n");
        return false;
      }
    } else if (c == '.') {  // delimiter for `connect`
      // '.' must lie in the middle of string literal
      if (ss.str().size() == 0) {
        PushError("PrimAttr name must not starts with `.`\n");
        return false;
      }
    } else if (std::isalnum(int(c))) {
      // number must not be allowed for the first char.
      if (ss.str().size() == 0) {
        if (!std::isalpha(int(c))) {
          PushError("PrimAttr name must not starts with number.\n");
          return false;
        }
      }
    } else {
      _sr->seek_from_current(-1);
      break;
    }

    _curr_cursor.col++;

    ss << c;
  }

  // ':' must lie in the middle of string literal
  if (ss.str().back() == ':') {
    PushError("PrimAttr name must not ends with `:`\n");
    return false;
  }

  // '.' must lie in the middle of string literal
  if (ss.str().back() == '.') {
    PushError("PrimAttr name must not ends with `.`\n");
    return false;
  }

  // Currently we only support '.connect'
  std::string tok = ss.str();

  if (contains(tok, '.')) {
    if (endsWith(tok, ".connect") || endsWith(tok, ".timeSamples")) {
      // OK
    } else {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, fmt::format("Must ends with `.connect` or `.timeSamples` for "
                              "attrbute name: `{}`",
                              tok));
    }

    // Multiple `.` is not allowed(e.g. attr.connect.timeSamples)
    if (counts(tok, '.') > 1) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, fmt::format("Attribute identifier `{}` containing multiple "
                              "`.` is not allowed.",
                              tok));
    }
  }

  (*token) = ss.str();
  DCOUT("primAttr identifier = " << (*token));
  return true;
}

bool AsciiParser::ReadIdentifier(std::string *token) {
  // identifier = (`_` | [a-zA-Z]) (`_` | [a-zA-Z0-9]+)
  std::stringstream ss;

  // The first character.
  {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      DCOUT("read1 failed.");
      return false;
    }

    if (c == '_') {
      // ok
    } else if (!std::isalpha(int(c))) {
      DCOUT(fmt::format("Invalid identiefier: '{}'", c));
      _sr->seek_from_current(-1);
      return false;
    }
    _curr_cursor.col++;

    ss << c;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '_') {
      // ok
    } else if (!std::isalnum(int(c))) {
      _sr->seek_from_current(-1);
      break;  // end of identifier(e.g. ' ')
    }

    _curr_cursor.col++;

    ss << c;
  }

  (*token) = ss.str();
  return true;
}

bool AsciiParser::ReadPathIdentifier(std::string *path_identifier) {
  // path_identifier = `<` string `>`
  std::stringstream ss;

  if (!Expect('<')) {
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  // read until '>'
  bool ok = false;
  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '>') {
      // end
      ok = true;
      _curr_cursor.col++;
      break;
    }

    // TODO: Check if character is valid for path identifier
    ss << c;
  }

  if (!ok) {
    return false;
  }

  (*path_identifier) = TrimString(ss.str());
  // std::cout << "PathIdentifier: " << (*path_identifier) << "\n";

  return true;
}

bool AsciiParser::SkipUntilNewline() {
  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '\n') {
      break;
    } else if (c == '\r') {
      // CRLF?
      if (_sr->tell() < (_sr->size() - 1)) {
        char d;
        if (!Char1(&d)) {
          // this should not happen.
          return false;
        }

        if (d == '\n') {
          break;
        }

        // unwind 1 char
        if (!_sr->seek_from_current(-1)) {
          // this should not happen.
          return false;
        }

        break;
      }

    } else {
      // continue
    }
  }

  _curr_cursor.row++;
  _curr_cursor.col = 0;
  return true;
}

// metadata_opt := string_literal '\n'
//              |  var '=' value '\n'
//
bool AsciiParser::ParseStageMetaOpt() {
  // Maybe string
  {
    StringData str;
    if (MaybeTripleQuotedString(&str)) {
      _stage_metas.strings.push_back(str);
      return true;
    } else if (MaybeString(&str)) {
      _stage_metas.strings.push_back(str);
      return true;
    }
  }

  std::string varname;
  if (!ReadIdentifier(&varname)) {
    return false;
  }

  DCOUT("varname = " << varname);

  if (!IsStageMeta(varname)) {
    std::string msg = "'" + varname + "' is not a Stage Metadata variable.\n";
    PushError(msg);
    return false;
  }

  if (!Expect('=')) {
    PUSH_ERROR_AND_RETURN("'=' expected in Stage Metadata opt.");
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  const VariableDef &vardef = _supported_stage_metas.at(varname);
  MetaVariable var;
  if (!ParseMetaValue(vardef, &var)) {
    PushError("Failed to parse meta value.\n");
    return false;
  }
  var.name = varname;

  if (varname == "defaultPrim") {
    value::token tok;
    if (var.get(&tok)) {
      DCOUT("defaultPrim = " << tok);
      _stage_metas.defaultPrim = tok;
    } else {
      PUSH_ERROR_AND_RETURN("`defaultPrim` isn't a token value.");
    }
  } else if (varname == "subLayers") {
    std::vector<value::AssetPath> paths;
    if (var.get(&paths)) {
      DCOUT("subLayers = " << paths);
      for (const auto &item : paths) {
        _stage_metas.subLayers.push_back(item);
      }
    } else {
      PUSH_ERROR_AND_RETURN("`subLayers` isn't an array of asset path");
    }
  } else if (varname == "upAxis") {
    if (auto pv = var.Get<value::token>()) {
      DCOUT("upAxis = " << pv.value());
      const std::string s = pv.value().str();
      if (s == "X") {
        _stage_metas.upAxis = Axis::X;
      } else if (s == "Y") {
        _stage_metas.upAxis = Axis::Y;
      } else if (s == "Z") {
        _stage_metas.upAxis = Axis::Z;
      } else {
        PUSH_ERROR_AND_RETURN(
            "Invalid `upAxis` value. Must be \"X\", \"Y\" or \"Z\", but got "
            "\"" +
            s + "\"(Note: Case sensitive)");
      }
    } else {
      PUSH_ERROR_AND_RETURN("`upAxis` isn't a token value.");
    }
  } else if ((varname == "doc") || (varname == "documentation")) {
    // `documentation` will be shorten to `doc`
    if (auto pv = var.Get<StringData>()) {
      DCOUT("doc = " << to_string(pv.value()));
      _stage_metas.doc = pv.value();
    } else if (auto pvs = var.Get<std::string>()) {
      StringData sdata;
      sdata.value = pvs.value();
      sdata.is_triple_quoted = false;
      _stage_metas.doc = sdata;
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("`{}` isn't a string value.", varname));
    }
  } else if (varname == "metersPerUnit") {
    DCOUT("ty = " << var.TypeName());
    if (auto pv = var.Get<float>()) {
      DCOUT("metersPerUnit = " << pv.value());
      _stage_metas.metersPerUnit = double(pv.value());
    } else if (auto pvd = var.Get<double>()) {
      DCOUT("metersPerUnit = " << pvd.value());
      _stage_metas.metersPerUnit = pvd.value();
    } else {
      PUSH_ERROR_AND_RETURN("`metersPerUnit` isn't a floating-point value.");
    }
  } else if (varname == "timeCodesPerSecond") {
    DCOUT("ty = " << var.TypeName());
    if (auto pv = var.Get<float>()) {
      DCOUT("metersPerUnit = " << pv.value());
      _stage_metas.timeCodesPerSecond = double(pv.value());
    } else if (auto pvd = var.Get<double>()) {
      DCOUT("metersPerUnit = " << pvd.value());
      _stage_metas.timeCodesPerSecond = pvd.value();
    } else {
      PUSH_ERROR_AND_RETURN(
          "`timeCodesPerSecond` isn't a floating-point value.");
    }
  } else if (varname == "startTimeCode") {
    if (auto pv = var.Get<float>()) {
      DCOUT("startTimeCode = " << pv.value());
      _stage_metas.startTimeCode = double(pv.value());
    } else if (auto pvd = var.Get<double>()) {
      DCOUT("startTimeCode = " << pvd.value());
      _stage_metas.startTimeCode = pvd.value();
    }
  } else if (varname == "endTimeCode") {
    if (auto pv = var.Get<float>()) {
      DCOUT("endTimeCode = " << pv.value());
      _stage_metas.endTimeCode = double(pv.value());
    } else if (auto pvd = var.Get<double>()) {
      DCOUT("endTimeCode = " << pvd.value());
      _stage_metas.endTimeCode = pvd.value();
    }
  } else if (varname == "framesPerSecond") {
    if (auto pv = var.Get<float>()) {
      DCOUT("framesPerSecond = " << pv.value());
      _stage_metas.framesPerSecond = double(pv.value());
    } else if (auto pvd = var.Get<double>()) {
      DCOUT("framesPerSecond = " << pvd.value());
      _stage_metas.framesPerSecond = pvd.value();
    }
  } else if (varname == "apiSchemas") {
    // TODO: ListEdit qualifer check
    if (auto pv = var.Get<std::vector<value::token>>()) {
      for (auto &item : pv.value()) {
        if (IsSupportedAPISchema(item.str())) {
          // OK
        } else {
          PUSH_ERROR_AND_RETURN("\"" << item.str()
                                     << "\" is not supported(at the moment) "
                                        "for `apiSchemas` in TinyUSDZ.");
        }
      }
    } else {
      PUSH_ERROR_AND_RETURN("`apiSchemas` isn't an `token[]` type.");
    }
  } else if (varname == "customLayerData") {
    if (auto pv = var.Get<CustomDataType>()) {
      _stage_metas.customLayerData = pv.value();
    } else {
      PUSH_ERROR_AND_RETURN("`customLayerData` isn't a dictionary value.");
    }
  } else {
    DCOUT("TODO: Stage meta: " << varname);
    PUSH_WARN("TODO: Stage meta: " << varname);
  }

  return true;
}

// Parse Stage meta
// meta = ( metadata_opt )
//      ;
bool AsciiParser::ParseStageMetas() {
  if (!Expect('(')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  while (!Eof()) {
    char c;
    if (!LookChar1(&c)) {
      return false;
    }

    if (c == ')') {
      if (!SeekTo(CurrLoc() + 1)) {
        return false;
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      DCOUT("Stage metas end");

      // end
      return true;

    } else {
      if (!SkipWhitespace()) {
        // eof
        return false;
      }

      if (!ParseStageMetaOpt()) {
        // parse error
        return false;
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }
  }

  DCOUT("ParseStageMetas end");
  return true;
}

// `#` style comment
bool AsciiParser::ParseSharpComment() {
  char c;
  if (!Char1(&c)) {
    // eol
    return false;
  }

  if (c != '#') {
    return false;
  }

  return true;
}

// Fetch 1 char. Do not change input stream position.
bool AsciiParser::LookChar1(char *c) {
  if (!Char1(c)) {
    return false;
  }

  Rewind(1);

  return true;
}

// Fetch N chars. Do not change input stream position.
bool AsciiParser::LookCharN(size_t n, std::vector<char> *nc) {
  std::vector<char> buf(n);

  auto loc = CurrLoc();

  bool ok = _sr->read(n, n, reinterpret_cast<uint8_t *>(buf.data()));
  if (ok) {
    (*nc) = buf;
  }

  SeekTo(loc);

  return ok;
}

bool AsciiParser::Char1(char *c) { return _sr->read1(c); }

bool AsciiParser::CharN(size_t n, std::vector<char> *nc) {
  std::vector<char> buf(n);

  bool ok = _sr->read(n, n, reinterpret_cast<uint8_t *>(buf.data()));
  if (ok) {
    (*nc) = buf;
  }

  return ok;
}

bool AsciiParser::Rewind(size_t offset) {
  if (!_sr->seek_from_current(-int64_t(offset))) {
    return false;
  }

  return true;
}

uint64_t AsciiParser::CurrLoc() { return _sr->tell(); }

bool AsciiParser::SeekTo(uint64_t pos) {
  if (!_sr->seek_set(pos)) {
    return false;
  }

  return true;
}

bool AsciiParser::PushParserState() {
  // Stack size must be less than the number of input bytes.
  if (parse_stack.size() >= _sr->size()) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "Parser state stack become too deep.");
  }

  uint64_t loc = _sr->tell();

  ParseState state;
  state.loc = int64_t(loc);
  parse_stack.push(state);

  return true;
}

bool AsciiParser::PopParserState(ParseState *state) {
  if (parse_stack.empty()) {
    return false;
  }

  (*state) = parse_stack.top();

  parse_stack.pop();

  return true;
}

bool AsciiParser::SkipWhitespace() {
  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }
    _curr_cursor.col++;

    if ((c == ' ') || (c == '\t') || (c == '\f')) {
      // continue
    } else {
      break;
    }
  }

  // unwind 1 char
  if (!_sr->seek_from_current(-1)) {
    return false;
  }
  _curr_cursor.col--;

  return true;
}

bool AsciiParser::SkipWhitespaceAndNewline(bool allow_semicolon) {
  // USDA also allow C-style ';' as a newline separator.
  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    // printf("sws c = %c\n", c);

    if ((c == ' ') || (c == '\t') || (c == '\f')) {
      _curr_cursor.col++;
      // continue
    } else if (allow_semicolon && (c == ';')) {
      _curr_cursor.col++;
      // continue
    } else if (c == '\n') {
      _curr_cursor.col = 0;
      _curr_cursor.row++;
      // continue
    } else if (c == '\r') {
      // CRLF?
      if (_sr->tell() < (_sr->size() - 1)) {
        char d;
        if (!Char1(&d)) {
          // this should not happen.
          return false;
        }

        if (d == '\n') {
          // CRLF
        } else {
          // unwind 1 char
          if (!_sr->seek_from_current(-1)) {
            // this should not happen.
            return false;
          }
        }
      }
      _curr_cursor.col = 0;
      _curr_cursor.row++;
      // continue
    } else {
      // end loop
      if (!_sr->seek_from_current(-1)) {
        return false;
      }
      break;
    }
  }

  return true;
}

bool AsciiParser::SkipCommentAndWhitespaceAndNewline() {
  // Skip multiple line of comments.
  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    // printf("sws c = %c\n", c);

    if (c == '#') {
      if (!SkipUntilNewline()) {
        return false;
      }
    } else if ((c == ' ') || (c == '\t') || (c == '\f')) {
      _curr_cursor.col++;
      // continue
    } else if (c == '\n') {
      _curr_cursor.col = 0;
      _curr_cursor.row++;
      // continue
    } else if (c == '\r') {
      // CRLF?
      if (_sr->tell() < (_sr->size() - 1)) {
        char d;
        if (!Char1(&d)) {
          // this should not happen.
          return false;
        }

        if (d == '\n') {
          // CRLF
        } else {
          // unwind 1 char
          if (!_sr->seek_from_current(-1)) {
            // this should not happen.
            return false;
          }
        }
      }
      _curr_cursor.col = 0;
      _curr_cursor.row++;
      // continue
    } else {
      // std::cout << "unwind\n";
      // end loop
      if (!_sr->seek_from_current(-1)) {
        return false;
      }
      break;
    }
  }

  return true;
}

bool AsciiParser::Expect(char expect_c) {
  if (!SkipWhitespace()) {
    return false;
  }

  char c;
  if (!Char1(&c)) {
    // this should not happen.
    return false;
  }

  bool ret = (c == expect_c);

  if (!ret) {
    std::string msg = "Expected `" + std::string(&expect_c, 1) + "` but got `" +
                      std::string(&c, 1) + "`\n";
    PushError(msg);

    // unwind
    _sr->seek_from_current(-1);
  } else {
    _curr_cursor.col++;
  }

  return ret;
}

// Parse magic
// #usda FLOAT
bool AsciiParser::ParseMagicHeader() {
  if (!SkipWhitespace()) {
    return false;
  }

  if (Eof()) {
    return false;
  }

  {
    char magic[6];
    if (!_sr->read(6, 6, reinterpret_cast<uint8_t *>(magic))) {
      // eol
      return false;
    }

    if ((magic[0] == '#') && (magic[1] == 'u') && (magic[2] == 's') &&
        (magic[3] == 'd') && (magic[4] == 'a') && (magic[5] == ' ')) {
      // ok
    } else {
      PUSH_ERROR_AND_RETURN(
          "Magic header must start with `#usda `(at least single whitespace "
          "after 'a') but got `" +
          std::string(magic, 6));

      return false;
    }
  }

  if (!SkipWhitespace()) {
    // eof
    return false;
  }

  // current we only accept "1.0"
  {
    char ver[3];
    if (!_sr->read(3, 3, reinterpret_cast<uint8_t *>(ver))) {
      return false;
    }

    if ((ver[0] == '1') && (ver[1] == '.') && (ver[2] == '0')) {
      // ok
      _version = 1.0f;
    } else {
      PUSH_ERROR_AND_RETURN("Version must be `1.0` but got `" +
                            std::string(ver, 3) + "`");
    }
  }

  SkipUntilNewline();

  return true;
}

bool AsciiParser::ParseCustomMetaValue() {
  // type identifier '=' value

  // return ParseAttributeMeta();
  PUSH_ERROR_AND_RETURN("TODO");
}

bool AsciiParser::ParseAssetIdentifier(value::AssetPath *out,
                                       bool *triple_deliminated) {
  // @...@
  // or @@@...@@@ (Triple '@'-deliminated asset identifier.)
  // @@@ = Path containing '@'. '@@@' in Path is encoded as '\@@@'
  //
  // Example:
  //   @bora@
  //   @@@bora@@@
  //   @@@bora\@@@dora@@@

  // TODO: Correctly support escape characters

  // look ahead.
  std::vector<char> buf;
  uint64_t curr = _sr->tell();
  bool maybe_triple{false};

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  if (CharN(3, &buf)) {
    if (buf[0] == '@' && buf[1] == '@' && buf[2] == '@') {
      maybe_triple = true;
    }
  }

  bool valid{false};

  if (!maybe_triple) {
    // std::cout << "maybe single-'@' asset reference\n";

    SeekTo(curr);
    char s;
    if (!Char1(&s)) {
      return false;
    }

    if (s != '@') {
      std::string sstr{s};
      PUSH_ERROR_AND_RETURN("Reference must start with '@', but got '" + sstr +
                            "'");
    }

    std::string tok;

    // Read until '@'
    bool found_delimiter = false;
    while (!Eof()) {
      char c;

      if (!Char1(&c)) {
        return false;
      }

      if (c == '@') {
        found_delimiter = true;
        break;
      }

      tok += c;
    }

    if (found_delimiter) {
      (*out) = tok;
      (*triple_deliminated) = false;

      valid = true;
    }

  } else {
    bool found_delimiter{false};
    bool escape_sequence{false};
    int at_cnt{0};
    std::string tok;

    // Read until '@@@' appears
    // Need to escaped '@@@'("\\@@@")
    while (!Eof()) {
      char c;

      if (!Char1(&c)) {
        return false;
      }

      if (c == '\\') {
        escape_sequence = true;
      }

      if (c == '@') {
        at_cnt++;
      } else {
        at_cnt--;
        if (at_cnt < 0) {
          at_cnt = 0;
        }
      }

      tok += c;

      if (at_cnt == 3) {
        if (escape_sequence) {
          // Still in path identifier...
          // Unescape "\\@@@"

          if (tok.size() > 3) {            // this should be true.
            if (endsWith(tok, "\\@@@")) {  // this also should be true.
              tok.erase(tok.size() - 4);
              tok.append("@@@");
            }
          }
          at_cnt = 0;
          escape_sequence = false;
        } else {
          // Got it. '@@@'
          found_delimiter = true;
          break;
        }
      }
    }

    if (found_delimiter) {
      // remote last '@@@'
      (*out) = removeSuffix(tok, "@@@");
      (*triple_deliminated) = true;

      valid = true;
    }
  }

  return valid;
}

// TODO: Return Path
bool AsciiParser::ParseReference(Reference *out, bool *triple_deliminated) {
  /*
    Asset reference = AsssetIdentifier + optially followd by prim path

    Example:
     @bora@
     @bora@</dora>
  */

  value::AssetPath ap;
  if (!ParseAssetIdentifier(&ap, triple_deliminated)) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "Failed to parse asset path identifier.");
  }
  out->asset_path = ap;

  // Parse optional prim_path
  if (!SkipWhitespace()) {
    return false;
  }

  {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == '<') {
      if (!Rewind(1)) {
        return false;
      }

      std::string path;
      if (!ReadPathIdentifier(&path)) {
        return false;
      }

      out->prim_path = Path(path, "");
    } else {
      if (!Rewind(1)) {
        return false;
      }
    }
  }

  return true;
}

bool AsciiParser::ParseMetaValue(const VariableDef &def, MetaVariable *outvar) {
  const std::string vartype = def.type;
  const std::string varname = def.name;

  MetaVariable var;

  bool is_array_type{false};
  if (def.allow_array_type) {
    // Seek '['
    char c;
    if (LookChar1(&c)) {
      if (c == '[') {
        is_array_type = true;
      }
    }
  }

  // TODO: Refactor.
  if (vartype == value::kBool) {
    bool value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Boolean value expected for `" + varname + "`.");
    }
    DCOUT("bool = " << value);

    var.set(value);
  } else if (vartype == value::kToken) {
    if (is_array_type) {
      std::vector<value::token> value;
      if (!ParseBasicTypeArray(&value)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii, fmt::format("token[] expected for `{}`.", varname));
      }
      DCOUT("token[] = " << value);

      var.set(value);
    } else {
      value::token value;
      if (!ReadBasicType(&value)) {
        std::string msg = "Token expected for `" + varname + "`.\n";
        PushError(msg);
        return false;
      }
      DCOUT("token = " << value);

      var.set(value);
    }
  } else if (vartype == "token[]") {
    std::vector<value::token> value;
    if (!ParseBasicTypeArray(&value)) {
      std::string msg = "Token array expected for `" + varname + "`.\n";
      PushError(msg);
      return false;
    }
    // TODO
    // DCOUT("token[] = " << to_string(value));

    var.set(value);
  } else if (vartype == value::kString) {
    StringData sdata;
    if (MaybeTripleQuotedString(&sdata)) {
      var.set(sdata);
    } else {
      std::string value;
      if (!ReadStringLiteral(&value)) {
        PUSH_ERROR_AND_RETURN("String literal expected for `" + varname + "`.");
      }
      var.set(value);
    }

  } else if (vartype == "string[]") {
    // TODO: Support multi-line string?
    std::vector<std::string> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "ref[]") {
    std::vector<Reference> values;
    if (!ParseBasicTypeArray(&values)) {
      PUSH_ERROR_AND_RETURN("Array of Reference expected for `" + varname +
                            "`.");
    }

    var.set(values);

  } else if (vartype == "int[]") {
    std::vector<int32_t> values;
    if (!ParseBasicTypeArray(&values)) {
      // std::string msg = "Array of int values expected for `" + var.name +
      // "`.\n"; PushError(msg);
      return false;
    }

    for (size_t i = 0; i < values.size(); i++) {
      DCOUT("int[" << i << "] = " << values[i]);
    }

    var.set(values);
  } else if (vartype == "float[]") {
    std::vector<float> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "float2[]") {
    std::vector<value::float2> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "float3[]") {
    std::vector<value::float3> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "float4[]") {
    std::vector<value::float4> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "double[]") {
    std::vector<double> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "double2[]") {
    std::vector<value::double2> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "double3[]") {
    std::vector<value::double3> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == "double4[]") {
    std::vector<value::double4> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.set(values);
  } else if (vartype == value::kFloat) {
    float value;
    if (!ReadBasicType(&value)) {
      return false;
    }
    var.set(value);
  } else if (vartype == value::kDouble) {
    double value;
    if (!ReadBasicType(&value)) {
      return false;
    }
    var.set(value);
  } else if (vartype == "int2") {
    value::int2 value;
    if (!ReadBasicType(&value)) {
      return false;
    }

    var.set(value);

  } else if (vartype == "int3") {
    value::int3 value;
    if (!ReadBasicType(&value)) {
      return false;
    }

    var.set(value);
  } else if (vartype == "int4") {
    value::int4 value;
    if (!ReadBasicType(&value)) {
      return false;
    }

    var.set(value);
  } else if (vartype == value::kPath) {
    if (is_array_type) {
      std::vector<Path> paths;
      if (!ParseBasicTypeArray(&paths)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadatum.", def.name));
      }
      var.set(paths);

    } else {
      Path path;
      if (!ReadBasicType(&path)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadatum.", def.name));
      }
      var.set(path);
    }

  } else if (vartype == value::kAssetPath) {
    if (is_array_type) {
      std::vector<value::AssetPath> paths;
      if (!ParseBasicTypeArray(&paths)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadataum.", def.name));
      }
      var.set(paths);
    } else {
      value::AssetPath asset_path;
      if (!ReadBasicType(&asset_path)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadataum.", def.name));
      }
      var.set(asset_path);
    }
  } else if (vartype == "Reference") {
    if (is_array_type) {
      std::vector<Reference> refs;
      if (!ParseBasicTypeArray(&refs)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadataum.", def.name));
      }
      var.set(refs);
    } else {
      nonstd::optional<Reference> ref;
      if (!ReadBasicType(&ref)) {
        PUSH_ERROR_AND_RETURN_TAG(
            kAscii,
            fmt::format("Failed to parse `{}` in Prim metadataum.", def.name));
      }
      if (ref) {
        var.set(ref.value());
      } else {
        // None
        var.set(value::ValueBlock());
      }
    }
  } else if (vartype == value::kDictionary) {
    DCOUT("Parse dict in meta.");
    CustomDataType dict;
    if (!ParseDict(&dict)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `dictonary` data in metadataum.");
    }
    var.set(dict);
  } else {
    PUSH_ERROR_AND_RETURN("TODO: vartype = " + vartype);
  }

  if (is_array_type) {
    var.type = vartype + "[]";
  } else {
    var.type = vartype;
  }

  (*outvar) = var;

  return true;
}

bool AsciiParser::LexFloat(std::string *result) {
  // FLOATVAL : ('+' or '-')? FLOAT
  // FLOAT
  //     :   ('0'..'9')+ '.' ('0'..'9')* EXPONENT?
  //     |   '.' ('0'..'9')+ EXPONENT?
  //     |   ('0'..'9')+ EXPONENT
  //     ;
  // EXPONENT : ('e'|'E') ('+'|'-')? ('0'..'9')+ ;

  std::stringstream ss;

  bool has_sign{false};
  bool leading_decimal_dots{false};
  {
    char sc;
    if (!Char1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    ss << sc;

    // sign, '.' or [0-9]
    if ((sc == '+') || (sc == '-')) {
      has_sign = true;

      char c;
      if (!Char1(&c)) {
        return false;
      }

      if (c == '.') {
        // ok. something like `+.7`, `-.53`
        leading_decimal_dots = true;
        _curr_cursor.col++;
        ss << c;

      } else {
        // unwind and continue
        _sr->seek_from_current(-1);
      }

    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else if (sc == '.') {
      // ok
      leading_decimal_dots = true;
    } else {
      PUSH_ERROR_AND_RETURN("Sign or `.` or 0-9 expected.");
    }
  }

  (void)has_sign;

  // 1. Read the integer part
  char curr;
  if (!leading_decimal_dots) {
    // std::cout << "1 read int part: ss = " << ss.str() << "\n";

    while (!Eof()) {
      if (!Char1(&curr)) {
        return false;
      }

      // std::cout << "1 curr = " << curr << "\n";
      if ((curr >= '0') && (curr <= '9')) {
        // continue
        ss << curr;
      } else {
        _sr->seek_from_current(-1);
        break;
      }
    }
  }

  if (Eof()) {
    (*result) = ss.str();
    return true;
  }

  if (!Char1(&curr)) {
    return false;
  }

  // std::cout << "before 2: ss = " << ss.str() << ", curr = " << curr <<
  // "\n";

  // 2. Read the decimal part
  if (curr == '.') {
    ss << curr;

    while (!Eof()) {
      if (!Char1(&curr)) {
        return false;
      }

      if ((curr >= '0') && (curr <= '9')) {
        ss << curr;
      } else {
        break;
      }
    }

  } else if ((curr == 'e') || (curr == 'E')) {
    // go to 3.
  } else {
    // end
    (*result) = ss.str();
    _sr->seek_from_current(-1);
    return true;
  }

  if (Eof()) {
    (*result) = ss.str();
    return true;
  }

  // 3. Read the exponent part
  bool has_exp_sign{false};
  if ((curr == 'e') || (curr == 'E')) {
    ss << curr;

    if (!Char1(&curr)) {
      return false;
    }

    if ((curr == '+') || (curr == '-')) {
      // exp sign
      ss << curr;
      has_exp_sign = true;

    } else if ((curr >= '0') && (curr <= '9')) {
      // ok
      ss << curr;
    } else {
      // Empty E is not allowed.
      PUSH_ERROR_AND_RETURN("Empty `E' is not allowed.");
    }

    while (!Eof()) {
      if (!Char1(&curr)) {
        return false;
      }

      if ((curr >= '0') && (curr <= '9')) {
        // ok
        ss << curr;

      } else if ((curr == '+') || (curr == '-')) {
        if (has_exp_sign) {
          // No multiple sign characters
          PUSH_ERROR_AND_RETURN("No multiple exponential sign characters.");
          return false;
        }

        ss << curr;
        has_exp_sign = true;
      } else {
        // end
        _sr->seek_from_current(-1);
        break;
      }
    }
  } else {
    _sr->seek_from_current(-1);
  }

  (*result) = ss.str();
  return true;
}

nonstd::optional<AsciiParser::VariableDef> AsciiParser::GetStageMetaDefinition(
    const std::string &name) {
  if (_supported_stage_metas.count(name)) {
    return _supported_stage_metas.at(name);
  }

  return nonstd::nullopt;
}

nonstd::optional<AsciiParser::VariableDef> AsciiParser::GetPrimMetaDefinition(
    const std::string &name) {
  if (_supported_prim_metas.count(name)) {
    return _supported_prim_metas.at(name);
  }

  return nonstd::nullopt;
}

nonstd::optional<AsciiParser::VariableDef> AsciiParser::GetPropMetaDefinition(
    const std::string &name) {
  if (_supported_prop_metas.count(name)) {
    return _supported_prop_metas.at(name);
  }

  return nonstd::nullopt;
}

bool AsciiParser::ParseStageMeta(std::pair<ListEditQual, MetaVariable> *out) {
  if (!SkipCommentAndWhitespaceAndNewline()) {
    return false;
  }

  tinyusdz::ListEditQual qual{ListEditQual::ResetToExplicit};
  if (!MaybeListEditQual(&qual)) {
    return false;
  }

  DCOUT("list-edit qual: " << tinyusdz::to_string(qual));

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::string varname;
  if (!ReadIdentifier(&varname)) {
    return false;
  }

  // std::cout << "varname = `" << varname << "`\n";

  if (!IsStageMeta(varname)) {
    PUSH_ERROR_AND_RETURN("Unsupported or invalid/empty variable name `" +
                          varname + "` for Stage metadatum");
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  if (!Expect('=')) {
    PushError("`=` expected.");
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  auto pvardef = GetStageMetaDefinition(varname);
  if (!pvardef) {
    // This should not happen though;
    return false;
  }

  auto vardef = (*pvardef);

  MetaVariable var;
  if (!ParseMetaValue(vardef, &var)) {
    return false;
  }
  var.name = varname;

  std::get<0>(*out) = qual;
  std::get<1>(*out) = var;

  return true;
}

nonstd::optional<std::pair<ListEditQual, MetaVariable>>
AsciiParser::ParsePrimMeta() {
  if (!SkipCommentAndWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  tinyusdz::ListEditQual qual{ListEditQual::ResetToExplicit};

  // May be string only
  // For some reason, string-only data is stored in `MetaVariable` and
  // reconstructed in ReconstructPrimMeta in usda-reader.cc
  //
  {
    StringData sdata;
    if (MaybeTripleQuotedString(&sdata)) {
      MetaVariable var;
      var.name = "";  // empty
      var.set(sdata);

      return std::make_pair(qual, var);

    } else if (MaybeString(&sdata)) {
      MetaVariable var;
      var.name = "";  // empty
      var.set(sdata);

      return std::make_pair(qual, var);
    }
  }

  if (!MaybeListEditQual(&qual)) {
    return nonstd::nullopt;
  }

  DCOUT("list-edit qual: " << tinyusdz::to_string(qual));

  if (!SkipWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  std::string varname;
  if (!ReadIdentifier(&varname)) {
    return nonstd::nullopt;
  }

  DCOUT("Identifier = " << varname);

  if (!IsPrimMeta(varname)) {
    std::string msg = "'" + varname + "' is not a Prim Metadata variable.\n";
    PushError(msg);
    return nonstd::nullopt;
  }

  if (!Expect('=')) {
    PushError("'=' expected in Prim Metadata line.\n");
    return nonstd::nullopt;
  }
  SkipWhitespace();

  if (auto pv = GetPrimMetaDefinition(varname)) {
    MetaVariable var;
    const auto vardef = pv.value();
    if (!ParseMetaValue(vardef, &var)) {
      PushError("Failed to parse Prim meta value.\n");
      return nonstd::nullopt;
    }
    var.name = varname;

    return std::make_pair(qual, var);
  } else {
    PushError(fmt::format("Unsupported/unimplemented PrimSpec metadata {}", varname));
    return nonstd::nullopt;
  }

}

bool AsciiParser::ParsePrimMetas(
    PrimMetaMap *args) {
  // '(' args ')'
  // args = list of argument, separated by newline.

  if (!Expect('(')) {
    return false;
  }

  if (!SkipCommentAndWhitespaceAndNewline()) {
    // std::cout << "skip comment/whitespace/nl failed\n";
    DCOUT("SkipCommentAndWhitespaceAndNewline failed.");
    return false;
  }

  while (!Eof()) {
    if (!SkipCommentAndWhitespaceAndNewline()) {
      // std::cout << "2: skip comment/whitespace/nl failed\n";
      return false;
    }

    char s;
    if (!Char1(&s)) {
      return false;
    }

    if (s == ')') {
      DCOUT("Prim meta end");
      // End
      break;
    }

    Rewind(1);

    DCOUT("Start PrimMeta parse.");

    // ty = std::pair<ListEditQual, MetaVariable>;
    if (auto m = ParsePrimMeta()) {
      DCOUT("PrimMeta: list-edit qual = "
            << tinyusdz::to_string(std::get<0>(m.value()))
            << ", name = " << std::get<1>(m.value()).name);

      (*args)[std::get<1>(m.value()).name] = m.value();
    } else {
      PUSH_ERROR_AND_RETURN("Failed to parse Meta value.");
    }
  }

  return true;
}

bool AsciiParser::ParseAttrMeta(AttrMeta *out_meta) {
  // '(' metas ')'
  //
  // currently we only support 'interpolation', 'elementSize' and 'cutomData'

  if (!SkipWhitespace()) {
    return false;
  }

  // The first character.
  {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '(') {
      // ok
    } else {
      _sr->seek_from_current(-1);

      // Still ok. No meta
      DCOUT("No attribute meta.");
      return true;
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c == ')') {
      // end meta
      break;
    } else {
      if (!Rewind(1)) {
        return false;
      }

      // May be string only
      {
        StringData sdata;
        if (MaybeTripleQuotedString(&sdata)) {
          out_meta->stringData.push_back(sdata);

          DCOUT("Add triple-quoted string to attr meta:" << to_string(sdata));
          if (!SkipWhitespaceAndNewline()) {
            return false;
          }
          continue;
        } else if (MaybeString(&sdata)) {
          out_meta->stringData.push_back(sdata);

          DCOUT("Add string to attr meta:" << to_string(sdata));
          if (!SkipWhitespaceAndNewline()) {
            return false;
          }
          continue;
        }
      }

      std::string varname;
      if (!ReadIdentifier(&varname)) {
        return false;
      }

      DCOUT("Property/Attribute meta name: " << varname);

      bool supported = _supported_prop_metas.count(varname);
      if (!supported) {
        PUSH_ERROR_AND_RETURN_TAG(kAscii,
            fmt::format("Unsupported Property metadatum name: {}", varname));
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      if (!Expect('=')) {
        return false;
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      //
      // First-class predefind prop metas.
      //
      if (varname == "interpolation") {
        std::string value;
        if (!ReadStringLiteral(&value)) {
          return false;
        }

        DCOUT("Got `interpolation` meta : " << value);
        out_meta->interpolation = InterpolationFromString(value);
      } else if (varname == "elementSize") {
        uint32_t value;
        if (!ReadBasicType(&value)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `elementSize`");
        }

        DCOUT("Got `elementSize` meta : " << value);
        out_meta->elementSize = value;
      } else if (varname == "colorSpace") {
        value::token tok;
        if (!ReadBasicType(&tok)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `colorSpace`");
        }
        // Add as custom meta value.
        MetaVariable metavar;
        metavar.name = "colorSpace";
        metavar.set(tok);
        out_meta->meta.emplace("colorSpace", metavar);
      } else if (varname == "customData") {
        CustomDataType dict;

        if (!ParseDict(&dict)) {
          return false;
        }

        DCOUT("Got `customData` meta");
        out_meta->customData = dict;

      } else {
        if (auto pv = GetPropMetaDefinition(varname)) {
          // Parse as generic metadata variable
          MetaVariable metavar;
          const auto &vardef = pv.value();

          if (!ParseMetaValue(vardef, &metavar)) {
            return false;
          }
          metavar.name = varname;

          // add to custom meta
          out_meta->meta.emplace(varname, metavar);

        } else {
          // This should not happen though.
          PUSH_ERROR_AND_RETURN_TAG(kAscii, fmt::format("[InternalErrror] Failed to parse Property metadataum `{}`", varname));
        }
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }
  }

  return true;
}

bool IsUSDA(const std::string &filename, size_t max_filesize) {
  // TODO: Read only first N bytes
  std::vector<uint8_t> data;
  std::string err;

  if (!io::ReadWholeFile(&data, &err, filename, max_filesize)) {
    return false;
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::ascii::AsciiParser parser(&sr);

  return parser.CheckHeader();
}

//
// -- Impl
//

///
/// Parse rel string
///
bool AsciiParser::ParseRelationship(Relationship *result) {
  char c;
  if (!LookChar1(&c)) {
    return false;
  }

  if (c == '"') {
    // string
    std::string value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse String.");
    }
    result->set(value);

  } else if (c == '<') {
    // Path
    Path value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse Path.");
    }
    result->set(value);
  } else if (c == '[') {
    // PathVector
    std::vector<Path> value;
    if (!ParseBasicTypeArray(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse PathVector.");
    }
    result->set(value);
  } else {
    PUSH_ERROR_AND_RETURN("Unexpected char \"" + std::to_string(c) +
                          "\" found. Expects string, Path or PathVector.");
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  return true;
}

template <typename T>
bool AsciiParser::ParseBasicPrimAttr(bool array_qual,
                                     const std::string &primattr_name,
                                     Attribute *out_attr) {
  Attribute attr;
  primvar::PrimVar var;
  bool blocked{false};

  if (array_qual) {
    if (value::TypeTraits<T>::type_name() == "bool") {
      PushError("Array of bool type is not supported.");
      return false;
    } else {
      std::vector<T> value;
      if (!ParseBasicTypeArray(&value)) {
        PUSH_ERROR_AND_RETURN("Failed to parse " +
                              std::string(value::TypeTraits<T>::type_name()) +
                              " array.");
        return false;
      }

      if (value.size()) {
        DCOUT("Got it: ty = " + std::string(value::TypeTraits<T>::type_name()) +
              ", sz = " + std::to_string(value.size()));
        var.set_scalar(value);
      } else {
        blocked = true;
      }
    }

  } else if (hasConnect(primattr_name)) {
    std::string value;  // TODO: Path
    if (!ReadPathIdentifier(&value)) {
      PushError("Failed to parse path identifier for `token`.\n");
      return false;
    }

    var.set_scalar(value);
  } else {
    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse " +
                            std::string(value::TypeTraits<T>::type_name()));
      return false;
    }

    if (value) {
      DCOUT("ParseBasicPrimAttr: " << value::TypeTraits<T>::type_name() << " = "
                                   << (*value));

      // TODO: TimeSampled
      value::TimeSamples ts;
      ts.values.push_back(*value);
      var.set_timesamples(ts);

    } else {
      blocked = true;
      // std::cout << "ParseBasicPrimAttr: " <<
      // value::TypeTraits<T>::type_name()
      //           << " = None\n";
    }
  }

  // optional: attribute meta.
  AttrMeta meta;
  if (!ParseAttrMeta(&meta)) {
    PUSH_ERROR_AND_RETURN("Failed to parse Attribute meta.");
  }
  attr.metas() = meta;

  if (blocked) {
    attr.set_blocked(true);
  } else {
    attr.set_var(std::move(var));
  }

  (*out_attr) = std::move(attr);

  return true;
}

bool AsciiParser::ParsePrimProps(std::map<std::string, Property> *props) {
  // prim_prop : (custom?) uniform type (array_qual?) name '=' value
  //           | (custom?) type (array_qual?) name '=' value interpolation?
  //           | (custom?) uniform type (array_qual?) name interpolation?
  //           | (custom?) (listeditqual?) rel attr_name = None
  //           | (custom?) (listeditqual?) rel attr_name = string meta
  //           | (custom?) (listeditqual?) rel attr_name = path meta
  //           | (custom?) (listeditqual?) rel attr_name = pathvector meta
  //           | (custom?) (listeditqual?) rel attr_name meta
  //           ;

  // Skip comment
  if (!SkipCommentAndWhitespaceAndNewline()) {
    return false;
  }

  // Parse `custom`
  bool custom_qual = MaybeCustom();

  if (!SkipWhitespace()) {
    return false;
  }

  // List editing only applicable for Relation for Primitive attributes.
  ListEditQual listop_qual;
  if (!MaybeListEditQual(&listop_qual)) {
    return false;
  }

  bool uniform_qual{false};
  std::string type_name;

  if (!ReadIdentifier(&type_name)) {
    return false;
  }

  if (!SkipWhitespace()) {
    return false;
  }

  DCOUT("type_name = " << type_name);

  // Relation('rel')
  if (type_name == kRel) {
    DCOUT("relation");

    // - prim_identifier
    // - prim_identifier, '(' metadataum ')'
    // - prim_identifier, '=', (None|string|path|pathvector)
    // NOTE: There should be no 'uniform rel'

    std::string attr_name;

    if (!ReadPrimAttrIdentifier(&attr_name)) {
      PUSH_ERROR_AND_RETURN(
          "Attribute name(Identifier) expected but got non-identifier.");
    }

    if (!SkipWhitespace()) {
      return false;
    }

    char c;
    if (!LookChar1(&c)) {
      return false;
    }

    nonstd::optional<AttrMeta> metap;

    if (c == '(') {
      // FIXME: Implement Relation specific metadatum parser?
      AttrMeta meta;
      if (!ParseAttrMeta(&meta)) {
        PUSH_ERROR_AND_RETURN("Failed to parse metadataum.");
      }

      metap = meta;

      if (!LookChar1(&c)) {
        return false;
      }

    }

    if (c != '=') {
      DCOUT("Relationship with no target: " << attr_name);

      // No targets. Define only.
      Property p(type_name, custom_qual);
      p.set_property_type(Property::Type::NoTargetsRelation);
      p.set_listedit_qual(listop_qual);

      if (metap) {
        // TODO: metadataum for Rel
        p.attribute().metas() = metap.value();
      }

      (*props)[attr_name] = p;

      return true;
    }

    // has targets
    if (!Expect('=')) {
      return false;
    }

    if (metap) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii, "Syntax error. Property metadatum must be defined after `=` and relationship target(s).");
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (MaybeNone()) {
      PUSH_ERROR_AND_RETURN("TODO: Support `None` for property.");
    }

    Relationship rel;
    if (!ParseRelationship(&rel)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `rel` property.");
    }

    if (!SkipWhitespace()) {
    }

    if (!LookChar1(&c)) {
      return false;
    }

    // TODO: Parse Meta.
    if (c == '(') {

      if (metap) {
        PUSH_ERROR_AND_RETURN_TAG(kAscii, "[InternalError] parser error.");
      }

      AttrMeta meta;

      // FIXME: Implement Relation specific metadatum parser?
      if (!ParseAttrMeta(&meta)) {
        PUSH_ERROR_AND_RETURN("Failed to parse metadataum.");
      }

      metap = meta;

    }

    DCOUT("Relationship with target: " << attr_name);
    Property p(rel, custom_qual);
    p.set_listedit_qual(listop_qual);

    if (metap) {
      p.attribute().metas() = metap.value();
    }

    (*props)[attr_name] = p;

    return true;
  }

  //
  // Attrib.
  //

  if (listop_qual != ListEditQual::ResetToExplicit) {
    PUSH_ERROR_AND_RETURN_TAG(
        kAscii, "List editing qualifier is not allowed for Attribute.");
  }

  if (type_name == "uniform") {
    uniform_qual = true;

    // next token should be type
    if (!ReadIdentifier(&type_name)) {
      PushError("`type` identifier expected but got non-identifier\n");
      return false;
    }

    // `type_name` is then overwritten.
  }

  if (!IsSupportedPrimAttrType(type_name)) {
    PUSH_ERROR_AND_RETURN("Unknown or unsupported primtive attribute type `" +
                          type_name + "`\n");
  }

  // Has array qualifier? `[]`
  bool array_qual = false;
  {
    char c0, c1;
    if (!Char1(&c0)) {
      return false;
    }

    if (c0 == '[') {
      if (!Char1(&c1)) {
        return false;
      }

      if (c1 == ']') {
        array_qual = true;
      } else {
        // Invalid syntax
        PushError("Invalid syntax found.\n");
        return false;
      }

    } else {
      if (!Rewind(1)) {
        return false;
      }
    }
  }

  if (!SkipWhitespace()) {
    return false;
  }

  std::string primattr_name;
  if (!ReadPrimAttrIdentifier(&primattr_name)) {
    PUSH_ERROR_AND_RETURN("Failed to parse primAttr identifier.");
  }

  if (!SkipWhitespace()) {
    return false;
  }

  // output node?
  if (type_name == "token" && hasOutputs(primattr_name) &&
      !hasConnect(primattr_name)) {
    // ok
    return true;
  }

  bool isTimeSample = endsWith(primattr_name, kTimeSamplesSuffix);
  bool isConnection = endsWith(primattr_name, kConnectSuffix);

  // Remove suffix
  std::string attr_name = primattr_name;
  if (isTimeSample) {
    attr_name = removeSuffix(primattr_name, kTimeSamplesSuffix);
  }
  if (isConnection) {
    attr_name = removeSuffix(primattr_name, kConnectSuffix);
  }

  bool define_only = false;
  {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != '=') {
      // Define only(e.g. output variable)
      define_only = true;
    }
  }

  DCOUT("define only:" << define_only);

  if (define_only) {
#if 0
    if (isConnection || isTimeSample) {
      PUSH_ERROR_AND_RETURN("`.connect` or `.timeSamples` suffix provided, but no target/values provided.");
    }
#endif

    Rewind(1);

    // optional: attribute meta.
    AttrMeta meta;
    if (!ParseAttrMeta(&meta)) {
      PUSH_ERROR_AND_RETURN("Failed to parse Attribute meta.");
    }

    DCOUT("Define only property = " + primattr_name);

    // Empty Attribute. type info only
    Property p(type_name, custom_qual);

    if (uniform_qual) {
      p.attribute().variability() = Variability::Uniform;
    }
    p.attribute().metas() = meta;

    (*props)[attr_name] = p;

    return true;
  }

  // Continue to parse argument
  if (!SkipWhitespace()) {
    return false;
  }

  if (isConnection) {
    DCOUT("isConnection");

    // Target Must be Path
    Path path;
    if (!ReadBasicType(&path)) {
      PUSH_ERROR_AND_RETURN("Path expected for .connect target.");
    }

    Relationship rel;
    rel.set(path);

    Property p(rel, /* value typename */ type_name, custom_qual);

    (*props)[attr_name] = p;

    DCOUT(fmt::format("Added {} as a attribute connection.", primattr_name));

    return true;

  } else if (isTimeSample) {
    //
    // TODO(syoyo): Refactror and implement value parser dispatcher.
    //
    if (array_qual) {
      DCOUT("timeSample data. type = " << type_name << "[]");
    } else {
      DCOUT("timeSample data. type = " << type_name);
    }

    value::TimeSamples ts;
    if (array_qual) {
      if (!ParseTimeSamplesOfArray(type_name, &ts)) {
        PUSH_ERROR_AND_RETURN_TAG(kAscii, fmt::format("Failed to parse TimeSamples of type {}[]", type_name));
      }
    } else {
      if (!ParseTimeSamples(type_name, &ts)) {
        PUSH_ERROR_AND_RETURN_TAG(kAscii, fmt::format("Failed to parse TimeSamples of type {}", type_name));
      }
    }

    //std::string varname = removeSuffix(primattr_name, ".timeSamples");
    Attribute attr;
    primvar::PrimVar var;
    var.set_timesamples(ts);

    attr.name() = attr_name;
    attr.set_var(std::move(var));

    DCOUT("timeSamples primattr: type = " << type_name
                                          << ", name = " << attr_name);

    Property p(attr, custom_qual);
    p.set_property_type(Property::Type::Attrib);
    (*props)[attr_name] = p;

    return true;

  } else {
    Attribute attr;

    // TODO: Refactor. ParseAttrMeta is currently called inside
    // ParseBasicPrimAttr()
    if (type_name == value::kBool) {
      if (!ParseBasicPrimAttr<bool>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kInt) {
      if (!ParseBasicPrimAttr<int>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kUInt) {
      if (!ParseBasicPrimAttr<uint32_t>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kInt64) {
      if (!ParseBasicPrimAttr<int64_t>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kUInt64) {
      if (!ParseBasicPrimAttr<uint64_t>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kDouble) {
      if (!ParseBasicPrimAttr<double>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kString) {
      if (!ParseBasicPrimAttr<StringData>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kToken) {
      if (!ParseBasicPrimAttr<value::token>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kHalf) {
      if (!ParseBasicPrimAttr<value::half>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kHalf2) {
      if (!ParseBasicPrimAttr<value::half2>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kHalf3) {
      if (!ParseBasicPrimAttr<value::half3>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kHalf4) {
      if (!ParseBasicPrimAttr<value::half4>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kFloat) {
      if (!ParseBasicPrimAttr<float>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == value::kFloat2) {
      if (!ParseBasicPrimAttr<value::float2>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == value::kFloat3) {
      if (!ParseBasicPrimAttr<value::float3>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == value::kFloat4) {
      if (!ParseBasicPrimAttr<value::float4>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == value::kDouble2) {
      if (!ParseBasicPrimAttr<value::double2>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kDouble3) {
      if (!ParseBasicPrimAttr<value::double3>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kDouble4) {
      if (!ParseBasicPrimAttr<value::double4>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kPoint3f) {
      if (!ParseBasicPrimAttr<value::point3f>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kColor3f) {
      if (!ParseBasicPrimAttr<value::color3f>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kColor4f) {
      if (!ParseBasicPrimAttr<value::color4f>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kPoint3d) {
      if (!ParseBasicPrimAttr<value::point3d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kNormal3f) {
      if (!ParseBasicPrimAttr<value::normal3f>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kNormal3d) {
      if (!ParseBasicPrimAttr<value::normal3d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kVector3f) {
      if (!ParseBasicPrimAttr<value::vector3f>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kVector3d) {
      if (!ParseBasicPrimAttr<value::vector3d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kColor3d) {
      if (!ParseBasicPrimAttr<value::color3d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kColor4d) {
      if (!ParseBasicPrimAttr<value::color4d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == value::kMatrix2d) {
      if (!ParseBasicPrimAttr<value::matrix2d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kMatrix3d) {
      if (!ParseBasicPrimAttr<value::matrix3d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == value::kMatrix4d) {
      if (!ParseBasicPrimAttr<value::matrix4d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }

    } else if (type_name == value::kTexCoord2f) {
      if (!ParseBasicPrimAttr<value::texcoord2f>(array_qual, primattr_name,
                                                 &attr)) {
        PUSH_ERROR_AND_RETURN("Failed to parse texCoord2f data.");
      }

    } else if (type_name == value::kAssetPath) {
      Reference asset_ref;
      bool triple_deliminated{false};
      if (!ParseReference(&asset_ref, &triple_deliminated)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `asset` data.");
      }

      DCOUT("Asset path = " << asset_ref.asset_path);
      value::AssetPath assetp(asset_ref.asset_path);
      primvar::PrimVar var;
      var.set_scalar(assetp);
      attr.set_var(std::move(var));

      // optional: attribute meta.
      AttrMeta meta;
      if (!ParseAttrMeta(&meta)) {
        PUSH_ERROR_AND_RETURN("Failed to parse Attribute meta.");
      }
      attr.metas() = meta;

    } else {
      PUSH_ERROR_AND_RETURN("TODO: type = " + type_name);
    }

    if (uniform_qual) {
      attr.variability() = Variability::Uniform;
    }
    attr.set_name(primattr_name);

    DCOUT("primattr: type = " << type_name << ", name = " << primattr_name);

    Property p(attr, custom_qual);

    (*props)[primattr_name] = p;

    return true;
  }
}

bool AsciiParser::ParseProperty(std::map<std::string, Property> *props) {
  // property : primm_attr
  //          | 'rel' name '=' path
  //          ;

  if (!SkipWhitespace()) {
    return false;
  }

  // rel?
  {
    uint64_t loc = CurrLoc();
    std::string tok;

    if (!ReadIdentifier(&tok)) {
      return false;
    }

    if (tok == "rel") {
      PUSH_ERROR_AND_RETURN("TODO: Parse rel");
    } else {
      SeekTo(loc);
    }
  }

  // attribute
  return ParsePrimProps(props);
}

std::string AsciiParser::GetCurrentPath() {
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

void AsciiParser::Setup() {
  RegisterStageMetas(_supported_stage_metas);
  RegisterPrimMetas(_supported_prim_metas);
  RegisterPropMetas(_supported_prop_metas);
  RegisterPrimAttrTypes(_supported_prim_attr_types);
  RegisterPrimTypes(_supported_prim_types);
  RegisterAPISchemas(_supported_api_schemas);
}

AsciiParser::~AsciiParser() {}

bool AsciiParser::CheckHeader() { return ParseMagicHeader(); }

bool AsciiParser::IsPrimMeta(const std::string &name) {
  return _supported_prim_metas.count(name) ? true : false;
}

bool AsciiParser::IsStageMeta(const std::string &name) {
  return _supported_stage_metas.count(name) ? true : false;
}

bool AsciiParser::ParseVariantSet(const int64_t primIdx,
                                  const int64_t parentPrimIdx,
                                  const uint32_t depth) {
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

  std::map<std::string, VariantContent> variantContentMap;

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
          PUSH_ERROR_AND_RETURN_TAG(kAscii, "Failed to parse PrimSpec metas in variant statement.");
        }
      }
    }

    if (!Expect('{')) {
      return false;
    }

    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }


    VariantContent variantContent;

    while (!Eof()) {
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

      if (!Rewind(tok.size())) {
        return false;
      }

      if (tok == "variantSet") {
        PUSH_ERROR_AND_RETURN("Nested `variantSet` is not supported yet.");
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
        // FIXME: Assign idx dedicated for variant.
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
        if (!ParsePrimProps(&variantContent.props)) {
          PUSH_ERROR_AND_RETURN("Failed to parse Prim attribute.");
        }
        DCOUT(fmt::format("Done parse ParsePrimProps."));
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    DCOUT(fmt::format("variantSet item {} parsed.", variantName));

    variantContentMap.emplace(variantName, variantContent);
  }

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
                             const int64_t parentPrimIdx,
                             const uint32_t depth,
                             const bool in_variantStaement) {
  DCOUT("ParseBlock");

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
          kAscii, "Internal error. Invalid Specifier token combination. def = " << def << ", spec = " << to_string(spec));
    }
  } else if (def == "over") {
    if (spec != Specifier::Over) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Internal error. Invalid Specifier token combination. def = " << def << ", spec = " << to_string(spec));
    }
  } else if (def == "class") {
    if (spec != Specifier::Class) {
      PUSH_ERROR_AND_RETURN_TAG(
          kAscii, "Internal error. Invalid Specifier token combination. def = " << def << ", spec = " << to_string(spec));
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

    if (!IsSupportedPrimType(prim_type)) {
      std::string msg =
          "`" + prim_type +
          "` is not a defined Prim type(or not supported in TinyUSDZ)\n";
      PushError(msg);
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
  if (!ValidatePrimName(prim_name)) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "Prim name contains invalid chacracter.");
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::map<std::string, std::pair<ListEditQual, MetaVariable>> in_metas;
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

  {
    std::string full_path = GetCurrentPath();
    if (full_path == "/") {
      full_path += prim_name;
    } else {
      full_path += "/" + prim_name;
    }
    PushPath(full_path);
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

        if (!ParseVariantSet(primIdx, parentPrimIdx, depth)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `variantSet` statement.");
        }

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
        if (!ParsePrimProps(&props)) {
          PUSH_ERROR_AND_RETURN("Failed to parse Prim attribute.");
        }
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }
  }

  std::string pTy = prim_type;

  if (prim_type.empty()) {
    // No Prim type specified. Treat it as Model

    pTy = "Model";
  }

  if (_prim_construct_fun_map.count(pTy)) {
    auto construct_fun = _prim_construct_fun_map[pTy];

    Path fullpath(GetCurrentPath(), "");
    Path pname(prim_name, "");
    nonstd::expected<bool, std::string> ret = construct_fun(
        fullpath, spec, pname, primIdx, parentPrimIdx, props, in_metas);

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

  PopPath();

  return true;
}

///
/// Parser entry point
/// TODO: Refactor
///
bool AsciiParser::Parse(LoadState state) {
  _sub_layered = (state == LoadState::SUBLAYER);
  _referenced = (state == LoadState::REFERENCE);
  _payloaded = (state == LoadState::PAYLOAD);

  bool header_ok = ParseMagicHeader();
  if (!header_ok) {
    PushError("Failed to parse USDA magic header.\n");
    return false;
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
      if (!ParseStageMetas()) {
        PUSH_ERROR_AND_RETURN("Failed to parse Stage metas.");
      }
    }
  }

  if (_stage_meta_process_fun) {
    DCOUT("StageMeta callback.");
    bool ret = _stage_meta_process_fun(_stage_metas);
    if (!ret) {
      PUSH_ERROR_AND_RETURN("Failed to reconstruct Stage metas.");
    }
  }

  PushPath("/");

  // parse blocks
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
      PushError("Identifier expected.\n");
      return false;
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
      PushError("Invalid specifier token '" + tok + "'");
      return false;
    }

    int64_t primIdx = _prim_idx_assign_fun(-1);
    DCOUT("Enter parseDef. primIdx = " << primIdx
                                       << ", parentPrimIdx = root(-1)");
    bool block_ok = ParseBlock(spec, primIdx, /* parent */ -1, /* depth */0, /* in_variantStmt */false);
    if (!block_ok) {
      PushError("Failed to parse `def` block.\n");
      return false;
    }
  }

  return true;
}

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
