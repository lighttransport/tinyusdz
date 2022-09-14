// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// TODO:
//   - [x] Parse here document in Stage metadataum.
//   - [x] Pars document-only line in Stage metadatum.
//   - [ ] List up more TODOs.

#include <cstdio>
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
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
//#include "ryu/ryu.h"
//#include "ryu/ryu_parse.h"

#include "external/fast_float/include/fast_float/fast_float.h"
#include "external/jsteemann/atoi.h"
#include "external/simple_match/include/simple_match/simple_match.hpp"
#include "nonstd/expected.hpp"
//#include "nonstd/optional.hpp"

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

struct Identifier : std::string {
  // using std::string;
};

struct PathIdentifier : std::string {
  // using std::string;
};

static void RegisterStageMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();
  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");
  metas["documentation"] = AsciiParser::VariableDef(value::kString, "doc"); // alias to 'doc'

  metas["comment"] =
      AsciiParser::VariableDef(value::kString, "comment");

  // TODO: both support float and double?
  metas["metersPerUnit"] =
      AsciiParser::VariableDef(value::kDouble, "metersPerUnit");
  metas["timeCodesPerSecond"] =
      AsciiParser::VariableDef(value::kDouble, "timeCodesPerSecond");

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
  metas["subLayers"] = AsciiParser::VariableDef(value::kAssetPath, "subLayers");
}

static void RegisterPrimMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["kind"] = AsciiParser::VariableDef(value::kToken, "kind");
  metas["doc"] = AsciiParser::VariableDef(value::kString, "doc");

  // Composition arcs
  // Type can be array. i.e. path, path[]
  metas["references"] =
      AsciiParser::VariableDef(value::kAssetPath, "references");
  metas["inherits"] = AsciiParser::VariableDef(value::kAssetPath, "inherits");
  metas["payload"] = AsciiParser::VariableDef(value::kAssetPath, "payload");

  metas["specializes"] =
      AsciiParser::VariableDef(value::kRelationship, "specializes");
  metas["variantSets"] =
      AsciiParser::VariableDef(value::kDictionary, "variantSets");

  metas["assetInfo"] =
      AsciiParser::VariableDef(value::kDictionary, "assetInfo");
  metas["customData"] =
      AsciiParser::VariableDef(value::kDictionary, "customData");
  metas["variants"] = AsciiParser::VariableDef(value::kDictionary, "variants");

  metas["active"] = AsciiParser::VariableDef(value::kBool, "active");

  // usdSkel
  metas["elementSize"] = AsciiParser::VariableDef(value::kInt, "elementSize");

  // ListOp
  metas["apiSchemas"] = AsciiParser::VariableDef(
      value::Add1DArraySuffix(value::kToken), "apiSchemas");
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
  d.insert(value::kTexCoord2f);
  d.insert(value::kVector3f);
  d.insert(value::kColor3f);

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

}  // namespace

struct ErrorDiagnositc {
  std::string err;
  int line_row = -1;
  int line_col = -1;
};

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

static nonstd::expected<float, std::string> ParseFloat(const std::string &s) {
#if 0
  // Pase with Ryu.
  float value;
  Status stat = s2f_n(s.data(), int(s.size()), &value);
  if (stat == SUCCESS) {
    return value;
  }

  if (stat == INPUT_TOO_SHORT) {
    return nonstd::make_unexpected("Input floating point literal is too short");
  } else if (stat == INPUT_TOO_LONG) {
    return nonstd::make_unexpected("Input floating point literal is too long");
  } else if (stat == MALFORMED_INPUT) {
    return nonstd::make_unexpected("Malformed input floating point literal");
  }

  return nonstd::make_unexpected("Unexpected error in ParseFloat");
#else
  // Parse with fast_float
  float result;
  auto ans = fast_float::from_chars(s.data(), s.data() + s.size(), result);
  if (ans.ec != std::errc()) {
    // Current `fast_float` implementation does not report detailed parsing err.
    return nonstd::make_unexpected("Parse failed.");
  }

  return result;
#endif
}

static nonstd::expected<double, std::string> ParseDouble(const std::string &s) {
#if 0
  // Pase with Ryu.
  double value;
  Status stat = s2d_n(s.data(), int(s.size()), &value);
  if (stat == SUCCESS) {
    return value;
  }

  if (stat == INPUT_TOO_SHORT) {
    return nonstd::make_unexpected("Input floating point literal is too short");
  } else if (stat == INPUT_TOO_LONG) {
    // fallback to our float parser.
  } else if (stat == MALFORMED_INPUT) {
    return nonstd::make_unexpected("Malformed input floating point literal");
  }

  if (tryParseDouble(s.c_str(), s.c_str() + s.size(), &value)) {
    return value;
  }

  return nonstd::make_unexpected("Failed to parse floating-point value.");
#else
  // Parse with fast_float
  double result;
  auto ans = fast_float::from_chars(s.data(), s.data() + s.size(), result);
  if (ans.ec != std::errc()) {
    // Current `fast_float` implementation does not report detailed parsing err.
    return nonstd::make_unexpected("Parse failed.");
  }

  return result;
#endif
}

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

//
// -- Parse
//

