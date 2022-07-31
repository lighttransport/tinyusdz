// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
//
// TODO:
//   - [ ] List-up TODOs :-)

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
#include <thread>
#include <mutex>
#endif
#include <vector>

#include "ascii-parser.hh"
#include "str-util.hh"

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

#include "external/jsteemann/atoi.h"
#include "fast_float/fast_float.h"
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

// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                     \
  do {                                                               \
    std::ostringstream ss_e;                                         \
    ss_e << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_e << s;                                                       \
    PushError(ss_e.str());                                           \
    return false;                                                    \
  } while (0)

#if 1
#define PUSH_WARN(s)                                                 \
  do {                                                               \
    std::ostringstream ss_w;                                         \
    ss_w << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss_w << s;                                                       \
    PushWarn(ss_w.str());                                            \
  } while (0)
#endif

#if 0
#define SLOG_INFO                                                         \
  do {                                                                    \
    std::cout << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
  } while (0);                                                            \
  std::cout

#define LOG_DEBUG(s)                                                     \
  do {                                                                   \
    std::ostringstream ss;                                               \
    ss << "[debug] " << __FILE__ << ":" << __func__ << "():" << __LINE__ \
       << " ";                                                           \
    ss << s;                                                             \
    std::cout << ss.str() << "\n";                                       \
  } while (0)

#define LOG_INFO(s)                                                     \
  do {                                                                  \
    std::ostringstream ss;                                              \
    ss << "[info] " << __FILE__ << ":" << __func__ << "():" << __LINE__ \
       << " ";                                                          \
    ss << s;                                                            \
    std::cout << ss.str() << "\n";                                      \
  } while (0)

#define LOG_WARN(s)                                                     \
  do {                                                                  \
    std::ostringstream ss;                                              \
    ss << "[warn] " << __FILE__ << ":" << __func__ << "():" << __LINE__ \
       << " ";                                                          \
    std::cout << ss.str() << "\n";                                      \
  } while (0)

#define LOG_ERROR(s)                                                     \
  do {                                                                   \
    std::ostringstream ss;                                               \
    ss << "[error] " << __FILE__ << ":" << __func__ << "():" << __LINE__ \
       << " ";                                                           \
    std::cerr << s;                                                      \
  } while (0)

#define LOG_FATAL(s)                                               \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    std::cerr << s;                                                \
    std::exit(-1);                                                 \
  } while (0)
#endif

#if !defined(TINYUSDZ_PRODUCTION_BUILD)
#define TINYUSDZ_LOCAL_DEBUG_PRINT
#endif

#if defined(TINYUSDZ_LOCAL_DEBUG_PRINT)
#define DCOUT(x)                                               \
  do {                                                         \
    std::cout << __FILE__ << ":" << __func__ << ":"            \
              << std::to_string(__LINE__) << " " << x << "\n"; \
  } while (false)
#else
#define DCOUT(x)
#endif

namespace tinyusdz {

namespace ascii {

// T = better-enum class
template<typename T>
std::string enum_join(const std::string &sep)
{
  std::ostringstream ss;

  // quote with "
  ss << quote(T::_names()[0], "\"");

  for (size_t i = 1; i < T::_size(); i++) {
    ss << sep << quote(T::_names()[i], "\"");
  }

  return ss.str();
}

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

  // TODO: both support float and double?
  metas["metersPerUnit"] =
      AsciiParser::VariableDef(value::kDouble, "metersPerUnit");
  metas["timeCodesPerSecond"] =
      AsciiParser::VariableDef(value::kDouble, "timeCodesPerSecond");

  metas["defaultPrim"] =
      AsciiParser::VariableDef(value::kString, "defaultPrim");
  metas["upAxis"] = AsciiParser::VariableDef(value::kString, "upAxis");
  metas["customLayerData"] =
      AsciiParser::VariableDef(value::kDictionary, "customLayerData");

  // Composition arc.
  // Type can be array. i.e. asset, asset[]
  metas["subLayers"] = AsciiParser::VariableDef(value::kAssetPath, "subLayers");
}

// T = better-enums
template<class T>
class OneOf
{
 public:
  nonstd::expected<bool, std::string> operator()(const std::string &name) {
    // strip double quotation.
    std::string identifier = unwrap(name);

    if (auto p = T::_from_string_nothrow(identifier.c_str())) {
      return true;
    }

    std::string err_msg = "Must be one of " + enum_join<T>(", ") + " but got \"" + name + "\"";

    return nonstd::make_unexpected(err_msg);
  }
};

static void RegisterPrimMetas(
    std::map<std::string, AsciiParser::VariableDef> &metas) {
  metas.clear();

  metas["kind"] = AsciiParser::VariableDef(value::kString, "kind", OneOf<Kind>());

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
      value::Add1DArraySuffix(value::kString), "apiSchemas");
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

  d.insert(value::kNormal3f);
  d.insert(value::kPoint3f);
  d.insert(value::kTexCoord2f);
  d.insert(value::kVector3f);
  d.insert(value::kColor3f);

  // It looks no `matrixNf` type for USDA
  //d.insert(value::kMatrix2f);
  //d.insert(value::kMatrix3f);
  //d.insert(value::kMatrix4f);

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

static void RegisterPrimTypes(std::set<std::string> &d)
{
  d.insert("Xform");
  d.insert("Sphere");
  d.insert("Cube");
  d.insert("Cylinder");
  d.insert("BasisCurves");
  d.insert("Mesh");
  d.insert("Scope");
  d.insert("Material");
  d.insert("NodeGraph");
  d.insert("Shader");
  d.insert("SphereLight");
  d.insert("DomeLight");
  d.insert("Camera");
  d.insert("SkelRoot");
  d.insert("Skeleton");

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

    ss << "Near line " << diag.cursor.row << ", col " << diag.cursor.col
       << ": ";
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

    ss << "Near line " << diag.cursor.row << ", col " << diag.cursor.col
       << ": ";
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

#if 0 // No `matrixNf` in USDA?
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
bool AsciiParser::ReadBasicType(std::string *value) {
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
  if (!_sr->read1(&sc)) {
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
    if (!_sr->read1(&sc)) {
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

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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
    if (!_sr->read1(&sc)) {
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

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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
  DCOUT("ss = " << ss.str() << ", retcode = " << retcode << ", result = " << result);
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
bool AsciiParser::ReadBasicType(uint64_t *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  bool negative = false;
  {
    char sc;
    if (!_sr->read1(&sc)) {
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

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
    // sep
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    char c;
    if (!_sr->read1(&c)) {
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
    PUSH_ERROR_AND_RETURN("Invalid purpose value: " + str + "\n");
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
  // TODO: array_qual
  //
  MetaVariable var;
  if (type_name == value::kBool) {
    bool val;
    if (!ReadBasicType(&val)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `bool`");
    }
    var.value = val;
  } else if (type_name == "float") {
    float val;
    if (!ReadBasicType(&val)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `float`");
    }
    var.value = val;
  } else if (type_name == "string") {
    std::string str;
    if (!ReadStringLiteral(&str)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `string`");
    }
    var.value = str;
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
      var.value = toks;
    } else {
      std::string str;
      if (!ReadStringLiteral(&str)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `token`");
      }
      value::token tok(str);
      var.value = tok;
    }
  } else if (type_name == "dictionary") {
    std::map<std::string, MetaVariable> dict;

    if (!ParseDict(&dict)) {
      PUSH_ERROR_AND_RETURN("Failed to parse `dictionary`");
    }
  } else {
    PUSH_ERROR_AND_RETURN("TODO: type = " + type_name);
  }

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

  while (!_sr->eof()) {
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

      assert(var.valid());

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
    return false;
  }

  if (tok == "prepend") {
    (*qual) = tinyusdz::ListEditQual::Prepend;
  } else if (tok == "append") {
    (*qual) = tinyusdz::ListEditQual::Append;
  } else if (tok == "add") {
    (*qual) = tinyusdz::ListEditQual::Add;
  } else if (tok == "delete") {
    (*qual) = tinyusdz::ListEditQual::Delete;
  } else {
    // unqualified
    // rewind
    SeekTo(loc);
    (*qual) = tinyusdz::ListEditQual::ResetToExplicit;
  }

  return true;
}

bool AsciiParser::IsSupportedPrimType(const std::string &ty) {
  return _supported_prim_types.count(ty);
}


bool AsciiParser::IsSupportedPrimAttrType(const std::string &ty) {
  return _supported_prim_attr_types.count(ty);
}

bool AsciiParser::ReadStringLiteral(std::string *literal) {
  std::stringstream ss;

  char c0;
  if (!_sr->read1(&c0)) {
    return false;
  }

  if (c0 != '"') {
    DCOUT("c0 = " << c0);
    PUSH_ERROR_AND_RETURN(
        "String literal expected but it does not start with '\"'");
  }

  bool end_with_quotation{false};

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
      // this should not happen.
      return false;
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

bool AsciiParser::ReadPrimAttrIdentifier(std::string *token) {
  // Example: xformOp:transform

  std::stringstream ss;

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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
    if (endsWith(tok, ".connect")) {
      PushError(
          "Must ends with `.connect` when a name contains punctuation `.`");
      return false;
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
    if (!_sr->read1(&c)) {
      // this should not happen.
      DCOUT("read1 failed.");
      return false;
    }

    if (c == '_') {
      // ok
    } else if (!std::isalpha(int(c))) {
      DCOUT("Invalid identiefier.");
      _sr->seek_from_current(-1);
      return false;
    }
    _curr_cursor.col++;

    ss << c;
  }

  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '_') {
      // ok
    } else if (!std::isalnum(int(c))) {
      _sr->seek_from_current(-1);
      break; // end of identifier(e.g. ' ')
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

  // Must start with '/'
  if (!Expect('/')) {
    PushError("Path identifier must start with '/'");
    return false;
  }

  ss << '/';

  // read until '>'
  bool ok = false;
  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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
  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
      // this should not happen.
      return false;
    }

    if (c == '\n') {
      break;
    } else if (c == '\r') {
      // CRLF?
      if (_sr->tell() < (_sr->size() - 1)) {
        char d;
        if (!_sr->read1(&d)) {
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

  //
  // Process Stage meta variables
  //
  if (varname == "defaultPrim" ) {
    if (auto pv = var.value.get_value<std::string>()) {
      DCOUT("defaultPrim = " << pv.value());
      _stage_metas.defaultPrim = pv.value();
    } else {
      PUSH_ERROR_AND_RETURN("`defaultPrim` isn't a string value.");
    }
  } else if (varname == "subLayers") {
    if (auto pv = var.value.get_value<std::vector<std::string>>()) {
      DCOUT("subLayers = " << pv.value());
      for (const auto &item : pv.value()) {
          _stage_metas.subLayers.push_back(item);
      }
    } else {
      PUSH_ERROR_AND_RETURN("`subLayers` isn't an array of string values.");
    }
  } else if (varname == "upAxis") {
    if (auto pv = var.value.get_value<std::string>()) {
      DCOUT("upAxis = " << pv.value());
      const std::string s = pv.value();
      if (s == "X") {
        _stage_metas.upAxis = Axis::X;
      } else if (s == "Y") {
        _stage_metas.upAxis = Axis::Y;
      } else if (s == "Z") {
        _stage_metas.upAxis = Axis::Z;
      } else {
        PUSH_ERROR_AND_RETURN("Invalid `upAxis` value. Must be \"X\", \"Y\" or \"Z\", but got \"" + s + "\"(Note: Case sensitive)");
      }
    } else {
      PUSH_ERROR_AND_RETURN("`upAxis` isn't a string value.");
    }
  } else if (varname == "doc") {
    if (auto pv = var.value.get_value<std::string>()) {
      DCOUT("doc = " << pv.value());
      _stage_metas.doc = pv.value();
    } else {
      PUSH_ERROR_AND_RETURN("`doc` isn't a string value.");
    }
  } else if (varname == "metersPerUnit") {
    DCOUT("ty = " << var.value.type_name());
    if (auto pv = var.value.get_value<float>()) {
      DCOUT("metersPerUnit = " << pv.value());
      _stage_metas.metersPerUnit = double(pv.value());
    } else if (auto pvd = var.value.get_value<double>()) {
      DCOUT("metersPerUnit = " << pvd.value());
      _stage_metas.metersPerUnit = pvd.value();
    } else {
      PUSH_ERROR_AND_RETURN("`metersPerUnit` isn't a floating-point value.");
    }
  } else if (varname == "timeCodesPerSecond") {
    DCOUT("ty = " << var.value.type_name());
    if (auto pv = var.value.get_value<float>()) {
      DCOUT("metersPerUnit = " << pv.value());
      _stage_metas.timeCodesPerSecond = double(pv.value());
    } else if (auto pvd = var.value.get_value<double>()) {
      DCOUT("metersPerUnit = " << pvd.value());
      _stage_metas.timeCodesPerSecond = pvd.value();
    } else {
      PUSH_ERROR_AND_RETURN("`timeCodesPerSecond` isn't a floating-point value.");
    }
  } else {
    DCOUT("TODO: Stage meta: " << varname);
    PUSH_WARN("TODO: Stage meta: " << varname);
  }


#if 0 // Load subLayers in usda-reader
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

#if 0 // TODO
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

  while (!_sr->eof()) {
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
      DCOUT("aaa not");
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
  if (!_sr->read1(&c)) {
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
  if (!_sr->read1(c)) {
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

bool AsciiParser::SeekTo(size_t pos) {
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
  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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

bool AsciiParser::SkipWhitespaceAndNewline() {
  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
      // this should not happen.
      return false;
    }

    // printf("sws c = %c\n", c);

    if ((c == ' ') || (c == '\t') || (c == '\f')) {
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
        if (!_sr->read1(&d)) {
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
  while (!_sr->eof()) {
    char c;
    if (!_sr->read1(&c)) {
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
        if (!_sr->read1(&d)) {
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
  if (!_sr->read1(&c)) {
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

template<>
value::TimeSamples AsciiParser::ConvertToTimeSamples(
  const TimeSampleData<float> &ts) {

  value::TimeSamples dst;

  for (const auto &item : ts) {

    dst.times.push_back(std::get<0>(item));

    if (item.second) {
      dst.values.push_back(item.second.value());
    } else {
      // Blocked.
      dst.values.push_back(value::Block());
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

  if (_sr->eof()) {
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

// TODO: Return Path
bool AsciiParser::ParseReference(Reference *out, bool *triple_deliminated) {
  // @...@
  // or @@@...@@@ (Triple '@'-deliminated asset references)
  // And optionally followed by prim path.
  // Example:
  //   @bora@
  //   @@@bora@@@
  //   @bora@</dora>

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
    while (!_sr->eof()) {
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
      out->asset_path = tok;
      (*triple_deliminated) = false;

      valid = true;
    }

  } else {
    bool found_delimiter{false};
    int at_cnt{0};
    std::string tok;

    // Read until '@@@' appears
    while (!_sr->eof()) {
      char c;

      if (!Char1(&c)) {
        return false;
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
        // Got it. '@@@'
        found_delimiter = true;
        break;
      }
    }

    if (found_delimiter) {
      out->asset_path = tok;
      (*triple_deliminated) = true;

      valid = true;
    }
  }

  if (!valid) {
    return false;
  }

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

      out->prim_path = path;
    } else {
      if (!Rewind(1)) {
        return false;
      }
    }
  }

  return true;
}

bool AsciiParser::ParseMetaValue(const VariableDef &def,
                                 MetaVariable *outvar) {
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

    var.value = value;
  } else if (vartype == value::kString) {

    std::string value;
    DCOUT("parse meta = " << value);
    if (!ReadStringLiteral(&value)) {
      std::string msg = "String literal expected for `" + varname + "`.\n";
      PushError(msg);
      return false;
    }
    DCOUT("string = " << value);

    auto ret = def.post_parse_handler(value);
    if (!ret) {
      DCOUT("error = " << ret.error());
      PUSH_ERROR_AND_RETURN("Invalid string for `" + varname + "`. " + ret.error());
    }

    var.value = value;
  } else if (vartype == "string[]") {
    std::vector<std::string> values;
    if (!ParseBasicTypeArray(&values)) {
      return false;
    }

    var.value = values;
  } else if (vartype == "ref[]") {
    std::vector<Reference> values;
    if (!ParseBasicTypeArray(&values)) {
      PUSH_ERROR_AND_RETURN("Array of Reference expected for `" + varname +
                            "`.");
    }

    var.value = values;

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

    var.value = values;
  } else if (vartype == "float[]") {
    std::vector<float> values;
    if (!ParseBasicTypeArray<float>(&values)) {
      return false;
    }

    var.value = values;
  } else if (vartype == "float3[]") {
    std::vector<std::array<float, 3>> values;
    if (!ParseTupleArray<float, 3>(&values)) {
      return false;
    }

    var.value = values;
  } else if (vartype == "double[]") {
    std::vector<double> values;
    if (!ParseBasicTypeArray<double>(&values)) {
      return false;
    }

    var.value = values;
  } else if (vartype == "double3[]") {
    std::vector<value::double3> values;
    if (!ParseTupleArray(&values)) {
      return false;
    }

    var.value = values;
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

    var.value = ret.value();
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

    var.value = ret.value();

  } else if (vartype == "int3") {
    std::array<int, 3> values;
    if (!ParseBasicTypeTuple<int, 3>(&values)) {
      // std::string msg = "Array of int values expected for `" + var.name +
      // "`.\n"; PushError(msg);
      return false;
    }

    var.value = values;
  } else if (vartype == value::kDictionary) {
    DCOUT("dict type");
    if (!Expect('{')) {
      PushError("'{' expected.\n");
      return false;
    }

    while (!_sr->eof()) {
      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

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

        if (!ParseCustomMetaValue()) {
          PushError("Failed to parse meta definition.\n");
          return false;
        }
      }
    }

    PUSH_WARN("TODO: Implement object type(customData)");
  } else {
    PUSH_ERROR_AND_RETURN("TODO: vartype = " + vartype);
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
    if (!_sr->read1(&sc)) {
      return false;
    }
    _curr_cursor.col++;

    ss << sc;

    // sign, '.' or [0-9]
    if ((sc == '+') || (sc == '-')) {
      has_sign = true;

      char c;
      if (!_sr->read1(&c)) {
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

    while (!_sr->eof()) {
      if (!_sr->read1(&curr)) {
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

  if (_sr->eof()) {
    (*result) = ss.str();
    return true;
  }

  if (!_sr->read1(&curr)) {
    return false;
  }

  // std::cout << "before 2: ss = " << ss.str() << ", curr = " << curr <<
  // "\n";

  // 2. Read the decimal part
  if (curr == '.') {
    ss << curr;

    while (!_sr->eof()) {
      if (!_sr->read1(&curr)) {
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

  if (_sr->eof()) {
    (*result) = ss.str();
    return true;
  }

  // 3. Read the exponent part
  bool has_exp_sign{false};
  if ((curr == 'e') || (curr == 'E')) {
    ss << curr;

    if (!_sr->read1(&curr)) {
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

    while (!_sr->eof()) {
      if (!_sr->read1(&curr)) {
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
      PushError("Failed to parse path identifier");
      return false;
    }

    var.value = value;

  } else if (vardef.type == "path[]") {
    std::vector<PathIdentifier> value;
    if (!ParseBasicTypeArray(&value)) {
      PushError("Failed to parse array of path identifier");

      std::cout << __LINE__ << " ParsePathIdentifierArray failed\n";
      return false;
    }

    // std::vector<Path> paths;
    // MetaVariable::Array arr;
    // for (const auto &v : value) {
    //   std::cout << "  " << v << "\n";
    //   MetaVariable _var;
    //   _var.value = v;
    //   arr.values.push_back(_var);
    // }

    // var.value = arr;
    PUSH_ERROR_AND_RETURN("TODO: Implement");

  } else if (vardef.type == "ref[]") {
    std::vector<Reference> value;
    if (!ParseBasicTypeArray(&value)) {
      PushError("Failed to parse array of assert reference");

      return false;
    }

    var.value = value;

  } else if (vardef.type == "string") {
    std::string value;
    if (!ReadStringLiteral(&value)) {
      std::cout << __LINE__ << " ReadStringLiteral failed\n";
      return false;
    }

    std::cout << "vardef.type: " << vardef.type << ", name = " << varname
              << "\n";
    var.value = value;
  } else if (vardef.type == "string[]") {
    std::vector<std::string> value;
    if (!ParseBasicTypeArray(&value)) {
      PUSH_ERROR_AND_RETURN("ReadStringArray failed.");
      return false;
    }

    DCOUT("vardef.type: " << vardef.type << ", name = " << varname);
    var.value = value;

  } else if (vardef.type == value::kBool) {
    bool value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("ReadBool failed.");
    }

    DCOUT("vardef.type: " << vardef.type << ", name = " << varname);
    var.value = value;

  } else {
    PUSH_ERROR_AND_RETURN("TODO: varname " + varname + ", type " + vardef.type);
  }

  std::get<0>(*out) = qual;
  std::get<1>(*out) = var;

  return true;
}

#if 0
nonstd::optional<AsciiParser::PrimMetas> AsciiParser::ReconstructPrimMetas(
    std::map<std::string, std::tuple<ListEditQual, MetaVariable>> &args) {
  PrimMetas metas;

  {
    auto it = args.find(kKind);
    if (it != args.end()) {
      if (auto s = std::get<1>(it->second).get_value<std::string>()) {
      }

      if (auto v = Kind::_from_string_nothrow() {
        metas.kind = v.value();
      } else {
        return nonstd::nullopt;
      }
    }
  }


  return metas;

}
#endif

nonstd::optional<std::tuple<ListEditQual, MetaVariable>>
AsciiParser::ParsePrimMeta() {

  if (!SkipCommentAndWhitespaceAndNewline()) {
    return nonstd::nullopt;
  }

  tinyusdz::ListEditQual qual{ListEditQual::ResetToExplicit};
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
      DCOUT("def args start");
      // ok
    } else {
      _sr->seek_from_current(-1);
      //DCOUT("Unknown c");
      //return false;
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
      DCOUT("def args end");
      // End
      break;
    }

    Rewind(1);

    DCOUT("Start PrimMeta parse.");

    // ty = std::tuple<ListEditQual, MetaVariable>;
    if (auto m = ParsePrimMeta()) {
      DCOUT("arg: list-edit qual = " << tinyusdz::to_string(std::get<0>(m.value()))
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
    if (!_sr->read1(&c)) {
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

  while (!_sr->eof()) {
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

      std::string token;
      if (!ReadIdentifier(&token)) {
        return false;
      }

      if ((token != "interpolation") && (token != "customData") &&
          (token != "elementSize")) {
        PushError(
            "Currently only `interpolation`, `elementSize` or `customData` "
            "is supported but "
            "got: " +
            token);
        return false;
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

        out_meta->interpolation = InterpolationFromString(value);
      } else if (token == "elementSize") {
        uint32_t value;
        if (!ReadBasicType(&value)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `elementSize`");
        }

        out_meta->elementSize = value;
      } else if (token == "customData") {
        std::map<std::string, MetaVariable> dict;

        if (!ParseDict(&dict)) {
          return false;
        }

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
bool AsciiParser::ParseRel(Rel *result) {
  PathIdentifier value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  result->path = value;

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

  if (array_qual) {
    if (value::TypeTrait<T>::type_name() == "bool") {
      PushError("Array of bool type is not supported.");
      return false;
    } else {
      std::vector<T> value;
      if (!ParseBasicTypeArray(&value)) {
        PUSH_ERROR_AND_RETURN("Failed to parse " +
                  std::string(value::TypeTrait<T>::type_name()) + " array.");
        return false;
      }

      DCOUT("Got it: ty = " + std::string(value::TypeTrait<T>::type_name()) +
            ", sz = " + std::to_string(value.size()));
      attr.var.set_scalar(value);
    }

  } else if (hasConnect(primattr_name)) {
    std::string value;  // TODO: Path
    if (!ReadPathIdentifier(&value)) {
      PushError("Failed to parse path identifier for `token`.\n");
      return false;
    }

    attr.var.set_scalar(value);  // TODO: set as `Path` type
  } else {
    nonstd::optional<T> value;
    if (!ReadBasicType(&value)) {
      PUSH_ERROR_AND_RETURN("Failed to parse " + std::string(value::TypeTrait<T>::type_name()));
      return false;
    }

    if (value) {
      DCOUT("ParseBasicPrimAttr: " << value::TypeTrait<T>::type_name() << " = "
                                   << (*value));

      // TODO: TimeSampled
      value::TimeSamples ts;
      ts.values.push_back(*value);
      attr.var.var = ts;

    } else {
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

  // if (meta.count("interpolation")) {
  //   const MetaVariable &var = meta.at("interpolation");
  //   auto p = var.value.get_value<value::token>();
  //   if (p) {
  //     attr.interpolation =
  //     tinyusdz::InterpolationFromString(p.value().str());
  //   }
  // }

  (*out_attr) = std::move(attr);

  return true;
}

// bool ParsePrimAttr(std::map<std::string, MetaVariable> *props) {
bool AsciiParser::ParsePrimAttr(std::map<std::string, Property> *props) {
  // prim_attr : (custom?) uniform type (array_qual?) name '=' value
  //           | (custom?) type (array_qual?) name '=' value interpolation?
  //           | (custom?) uniform type (array_qual?) name interpolation?
  //           ;

  bool custom_qual = MaybeCustom();

  if (!SkipWhitespace()) {
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

  bool isTimeSample = endsWith(primattr_name, ".timeSamples");

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

  if (define_only) {
    // TODO:

    // attr.custom = custom_qual;
    // attr.uniform = uniform_qual;
    // attr.name = primattr_name;

    // DCOUT("primattr_name = " + primattr_name);

    //// FIXME
    //{
    //  (*props)[primattr_name].rel = rel;
    //  (*props)[primattr_name].is_rel = true;
    //}

    return true;
  }

  // Continue to parse argument
  if (!SkipWhitespace()) {
    return false;
  }

  //
  // TODO(syoyo): Refactror and implement value parser dispatcher.
  //
  if (isTimeSample) {
      if (type_name == "float") {
        if (auto pv = TryParseTimeSamples<float>()) {
          value::TimeSamples ts = ConvertToTimeSamples<float>(pv.value());

          PUSH_ERROR_AND_RETURN("TODO");
        }

        // TODO: Implement

        //Property prop;
        //prop.attrib.timeSampledValue = values;
        //std::cout << "timeSample float:" << primattr_name << " = " << to_string(values) << "\n";
        //(*props)[primattr_name] = var;

#if 0  // TODO
      } else if (type_name == "double") {
        TimeSampledDataDouble values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        MetaVariable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;

      } else if (type_name == "float3") {
        TimeSampledDataFloat3 values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        MetaVariable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;
      } else if (type_name == "double3") {
        TimeSampledDataDouble3 values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        MetaVariable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;
      } else if (type_name == "matrix4d") {
        TimeSampledDatavalue::matrix4d values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        MetaVariable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;

      } else {
        PushError(std::to_string(__LINE__) + " : TODO: timeSamples type " + type_name);
        return false;
      }
#else
      } else {
        PUSH_ERROR_AND_RETURN(" : TODO: timeSamples type " + type_name);
      }
#endif

    PUSH_ERROR_AND_RETURN("TODO: timeSamples type " + type_name);
    return false;

  } else {
    PrimAttrib attr;
    Rel rel;

    bool is_rel = false;

    if (type_name == value::kBool) {
      if (!ParseBasicPrimAttr<bool>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "float") {
      if (!ParseBasicPrimAttr<float>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "int") {
      if (!ParseBasicPrimAttr<int>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "double") {
      if (!ParseBasicPrimAttr<double>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "string") {
      if (!ParseBasicPrimAttr<std::string>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "token") {
      if (!ParseBasicPrimAttr<std::string>(array_qual, primattr_name, &attr)) {
        return false;
      }
    } else if (type_name == "float2") {
      if (!ParseBasicPrimAttr<value::float2>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == "float3") {
      if (!ParseBasicPrimAttr<value::float3>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == "float4") {
      if (!ParseBasicPrimAttr<value::float4>(array_qual, primattr_name,
                                             &attr)) {
        return false;
      }
    } else if (type_name == "double2") {
      if (!ParseBasicPrimAttr<value::double2>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "double3") {
      if (!ParseBasicPrimAttr<value::double3>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "double4") {
      if (!ParseBasicPrimAttr<value::double4>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "point3f") {
      DCOUT("point3f, array_qual = " + std::to_string(array_qual));
      if (!ParseBasicPrimAttr<value::point3f>(array_qual, primattr_name,
                                              &attr)) {
        DCOUT("Failed to parse point3f data.");
        return false;
      }
    } else if (type_name == "color3f") {
      if (!ParseBasicPrimAttr<value::color3f>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "color4f") {
      if (!ParseBasicPrimAttr<value::color4f>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "point3d") {
      if (!ParseBasicPrimAttr<value::point3d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "normal3f") {
      DCOUT("normal3f, array_qual = " + std::to_string(array_qual));
      if (!ParseBasicPrimAttr<value::normal3f>(array_qual, primattr_name,
                                               &attr)) {
        DCOUT("Failed to parse normal3f data.");
        return false;
      }
      DCOUT("Got it");
    } else if (type_name == "normal3d") {
      if (!ParseBasicPrimAttr<value::normal3d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == "color3d") {
      if (!ParseBasicPrimAttr<value::color3d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "color4d") {
      if (!ParseBasicPrimAttr<value::color4d>(array_qual, primattr_name,
                                              &attr)) {
        return false;
      }
    } else if (type_name == "matrix2d") {
      if (!ParseBasicPrimAttr<value::matrix2d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == "matrix3d") {
      if (!ParseBasicPrimAttr<value::matrix3d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }
    } else if (type_name == "matrix4d") {
      if (!ParseBasicPrimAttr<value::matrix4d>(array_qual, primattr_name,
                                               &attr)) {
        return false;
      }

    } else if (type_name == value::kRelationship) {
      if (!ParseRel(&rel)) {
        PushError("Failed to parse value with type `rel`.\n");
        return false;
      }

      is_rel = true;
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

      value::asset_path assetp(asset_ref.asset_path);
      attr.var.set_scalar(assetp);

    } else {
      PUSH_ERROR_AND_RETURN("TODO: type = " + type_name);
    }

    attr.uniform = uniform_qual;
    attr.name = primattr_name;

    DCOUT("primattr_name = " + primattr_name);

    (*props)[primattr_name].is_custom = custom_qual;

    if (is_rel) {
      (*props)[primattr_name].rel = rel;
      (*props)[primattr_name].is_rel = true;

    } else {
      (*props)[primattr_name].attrib = attr;
    }

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
    size_t loc = CurrLoc();
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

AsciiParser::AsciiParser() {
  Setup();
}

AsciiParser::AsciiParser(StreamReader *sr) : _sr(sr) {
  Setup();
}

void AsciiParser::Setup() {
  RegisterStageMetas(_supported_stage_metas);
  RegisterPrimMetas(_supported_prim_metas);
  RegisterPrimAttrTypes(_supported_prim_attr_types);
  RegisterPrimTypes(_supported_prim_types);
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
bool AsciiParser::ParseClassBlock() {
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
  while (!_sr->eof()) {
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
        if (!ParseDefBlock()) {
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
bool AsciiParser::ParseOverBlock() {
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
bool AsciiParser::ParseDefBlock(uint32_t nestlevel) {

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

#if 0
  if (auto v = ReconstructPrimMetas(in_metas)) {
    DCOUT("TODO: ");
  } else {
    return false;
  }
#endif

  std::map<std::string, Property> props;

  {
    std::string path = GetCurrentPath();
    if (path == "/") {
      path += prim_name;
    } else {
      path += "/" + prim_name;
    }
    PushPath(path);
  }

  // expect = '}'
  //        | def_block
  //        | prim_attr+
  while (!_sr->eof()) {
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
        if (!ParseDefBlock(nestlevel + 1)) {
          PUSH_ERROR_AND_RETURN("`def` block parse failed.");
        }
      } else {
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

  if (prim_type.empty()) {
    // No Prim type specified. Treat it as GPrim

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
  } else {

    if (_prim_construct_fun_map.count(prim_type)) {

      auto construct_fun = _prim_construct_fun_map[prim_type];

      Path path(GetCurrentPath());
      if (!construct_fun(path, props, references)) {
        // construction failed.
        PUSH_ERROR_AND_RETURN("Constructing " + prim_type + " failed.");
      }

    }
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
    _stage_meta_process_fun(_stage_metas);
  }

  PushPath("/");

  // parse blocks
  while (!_sr->eof()) {
    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    if (_sr->eof()) {
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
      bool block_ok = ParseDefBlock();
      if (!block_ok) {
        PushError("Failed to parse `def` block.\n");
        return false;
      }
    } else if (tok == "over") {
      bool block_ok = ParseOverBlock();
      if (!block_ok) {
        PushError("Failed to parse `over` block.\n");
        return false;
      }
    } else if (tok == "class") {
      bool block_ok = ParseClassBlock();
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