template <>
bool AsciiParser::ParseMatrix(value::matrix2d *result) {
  // Assume column major(OpenGL style).

  if (!Expect('(')) {
    return false;
  }

  std::vector<std::array<double, 2>> content;
  if (!SepBy1TupleType<double, 2>(',', &content)) {
    return false;
  }

  if (content.size() != 2) {
    PushError("# of rows in matrix2d must be 2, but got " +
              std::to_string(content.size()) + "\n");
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  for (size_t i = 0; i < 2; i++) {
    result->m[i][0] = content[i][0];
    result->m[i][1] = content[i][1];
  }

  return true;
}

template <>
bool AsciiParser::ParseMatrix(value::matrix3d *result) {
  // Assume column major(OpenGL style).

  if (!Expect('(')) {
    return false;
  }

  std::vector<std::array<double, 3>> content;
  if (!SepBy1TupleType<double, 3>(',', &content)) {
    return false;
  }

  if (content.size() != 3) {
    PushError("# of rows in matrix3d must be 3, but got " +
              std::to_string(content.size()) + "\n");
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  for (size_t i = 0; i < 3; i++) {
    result->m[i][0] = content[i][0];
    result->m[i][1] = content[i][1];
    result->m[i][2] = content[i][2];
  }

  return true;
}

template <>
bool AsciiParser::ParseMatrix(value::matrix4d *result) {
  // Assume column major(OpenGL style).

  if (!Expect('(')) {
    return false;
  }

  std::vector<std::array<double, 4>> content;
  if (!SepBy1TupleType<double, 4>(',', &content)) {
    return false;
  }

  if (content.size() != 4) {
    PushError("# of rows in matrix4d must be 4, but got " +
              std::to_string(content.size()) + "\n");
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  for (size_t i = 0; i < 4; i++) {
    result->m[i][0] = content[i][0];
    result->m[i][1] = content[i][1];
    result->m[i][2] = content[i][2];
    result->m[i][3] = content[i][3];
  }

  return true;
}

template <>
bool AsciiParser::ReadBasicType(Path *value) {
  if (value) {
    std::string str;
    if (!ReadPathIdentifier(&str)) {
      return false;
    }

    (*value) = Path(str, "");

    return true;
  } else {
    return false;
  }
}

template <>
bool AsciiParser::ReadBasicType(value::matrix2d *value) {
  if (value) {
    return ParseMatrix(value);
  } else {
    return false;
  }
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::matrix2d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::matrix2d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::matrix3d *value) {
  if (value) {
    return ParseMatrix(value);
  } else {
    return false;
  }
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::matrix3d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::matrix3d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::matrix4d *value) {
  if (value) {
    return ParseMatrix(value);
  } else {
    return false;
  }
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::matrix4d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::matrix4d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

#if 0  // No `matrixNf` in USDA?
template<>
bool AsciiParser::ReadBasicType(value::matrix4f *value) {
  if (value) {
    return ParseMatrix(value);
  } else {
    return false;
  }
}

template<>
bool AsciiParser::ReadBasicType(
    nonstd::optional<value::matrix4f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::matrix4f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}
#endif

///
/// Parse the array of tuple. some may be None(e.g. `float3`: [(0, 1, 2),
/// None, (2, 3, 4), ...] )
///
template <typename T, size_t N>
bool AsciiParser::ParseTupleArray(
    std::vector<nonstd::optional<std::array<T, N>>> *result) {
  if (!Expect('[')) {
    return false;
  }

  if (!SepBy1TupleType<T, N>(',', result)) {
    return false;
  }

  if (!Expect(']')) {
    return false;
  }

  return true;
}

///
/// Parse the array of tuple(e.g. `float3`: [(0, 1, 2), (2, 3, 4), ...] )
///
template <typename T, size_t N>
bool AsciiParser::ParseTupleArray(std::vector<std::array<T, N>> *result) {
  (void)result;

  if (!Expect('[')) {
    return false;
  }

  if (!SepBy1TupleType<T, N>(',', result)) {
    return false;
  }

  if (!Expect(']')) {
    return false;
  }

  return true;
}

template <>
bool AsciiParser::ReadBasicType(Identifier *value) {
  return ReadIdentifier(value);
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<Identifier> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  Identifier v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::token *value) {
  std::string s;
  if (!ReadStringLiteral(&s)) {
    return false;
  }

  (*value) = value::token(s);
  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::token> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::token v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(std::string *value) {
  if (!value) {
    return false;
  }

  // May be triple-quoted string
  {
    StringData sdata;
    if (MaybeTripleQuotedString(&sdata)) {
      (*value) = sdata.value;
      return true;

    } else if (MaybeString(&sdata)) {
      (*value) = sdata.value;
      return true;
    }
  }

  // Just in case
  return ReadStringLiteral(value);
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<std::string> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  std::string v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(StringData *value) {
  if (!value) {
    return false;
  }

  // May be triple-quoted string
  {
    StringData sdata;
    if (MaybeTripleQuotedString(&sdata)) {
      (*value) = sdata;
      return true;

    } else if (MaybeString(&sdata)) {
      (*value) = sdata;
      return true;
    }
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<StringData> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  StringData v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(PathIdentifier *value) {
  return ReadPathIdentifier(value);
}

template <>
bool AsciiParser::ReadBasicType(bool *value) {
  // 'true', 'false', '0' or '1'
  {
    std::string tok;

    auto loc = CurrLoc();
    bool ok = ReadIdentifier(&tok);

    if (ok) {
      if (tok == "true") {
        (*value) = true;
        return true;
      } else if (tok == "false") {
        (*value) = false;
        return true;
      }
    }

    // revert
    SeekTo(loc);
  }

  char sc;
  if (!Char1(&sc)) {
    return false;
  }
  _curr_cursor.col++;

  // sign or [0-9]
  if (sc == '0') {
    (*value) = false;
    return true;
  } else if (sc == '1') {
    (*value) = true;
    return true;
  } else {
    PushError("'0' or '1' expected.\n");
    return false;
  }
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<bool> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  bool v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(int *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  // bool negative = false;
  {
    char sc;
    if (!Char1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    // sign or [0-9]
    if (sc == '+') {
      // negative = false;
      has_sign = true;
    } else if (sc == '-') {
      // negative = true;
      has_sign = true;
    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else {
      PushError("Sign or 0-9 expected, but got '" + std::to_string(sc) +
                "'.\n");
      return false;
    }

    ss << sc;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if ((c >= '0') && (c <= '9')) {
      ss << c;
    } else {
      _sr->seek_from_current(-1);
      break;
    }
  }

  if (has_sign && (ss.str().size() == 1)) {
    // sign only
    PushError("Integer value expected but got sign character only.\n");
    return false;
  }

  if ((ss.str().size() > 1) && (ss.str()[0] == '0')) {
    PushError("Zero padded integer value is not allowed.\n");
    return false;
  }

  // std::cout << "ReadInt token: " << ss.str() << "\n";

  int int_value;
  int err = parseInt(ss.str(), &int_value);
  if (err != 0) {
    if (err == -1) {
      PushError("Invalid integer input: `" + ss.str() + "`\n");
      return false;
    } else if (err == -2) {
      PushError("Integer overflows: `" + ss.str() + "`\n");
      return false;
    } else if (err == -3) {
      PushError("Integer underflows: `" + ss.str() + "`\n");
      return false;
    } else {
      PushError("Unknown parseInt error.\n");
      return false;
    }
  }

  (*value) = int_value;

  return true;
}

template <>
bool AsciiParser::ReadBasicType(uint32_t *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  bool negative = false;
  {
    char sc;
    if (!Char1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    // sign or [0-9]
    if (sc == '+') {
      negative = false;
      has_sign = true;
    } else if (sc == '-') {
      negative = true;
      has_sign = true;
    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else {
      PushError("Sign or 0-9 expected, but got '" + std::to_string(sc) +
                "'.\n");
      return false;
    }

    ss << sc;
  }

  if (negative) {
    PushError("Unsigned value expected but got '-' sign.");
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if ((c >= '0') && (c <= '9')) {
      ss << c;
    } else {
      _sr->seek_from_current(-1);
      break;
    }
  }

  if (has_sign && (ss.str().size() == 1)) {
    // sign only
    PushError("Integer value expected but got sign character only.\n");
    return false;
  }

  if ((ss.str().size() > 1) && (ss.str()[0] == '0')) {
    PushError("Zero padded integer value is not allowed.\n");
    return false;
  }

  // std::cout << "ReadInt token: " << ss.str() << "\n";

#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (*value) = std::stoull(ss.str());
  } catch (const std::invalid_argument &e) {
    (void)e;
    PushError("Not an 64bit unsigned integer literal.\n");
    return false;
  } catch (const std::out_of_range &e) {
    (void)e;
    PushError("64bit unsigned integer value out of range.\n");
    return false;
  }
  return true;
#else
  // use jsteemann/atoi
  int retcode = 0;
  auto result = jsteemann::atoi<uint32_t>(
      ss.str().c_str(), ss.str().c_str() + ss.str().size(), retcode);
  DCOUT("sz = " << ss.str().size());
  DCOUT("ss = " << ss.str() << ", retcode = " << retcode
                << ", result = " << result);
  if (retcode == jsteemann::SUCCESS) {
    (*value) = result;
    return true;
  } else if (retcode == jsteemann::INVALID_INPUT) {
    PushError("Not an 32bit unsigned integer literal.\n");
    return false;
  } else if (retcode == jsteemann::INVALID_NEGATIVE_SIGN) {
    PushError("Negative sign `-` specified for uint32 integer.\n");
    return false;
  } else if (retcode == jsteemann::VALUE_OVERFLOW) {
    PushError("Integer value overflows.\n");
    return false;
  }

  PushError("Invalid integer literal\n");
  return false;
#endif
}

template <>
bool AsciiParser::ReadBasicType(int64_t *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  bool negative = false;
  {
    char sc;
    if (!Char1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    // sign or [0-9]
    if (sc == '+') {
      negative = false;
      has_sign = true;
    } else if (sc == '-') {
      negative = true;
      has_sign = true;
    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else {
      PushError("Sign or 0-9 expected, but got '" + std::to_string(sc) +
                "'.\n");
      return false;
    }

    ss << sc;
  }

  if (negative) {
    PushError("Unsigned value expected but got '-' sign.");
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if ((c >= '0') && (c <= '9')) {
      ss << c;
    } else {
      _sr->seek_from_current(-1);
      break;
    }
  }

  if (has_sign && (ss.str().size() == 1)) {
    // sign only
    PushError("Integer value expected but got sign character only.\n");
    return false;
  }

  if ((ss.str().size() > 1) && (ss.str()[0] == '0')) {
    PushError("Zero padded integer value is not allowed.\n");
    return false;
  }

  // std::cout << "ReadInt token: " << ss.str() << "\n";

  // TODO(syoyo): Use ryu parse.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (*value) = std::stoull(ss.str());
  } catch (const std::invalid_argument &e) {
    (void)e;
    PushError("Not an 64bit unsigned integer literal.\n");
    return false;
  } catch (const std::out_of_range &e) {
    (void)e;
    PushError("64bit unsigned integer value out of range.\n");
    return false;
  }

  return true;
#else
  // use jsteemann/atoi
  int retcode;
  auto result = jsteemann::atoi<int64_t>(
      ss.str().c_str(), ss.str().c_str() + ss.str().size(), retcode);
  if (retcode == jsteemann::SUCCESS) {
    (*value) = result;
    return true;
  } else if (retcode == jsteemann::INVALID_INPUT) {
    PushError("Not an 32bit unsigned integer literal.\n");
    return false;
  } else if (retcode == jsteemann::INVALID_NEGATIVE_SIGN) {
    PushError("Negative sign `-` specified for uint32 integer.\n");
    return false;
  } else if (retcode == jsteemann::VALUE_OVERFLOW) {
    PushError("Integer value overflows.\n");
    return false;
  }

  PushError("Invalid integer literal\n");
  return false;
#endif

  // std::cout << "read int ok\n";
}

template <>
bool AsciiParser::ReadBasicType(uint64_t *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  bool negative = false;
  {
    char sc;
    if (!Char1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    // sign or [0-9]
    if (sc == '+') {
      negative = false;
      has_sign = true;
    } else if (sc == '-') {
      negative = true;
      has_sign = true;
    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else {
      PushError("Sign or 0-9 expected, but got '" + std::to_string(sc) +
                "'.\n");
      return false;
    }

    ss << sc;
  }

  if (negative) {
    PushError("Unsigned value expected but got '-' sign.");
    return false;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if ((c >= '0') && (c <= '9')) {
      ss << c;
    } else {
      _sr->seek_from_current(-1);
      break;
    }
  }

  if (has_sign && (ss.str().size() == 1)) {
    // sign only
    PushError("Integer value expected but got sign character only.\n");
    return false;
  }

  if ((ss.str().size() > 1) && (ss.str()[0] == '0')) {
    PushError("Zero padded integer value is not allowed.\n");
    return false;
  }

  // std::cout << "ReadInt token: " << ss.str() << "\n";

  // TODO(syoyo): Use ryu parse.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  try {
    (*value) = std::stoull(ss.str());
  } catch (const std::invalid_argument &e) {
    (void)e;
    PushError("Not an 64bit unsigned integer literal.\n");
    return false;
  } catch (const std::out_of_range &e) {
    (void)e;
    PushError("64bit unsigned integer value out of range.\n");
    return false;
  }

  return true;
#else
  // use jsteemann/atoi
  int retcode;
  auto result = jsteemann::atoi<uint64_t>(
      ss.str().c_str(), ss.str().c_str() + ss.str().size(), retcode);
  if (retcode == jsteemann::SUCCESS) {
    (*value) = result;
    return true;
  } else if (retcode == jsteemann::INVALID_INPUT) {
    PushError("Not an 32bit unsigned integer literal.\n");
    return false;
  } else if (retcode == jsteemann::INVALID_NEGATIVE_SIGN) {
    PushError("Negative sign `-` specified for uint32 integer.\n");
    return false;
  } else if (retcode == jsteemann::VALUE_OVERFLOW) {
    PushError("Integer value overflows.\n");
    return false;
  }

  PushError("Invalid integer literal\n");
  return false;
#endif

  // std::cout << "read int ok\n";
}

template <>
bool AsciiParser::ReadBasicType(value::float2 *value) {
  return ParseBasicTypeTuple(value);
}

template <>
bool AsciiParser::ReadBasicType(value::float3 *value) {
  return ParseBasicTypeTuple(value);
}

template <>
bool AsciiParser::ReadBasicType(value::point3f *value) {
  value::float3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::normal3f *value) {
  value::float3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::vector3f *value) {
  value::float3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::vector3d *value) {
  value::double3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::float4 *value) {
  return ParseBasicTypeTuple(value);
}

template <>
bool AsciiParser::ReadBasicType(value::double2 *value) {
  return ParseBasicTypeTuple(value);
}

template <>
bool AsciiParser::ReadBasicType(value::double3 *value) {
  return ParseBasicTypeTuple(value);
}

template <>
bool AsciiParser::ReadBasicType(value::point3d *value) {
  value::double3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::color3f *value) {
  value::float3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->r = v[0];
    value->g = v[1];
    value->b = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::color3d *value) {
  value::double3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->r = v[0];
    value->g = v[1];
    value->b = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::color4f *value) {
  value::float3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->r = v[0];
    value->g = v[1];
    value->b = v[2];
    value->a = v[3];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::color4d *value) {
  value::double4 v;
  if (ParseBasicTypeTuple(&v)) {
    value->r = v[0];
    value->g = v[1];
    value->b = v[2];
    value->a = v[3];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::normal3d *value) {
  value::double3 v;
  if (ParseBasicTypeTuple(&v)) {
    value->x = v[0];
    value->y = v[1];
    value->z = v[2];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::double4 *value) {
  return ParseBasicTypeTuple(value);
}

///
/// Parses 1 or more occurences of value with basic type 'T', separated by
/// `sep`
///
template <typename T>
bool AsciiParser::SepBy1BasicType(const char sep,
                                  std::vector<nonstd::optional<T>> *result) {
  result->clear();

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      PushError("Not starting with the value of requested type.\n");
      return false;
    }

    result->push_back(value);
  }

  while (!Eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != sep) {
      // end
      _sr->seek_from_current(-1);  // unwind single char
      break;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      break;
    }

    result->push_back(value);
  }

  if (result->empty()) {
    PushError("Empty array.\n");
    return false;
  }

  return true;
}

///
/// Parses 1 or more occurences of value with basic type 'T', separated by
/// `sep`
///
template <typename T>
bool AsciiParser::SepBy1BasicType(const char sep, std::vector<T> *result) {
  result->clear();

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    T value;
    if (!ReadBasicType(&value)) {
      PushError("Not starting with the value of requested type.\n");
      return false;
    }

    result->push_back(value);
  }

  while (!Eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != sep) {
      // end
      _sr->seek_from_current(-1);  // unwind single char
      break;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    T value;
    if (!ReadBasicType(&value)) {
      break;
    }

    result->push_back(value);
  }

  if (result->empty()) {
    PushError("Empty array.\n");
    return false;
  }

  return true;
}

///
/// Parses 1 or more occurences of value with tuple type 'T', separated by
/// `sep`
///
template <typename T, size_t N>
bool AsciiParser::SepBy1TupleType(
    const char sep, std::vector<nonstd::optional<std::array<T, N>>> *result) {
  result->clear();

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  if (MaybeNone()) {
    result->push_back(nonstd::nullopt);
  } else {
    std::array<T, N> value;
    if (!ParseBasicTypeTuple<T, N>(&value)) {
      PushError("Not starting with the tuple value of requested type.\n");
      return false;
    }

    result->push_back(value);
  }

  while (!Eof()) {
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != sep) {
      // end
      _sr->seek_from_current(-1);  // unwind single char
      break;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (MaybeNone()) {
      result->push_back(nonstd::nullopt);
    } else {
      std::array<T, N> value;
      if (!ParseBasicTypeTuple<T, N>(&value)) {
        break;
      }
      result->push_back(value);
    }
  }

  if (result->empty()) {
    PushError("Empty array.\n");
    return false;
  }

  return true;
}

///
/// Parses 1 or more occurences of value with tuple type 'T', separated by
/// `sep`
///
template <typename T, size_t N>
bool AsciiParser::SepBy1TupleType(const char sep,
                                  std::vector<std::array<T, N>> *result) {
  result->clear();

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    std::array<T, N> value;
    if (!ParseBasicTypeTuple<T, N>(&value)) {
      PushError("Not starting with the tuple value of requested type.\n");
      return false;
    }

    result->push_back(value);
  }

  while (!Eof()) {
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != sep) {
      // end
      _sr->seek_from_current(-1);  // unwind single char
      break;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::array<T, N> value;
    if (!ParseBasicTypeTuple<T, N>(&value)) {
      break;
    }

    result->push_back(value);
  }

  if (result->empty()) {
    PushError("Empty array.\n");
    return false;
  }

  return true;
}

///
/// Parse '[', Sep1By(','), ']'
///
template <typename T>
bool AsciiParser::ParseBasicTypeArray(
    std::vector<nonstd::optional<T>> *result) {
  if (!Expect('[')) {
    return false;
  }

  if (!SepBy1BasicType<T>(',', result)) {
    return false;
  }

  if (!Expect(']')) {
    return false;
  }

  return true;
}

///
/// Parse '[', Sep1By(','), ']'
///
template <typename T>
bool AsciiParser::ParseBasicTypeArray(std::vector<T> *result) {
  if (!Expect('[')) {
    return false;
  }

  if (!SepBy1BasicType<T>(',', result)) {
    return false;
  }

  if (!Expect(']')) {
    return false;
  }
  return true;
}

///
/// Parses 1 or more occurences of asset references, separated by
/// `sep`
/// TODO: Parse LayerOffset: e.g. `(offset = 10; scale = 2)`
///
template <>
bool AsciiParser::SepBy1BasicType(const char sep,
                                  std::vector<Reference> *result) {
  result->clear();

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    Reference ref;
    bool triple_deliminated{false};

    if (!ParseReference(&ref, &triple_deliminated)) {
      PushError("Failed to parse Reference.\n");
      return false;
    }

    (void)triple_deliminated;

    result->push_back(ref);
  }

  while (!Eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != sep) {
      // end
      _sr->seek_from_current(-1);  // unwind single char
      break;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    Reference ref;
    bool triple_deliminated{false};
    if (!ParseReference(&ref, &triple_deliminated)) {
      break;
    }

    (void)triple_deliminated;
    result->push_back(ref);
  }

  if (result->empty()) {
    PushError("Empty array.\n");
    return false;
  }

  return true;
}

bool AsciiParser::ParsePurpose(Purpose *result) {
  if (!result) {
    return false;
  }

  if (!SkipCommentAndWhitespaceAndNewline()) {
    return false;
  }

  std::string str;
  if (!ReadStringLiteral(&str)) {
    return false;
  }

  if (str == "\"default\"") {
    (*result) = Purpose::Default;
  } else if (str == "\"render\"") {
    (*result) = Purpose::Render;
  } else if (str == "\"proxy\"") {
    (*result) = Purpose::Proxy;
  } else if (str == "\"guide\"") {
    (*result) = Purpose::Guide;
  } else {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "Invalid purpose value: " + str + "\n");
  }

  return true;
}

template <typename T, size_t N>
bool AsciiParser::ParseBasicTypeTuple(std::array<T, N> *result) {
  if (!Expect('(')) {
    return false;
  }

  std::vector<T> values;
  if (!SepBy1BasicType<T>(',', &values)) {
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != N) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(N) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PushError(msg);
    return false;
  }

  for (size_t i = 0; i < N; i++) {
    (*result)[i] = values[i];
  }

  return true;
}

template <typename T, size_t N>
bool AsciiParser::ParseBasicTypeTuple(
    nonstd::optional<std::array<T, N>> *result) {
  if (MaybeNone()) {
    (*result) = nonstd::nullopt;
    return true;
  }

  if (!Expect('(')) {
    return false;
  }

  std::vector<T> values;
  if (!SepBy1BasicType<T>(',', &values)) {
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != N) {
    PUSH_ERROR_AND_RETURN("The number of tuple elements must be " +
                          std::to_string(N) + ", but got " +
                          std::to_string(values.size()));
  }

  std::array<T, N> ret;
  for (size_t i = 0; i < N; i++) {
    ret[i] = values[i];
  }

  (*result) = ret;

  return true;
}

///
/// Parse array of asset references
/// Allow non-list version
///
template <>
bool AsciiParser::ParseBasicTypeArray(std::vector<Reference> *result) {
  if (!SkipWhitespace()) {
    return false;
  }

  char c;
  if (!Char1(&c)) {
    return false;
  }

  if (c != '[') {
    Rewind(1);

    DCOUT("Guess non-list version");
    // Guess non-list version
    Reference ref;
    bool triple_deliminated{false};
    if (!ParseReference(&ref, &triple_deliminated)) {
      return false;
    }

    (void)triple_deliminated;
    result->clear();
    result->push_back(ref);

  } else {
    if (!SepBy1BasicType(',', result)) {
      return false;
    }

    if (!Expect(']')) {
      return false;
    }
  }

  return true;
}

///
/// Parse PathVector
///
template <>
bool AsciiParser::ParseBasicTypeArray(std::vector<Path> *result) {
  if (!SkipWhitespace()) {
    return false;
  }

  if (!Expect('[')) {
    return false;
  }

  if (!SepBy1BasicType(',', result)) {
    return false;
  }

  if (!Expect(']')) {
    return false;
  }

  return true;
}

template <typename T>
bool AsciiParser::MaybeNonFinite(T *out) {
  auto loc = CurrLoc();

  // "-inf", "inf" or "nan"
  std::vector<char> buf(4);
  if (!CharN(3, &buf)) {
    return false;
  }
  SeekTo(loc);

  if ((buf[0] == 'i') && (buf[1] == 'n') && (buf[2] == 'f')) {
    (*out) = std::numeric_limits<T>::infinity();
    return true;
  }

  if ((buf[0] == 'n') && (buf[1] == 'a') && (buf[2] == 'n')) {
    (*out) = std::numeric_limits<T>::quiet_NaN();
    return true;
  }

  bool ok = CharN(4, &buf);
  SeekTo(loc);

  if (ok) {
    if ((buf[0] == '-') && (buf[1] == 'i') && (buf[2] == 'n') &&
        (buf[3] == 'f')) {
      (*out) = -std::numeric_limits<T>::infinity();
      return true;
    }

    // NOTE: support "-nan"?
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::texcoord2f *value) {
  if (!Expect('(')) {
    return false;
  }

  std::vector<float> values;
  if (!SepBy1BasicType<float>(',', &values)) {
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 2) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(2) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->s = values[0];
  value->t = values[1];

  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::texcoord2f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::texcoord2f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::texcoord3f *value) {
  if (!Expect('(')) {
    return false;
  }

  std::vector<float> values;
  if (!SepBy1BasicType<float>(',', &values)) {
    return false;
  }

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->s = values[0];
  value->t = values[1];
  value->r = values[2];

  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::texcoord3f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::texcoord3f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::float2> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::float2 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::float3> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::float3 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::float4> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::float4 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::double2> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::double2 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::double3> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::double3 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::double4> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::double4 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::point3f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::point3f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::point3d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::point3d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::normal3f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::normal3f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::normal3d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::normal3d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::vector3f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::vector3f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::vector3d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::vector3d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::color3f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::color3f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::color4f> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::color4f v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::color3d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::color3d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::color4d> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::color4d v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<int> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  int v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<uint32_t> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  uint32_t v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<int64_t> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  int64_t v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<uint64_t> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  uint64_t v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(float *value) {
  // -inf, inf, nan
  {
    float v;
    if (MaybeNonFinite(&v)) {
      (*value) = v;
      return true;
    }
  }

  std::string value_str;
  if (!LexFloat(&value_str)) {
    PUSH_ERROR_AND_RETURN("Failed to lex floating value literal.");
  }

  auto flt = ParseFloat(value_str);
  if (flt) {
    (*value) = flt.value();
  } else {
    PUSH_ERROR_AND_RETURN("Failed to parse floating value.");
  }

  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<float> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  float v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(double *value) {
  // -inf, inf, nan
  {
    double v;
    if (MaybeNonFinite(&v)) {
      (*value) = v;
      return true;
    }
  }

  std::string value_str;
  if (!LexFloat(&value_str)) {
    PUSH_ERROR_AND_RETURN("Failed to lex floating value literal.");
  }

  auto flt = ParseDouble(value_str);
  if (!flt) {
    PUSH_ERROR_AND_RETURN("Failed to parse floating value.");
  } else {
    (*value) = flt.value();
  }

  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<double> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  double v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::half *value) {
  // Parse as float
  float v;
  if (!ReadBasicType(&v)) {
    return false;
  }

  (*value) = float_to_half_full(v);
  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::half> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::half v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::half2 *value) {
  // Parse as float
  value::float2 v;
  if (!ReadBasicType(&v)) {
    return false;
  }

  (*value)[0] = float_to_half_full(v[0]);
  (*value)[1] = float_to_half_full(v[1]);
  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::half2> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::half2 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::half3 *value) {
  // Parse as float
  value::float3 v;
  if (!ReadBasicType(&v)) {
    return false;
  }

  (*value)[0] = float_to_half_full(v[0]);
  (*value)[1] = float_to_half_full(v[1]);
  (*value)[2] = float_to_half_full(v[2]);
  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::half3> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::half3 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::half4 *value) {
  // Parse as float
  value::float4 v;
  if (!ReadBasicType(&v)) {
    return false;
  }

  (*value)[0] = float_to_half_full(v[0]);
  (*value)[1] = float_to_half_full(v[1]);
  (*value)[2] = float_to_half_full(v[2]);
  (*value)[3] = float_to_half_full(v[3]);
  return true;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::half4> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::half4 v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::quath *value) {
  value::half4 v;
  if (ReadBasicType(&v)) {
    value->real = v[0];
    value->imag[0] = v[1];
    value->imag[1] = v[2];
    value->imag[2] = v[3];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::quath> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::quath v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::quatf *value) {
  value::float4 v;
  if (ReadBasicType(&v)) {
    value->real = v[0];
    value->imag[0] = v[1];
    value->imag[1] = v[2];
    value->imag[2] = v[3];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::quatf> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::quatf v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::quatd *value) {
  value::double4 v;
  if (ReadBasicType(&v)) {
    value->real = v[0];
    value->imag[0] = v[1];
    value->imag[1] = v[2];
    value->imag[2] = v[3];
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::quatd> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::quatd v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

template <>
bool AsciiParser::ReadBasicType(value::AssetPath *value) {

  bool triple_deliminated;
  if (ParseAssetIdentifier(value, &triple_deliminated)) {
    return true;
  }
  return false;
}

template <>
bool AsciiParser::ReadBasicType(nonstd::optional<value::AssetPath> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  value::AssetPath v;
  if (ReadBasicType(&v)) {
    (*value) = v;
    return true;
  }

  return false;
}

// 1D array
template <typename T>
bool AsciiParser::ReadBasicType(std::vector<T> *value) {
  return ParseBasicTypeArray(value);
}

template <typename T>
bool AsciiParser::ReadBasicType(nonstd::optional<std::vector<T>> *value) {
  if (MaybeNone()) {
    (*value) = nonstd::nullopt;
    return true;
  }

  std::vector<T> v;
  if (ParseBasicTypeArray(&v)) {
    (*value) = v;
    return true;
  }

  return false;
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
    var.Set(val);
  } else if (type_name == "float") {
    if (array_qual) {
      std::vector<float> vss;
      if (!ParseBasicTypeArray(&vss)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `float[]`");
      }
      var.Set(vss);
    } else {
      float val;
      if (!ReadBasicType(&val)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `float`");
      }
      var.Set(val);
    }
  } else if (type_name == "string") {
    if (array_qual) {
      std::vector<std::string> strs;
      if (!ParseBasicTypeArray(&strs)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `string[]`");
      }
      var.Set(strs);
    } else {
      std::string str;
      if (!ReadStringLiteral(&str)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `string`");
      }
      var.Set(str);
    }
  } else if (type_name == "token") {
    if (array_qual) {
      std::vector<std::string> strs;
      if (!ParseBasicTypeArray(&strs)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `token[]`");
      }
      std::vector<value::token> toks;
      for (auto item : strs) {
        toks.push_back(value::token(item));
      }
      var.Set(toks);
    } else {
      std::string str;
      if (!ReadStringLiteral(&str)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `token`");
      }
      value::token tok(str);
      var.Set(tok);
    }
  } else if (type_name == "dictionary") {
    CustomDataType dict;

    DCOUT("Parse dictionary");
    if (!ParseDict(&dict)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `dictionary`");
    }
    var.Set(dict);
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

      if (!var.Valid()) {
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

  if (c0 != '"') {
    DCOUT("c0 = " << c0);
    PUSH_ERROR_AND_RETURN(
        "String literal expected but it does not start with '\"'");
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

    if (c == '"') {
      end_with_quotation = true;
      break;
    }

    ss << c;
  }

  if (!end_with_quotation) {
    PUSH_ERROR_AND_RETURN(
        "String literal expected but it does not end with '\"'");
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

  if (triple_quote[0] == '"' && triple_quote[1] == '"' &&
      triple_quote[2] == '"') {
    // ok
  } else {
    SeekTo(loc);
    return false;
  }

  // Read until next triple-quote `"""`
  std::stringstream str_buf;

  auto locinfo = _curr_cursor;

  int quote_count = 0;

  while (!Eof()) {
    char c;

    if (!Char1(&c)) {
      SeekTo(loc);
      return false;
    }

    str_buf << c;

    quote_count = (c == '"') ? (quote_count + 1) : 0;

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

    if (quote_count == 3) {
      // got '"""'
      break;
    }
  }

  if (quote_count != 3) {
    SeekTo(loc);
    return false;
  }

  DCOUT("Triple quoted string found. col " << start_cursor.col << ", row "
                                           << start_cursor.row);

  // remove last '"""'
  str->value = removeSuffix(str_buf.str(), "\"\"\"");
  str->line_col = start_cursor.col;
  str->line_row = start_cursor.row;
  str->is_triple_quoted = true;

  _curr_cursor = locinfo;

  return true;
}

bool AsciiParser::ReadPrimAttrIdentifier(std::string *token) {
  // Example: xformOp:transform

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
    } else if (!std::isalpha(int(c))) {
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
      DCOUT("Invalid identiefier: c = " << c);
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

  if (varname == "defaultPrim") {
    if (auto pv = var.Get<value::token>()) {
      DCOUT("defaultPrim = " << pv.value());
      _stage_metas.defaultPrim = pv.value();
    } else {
      PUSH_ERROR_AND_RETURN("`defaultPrim` isn't a token value.");
    }
  } else if (varname == "subLayers") {
    if (auto pv = var.Get<std::vector<value::token>>()) {
      DCOUT("subLayers = " << pv.value());
      for (const auto &item : pv.value()) {
        _stage_metas.subLayers.push_back(item);
      }
    } else {
      PUSH_ERROR_AND_RETURN("`subLayers` isn't an array of string values.");
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

#if 0  // Load subLayers in usda-reader
  // Load subLayers
  if (sublayers.size()) {
    // Create another USDA parser.

    for (size_t i = 0; i < sublayers.size(); i++) {
      std::string filepath = io::JoinPath(_base_dir, sublayers[i]);

      std::vector<uint8_t> data;
      std::string err;
      if (!io::ReadWholeFile(&data, &err, filepath, /* max_filesize */ 0)) {
        PUSH_ERROR_AND_RETURN("Failed to read file: " + filepath);
      }

      tinyusdz::StreamReader sr(data.data(), data.size(),
                                /* swap endian */ false);
      tinyusdz::ascii::AsciiParser parser(&sr);

      std::string base_dir = io::GetBaseDir(filepath);

      parser.SetBaseDir(base_dir);

      {
        bool ret = parser.Parse(tinyusdz::ascii::LOAD_STATE_SUBLAYER);

        if (!ret) {
          PUSH_WARN("Failed to parse .usda: " << parser.GetError());
        }
      }
    }

    // TODO: Merge/Import subLayer.
  }
#endif

#if 0  // TODO
    if (var.type == "string") {
      std::string value;
      std::cout << "read string literal\n";
      if (!ReadStringLiteral(&value)) {
        std::string msg = "String literal expected for `" + var.name + "`.\n";
        PushError(msg);
        return false;
      }
    } else if (var.type == "int[]") {
      std::vector<int> values;
      if (!ParseBasicTypeArray<int>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; PushError(msg);
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "int[" << i << "] = " << values[i] << "\n";
      }
    } else if (var.type == "float[]") {
      std::vector<float> values;
      if (!ParseBasicTypeArray<float>(&values)) {
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "float[" << i << "] = " << values[i] << "\n";
      }
    } else if (var.type == "float3[]") {
      std::vector<std::array<float, 3>> values;
      if (!ParseTupleArray<float, 3>(&values)) {
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "float[" << i << "] = " << values[i][0] << ", "
                  << values[i][1] << ", " << values[i][2] << "\n";
      }
    } else if (var.type == "float") {
      std::string fval;
      std::string ferr;
      if (!LexFloat(&fval, &ferr)) {
        std::string msg =
            "Floating point literal expected for `" + var.name + "`.\n";
        if (!ferr.empty()) {
          msg += ferr;
        }
        PushError(msg);
        return false;
      }
      std::cout << "float : " << fval << "\n";
      float value;
      if (!ParseFloat(fval, &value, &ferr)) {
        std::string msg =
            "Failed to parse floating point literal for `" + var.name + "`.\n";
        if (!ferr.empty()) {
          msg += ferr;
        }
        PushError(msg);
        return false;
      }
      std::cout << "parsed float : " << value << "\n";

    } else if (var.type == "int3") {
      std::array<int, 3> values;
      if (!ParseBasicTypeTuple<int, 3>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; PushError(msg);
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "int[" << i << "] = " << values[i] << "\n";
      }
    } else if (var.type == "object") {
      // TODO: support nested parameter.
    }
#endif

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
  assert(parse_stack.size() < _sr->size());

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

//
// -- impl ParseTimeSampleData
//

template <typename T>
nonstd::optional<AsciiParser::TimeSampleData<T>>
AsciiParser::TryParseTimeSamples() {
  // timeSamples = '{' ((int : T) sep)+ '}'
  // sep = ','(may ok to omit for the last element.

  TimeSampleData<T> data;

  if (!Expect('{')) {
    return nonstd::nullopt;
  }

  if (!SkipWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  while (!Eof()) {
    char c;
    if (!Char1(&c)) {
      return nonstd::nullopt;
    }

    if (c == '}') {
      break;
    }

    Rewind(1);

    double timeVal;
    // -inf, inf and nan are handled.
    if (!ReadBasicType(&timeVal)) {
      PushError("Parse time value failed.");
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    if (!Expect(':')) {
      return nonstd::nullopt;
    }

    if (!SkipWhitespace()) {
      return nonstd::nullopt;
    }

    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      return nonstd::nullopt;
    }

    // The last element may have separator ','
    {
      // Semicolon ';' is not allowed as a separator for timeSamples array
      // values.
      if (!SkipWhitespace()) {
        return nonstd::nullopt;
      }

      char sep;
      if (!Char1(&sep)) {
        return nonstd::nullopt;
      }

      DCOUT("sep = " << sep);
      if (sep == '}') {
        // End of item
        data.push_back({timeVal, value});
        break;
      } else if (sep == ',') {
        // ok
      } else {
        Rewind(1);

        // Look ahead Newline + '}'
        auto loc = CurrLoc();

        if (SkipWhitespaceAndNewline()) {
          char nc;
          if (!Char1(&nc)) {
            return nonstd::nullopt;
          }

          if (nc == '}') {
            // End of item
            data.push_back({timeVal, value});
            break;
          }
        }

        // Rewind and continue parsing.
        SeekTo(loc);
      }
    }

    if (!SkipWhitespaceAndNewline()) {
      return nonstd::nullopt;
    }

    data.push_back({timeVal, value});
  }

  DCOUT("Parse TimeSamples success. # of items = " << data.size());

  return std::move(data);
}

template <typename T>
value::TimeSamples AsciiParser::ConvertToTimeSamples(
    const TimeSampleData<T> &ts) {
  value::TimeSamples dst;

  for (const auto &item : ts) {
    dst.times.push_back(std::get<0>(item));

    if (item.second) {
      dst.values.push_back(item.second.value());
    } else {
      // Blocked.
      dst.values.push_back(value::ValueBlock());
    }
  }

  return dst;
}

template <typename T>
value::TimeSamples AsciiParser::ConvertToTimeSamples(
    const TimeSampleData<std::vector<T>> &ts) {
  value::TimeSamples dst;

  for (const auto &item : ts) {
    dst.times.push_back(std::get<0>(item));

    if (item.second) {
      dst.values.push_back(item.second.value());
    } else {
      // Blocked.
      dst.values.push_back(value::ValueBlock());
    }
  }

  return dst;
}

#if 0
template<>
bool AsciiParser::ParseTimeSamples(
    nonstd::optional<value::float2> *out_value) {
  nonstd::optional<std::array<float, 2>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::float3> *out_value) {
  nonstd::optional<std::array<float, 3>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::float4> *out_value) {
  nonstd::optional<std::array<float, 4>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(nonstd::optional<float> *out_value) {
  nonstd::optional<float> value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<double> *out_value) {
  nonstd::optional<double> value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::double2> *out_value) {
  nonstd::optional<value::double2> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::double3> *out_value) {
  nonstd::optional<value::double3> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::double4> *out_value) {
  nonstd::optional<value::double4> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<std::vector<value::float3>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
    return true;
  }

  std::vector<std::array<float, 3>> value;
  if (!ParseTupleArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<std::vector<value::double3>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
    return true;
  }

  std::vector<std::array<double, 3>> value;
  if (!ParseTupleArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<std::vector<float>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
    return true;
  }

  std::vector<float> value;
  if (!ParseBasicTypeArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<std::vector<double>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
    return true;
  }

  std::vector<double> value;
  if (!ParseBasicTypeArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}


template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<std::vector<value::matrix4d>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
  }

  std::vector<value::matrix4d> value;
  if (!ParseBasicTypeArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

template<>
bool AsciiParser::ParseTimeSampleData(
    nonstd::optional<value::matrix4d> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
  }

  value::matrix4d value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  (*out_value) = value;


  return true;
}
#endif

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

bool AsciiParser::ParseAssetIdentifier(value::AssetPath *out, bool *triple_deliminated)
{
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

  // TODO: Refactor.
  if (vartype == value::kBool) {
    bool value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Boolean value expected for `" + varname + "`.");
    }
    DCOUT("bool = " << value);

    var.Set(value);
  } else if (vartype == value::kToken) {
    value::token value;
    if (!ReadBasicType(&value)) {
      std::string msg = "Token expected for `" + varname + "`.\n";
      PushError(msg);
      return false;
    }
    DCOUT("token = " << value);

#if 0  // TODO
    auto ret = def.post_parse_handler(value);
    if (!ret) {
      DCOUT("error = " << ret.error());
      PUSH_ERROR_AND_RETURN("Invalid token for `" + varname + "`. " + ret.error());
    }
#endif

    var.Set(value);
  } else if (vartype == "token[]") {
    std::vector<value::token> value;
    if (!ParseBasicTypeArray(&value)) {
      std::string msg = "Token array expected for `" + varname + "`.\n";
      PushError(msg);
      return false;
    }
    // TODO
    // DCOUT("token[] = " << to_string(value));

    var.Set(value);
  } else if (vartype == value::kString) {
    StringData sdata;
    if (MaybeTripleQuotedString(&sdata)) {
    } else {
      std::string value;
      auto lineinfo = _curr_cursor;
      if (!ReadStringLiteral(&value)) {
        PUSH_ERROR_AND_RETURN("String literal expected for `" + varname + "`.");
      }

      sdata.value = value;
      sdata.is_triple_quoted = false;
      sdata.line_row = lineinfo.row;
      sdata.line_col = lineinfo.col;
    }
    DCOUT("string = " << sdata.value);

    var.Set(sdata);
  } else if (vartype == "string[]") {
    // TODO: Support multi-line string?
    std::vector<std::string> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.Set(values);
  } else if (vartype == "ref[]") {
    std::vector<Reference> values;
    if (!ParseBasicTypeArray(&values)) {
      PUSH_ERROR_AND_RETURN("Array of Reference expected for `" + varname +
                            "`.");
    }

    var.Set(values);

  } else if (vartype == "int[]") {
    std::vector<int> values;
    if (!ParseBasicTypeArray<int>(&values)) {
      // std::string msg = "Array of int values expected for `" + var.name +
      // "`.\n"; PushError(msg);
      return false;
    }

    for (size_t i = 0; i < values.size(); i++) {
      DCOUT("int[" << i << "] = " << values[i]);
    }

    var.Set(values);
  } else if (vartype == "float[]") {
    std::vector<float> values;
    if (!ParseBasicTypeArray<float>(&values)) {
      return false;
    }

    var.Set(values);
  } else if (vartype == "float3[]") {
    std::vector<std::array<float, 3>> values;
    if (!ParseTupleArray<float, 3>(&values)) {
      return false;
    }

    var.Set(values);
  } else if (vartype == "double[]") {
    std::vector<double> values;
    if (!ParseBasicTypeArray<double>(&values)) {
      return false;
    }

    var.Set(values);
  } else if (vartype == "double3[]") {
    std::vector<value::double3> values;
    if (!ParseTupleArray(&values)) {
      return false;
    }

    var.Set(values);
  } else if (vartype == value::kFloat) {
    std::string fval;
    std::string ferr;
    if (!LexFloat(&fval)) {
      PUSH_ERROR_AND_RETURN("Floating point literal expected for `" + varname +
                            "`.");
    }
    auto ret = ParseFloat(fval);
    if (!ret) {
      PUSH_ERROR_AND_RETURN("Failed to parse floating point literal for `" +
                            varname + "`.");
    }

    var.Set(ret.value());
  } else if (vartype == value::kDouble) {
    std::string fval;
    std::string ferr;
    if (!LexFloat(&fval)) {
      PUSH_ERROR_AND_RETURN("Floating point literal expected for `" + varname +
                            "`.");
    }
    auto ret = ParseDouble(fval);
    if (!ret) {
      PUSH_ERROR_AND_RETURN("Failed to parse floating point literal for `" +
                            varname + "`.");
    }

    var.Set(ret.value());

  } else if (vartype == "int3") {
    std::array<int, 3> values;
    if (!ParseBasicTypeTuple<int, 3>(&values)) {
      // std::string msg = "Array of int values expected for `" + var.name +
      // "`.\n"; PushError(msg);
      return false;
    }

    var.Set(values);
  } else if (vartype == value::kDictionary) {
    DCOUT("Parse dict in meta.");
    CustomDataType dict;
    if (!ParseDict(&dict)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `dictonary` in metadataum.");
    }
    var.Set(dict);
  } else {
    PUSH_ERROR_AND_RETURN("TODO: vartype = " + vartype);
  }

  var.type = vartype;

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

bool AsciiParser::ParseStageMeta(std::tuple<ListEditQual, MetaVariable> *out) {
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
  var.name = varname;

  if (vardef.type == "path") {
    std::string value;
    if (!ReadPathIdentifier(&value)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii, "Failed to parse path identifier");
    }

    Path p(value, "");
    var.Set(p);

  } else if (vardef.type == "path[]") {
    std::vector<PathIdentifier> values;
    if (!ParseBasicTypeArray(&values)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii,
                                "Failed to parse array of path identifiers");
    }

    std::vector<Path> pvs;
    for (const auto &v : values) {
      pvs.push_back(Path(v, ""));
    }

    var.Set(pvs);

  } else if (vardef.type == "ref[]") {
    std::vector<Reference> value;
    if (!ParseBasicTypeArray(&value)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii,
                                "Failed to parse array of assert reference");
    }

    var.Set(value);

  } else if (vardef.type == "string") {
    std::string value;
    if (!ReadStringLiteral(&value)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii, " ReadStringLiteral failed");
      return false;
    }

    var.Set(value);
  } else if (vardef.type == "string[]") {
    std::vector<std::string> value;
    if (!ParseBasicTypeArray(&value)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii, "ReadStringArray failed.");
      return false;
    }

    DCOUT("vardef.type: " << vardef.type << ", name = " << varname);
    var.Set(value);

  } else if (vardef.type == value::kBool) {
    bool value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN_TAG(kAscii, "ReadBool failed.");
    }

    DCOUT("vardef.type: " << vardef.type << ", name = " << varname);
    var.Set(value);

  } else {
    PUSH_ERROR_AND_RETURN("TODO: varname " + varname + ", type " + vardef.type);
  }

  std::get<0>(*out) = qual;
  std::get<1>(*out) = var;

  return true;
}

nonstd::optional<std::tuple<ListEditQual, MetaVariable>>
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
      var.Set(sdata);

      return std::make_tuple(qual, var);

    } else if (MaybeString(&sdata)) {
      MetaVariable var;
      var.name = "";  // empty
      var.Set(sdata);

      return std::make_tuple(qual, var);
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

  const VariableDef &vardef = _supported_prim_metas.at(varname);
  MetaVariable var;
  if (!ParseMetaValue(vardef, &var)) {
    PushError("Failed to parse Prim meta value.\n");
    return nonstd::nullopt;
  }
  var.name = varname;

  return std::make_tuple(qual, var);
}

bool AsciiParser::ParsePrimMetas(
    std::map<std::string, std::tuple<ListEditQual, MetaVariable>> *args) {
  // '(' args ')'
  // args = list of argument, separated by newline.

  if (!SkipWhitespaceAndNewline()) {
    DCOUT("SkipWhitespaceAndNewline failed.");
    return false;
  }

  // The first character.
  {
    char c;
    if (!Char1(&c)) {
      // this should not happen.
      return false;
    }

    DCOUT("c = " << c);

    if (c == '(') {
      DCOUT("Prim meta start");
      // ok
    } else {
      _sr->seek_from_current(-1);
      // DCOUT("Unknown c");
      // return false;
    }
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

    // ty = std::tuple<ListEditQual, MetaVariable>;
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

      std::string token;
      if (!ReadIdentifier(&token)) {
        return false;
      }

      DCOUT("Attribute meta name: " << token);

      if ((token != "interpolation") && (token != "customData") &&
          (token != "elementSize")) {
        PUSH_ERROR_AND_RETURN(
            "Currently only string-only data, `interpolation`, `elementSize` "
            "or `customData` "
            "is supported but "
            "got: " +
            token);
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

      if (token == "interpolation") {
        std::string value;
        if (!ReadStringLiteral(&value)) {
          return false;
        }

        DCOUT("Got `interpolation` meta : " << value);
        out_meta->interpolation = InterpolationFromString(value);
      } else if (token == "elementSize") {
        uint32_t value;
        if (!ReadBasicType(&value)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `elementSize`");
        }

        DCOUT("Got `elementSize` meta : " << value);
        out_meta->elementSize = value;
      } else if (token == "customData") {
        std::map<std::string, MetaVariable> dict;

        if (!ParseDict(&dict)) {
          return false;
        }

        DCOUT("Got `customData` meta");
        out_meta->customData = dict;

      } else {
        // ???
        return false;
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
bool AsciiParser::ParseRelation(Relation *result) {
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
    result->Set(value);

  } else if (c == '<') {
    // Path
    Path value;
    if (!ReadBasicType<Path>(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse Path.");
    }
    result->Set(value);
  } else if (c == '[') {
    // PathVector
    std::vector<Path> value;
    if (!ParseBasicTypeArray(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse PathVector.");
    }
    result->Set(value);
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
                                     PrimAttrib *out_attr) {
  PrimAttrib attr;
  primvar::PrimVar var;
  bool blocked{false};

  if (array_qual) {
    if (value::TypeTrait<T>::type_name() == "bool") {
      PushError("Array of bool type is not supported.");
      return false;
    } else {
      std::vector<T> value;
      if (!ParseBasicTypeArray(&value)) {
        PUSH_ERROR_AND_RETURN("Failed to parse " +
                              std::string(value::TypeTrait<T>::type_name()) +
                              " array.");
        return false;
      }

      if (value.size()) {
        DCOUT("Got it: ty = " + std::string(value::TypeTrait<T>::type_name()) +
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
                            std::string(value::TypeTrait<T>::type_name()));
      return false;
    }

    if (value) {
      DCOUT("ParseBasicPrimAttr: " << value::TypeTrait<T>::type_name() << " = "
                                   << (*value));

      // TODO: TimeSampled
      value::TimeSamples ts;
      ts.values.push_back(*value);
      var.set_timesamples(ts);

    } else {
      blocked = true;
      // std::cout << "ParseBasicPrimAttr: " <<
      // value::TypeTrait<T>::type_name()
      //           << " = None\n";
    }
  }

  // optional: interpolation parameter
  AttrMeta meta;
  if (!ParseAttrMeta(&meta)) {
    PUSH_ERROR_AND_RETURN("Failed to parse PrimAttrib meta.");
  }
  attr.meta = meta;

  if (blocked) {
    attr.set_blocked(true);
  } else {
    attr.set_var(std::move(var));
  }

  (*out_attr) = std::move(attr);

  return true;
}

bool AsciiParser::ParsePrimAttr(std::map<std::string, Property> *props) {
  // prim_attr : (custom?) uniform type (array_qual?) name '=' value
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

    if (c == '(') {
      // TODO:
      PUSH_ERROR_AND_RETURN("TODO: Support parsing Property meta.");
    }

    if (c != '=') {
      DCOUT("Relationship with no target: " << attr_name);

      // No targets. Define only.
      Property p(type_name, custom_qual);
      p.type = Property::Type::NoTargetsRelation;
      p.listOpQual = listop_qual;

      (*props)[attr_name] = p;

      return true;
    }

    // has targets
    if (!Expect('=')) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (MaybeNone()) {
      PUSH_ERROR_AND_RETURN("TODO: Support `None` for property.");
    }

    Relation rel;
    if (!ParseRelation(&rel)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `rel` property.");
    }

    if (!SkipWhitespace()) {
    }

    if (!LookChar1(&c)) {
      return false;
    }

    // TODO: Parse Meta.
    if (c == '(') {
      PUSH_ERROR_AND_RETURN("TODO: Parse metadatum of property \"" + attr_name +
                            "\"");
    }

    DCOUT("Relationship with target: " << attr_name);
    Property p(rel, /* isConnection */ false, custom_qual);
    p.listOpQual = listop_qual;

    (*props)[attr_name] = p;

    return true;
  }

  //
  // Attrib.
  //

  if (listop_qual != ListEditQual::ResetToExplicit) {
    PUSH_ERROR_AND_RETURN_TAG(kAscii, "List editing qualifier is not allowed for Attribute.");
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

    DCOUT("Define only property = " + primattr_name);

    // Empty Attribute. type info only
    Property p(type_name, custom_qual);

    if (uniform_qual) {
      p.attrib.variability = Variability::Uniform;
    }

    (*props)[primattr_name] = p;

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

    Relation rel;
    rel.Set(path);

    Property p(rel, /* isConnection*/ true, custom_qual);
    p.attrib.set_type_name(type_name);

    (*props)[primattr_name] = p;

    DCOUT(fmt::format("Added {} as a connection.", primattr_name));

    return true;

  } else if (isTimeSample) {
    //
    // TODO(syoyo): Refactror and implement value parser dispatcher.
    //
    DCOUT("timeSample data. type = " << type_name);

#define PARSE_TYPE(__type)                                                  \
  if (type_name == value::TypeTrait<__type>::type_name()) {                 \
    if (array_qual) {                                                       \
      if (auto pv = TryParseTimeSamples<std::vector<__type>>()) {           \
        ts = ConvertToTimeSamples<std::vector<__type>>(pv.value());         \
      } else {                                                              \
        PUSH_ERROR_AND_RETURN("Failed to parse timeSample data with type `" \
                              << value::TypeTrait<__type>::type_name()      \
                              << "[]`");                                    \
      }                                                                     \
    } else {                                                                \
      if (auto pv = TryParseTimeSamples<__type>()) {                        \
        ts = ConvertToTimeSamples<__type>(pv.value());                      \
      } else {                                                              \
        PUSH_ERROR_AND_RETURN("Failed to parse timeSample data with type `" \
                              << value::TypeTrait<__type>::type_name()      \
                              << "`");                                      \
      }                                                                     \
    }                                                                       \
  } else

    value::TimeSamples ts;

    // NOTE: `string` does not support multi-line string.

    PARSE_TYPE(value::AssetPath)
    PARSE_TYPE(value::token)
    PARSE_TYPE(std::string)
    PARSE_TYPE(float)
    PARSE_TYPE(int)
    PARSE_TYPE(uint32_t)
    PARSE_TYPE(int64_t)
    PARSE_TYPE(uint64_t)
    PARSE_TYPE(value::half)
    PARSE_TYPE(value::half2)
    PARSE_TYPE(value::half3)
    PARSE_TYPE(value::half4)
    PARSE_TYPE(float)
    PARSE_TYPE(value::float2)
    PARSE_TYPE(value::float3)
    PARSE_TYPE(value::float4)
    PARSE_TYPE(double)
    PARSE_TYPE(value::double2)
    PARSE_TYPE(value::double3)
    PARSE_TYPE(value::double4)
    PARSE_TYPE(value::quath)
    PARSE_TYPE(value::quatf)
    PARSE_TYPE(value::quatd)
    PARSE_TYPE(value::color3f)
    PARSE_TYPE(value::color4f)
    PARSE_TYPE(value::color3d)
    PARSE_TYPE(value::color4d)
    PARSE_TYPE(value::vector3f)
    PARSE_TYPE(value::normal3f)
    PARSE_TYPE(value::point3f)
    PARSE_TYPE(value::texcoord2f)
    PARSE_TYPE(value::texcoord3f)
    PARSE_TYPE(value::matrix4d) {
      PUSH_ERROR_AND_RETURN(" : TODO: timeSamples type " + type_name);
    }

#undef PARSE_TYPE

    std::string varname = removeSuffix(primattr_name, ".timeSamples");
    PrimAttrib attr;
    primvar::PrimVar var;
    var.set_timesamples(ts);

    attr.name = varname;
    attr.set_var(std::move(var));

    DCOUT("timeSamples primattr: type = " << type_name
                                          << ", name = " << varname);

    Property p(attr, custom_qual);
    p.type = Property::Type::Attrib;
    (*props)[varname] = p;

    return true;

  } else {
    PrimAttrib attr;

    // TODO: Refactor.
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
      // TODO: Use StringData?
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

    } else {
      PUSH_ERROR_AND_RETURN("TODO: type = " + type_name);
    }

    if (uniform_qual) {
      attr.variability = Variability::Uniform;
    }
    attr.name = primattr_name;

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
  return ParsePrimAttr(props);
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

///
/// Parse `class` block.
///
bool AsciiParser::ParseClassBlock(const int64_t primIdx,
                                  const int64_t parentPrimIdx,
                                  const uint32_t depth) {
  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    std::string tok;
    if (!ReadIdentifier(&tok)) {
      return false;
    }

    if (tok != "class") {
      PushError("`class` is expected.");
      return false;
    }
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::string target;

  if (!ReadBasicType(&target)) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::map<std::string, std::tuple<ListEditQual, MetaVariable>> metas;
  if (!ParsePrimMetas(&metas)) {
    return false;
  }

  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  {
    std::string path = GetCurrentPath();
    if (path == "/") {
      path += target;
    } else {
      path += "/" + target;
    }
    PushPath(path);
  }

  // TODO: Support nested 'class'?

  // expect = '}'
  //        | def_block
  //        | prim_attr+
  std::map<std::string, Property> props;
  while (!Eof()) {
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

      Identifier tok;
      if (!ReadBasicType(&tok)) {
        return false;
      }

      if (!Rewind(tok.size())) {
        return false;
      }

      if (tok == "def") {
        // recusive call
        int64_t idx = _prim_idx_assign_fun(primIdx);
        DCOUT("Enter parseDef. primIdx = " << idx
                                           << ", parentPrimIdx = " << primIdx);
        if (!ParseDefBlock(idx, primIdx, depth + 1)) {
          return false;
        }
      } else {
        // Assume PrimAttr
        if (!ParsePrimAttr(&props)) {
          return false;
        }
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }
  }

  Klass klass;
  for (const auto &prop : props) {
    // TODO: list-edit qual
    klass.props[prop.first] = prop.second;
  }

  // TODO: Check key existance.
  _klasses[GetCurrentPath()] = klass;

  PopPath();

  return true;
}

///
/// Parse `over` block.
///
bool AsciiParser::ParseOverBlock(const int64_t primIdx,
                                 const int64_t parentPrimIdx,
                                 const uint32_t depth) {
  std::string tok;

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  if (!ReadIdentifier(&tok)) {
    return false;
  }

  if (tok != "over") {
    PushError("`over` is expected.");
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::string target;

  if (!ReadBasicType(&target)) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::map<std::string, std::tuple<ListEditQual, MetaVariable>> metas;
  if (!ParsePrimMetas(&metas)) {
    return false;
  }

  {
    std::string path = GetCurrentPath();
    if (path == "/") {
      path += target;
    } else {
      path += "/" + target;
    }
    PushPath(path);
  }

  if (!Expect('{')) {
    return false;
  }

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  // TODO: Parse block content

  if (!Expect('}')) {
    return false;
  }

  PopPath();

  return true;
}

///
/// Parse `def` block.
///
/// def = `def` prim_type? token metas? { ... }
///
/// metas = '(' args ')'
///
/// TODO: Support `def` without type(i.e. actual definition is defined in
/// another USD file or referenced USD)
///
bool AsciiParser::ParseDefBlock(const int64_t primIdx,
                                const int64_t parentPrimIdx,
                                const uint32_t depth) {
  DCOUT("ParseDefBlock");

  if (!SkipCommentAndWhitespaceAndNewline()) {
    DCOUT("SkipCommentAndWhitespaceAndNewline failed");
    return false;
  }

  Identifier def;
  if (!ReadIdentifier(&def)) {
    DCOUT("ReadIdentifier failed");
    return false;
  }
  DCOUT("def = " << def);

  if (def != "def") {
    PUSH_ERROR_AND_RETURN("`def` is expected.");
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

  if (!SkipWhitespaceAndNewline()) {
    return false;
  }

  std::map<std::string, std::tuple<ListEditQual, MetaVariable>> in_metas;
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

  std::vector<std::pair<ListEditQual, Reference>> references;
  DCOUT("`references.count` = " + std::to_string(in_metas.count("references")));

  if (in_metas.count("references")) {
    // TODO
    // references = GetReferences(args["references"]);
    // DCOUT("`references.size` = " + std::to_string(references.size()));
  }

#if 0  // TODO
  if (auto v = ReconstructPrimMetas(in_metas)) {
    DCOUT("TODO: ");
  } else {
    return false;
  }
#endif

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

      if (!Rewind(tok.size())) {
        return false;
      }

      if (tok == "def") {
        int64_t idx = _prim_idx_assign_fun(parentPrimIdx);
        DCOUT("enter parseDef. idx = " << idx << ", rootIdx = " << primIdx);

        // recusive call
        if (!ParseDefBlock(idx, primIdx, depth + 1)) {
          PUSH_ERROR_AND_RETURN("`def` block parse failed.");
        }
        DCOUT(fmt::format("Done parse `def` block."));
      } else {
        DCOUT("Enter ParsePrimAttr.");
        // Assume PrimAttr
        if (!ParsePrimAttr(&props)) {
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
    // TODO: support `references` and infer prim type from referenced asset.

    pTy = "Model";

#if 0  // TODO
    if (IsToplevel()) {
      if (references.size()) {
        // Infer prim type from referenced asset.

        if (references.size() > 1) {
          PUSH_ERROR_AND_RETURN("TODO: multiple references\n");
        }

        auto it = references.begin();
        const Reference &ref = it->second;
        std::string filepath = ref.asset_path;

        // usdOBJ?
        if (endsWith(filepath, ".obj")) {
          prim_type = "geom_mesh";
        } else {
          if (!io::IsAbsPath(filepath)) {
            filepath = io::JoinPath(_base_dir, ref.asset_path);
          }

          if (_reference_cache.count(filepath)) {
            LOG_ERROR("TODO: Use cached info");
          }

          DCOUT("Reading references: " + filepath);

          std::vector<uint8_t> data;
          std::string err;
          if (!io::ReadWholeFile(&data, &err, filepath,
                                 /* max_filesize */ 0)) {
            PUSH_ERROR_AND_RETURN("Failed to read file: " + filepath);
          }

          tinyusdz::StreamReader sr(data.data(), data.size(),
                                    /* swap endian */ false);
#if 0  // TODO
            tinyusdz::ascii::AsciiParser parser(&sr);

            std::string base_dir = io::GetBaseDir(filepath);

            parser.SetBaseDir(base_dir);

            {
              bool ret = parser.Parse(tinyusdz::ascii::LOAD_STATE_REFERENCE);

              if (!ret) {
                PUSH_WARN("Failed to parse .usda: " << parser.GetError());
              } else {
                DCOUT("`references` load ok.");
              }
            }
#else
          USDAReader reader(sr);
#endif

#if 0  // TODO
            std::string defaultPrim = parser.GetDefaultPrimName();

            DCOUT("defaultPrim: " + parser.GetDefaultPrimName());

            const std::vector<GPrim> &root_nodes = parser.GetGPrims();
            if (root_nodes.empty()) {
              LOG_WARN("USD file does not contain any Prim node.");
            } else {
              size_t default_idx =
                  0;  // Use the first element when corresponding defaultPrim
                      // node is not found.

              auto node_it = std::find_if(root_nodes.begin(), root_nodes.end(),
                                          [defaultPrim](const GPrim &a) {
                                            return !defaultPrim.empty() &&
                                                   (a.name == defaultPrim);
                                          });

              if (node_it != root_nodes.end()) {
                default_idx =
                    size_t(std::distance(root_nodes.begin(), node_it));
              }

              DCOUT("defaultPrim node: " + root_nodes[default_idx].name);
              for (size_t i = 0; i < root_nodes.size(); i++) {
                DCOUT("root nodes: " + root_nodes[i].name);
              }

              // Store result to cache
              _reference_cache[filepath] = {default_idx, root_nodes};

              prim_type = root_nodes[default_idx].prim_type;
              DCOUT("Infered prim type: " + prim_type);
            }
#else
          PUSH_WARN("TODO: References");
#endif
        }
      }
    } else {
      // Unknown or unresolved node type
      LOG_ERROR("TODO: unresolved node type\n");
    }
#endif
  }

  if (_prim_construct_fun_map.count(pTy)) {
    auto construct_fun = _prim_construct_fun_map[pTy];

    Path fullpath(GetCurrentPath(), "");
    Path pname(prim_name, "");
    nonstd::expected<bool, std::string> ret = construct_fun(
        fullpath, pname, primIdx, parentPrimIdx, props, references, in_metas);

    if (!ret) {
      // construction failed.
      PUSH_ERROR_AND_RETURN("Constructing Prim type `" + pTy +
                            "` failed: " + ret.error());
    }
  } else {
    PUSH_WARN(fmt::format("TODO: Unsupported/Unimplemented Prim type: `{}`. Skipping parsing.", pTy));
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

    if (tok == "def") {
      int64_t primIdx = _prim_idx_assign_fun(-1);
      DCOUT("Enter parseDef. primIdx = " << primIdx
                                         << ", parentPrimIdx = root(-1)");
      bool block_ok = ParseDefBlock(primIdx, /* parent */ -1);
      if (!block_ok) {
        PushError("Failed to parse `def` block.\n");
        return false;
      }
    } else if (tok == "over") {
      int64_t primIdx = _prim_idx_assign_fun(-1);
      DCOUT("Enter parseOver. primIdx = " << primIdx
                                          << ", parentPrimIdx = root(-1)");
      bool block_ok = ParseOverBlock(primIdx, /* parent */ -1);
      if (!block_ok) {
        PushError("Failed to parse `over` block.\n");
        return false;
      }
    } else if (tok == "class") {
      int64_t primIdx = _prim_idx_assign_fun(-1);
      DCOUT("Enter parseClass. primIdx = " << primIdx
                                           << ", parentPrimIdx = root(-1)");
      bool block_ok = ParseClassBlock(primIdx, /* parent */ -1);
      if (!block_ok) {
        PushError("Failed to parse `class` block.\n");
        return false;
      }
    } else {
      PushError("Unknown token '" + tok + "'");
      return false;
    }
  }

  return true;
}

}  // namespace ascii
}  // namespace tinyusdz

#else  // TINYUSDZ_DISABLE_MODULE_USDA_READER

#if 0
#endif

#if 0
// Extract array of References from Variable.
ReferenceList GetReferences(
    const std::tuple<ListEditQual, value::any_value> &_var) {
  ReferenceList result;

  ListEditQual qual = std::get<0>(_var);

  auto var = std::get<1>(_var);

  SDCOUT << "GetReferences. var.name = " << var.name << "\n";

  if (var.IsArray()) {
    DCOUT("IsArray");
    auto parr = var.as_array();
    if (parr) {
      DCOUT("parr");
      for (const auto &v : parr->values) {
        DCOUT("Maybe Value");
        if (v.IsValue()) {
          DCOUT("Maybe Reference");
          if (auto pref = nonstd::get_if<Reference>(v.as_value())) {
            DCOUT("Got it");
            result.push_back({qual, *pref});
          }
        }
      }
    }
  } else if (var.IsValue()) {
    DCOUT("IsValue");
    if (auto pv = var.as_value()) {
      DCOUT("Maybe Reference");
      if (auto pas = nonstd::get_if<Reference>(pv)) {
        DCOUT("Got it");
        result.push_back({qual, *pas});
      }
    }
  } else {
    DCOUT("Unknown var type: " + Variable::type_name(var));
  }

  return result;
}
#endif

#endif  // TINYUSDZ_DISABLE_MODULE_USDA_READER
