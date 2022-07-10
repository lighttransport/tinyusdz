// SPDX-License-Identifier: MIT
// Copyright 2021 - Present, Syoyo Fujita.
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <stack>
#if defined(__wasi__)
#else
#include <thread>
#endif
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// external
//#include "ryu/ryu.h"
//#include "ryu/ryu_parse.h"

#include "fast_float/fast_float.h"
#include "nonstd/expected.hpp"
#include "nonstd/optional.hpp"

// Workaround: Compilation fails when using C++17 std::variant for Variable
// class. so use nonstd::variant on C++17
//#define variant_CONFIG_SELECT_VARIANT variant_VARIANT_NONSTD
//#include "nonstd/variant.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

// Tentative
#ifdef __clang__
#pragma clang diagnostic ignored "-Wunused-parameter"
#endif

#include "io-util.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "prim-types.hh"
#include "str-util.hh"
//#include "simple-type-reflection.hh"
#include "primvar.hh"
#include "stream-reader.hh"
#include "tinyusdz.hh"
#include "usdObj.hh"
#include "usda-parser.hh"
#include "value-pprint.hh"
#include "value-type.hh"

// s = std::string
#define PUSH_ERROR_AND_RETURN(s)                                   \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    PushError(ss.str());                                           \
    return false;                                                  \
  } while (0)

#define PUSH_WARN(s)                                               \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    ss << s;                                                       \
    PushWarn(ss.str());                                            \
  } while (0)

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

#if 0
#define LOG_FATAL(s)                                               \
  do {                                                             \
    std::ostringstream ss;                                         \
    ss << __FILE__ << ":" << __func__ << "():" << __LINE__ << " "; \
    std::cerr << s;                                                \
    std::exit(-1);                                                 \
  } while (0)
#endif

#if defined(TINYUSDZ_PRODUCTION_BUILD)
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

namespace usda {

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
      if ((-result) < std::numeric_limits<int>::min()) {
        return -3;
      }
    } else {
      if (result > std::numeric_limits<int>::max()) {
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

// TODO:
#if 0
std::string to_string(const TimeSampleType &tsv) {
  std::stringstream ss;

  if (const float *f = nonstd::get_if<float>(&tsv)) {
    ss << (*f);
  } else if (const double *d = nonstd::get_if<double>(&tsv)) {
    ss << (*d);
  } else if (auto p = nonstd::get_if<value::float3>(&tsv)) {
    ss << to_string(*p);
  } else {
    ss << "[[Unsupported/unimplemented TimeSampledValue type]]";
  }

  return ss.str();
}
std::string to_string(const TimeSampledDataFloat &values, uint32_t indent = 0)
{
  std::stringstream ss;

  ss << Indent(indent) << "{\n";

  for (size_t i = 0; i < values.size(); i++) {
    ss << Indent(indent+1) << std::get<0>(values[i]) << ":";
    ead(auto p = std::get<1>(values[i])) {
      ss << *p;
    } else {
      ss << "None";
    }
    ss << ",\n";
  }

  ss << Indent(indent) << "}\n";

  return ss.str();
}

std::string to_string(const TimeSampledDataFloat3 &values, uint32_t indent = 0)
{
  std::stringstream ss;

  ss << Indent(indent) << "{\n";

  for (size_t i = 0; i < values.size(); i++) {
    ss << Indent(indent+1) << std::get<0>(values[i]) << ":";
    if (auto p = std::get<1>(values[i])) {
      ss << to_string(*p);
    } else {
      ss << "None";
    }
    ss << ",\n";
  }

  ss << Indent(indent) << "}\n";

  return ss.str();
}
#endif

}  // namespace

struct ErrorDiagnositc {
  std::string err;
  int line_row = -1;
  int line_col = -1;
};

#if 0
struct Path {
  std::string path;
};

using PathList = std::vector<Path>;
#endif

class VariableDef {
 public:
  std::string type;
  std::string name;

  VariableDef() = default;

  VariableDef(const std::string &t, const std::string &n) : type(t), name(n) {}

  VariableDef(const VariableDef &rhs) = default;

  VariableDef &operator=(const VariableDef &rhs) {
    type = rhs.type;
    name = rhs.name;

    return *this;
  }
};

class Variable {
 public:
  std::string type;  // Explicit name of type
  std::string name;
  bool custom{false};

  // using Array = std::vector<Variable>;
  using Object = std::map<std::string, Variable>;

  value::Value value;
  // Array arr_value;
  Object obj_value;
  value::TimeSamples timeSamples;

  Variable &operator=(const Variable &rhs) {
    type = rhs.type;
    name = rhs.name;
    custom = rhs.custom;
    value = rhs.value;
    // arr_value = rhs.arr_value;
    obj_value = rhs.obj_value;

    return *this;
  }

  Variable(const Variable &rhs) {
    type = rhs.type;
    name = rhs.name;
    custom = rhs.custom;
    value = rhs.value;
    obj_value = rhs.obj_value;
  }

  static std::string type_name(const Variable &v) {
    if (!v.type.empty()) {
      return v.type;
    }

    // infer type from value content
    if (v.IsObject()) {
      return "dict";
    } else if (v.IsTimeSamples()) {
      std::string ts_type = "TODO: TimeSample typee";
      // FIXME
#if 0
      auto ts_struct = v.as_timesamples();

      for (const TimeSampleType &item : ts_struct->values) {
        auto tname = value::type_name(item);
        if (tname != "none") {
          return tname;
        }
      }
#endif

      // ??? TimeSamples data contains all `None` values
      return ts_type;

    } else if (v.IsEmpty()) {
      return "none";
    } else {
      return v.value.type_name();
    }
  }

#if 0
  // scalar type
  Value value;

  // TimeSampled values
  TimeSamples timeSamples;

  Array array;
  Object object;
#endif

  // template <typename T>
  // bool is() const {
  //   return value.index() == ValueType::index_of<T>();
  // }

  // TODO
  bool IsEmpty() const { return false; }
  bool IsValue() const { return false; }
  // bool IsArray() const {
  // }

  // bool IsArray() const {
  //  auto p = nonstd::get_if<mapbox::util::recursive_wrapper<Array>>(&value);
  //
  //  return p ? true: false;
  //}

  bool IsObject() const { return obj_value.size(); }

  // TODO
  bool IsTimeSamples() const { return false; }

  // const Array *as_array() const {
  //   const auto p =
  //       nonstd::get_if<mapbox::util::recursive_wrapper<Array>>(&value);
  //   return p->get_pointer();
  // }

  // const value::any_value *as_value() const {
  //   const auto p = nonstd::get_if<value::any_value>(&value);
  //   return p;
  // }

  // const Object *as_object() const {
  //   const auto p =
  //       nonstd::get_if<mapbox::util::recursive_wrapper<Object>>(&value);
  //   return p->get_pointer();
  // }

  // const TimeSamples *as_timesamples() const {
  //   const auto p = nonstd::get_if<TimeSamples>(&value);
  //   return p;
  // }

  // For Value
#if 0
  template <typename T>
  const nonstd::optional<T> cast() const {
    printf("cast\n");
    if (IsValue()) {
      std::cout << "type_name = " << Variable::type_name(*this) << "\n";
      const T *p = nonstd::get_if<T>(&value);
      printf("p = %p\n", static_cast<const void *>(p));
      if (p) {
        return *p;
      } else {
        return nonstd::nullopt;
      }
    }
    return nonstd::nullopt;
  }
#endif

  bool valid() const { return !IsEmpty(); }

  Variable() = default;
  // Variable(std::string ty, std::string n) : type(ty), name(n) {}
  // Variable(std::string ty) : type(ty) {}

  // friend std::ostream &operator<<(std::ostream &os, const Object &obj);
  friend std::ostream &operator<<(std::ostream &os, const Variable &var);

  // friend std::string str_object(const Object &obj, int indent = 0); // string
  // representation of Object.
};

namespace {

using ReferenceList = std::vector<std::pair<ListEditQual, Reference>>;

#if 0
// Extract array of References from Variable.
ReferenceList GetReferences(
    const std::tuple<ListEditQual, value::any_value> &_var) {
  ReferenceList result;

  ListEditQual qual = std::get<0>(_var);

  auto var = std::get<1>(_var);

  SLOG_INFO << "GetReferences. var.name = " << var.name << "\n";

  if (var.IsArray()) {
    LOG_INFO("IsArray");
    auto parr = var.as_array();
    if (parr) {
      LOG_INFO("parr");
      for (const auto &v : parr->values) {
        LOG_INFO("Maybe Value");
        if (v.IsValue()) {
          LOG_INFO("Maybe Reference");
          if (auto pref = nonstd::get_if<Reference>(v.as_value())) {
            LOG_INFO("Got it");
            result.push_back({qual, *pref});
          }
        }
      }
    }
  } else if (var.IsValue()) {
    LOG_INFO("IsValue");
    if (auto pv = var.as_value()) {
      LOG_INFO("Maybe Reference");
      if (auto pas = nonstd::get_if<Reference>(pv)) {
        LOG_INFO("Got it");
        result.push_back({qual, *pas});
      }
    }
  } else {
    LOG_INFO("Unknown var type: " + Variable::type_name(var));
  }

  return result;
}
#endif

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

#if 0  // TODO
std::string str_object(const Variable::Object &obj, int indent) {
  std::stringstream ss;

  auto Indent = [](int _indent) {
    std::stringstream _ss;

    for (int i = 0; i < _indent; i++) {
      _ss << "    ";
    }

    return _ss.str();
  };

  ss << "{\n";

  for (const auto &item : obj) {
    if (item.second.IsObject()) {
      ss << Indent(indent + 1) << "dict " << item.first << " = ";
      ss << "TODO: str_object()";
      //std::string str = str_object(
      //    nonstd::get<Variable::Object>(item.second.value), indent + 1);
      //ss << str;
    } else if (item.second.IsValue()) {
      // TODO
      // Value
      ss << item.second;
    }

    ss << "\n";
  }

  ss << Indent(indent) + "}";

  return ss.str();
}
#endif

}  // namespace

#if 1
std::ostream &operator<<(std::ostream &os, const Variable &var) {
  os << "TODO: Variable";

#if 0
  if (var.IsValue()) {
    // const Value &v = nonstd::get<Value>(var.value);

    nonstd::variant<float, int> v;

    struct PrintVisitor {
      std::string operator()(int v) const {
        std::ostringstream ss;
        ss << v;
        return ss.str();
      }
      std::string operator()(float v) const {
        std::ostringstream ss;
        ss << v;
        return ss.str();
      }
    };

    std::string str = nonstd::visit(PrintVisitor(), v);
    os << str;

#if 0
    if (v.is<bool>()) {
      os << nonstd::get<bool>(v);
    } else if (v.is<int>()) {
      os << nonstd::get<int>(v);
    } else if (v.is<float>()) {
      os << nonstd::get<float>(v);
    } else if (v.is<double>()) {
      os << nonstd::get<double>(v);
    } else if (v.is<std::string>()) {
      os << nonstd::get<std::string>(v);
    } else if (v.is<Rel>()) {
      os << nonstd::get<Rel>(v);
    } else {
      os << __LINE__ << " [TODO]";
    }
#endif
  } else if (var.IsArray()) {
    os << __LINE__ << " [TODO: Array type]";
  } else if (var.IsObject()) {
    os << str_object(nonstd::get<Variable::Object>(var.value), /* indent */ 0);
  } else if (var.IsEmpty()) {
    os << "[Variable is empty]";
  } else {
    os << "[???Variable???]";
  }
#endif

  return os;
}
#else
std::ostream &operator<<(std::ostream &os, const Variable &var) {
  // TODO
  return os;
}
#endif

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

#if 0  // use `fast_float`
// Tries to parse a floating point number located at s.
//
// s_end should be a location in the string where reading should absolutely
// stop. For example at the end of the string, to prevent buffer overflows.
//
// Parses the following EBNF grammar:
//   sign    = "+" | "-" ;
//   END     = ? anything not in digit ?
//   digit   = "0" | "1" | "2" | "3" | "4" | "5" | "6" | "7" | "8" | "9" ;
//   integer = [sign] , digit , {digit} ;
//   decimal = integer , ["." , integer] ;
//   float   = ( decimal , END ) | ( decimal , ("E" | "e") , integer , END ) ;
//
//  Valid strings are for example:
//   -0  +3.1417e+2  -0.0E-3  1.0324  -1.41   11e2
//
// If the parsing is a success, result is set to the parsed value and true
// is returned.
//
// The function is greedy and will parse until any of the following happens:
//  - a non-conforming character is encountered.
//  - s_end is reached.
//
// The following situations triggers a failure:
//  - s >= s_end.
//  - parse failure.
//
static bool tryParseDouble(const char *s, const char *s_end, double *result) {
  if (s >= s_end) {
    return false;
  }

  double mantissa = 0.0;
  // This exponent is base 2 rather than 10.
  // However the exponent we parse is supposed to be one of ten,
  // thus we must take care to convert the exponent/and or the
  // mantissa to a * 2^E, where a is the mantissa and E is the
  // exponent.
  // To get the final double we will use ldexp, it requires the
  // exponent to be in base 2.
  int exponent = 0;

  // NOTE: THESE MUST BE DECLARED HERE SINCE WE ARE NOT ALLOWED
  // TO JUMP OVER DEFINITIONS.
  char sign = '+';
  char exp_sign = '+';
  char const *curr = s;

  // How many characters were read in a loop.
  int read = 0;
  // Tells whether a loop terminated due to reaching s_end.
  bool end_not_reached = false;
  bool leading_decimal_dots = false;

  /*
          BEGIN PARSING.
  */

  // Find out what sign we've got.
  if (*curr == '+' || *curr == '-') {
    sign = *curr;
    curr++;
    if ((curr != s_end) && (*curr == '.')) {
      // accept. Somethig like `.7e+2`, `-.5234`
      leading_decimal_dots = true;
    }
  } else if (is_digit(*curr)) { /* Pass through. */
  } else if (*curr == '.') {
    // accept. Somethig like `.7e+2`, `-.5234`
    leading_decimal_dots = true;
  } else {
    goto fail;
  }

  // Read the integer part.
  end_not_reached = (curr != s_end);
  if (!leading_decimal_dots) {
    while (end_not_reached && is_digit(*curr)) {
      mantissa *= 10;
      mantissa += static_cast<int>(*curr - 0x30);
      curr++;
      read++;
      end_not_reached = (curr != s_end);
    }

    // We must make sure we actually got something.
    if (read == 0) goto fail;
  }

  // We allow numbers of form "#", "###" etc.
  if (!end_not_reached) goto assemble;

  // Read the decimal part.
  if (*curr == '.') {
    curr++;
    read = 1;
    end_not_reached = (curr != s_end);
    while (end_not_reached && is_digit(*curr)) {
      static const double pow_lut[] = {
          1.0, 0.1, 0.01, 0.001, 0.0001, 0.00001, 0.000001, 0.0000001,
      };
      const int lut_entries = sizeof pow_lut / sizeof pow_lut[0];

      // NOTE: Don't use powf here, it will absolutely murder precision.
      mantissa += static_cast<int>(*curr - 0x30) *
                  (read < lut_entries ? pow_lut[read] : std::pow(10.0, -read));
      read++;
      curr++;
      end_not_reached = (curr != s_end);
    }
  } else if (*curr == 'e' || *curr == 'E') {
  } else {
    goto assemble;
  }

  if (!end_not_reached) goto assemble;

  // Read the exponent part.
  if (*curr == 'e' || *curr == 'E') {
    curr++;
    // Figure out if a sign is present and if it is.
    end_not_reached = (curr != s_end);
    if (end_not_reached && (*curr == '+' || *curr == '-')) {
      exp_sign = *curr;
      curr++;
    } else if (is_digit(*curr)) { /* Pass through. */
    } else {
      // Empty E is not allowed.
      goto fail;
    }

    read = 0;
    end_not_reached = (curr != s_end);
    while (end_not_reached && is_digit(*curr)) {
      if (exponent > std::numeric_limits<int>::max() / 10) {
        // Integer overflow
        goto fail;
      }
      exponent *= 10;
      exponent += static_cast<int>(*curr - 0x30);
      curr++;
      read++;
      end_not_reached = (curr != s_end);
    }
    exponent *= (exp_sign == '+' ? 1 : -1);
    if (read == 0) goto fail;
  }

assemble:
  *result = (sign == '+' ? 1 : -1) *
            (exponent ? std::ldexp(mantissa * std::pow(5.0, exponent), exponent)
                      : mantissa);
  return true;
fail:
  return false;
}
#endif

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

//
// TODO: multi-threaded value array parser.
//
// Assumption.
//   - Assume input string is one-line(no newline) and startsWith/endsWith
//   brackets(e.g. `((1,2),(3, 4))`)
// Strategy.
//   - Divide input string to N items equally.
//   - Skip until valid character found(e.g. tuple character `(`)
//   - Parse array items with delimiter.
//   - Concatenate result.

#if 0
static uint32_t GetNumThreads(uint32_t max_threads = 128u)
{
  uint32_t nthreads = std::thread::hardware_concurrency();
  return std::min(std::max(1u, nthreads), max_threads);
}

typedef struct {
  size_t pos;
  size_t len;
} LineInfo;

inline bool is_line_ending(const char *p, size_t i, size_t end_i) {
  if (p[i] == '\0') return true;
  if (p[i] == '\n') return true;  // this includes \r\n
  if (p[i] == '\r') {
    if (((i + 1) < end_i) && (p[i + 1] != '\n')) {  // detect only \r case
      return true;
    }
  }
  return false;
}

static bool ParseTupleThreaded(
  const uint8_t *buffer_data,
  const size_t buffer_len,
  const char bracket_char,  // Usually `(` or `[`
  int32_t num_threads, uint32_t max_threads = 128u)
{
  uint32_t nthreads = (num_threads <= 0) ? GetNumThreads(max_threads) : std::min(std::max(1u, uint32_t(num_threads)), max_threads);

  std::vector<std::vector<LineInfo>> line_infos;
  line_infos.resize(nthreads);

  for (size_t t = 0; t < nthreads; t++) {
    // Pre allocate enough memory. len / 128 / num_threads is just a heuristic
    // value.
    line_infos[t].reserve(buffer_len / 128 / nthreads);
  }

  // TODO
  // Find bracket character.

  (void)buffer_data;
  (void)bracket_char;

  return false;
}
#endif

class USDAParser::Impl {
 private:
  Scene scene_;

 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  Impl(tinyusdz::StreamReader *sr) : _sr(sr) {
    RegisterBuiltinMeta();
    RegisterNodeTypes();
    RegisterNodeArgs();
    RegisterPrimAttrTypes();
  }

  //
  // -- ReadBasicType --
  //
  bool ReadBasicType(nonstd::optional<std::string> *value);
  bool ReadBasicType(nonstd::optional<int> *value);
  bool ReadBasicType(nonstd::optional<float> *value);
  bool ReadBasicType(nonstd::optional<value::float2> *value);
  bool ReadBasicType(nonstd::optional<value::float3> *value);
  bool ReadBasicType(nonstd::optional<value::float4> *value);
  bool ReadBasicType(nonstd::optional<double> *value);
  bool ReadBasicType(nonstd::optional<value::double2> *value);
  bool ReadBasicType(nonstd::optional<value::double3> *value);
  bool ReadBasicType(nonstd::optional<value::double4> *value);
  bool ReadBasicType(nonstd::optional<bool> *value);
  bool ReadBasicType(nonstd::optional<value::matrix4f> *value);
  bool ReadBasicType(nonstd::optional<value::matrix2d> *value);
  bool ReadBasicType(nonstd::optional<value::matrix3d> *value);
  bool ReadBasicType(nonstd::optional<value::matrix4d> *value);
  bool ReadBasicType(nonstd::optional<value::point3f> *value);
  bool ReadBasicType(nonstd::optional<value::point3d> *value);
  bool ReadBasicType(nonstd::optional<value::normal3f> *value);
  bool ReadBasicType(nonstd::optional<value::normal3d> *value);
  bool ReadBasicType(nonstd::optional<value::texcoord2f> *value);

  bool ReadBasicType(nonstd::optional<value::color3f> *value);
  bool ReadBasicType(nonstd::optional<value::color4f> *value);
  bool ReadBasicType(nonstd::optional<value::color3d> *value);
  bool ReadBasicType(nonstd::optional<value::color4d> *value);

  bool ReadBasicType(nonstd::optional<value::token> *value);

  bool ReadBasicType(std::string *value);
  bool ReadBasicType(int *value);
  bool ReadBasicType(float *value);
  bool ReadBasicType(value::float2 *value);
  bool ReadBasicType(value::float3 *value);
  bool ReadBasicType(value::float4 *value);
  bool ReadBasicType(double *value);
  bool ReadBasicType(value::double2 *value);
  bool ReadBasicType(value::double3 *value);
  bool ReadBasicType(value::double4 *value);
  bool ReadBasicType(bool *value);
  bool ReadBasicType(uint64_t *value);
  bool ReadBasicType(value::matrix4f *value);
  bool ReadBasicType(value::matrix2d *value);
  bool ReadBasicType(value::matrix3d *value);
  bool ReadBasicType(value::matrix4d *value);
  bool ReadBasicType(value::point3f *value);
  bool ReadBasicType(value::point3d *value);
  bool ReadBasicType(value::normal3f *value);
  bool ReadBasicType(value::normal3d *value);
  bool ReadBasicType(value::texcoord2f *value);

  bool ReadBasicType(value::color3f *value);
  bool ReadBasicType(value::color3d *value);
  bool ReadBasicType(value::color4f *value);
  bool ReadBasicType(value::color4d *value);

  bool ReadBasicType(value::token *value);

  // TimeSample data
  bool ReadTimeSampleData(nonstd::optional<value::float2> *value);
  bool ReadTimeSampleData(nonstd::optional<value::float3> *value);
  bool ReadTimeSampleData(nonstd::optional<value::float4> *value);
  bool ReadTimeSampleData(nonstd::optional<float> *value);
  bool ReadTimeSampleData(nonstd::optional<double> *value);
  bool ReadTimeSampleData(nonstd::optional<value::double2> *value);
  bool ReadTimeSampleData(nonstd::optional<value::double3> *value);
  bool ReadTimeSampleData(nonstd::optional<value::double4> *value);
  bool ReadTimeSampleData(nonstd::optional<value::matrix4f> *value);
  bool ReadTimeSampleData(nonstd::optional<value::matrix4d> *value);

  // Array version
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::float2>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::float3>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::float4>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<float>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<double>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::double2>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::double3>> *value);
  bool ReadTimeSampleData(nonstd::optional<std::vector<value::double4>> *value);
  bool ReadTimeSampleData(
      nonstd::optional<std::vector<value::matrix4f>> *value);
  bool ReadTimeSampleData(
      nonstd::optional<std::vector<value::matrix4d>> *value);

  bool MaybeNone();

  // Return the flag if the .usda is read from `references`
  bool IsReferenced() { return _referenced; }

  // Return the flag if the .usda is read from `subLayers`
  bool IsSubLayered() { return _sub_layered; }

  // Return the flag if the .usda is read from `payload`
  bool IsPayloaded() { return _payloaded; }

  // Return true if the .udsa is read in the top layer(stage)
  bool IsToplevel() {
    return !IsReferenced() && !IsSubLayered() && !IsPayloaded();
  }

  void SetBaseDir(const std::string &str) { _base_dir = str; }

  std::string GetCurrentPath() {
    if (_path_stack.empty()) {
      return "/";
    }

    return _path_stack.top();
  }

  bool PathStackDepth() { return _path_stack.size(); }

  void PushPath(const std::string &p) { _path_stack.push(p); }

  void PopPath() {
    if (!_path_stack.empty()) {
      _path_stack.pop();
    }
  }

  template <typename T>
  bool MaybeNonFinite(T *out) {
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

  bool LexFloat(std::string *result, std::string *err) {
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
      _line_col++;

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
          _line_col++;
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
        (*err) = "Sign or `.` or 0-9 expected.\n";
        return false;
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
        (*err) = "Empty E is not allowed. curr = " + ss.str() + "\n";
        return false;
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
            (*err) = "No multiple exponential sign characters.\n";
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

  bool ReadToken(std::string *result) {
    std::stringstream ss;

    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        return false;
      }

      if (std::isspace(int(c))) {
        _sr->seek_from_current(-1);
        break;
      }

      ss << c;
      _line_col++;
    }

    (*result) = ss.str();

    return true;
  }

  bool ParsePurpose(Purpose *result) {
    if (!result) {
      return false;
    }

    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    std::string token;
    if (!ReadToken(&token)) {
      return false;
    }

    if (token == "\"default\"") {
      (*result) = Purpose::Default;
    } else if (token == "\"render\"") {
      (*result) = Purpose::Render;
    } else if (token == "\"proxy\"") {
      (*result) = Purpose::Proxy;
    } else if (token == "\"guide\"") {
      (*result) = Purpose::Guide;
    } else {
      PUSH_ERROR_AND_RETURN("Invalid purpose token name: " + token + "\n");
    }

    return true;
  }

  bool ParseDefArg(std::tuple<ListEditQual, Variable> *out) {
    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    tinyusdz::ListEditQual qual{ListEditQual::ResetToExplicit};
    if (!MaybeListEditQual(&qual)) {
      return false;
    }

    std::cout << "list-edit qual: " << tinyusdz::to_string(qual) << "\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::string varname;
    if (!ReadToken(&varname)) {
      return false;
    }

    std::cout << "varname = `" << varname << "`\n";

    if (!IsNodeArg(varname)) {
      PUSH_ERROR_AND_RETURN("Unsupported or invalid/empty variable name `" +
                            varname);
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

    auto pvardef = GetNodeArg(varname);
    if (!pvardef) {
      // This should not happen though;
      return false;
    }

    auto vardef = (*pvardef);

    Variable var;
    var.name = varname;

    if (vardef.type == "path") {
      std::string value;
      if (!ReadPathIdentifier(&value)) {
        PushError("Failed to parse path identifier");
        return false;
      }

      var.value = value;

    } else if (vardef.type == "path[]") {
      std::vector<std::string> value;
      if (!ParsePathIdentifierArray(&value)) {
        PushError("Failed to parse array of path identifier");

        std::cout << __LINE__ << " ParsePathIdentifierArray failed\n";
        return false;
      }

      // std::vector<Path> paths;
      // Variable::Array arr;
      // for (const auto &v : value) {
      //   std::cout << "  " << v << "\n";
      //   Variable _var;
      //   _var.value = v;
      //   arr.values.push_back(_var);
      // }

      // var.value = arr;
      PUSH_ERROR_AND_RETURN("TODO: Implement");

    } else if (vardef.type == "ref[]") {
      std::vector<Reference> value;
      if (!ParseReferenceArray(&value)) {
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
      PUSH_ERROR_AND_RETURN("TODO: varname " + varname + ", type " +
                            vardef.type);
    }

    std::get<0>(*out) = qual;
    std::get<1>(*out) = var;

    return true;
  }

  bool ParseDefArgs(
      std::map<std::string, std::tuple<ListEditQual, Variable>> *args) {
    // '(' args ')'
    // args = list of argument, separated by newline.

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
        std::cout << "def args start\n";
        // ok
      } else {
        _sr->seek_from_current(-1);
        return false;
      }
    }

    if (!SkipCommentAndWhitespaceAndNewline()) {
      std::cout << "skip comment/whitespace/nl failed\n";
      return false;
    }

    while (!Eof()) {
      if (!SkipCommentAndWhitespaceAndNewline()) {
        std::cout << "2: skip comment/whitespace/nl failed\n";
        return false;
      }

      char s;
      if (!Char1(&s)) {
        return false;
      }

      if (s == ')') {
        std::cout << "def args end\n";
        // End
        break;
      }

      Rewind(1);

      std::tuple<ListEditQual, Variable> arg;
      if (!ParseDefArg(&arg)) {
        return false;
      }

      SLOG_INFO << "arg: list-edit qual = "
                << tinyusdz::to_string(std::get<0>(arg))
                << ", name = " << std::get<1>(arg).name;

      (*args)[std::get<1>(arg).name] = arg;
    }

    return true;
  }

  bool ParseDict(std::map<std::string, Variable> *out_dict) {
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
        Variable var;
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

  bool MaybeListEditQual(tinyusdz::ListEditQual *qual) {
    if (!SkipWhitespace()) {
      return false;
    }

    std::string tok;

    auto loc = CurrLoc();
    if (!ReadToken(&tok)) {
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

  bool ParseAttrMeta(std::map<std::string, Variable> *out_meta) {
    // '(' metas ')'
    //
    // currently we only support 'interpolation' and 'cutomData'

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
        if (!ReadToken(&token)) {
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

          Variable var;
          var.name = token;
          var.value = value;

          assert(var.valid());

          (*out_meta)["interpolation"] = var;
        } else if (token == "elementSize") {
          int value;
          if (!ReadBasicType(&value)) {
            PUSH_ERROR_AND_RETURN("Failed to parse `elementSize`");
          }

          Variable var;
          var.name = token;
          var.value = value;

          (*out_meta)["elementSize"] = var;
        } else if (token == "customData") {
          std::map<std::string, Variable> dict;

          if (!ParseDict(&dict)) {
            return false;
          }

          // Variable::Object d;
          // d.obj_value = dict;

          Variable var;
          var.name = token;
          var.obj_value = dict;

          assert(var.valid());

          (*out_meta)["customData"] = var;

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

  bool ParsePrimOptional() {
    // TODO: Implement

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
        return false;
      }
    }

    // Skip until ')' for now
    bool done = false;
    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        return false;
      }

      if (c == ')') {
        done = true;
        break;
      }
    }

    if (done) {
      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      return true;
    }

    return false;
  }

  bool ParseMetaAttr() {
    // meta_attr : uniform type (array_qual?) name '=' value
    //           | type (array_qual?) name '=' value
    //           ;

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
        PUSH_ERROR_AND_RETURN("`type` identifier expected but got non-identifier");
      }

      // `type_name` is then overwritten.
    }

    if (!IsRegisteredPrimAttrType(type_name)) {
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
      PushError("Failed to parse primAttr identifier.\n");
      return false;
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
    // TODO(syoyo): Refactror and implement value parser dispatcher.
    // Currently only `string` is provided
    //
    if (type_name == "string") {
      if (array_qual) {
        std::vector<std::string> value;
        if (!ParseBasicTypeArray(&value)) {
          PUSH_ERROR_AND_RETURN("Failed to parse string array.");
        }
      } else {
        std::string value;
        if (!ReadStringLiteral(&value)) {
          PUSH_ERROR_AND_RETURN("Failed to parse string literal.");
        }
      }

    } else {
      PUSH_ERROR_AND_RETURN("Unimplemented or unsupported type: " + type_name +
                            "\n");
    }

    (void)uniform_qual;

    return true;
  }

  template <typename T>
  bool ParseTimeSamples(
      std::vector<std::pair<uint64_t, nonstd::optional<T>>> *out_samples) {
    // timeSamples = '{' (int : T), + '}'

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
      }

      Rewind(1);

      uint64_t timeVal;
      if (!ReadBasicType(&timeVal)) {
        PushError("Parse time value failed.");
        return false;
      }

      if (!SkipWhitespace()) {
        return false;
      }

      if (!Expect(':')) {
        return false;
      }

      if (!SkipWhitespace()) {
        return false;
      }

      nonstd::optional<T> value;
      if (!ReadTimeSampleData(&value)) {
        return false;
      }

      // It looks the last item also requires ','
      if (!Expect(',')) {
        return false;
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      out_samples->push_back({timeVal, value});
    }

    return true;
  }

  bool ParseDictElement(std::string *out_key, Variable *out_var) {
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

    if (!IsRegisteredPrimAttrType(type_name)) {
      PUSH_ERROR_AND_RETURN("Unknown or unsupported type `" + type_name +
                            "`\n");
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
    Variable var;
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
      if (!ReadBasicType(&str)) {
        PUSH_ERROR_AND_RETURN("Failed to parse `string`");
      }
      var.value = str;
    } else if (type_name == "token") {
      if (array_qual) {
        std::vector<value::token> toks;
        if (!ParseBasicTypeArray(&toks)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `token[]`");
        }
        var.value = toks;
      } else {
        value::token tok;
        if (!ReadBasicType(&tok)) {
          PUSH_ERROR_AND_RETURN("Failed to parse `token`");
        }
        var.value = tok;
      }
    } else if (type_name == "dictionary") {
      std::map<std::string, Variable> dict;

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

  bool MaybeCustom() {
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

  template <typename T>
  bool ParseBasicPrimAttr(bool array_qual, const std::string &primattr_name,
                          PrimAttrib *out_attr) {
    PrimAttrib attr;

    if (array_qual) {
      if (value::TypeTrait<T>::type_name() == "bool") {
        PushError("Array of bool type is not supported.");
        return false;
      } else {
        std::vector<T> value;
        if (!ParseBasicTypeArray(&value)) {
          PushError("Failed to parse " +
                    std::string(value::TypeTrait<T>::type_name()) +
                    " array.\n");
          return false;
        }

        LOG_INFO(
            "Got it: ty = " + std::string(value::TypeTrait<T>::type_name()) +
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
        PushError("Failed to parse " +
                  std::string(value::TypeTrait<T>::type_name()) + " .\n");
        return false;
      }

      if (value) {
        LOG_DEBUG("ParseBasicPrimAttr: " << value::TypeTrait<T>::type_name()
                                         << " = " << (*value));

        // TODO: TimeSampled
        value::TimeSamples ts;
        ts.values.push_back(*value);
        attr.var.var = ts;

      } else {
        //std::cout << "ParseBasicPrimAttr: " << value::TypeTrait<T>::type_name()
        //          << " = None\n";
      }
    }

    // optional: interpolation parameter
    std::map<std::string, Variable> meta;
    if (!ParseAttrMeta(&meta)) {
      PushError("Failed to parse PrimAttrib meta.");
      return false;
    }

    if (meta.count("interpolation")) {
      const Variable &var = meta.at("interpolation");
      auto p = var.value.get_value<value::token>();
      if (p) {
        attr.interpolation = tinyusdz::InterpolationFromString(p.value().str());
      }
    }

    (*out_attr) = std::move(attr);

    return true;
  }

  // bool ParsePrimAttr(std::map<std::string, Variable> *props) {
  bool ParsePrimAttr(std::map<std::string, Property> *props) {
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

    if (!IsRegisteredPrimAttrType(type_name)) {
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
      PushError("Failed to parse primAttr identifier.\n");
      return false;
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

      // LOG_INFO("primattr_name = " + primattr_name);

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
#if 0  // TODO
      if (type_name == "float") {
        TimeSampledDataFloat values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        Variable var;
        var.timeSampledValue = values;
        std::cout << "timeSample float:" << primattr_name << " = " << to_string(values) << "\n";
        (*props)[primattr_name] = var;

      } else if (type_name == "double") {
        TimeSampledDataDouble values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        Variable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;

      } else if (type_name == "float3") {
        TimeSampledDataFloat3 values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        Variable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;
      } else if (type_name == "double3") {
        TimeSampledDataDouble3 values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        Variable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;
      } else if (type_name == "matrix4d") {
        TimeSampledDatavalue::matrix4d values;
        if (!ParseTimeSamples(&values)) {
          return false;
        }

        Variable var;
        var.timeSampledValue = values;
        (*props)[primattr_name] = var;

      } else {
        PushError(std::to_string(__LINE__) + " : TODO: timeSamples type " + type_name);
        return false;
      }
#endif

      PushError(std::to_string(__LINE__) + " : TODO: timeSamples type " +
                type_name);
      return false;

    } else {
      PrimAttrib attr;
      Rel rel;

      bool is_rel = false;

      if (type_name == "bool") {
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
        if (!ParseBasicPrimAttr<std::string>(array_qual, primattr_name,
                                             &attr)) {
          return false;
        }
      } else if (type_name == "token") {
        if (!ParseBasicPrimAttr<std::string>(array_qual, primattr_name,
                                             &attr)) {
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
        LOG_INFO("point3f, array_qual = " + std::to_string(array_qual));
        if (!ParseBasicPrimAttr<value::point3f>(array_qual, primattr_name,
                                                &attr)) {
          LOG_INFO("Failed to parse point3f data.");
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
        LOG_INFO("normal3f, array_qual = " + std::to_string(array_qual));
        if (!ParseBasicPrimAttr<value::normal3f>(array_qual, primattr_name,
                                                 &attr)) {
          LOG_INFO("Failed to parse normal3f data.");
          return false;
        }
        LOG_INFO("Got it");
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

      } else if (type_name == "rel") {
        if (!ParseRel(&rel)) {
          PushError("Failed to parse value with type `rel`.\n");
          return false;
        }

        is_rel = true;
      } else if (type_name == "texCoord2f") {
        if (!ParseBasicPrimAttr<value::texcoord2f>(array_qual, primattr_name,
                                                   &attr)) {
          PUSH_ERROR_AND_RETURN("Failed to parse texCoord2f data.");
        }

      } else if (type_name == "asset") {
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

      LOG_INFO("primattr_name = " + primattr_name);

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

  bool ParseProperty(std::map<std::string, Property> *props) {
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

  /// == DORA ==

  ///
  /// Parse rel string
  ///
  bool ParseRel(Rel *result) {
    std::string value;
    if (!ReadPathIdentifier(&value)) {
      return false;
    }

    result->path = value;

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    return true;
  }

  ///
  /// Parses 1 or more occurences of asset references, separated by
  /// `sep`
  /// TODO: Parse LayerOffset: e.g. `(offset = 10; scale = 2)`
  ///
  bool SepBy1Reference(const char sep, std::vector<Reference> *result) {
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

  ///
  /// Parses 1 or more occurences of value with basic type 'T', separated by
  /// `sep`
  ///
  template <typename T>
  bool SepBy1BasicType(const char sep,
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
  bool SepBy1BasicType(const char sep, std::vector<T> *result) {
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
  bool SepBy1TupleType(
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
  bool SepBy1TupleType(const char sep, std::vector<std::array<T, N>> *result) {
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
  bool ParseBasicTypeArray(std::vector<nonstd::optional<T>> *result) {
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
  bool ParseBasicTypeArray(std::vector<T> *result) {
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
  /// Parse array of asset references
  /// Allow non-list version
  ///
  bool ParseReferenceArray(std::vector<Reference> *result) {
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
      if (!SepBy1Reference(',', result)) {
        return false;
      }

      if (!Expect(']')) {
        return false;
      }
    }

    return true;
  }

  ///
  /// Parses 1 or more occurences of paths, separated by
  /// `sep`
  ///
  bool SepBy1PathIdentifier(const char sep, std::vector<std::string> *result) {
    result->clear();

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      std::string path;

      if (!ReadPathIdentifier(&path)) {
        PushError("Failed to parse Path.\n");
        return false;
      }

      result->push_back(path);
    }

    while (!_sr->eof()) {
      // sep
      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        DCOUT("read1 failure.");
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

      std::string path;
      if (!ReadPathIdentifier(&path)) {
        break;
      }

      result->push_back(path);
    }

    if (result->empty()) {
      PushError("Empty array.\n");
      return false;
    }

    return true;
  }

  ///
  /// Parse array of path
  /// Allow non-list version
  ///
  bool ParsePathIdentifierArray(std::vector<std::string> *result) {
    char c;
    if (!Char1(&c)) {
      return false;
    }

    if (c != '[') {
      // Guess non-list version
      std::string path;
      if (!ReadPathIdentifier(&path)) {
        return false;
      }

      result->clear();
      result->push_back(path);

    } else {
      if (!SepBy1PathIdentifier(',', result)) {
        return false;
      }

      if (!Expect(']')) {
        return false;
      }
    }

    return true;
  }

  ///
  /// Parse '(', Sep1By(','), ')'
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(std::array<T, N> *result) {
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

  ///
  /// Parse '(', Sep1By(','), ')'
  /// Can have `None`
  ///
  template <typename T, size_t N>
  bool ParseBasicTypeTuple(nonstd::optional<std::array<T, N>> *result) {
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
      std::string msg = "The number of tuple elements must be " +
                        std::to_string(N) + ", but got " +
                        std::to_string(values.size()) + "\n";
      PushError(msg);
      return false;
    }

    std::array<T, N> ret;
    for (size_t i = 0; i < N; i++) {
      ret[i] = values[i];
    }

    (*result) = ret;

    return true;
  }

  ///
  /// Parse matrix4f (e.g. ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0,
  /// 1))
  ///
  bool ParseMatrix4f(float result[4][4]) {
    // Assume column major(OpenGL style).

    if (!Expect('(')) {
      return false;
    }

    std::vector<std::array<float, 4>> content;
    if (!SepBy1TupleType<float, 4>(',', &content)) {
      return false;
    }

    if (content.size() != 4) {
      PushError("# of rows in matrix4f must be 4, but got " +
                std::to_string(content.size()) + "\n");
      return false;
    }

    if (!Expect(')')) {
      return false;
    }

    for (size_t i = 0; i < 4; i++) {
      result[i][0] = content[i][0];
      result[i][1] = content[i][1];
      result[i][2] = content[i][2];
      result[i][3] = content[i][3];
    }

    return true;
  }

  ///
  /// Parse matrix4d (e.g. ((1, 0, 0, 0), (0, 1, 0, 0), (0, 0, 1, 0), (0, 0, 0,
  /// 1))
  ///
  bool ParseMatrix4d(double result[4][4]) {
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
      result[i][0] = content[i][0];
      result[i][1] = content[i][1];
      result[i][2] = content[i][2];
      result[i][3] = content[i][3];
    }

    return true;
  }

  ///
  /// Parses 1 or more occurences of matrix4d, separated by
  /// `sep`
  ///
  bool SepBy1Matrix4d(const char sep, std::vector<value::matrix4d> *result) {
    result->clear();

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      value::matrix4d m;

      if (!ParseMatrix4d(m.m)) {
        PushError("Failed to parse Matrix4d.\n");
        return false;
      }

      result->push_back(m);
    }


    while (!_sr->eof()) {
      // sep
      if (!SkipWhitespaceAndNewline()) {
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        DCOUT("read1 failure.");
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

      value::matrix4d m;
      if (!ParseMatrix4d(m.m)) {
        break;
      }

      result->push_back(m);
    }

    if (result->empty()) {
      PushError("Empty array.\n");
      return false;
    }

    return true;
  }

  bool ParseMatrix3d(double result[3][3]) {
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
      result[i][0] = content[i][0];
      result[i][1] = content[i][1];
      result[i][2] = content[i][2];
    }

    return true;
  }

  bool SepBy1Matrix3d(const char sep, std::vector<value::matrix3d> *result) {
    result->clear();

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      value::matrix3d m;

      if (!ParseMatrix3d(m.m)) {
        PushError("Failed to parse Matrix3d.\n");
        return false;
      }

      result->push_back(m);
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

      value::matrix3d m;
      if (!ParseMatrix3d(m.m)) {
        break;
      }

      result->push_back(m);
    }

    if (result->empty()) {
      PushError("Empty array.\n");
      return false;
    }

    return true;
  }

  bool ParseMatrix2d(double result[2][2]) {
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
      result[i][0] = content[i][0];
      result[i][1] = content[i][1];
    }

    return true;
  }

  bool SepBy1Matrix2d(const char sep, std::vector<value::matrix2d> *result) {
    result->clear();

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      value::matrix2d m;

      if (!ParseMatrix2d(m.m)) {
        PushError("Failed to parse Matrix2d.\n");
        return false;
      }

      result->push_back(m);
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

      value::matrix2d m;
      if (!ParseMatrix2d(m.m)) {
        break;
      }

      result->push_back(m);
    }

    if (result->empty()) {
      PushError("Empty array.\n");
      return false;
    }

    return true;
  }

  bool SepBy1Matrix4f(const char sep, std::vector<value::matrix4f> *result) {
    result->clear();

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      value::matrix4f m;

      if (!ParseMatrix4f(m.m)) {
        PushError("Failed to parse Matrix4f.\n");
        return false;
      }

      result->push_back(m);
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

      value::matrix4f m;
      if (!ParseMatrix4f(m.m)) {
        break;
      }

      result->push_back(m);
    }

    if (result->empty()) {
      PushError("Empty array.\n");
      return false;
    }

    return true;
  }

  bool ParseMatrix4dArray(std::vector<value::matrix4d> *result) {
    if (!Expect('[')) {
      return false;
    }

    if (!SepBy1Matrix4d(',', result)) {
      return false;
    }

    if (!Expect(']')) {
      return false;
    }

    return true;
  }

  bool ParseMatrix4fArray(std::vector<value::matrix4f> *result) {
    if (!Expect('[')) {
      return false;
    }

    if (!SepBy1Matrix4f(',', result)) {
      return false;
    }

    if (!Expect(']')) {
      return false;
    }

    return true;
  }

  ///
  /// Parse the array of tuple. some may be None(e.g. `float3`: [(0, 1, 2),
  /// None, (2, 3, 4), ...] )
  ///
  template <typename T, size_t N>
  bool ParseTupleArray(
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
  bool ParseTupleArray(std::vector<std::array<T, N>> *result) {
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

  bool ReadStringLiteral(std::string *literal) {
    std::stringstream ss;

    char c0;
    if (!_sr->read1(&c0)) {
      return false;
    }

    if (c0 != '"') {
      ErrorDiagnositc diag;
      diag.err = "String literal expected but it does not start with '\"'\n";
      diag.line_col = _line_col;
      diag.line_row = _line_row;

      err_stack.push(diag);
      return false;
    }

    // ss << "\"";

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
      ErrorDiagnositc diag;
      diag.err = "String literal expected but it does not end with '\"'\n";
      diag.line_col = _line_col;
      diag.line_row = _line_row;

      err_stack.push(diag);
      return false;
    }

    (*literal) = ss.str();

    _line_col += int(literal->size() + 2);  // +2 for quotation chars

    return true;
  }

  bool ReadPrimAttrIdentifier(std::string *token) {
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

      _line_col++;

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
    //std::cout << "primAttr identifier = " << (*token) << "\n";
    return true;
  }

  bool ReadIdentifier(std::string *token) {
    // identifier = (`_` | [a-zA-Z]) (`_` | [a-zA-Z0-9]+)
    std::stringstream ss;


    // The first character.
    {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        return false;
      }

      if (c == '_') {
        // ok
      } else if (!std::isalpha(int(c))) {
        _sr->seek_from_current(-1);
        return false;
      }
      _line_col++;

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
        break;
      }

      _line_col++;

      ss << c;
    }

    (*token) = ss.str();
    return true;
  }

  bool ReadPathIdentifier(std::string *path_identifier) {
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
        _line_col++;
        break;
      }

      // TODO: Check if character is valid for path identifier
      ss << c;
    }

    if (!ok) {
      return false;
    }

    (*path_identifier) = TrimString(ss.str());
    //std::cout << "PathIdentifier: " << (*path_identifier) << "\n";

    return true;
  }

  bool SkipUntilNewline() {
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

    _line_row++;
    _line_col = 0;
    return true;
  }

  bool SkipWhitespace() {
    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        return false;
      }
      _line_col++;

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
    _line_col--;

    return true;
  }

  bool SkipWhitespaceAndNewline() {
    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        return false;
      }

      // printf("sws c = %c\n", c);

      if ((c == ' ') || (c == '\t') || (c == '\f')) {
        _line_col++;
        // continue
      } else if (c == '\n') {
        _line_col = 0;
        _line_row++;
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
        _line_col = 0;
        _line_row++;
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

  bool SkipCommentAndWhitespaceAndNewline() {
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
        _line_col++;
        // continue
      } else if (c == '\n') {
        _line_col = 0;
        _line_row++;
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
        _line_col = 0;
        _line_row++;
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

  bool Expect(char expect_c) {
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
      std::string msg = "Expected `" + std::string(&expect_c, 1) +
                        "` but got `" + std::string(&c, 1) + "`\n";
      PushError(msg);

      // unwind
      _sr->seek_from_current(-1);
    } else {
      _line_col++;
    }

    return ret;
  }

  // Parse magic
  // #usda FLOAT
  bool ParseMagicHeader() {
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
        ErrorDiagnositc diag;
        diag.line_row = _line_row;
        diag.line_col = _line_col;
        diag.err =
            "Magic header must start with `#usda `(at least single whitespace "
            "after 'a') but got `" +
            std::string(magic, 6) + "`\n";
        err_stack.push(diag);

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
        ErrorDiagnositc diag;
        diag.line_row = _line_row;
        diag.line_col = _line_col;
        diag.err =
            "Version must be `1.0` but got `" + std::string(ver, 3) + "`\n";
        err_stack.push(diag);

        return false;
      }
    }

    SkipUntilNewline();

    return true;
  }

  bool ParseCustomMetaValue() {
    // type identifier '=' value

    return ParseMetaAttr();
  }

  // TODO: Return Path
  bool ParseReference(Reference *out, bool *triple_deliminated) {
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
      //std::cout << "maybe single-'@' asset reference\n";

      SeekTo(curr);
      char s;
      if (!Char1(&s)) {
        return false;
      }

      if (s != '@') {
        std::string sstr{s};
        PUSH_ERROR_AND_RETURN("Reference must start with '@', but got '" + sstr + "'");
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

  bool ParseMetaValue(const std::string &vartype, const std::string &varname,
                      Variable *outvar) {
    Variable var;

    // TODO: Refactor.
    if (vartype == "string") {
      std::string value;
      if (!ReadStringLiteral(&value)) {
        std::string msg = "String literal expected for `" + varname + "`.\n";
        PushError(msg);
        return false;
      }
      var.value = value;
    } else if (vartype == "ref[]") {
      std::vector<Reference> values;
      if (!ParseReferenceArray(&values)) {
        std::string msg =
            "Array of Reference expected for `" + varname + "`.\n";
        PushError(msg);
        return false;
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


      // Variable::Array arr;
      // for (size_t i = 0; i < values.size(); i++) {
      //   Variable v;
      //   v.value = values[i];
      //   arr.values.push_back(v);
      // }

      var.value = values;
    } else if (vartype == "float3[]") {
      std::vector<std::array<float, 3>> values;
      if (!ParseTupleArray<float, 3>(&values)) {
        return false;
      }


      var.value = values;
    } else if (vartype == "float") {
      std::string fval;
      std::string ferr;
      if (!LexFloat(&fval, &ferr)) {
        std::string msg =
            "Floating point literal expected for `" + varname + "`.\n";
        if (!ferr.empty()) {
          msg += ferr;
        }
        PushError(msg);
        return false;
      }
      auto ret = ParseFloat(fval);
      if (!ret) {
        std::string msg =
            "Failed to parse floating point literal for `" + varname + "`.\n";
        if (!ferr.empty()) {
          msg += ret.error();
        }
        PushError(msg);
        return false;
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
    } else if (vartype == "object") {
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

  // metadata_opt := string_literal '\n'
  //              |  var '=' value '\n'
  //
  bool ParseWorldMetaOpt() {
    {
      uint64_t loc = _sr->tell();

      std::string note;
      if (!ReadStringLiteral(&note)) {
        // revert
        if (!SeekTo(loc)) {
          return false;
        }
      } else {
        // Got note.

        return true;
      }
    }

    std::string varname;
    if (!ReadIdentifier(&varname)) {
      return false;
    }

    if (!IsBuiltinMeta(varname)) {
      std::string msg =
          "'" + varname + "' is not a builtin Metadata variable.\n";
      PushError(msg);
      return false;
    }

    if (!Expect('=')) {
      PushError("'=' expected in Metadata line.\n");
      return false;
    }
    SkipWhitespace();

    VariableDef &vardef = _builtin_metas.at(varname);
    Variable var;
    if (!ParseMetaValue(vardef.type, vardef.name, &var)) {
      PushError("Failed to parse meta value.\n");
      return false;
    }

    //
    // Materialize builtin variables
    //
#if 0  // TODO
    if (varname == "defaultPrim") {
      if (auto pv = var.as_value()) {
        if (auto p = nonstd::get_if<std::string>(pv)) {
          _defaultPrim = *p;
        }
      }
    }
#endif

    std::vector<std::string> sublayers;
#if 0  // TODO
    if (varname == "subLayers") {
      if (var.IsArray()) {
        auto parr = var.as_array();

        if (parr) {
          auto arr = parr->values;
          for (size_t i = 0; i < arr.size(); i++) {
            if (arr[i].IsValue()) {
              auto pv = arr[i].as_value();
              if (auto p = nonstd::get_if<std::string>(pv)) {
                sublayers.push_back(*p);
              }
            }
          }
        }
      }
    }
#endif

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
        tinyusdz::usda::USDAParser parser(&sr);

        std::string base_dir = io::GetBaseDir(filepath);

        parser.SetBaseDir(base_dir);

        {
          bool ret = parser.Parse(tinyusdz::usda::LOAD_STATE_SUBLAYER);

          if (!ret) {
            PUSH_WARN("Failed to parse .usda: " << parser.GetError());
          }
        }
      }

      // TODO: Merge/Import subLayer.
    }

#if 0
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

  // Parse World meta
  // meta = ( metadata_opt )
  //      | empty
  //      ;
  bool ParseWorldMeta() {
    if (!Expect('(')) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    while (!_sr->eof()) {
      if (Expect(')')) {
        if (!SkipWhitespaceAndNewline()) {
          return false;
        }

        // end
        return true;

      } else {
        if (!SkipWhitespace()) {
          // eof
          return false;
        }

        if (!ParseWorldMetaOpt()) {
          // parse error
          return false;
        }
      }

      if (!SkipWhitespaceAndNewline()) {
        return false;
      }
    }

    return true;
  }

  // `#` style comment
  bool ParseSharpComment() {
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

  bool Eof() { return _sr->eof(); }

  // Fetch 1 char. Do not change input stream position.
  bool LookChar1(char *c) {
    if (!_sr->read1(c)) {
      return false;
    }

    Rewind(1);

    return true;
  }

  // Fetch N chars. Do not change input stream position.
  bool LookCharN(size_t n, std::vector<char> *nc) {
    std::vector<char> buf(n);

    auto loc = CurrLoc();

    bool ok = _sr->read(n, n, reinterpret_cast<uint8_t *>(buf.data()));
    if (ok) {
      (*nc) = buf;
    }

    SeekTo(loc);

    return ok;
  }

  bool Char1(char *c) { return _sr->read1(c); }

  bool CharN(size_t n, std::vector<char> *nc) {
    std::vector<char> buf(n);

    bool ok = _sr->read(n, n, reinterpret_cast<uint8_t *>(buf.data()));
    if (ok) {
      (*nc) = buf;
    }

    return ok;
  }

  bool Rewind(size_t offset) {
    if (!_sr->seek_from_current(-int64_t(offset))) {
      return false;
    }

    return true;
  }

  uint64_t CurrLoc() { return _sr->tell(); }

  bool SeekTo(size_t pos) {
    if (!_sr->seek_set(pos)) {
      return false;
    }

    return true;
  }

  bool Push() {
    // Stack size must be less than the number of input bytes.
    assert(parse_stack.size() < _sr->size());

    uint64_t loc = _sr->tell();

    ParseState state;
    state.loc = int64_t(loc);
    parse_stack.push(state);

    return true;
  }

  bool Pop(ParseState *state) {
    if (parse_stack.empty()) {
      return false;
    }

    (*state) = parse_stack.top();

    parse_stack.pop();

    return true;
  }

  ///
  /// Parse `class` block.
  ///
  bool ParseClassBlock() {
    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    {
      std::string tok;
      if (!ReadToken(&tok)) {
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

    if (!ReadToken(&target)) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::map<std::string, std::tuple<ListEditQual, Variable>> args;
    if (!ParseDefArgs(&args)) {
      return false;
    }

    if (!Expect('{')) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::string path = GetCurrentPath() + "/" + target;

    PushPath(path);

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

        std::string tok;
        if (!ReadToken(&tok)) {
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
    _klasses[path] = klass;

    PopPath();

    return true;
  }

  ///
  /// Parse `over` block.
  ///
  bool ParseOverBlock() {
    std::string tok;

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (!ReadToken(&tok)) {
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

    if (!ReadToken(&target)) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::map<std::string, std::tuple<ListEditQual, Variable>> args;
    if (!ParseDefArgs(&args)) {
      return false;
    }

    std::string path = GetCurrentPath() + "/" + target;
    PushPath(path);

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
  /// def = `def` prim_type? token optional_arg? { ... }
  ///
  /// optional_arg = '(' args ')'
  ///
  /// TODO: Support `def` without type(i.e. actual definition is defined in
  /// another USD file or referenced USD)
  ///
  bool ParseDefBlock(uint32_t nestlevel = 0) {
    std::string def;

    if (!SkipCommentAndWhitespaceAndNewline()) {
      return false;
    }

    if (!ReadToken(&def)) {
      return false;
    }

    if (def != "def") {
      PushError("`def` is expected.");
      return false;
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

    std::string prim_type;

    if (has_primtype) {
      if (!ReadToken(&prim_type)) {
        return false;
      }

      if (!_node_types.count(prim_type)) {
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

    std::string node_name;
    if (!ReadBasicType(&node_name)) {
      return false;
    }


    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    // optional args
    std::map<std::string, std::tuple<ListEditQual, Variable>> args;
    {
      // look ahead
      char c;
      if (!LookChar1(&c)) {
        return false;
      }

      if (c == '(') {
        // arg

        if (!ParseDefArgs(&args)) {
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
    LOG_INFO("`references.count` = " +
             std::to_string(args.count("references")));

    if (args.count("references")) {
      // TODO
      // references = GetReferences(args["references"]);
      // LOG_INFO("`references.size` = " + std::to_string(references.size()));
    }

    std::map<std::string, Property> props;

    std::string path = GetCurrentPath() + "/" + node_name;
    PushPath(path);

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

        std::string tok;
        if (!ReadToken(&tok)) {
          return false;
        }


        if (!Rewind(tok.size())) {
          return false;
        }

        if (tok == "def") {
          // recusive call
          if (!ParseDefBlock(nestlevel + 1)) {
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

    if (prim_type.empty()) {
      if (IsToplevel()) {
        if (references.size()) {
          // Infer prim type from referenced asset.

          if (references.size() > 1) {
            LOG_ERROR("TODO: multiple references\n");
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

            LOG_INFO("Reading references: " + filepath);

            std::vector<uint8_t> data;
            std::string err;
            if (!io::ReadWholeFile(&data, &err, filepath,
                                   /* max_filesize */ 0)) {
              PUSH_ERROR_AND_RETURN("Failed to read file: " + filepath);
            }

            tinyusdz::StreamReader sr(data.data(), data.size(),
                                      /* swap endian */ false);
            tinyusdz::usda::USDAParser parser(&sr);

            std::string base_dir = io::GetBaseDir(filepath);

            parser.SetBaseDir(base_dir);

            {
              bool ret = parser.Parse(tinyusdz::usda::LOAD_STATE_REFERENCE);

              if (!ret) {
                PUSH_WARN("Failed to parse .usda: " << parser.GetError());
              } else {
                DCOUT("`references` load ok.");
              }
            }

            std::string defaultPrim = parser.GetDefaultPrimName();

            LOG_INFO("defaultPrim: " + parser.GetDefaultPrimName());

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

              LOG_INFO("defaultPrim node: " + root_nodes[default_idx].name);
              for (size_t i = 0; i < root_nodes.size(); i++) {
                LOG_INFO("root nodes: " + root_nodes[i].name);
              }

              // Store result to cache
              _reference_cache[filepath] = {default_idx, root_nodes};

              prim_type = root_nodes[default_idx].prim_type;
              LOG_INFO("Infered prim type: " + prim_type);
            }
          }
        }
      } else {
        // Unknown or unresolved node type
        LOG_ERROR("TODO: unresolved node type\n");
      }
    }

    if (IsToplevel()) {
      if (prim_type.empty()) {
        // Reconstuct Generic Prim.

        GPrim gprim;
        if (!ReconstructGPrim(props, references, &gprim)) {
          PushError("Failed to reconstruct GPrim.");
          return false;
        }
        gprim.name = node_name;
        scene_.gprims.emplace_back(gprim);


      } else {

        // Reconstruct concrete C++ object

#define RECONSTRUCT_NODE(__tyname, __reconstruct_fn, __dty, __scene) \
        } else if (prim_type == __tyname) { \
          __dty node; \
          if (!__reconstruct_fn(props, references, &node)) { \
            PUSH_ERROR_AND_RETURN("Failed to reconstruct " << __tyname); \
          } \
          node.name = node_name; \
          __scene.emplace_back(node); 

        if (0) {
        RECONSTRUCT_NODE("Xform", ReconstructXform, Xform, scene_.xforms)
        RECONSTRUCT_NODE("Mesh", ReconstructGeomMesh, GeomMesh, scene_.geom_meshes)
        RECONSTRUCT_NODE("Sphere", ReconstructGeomSphere, GeomSphere, scene_.geom_spheres)
        RECONSTRUCT_NODE("Cone", ReconstructGeomCone, GeomCone, scene_.geom_cones)
        RECONSTRUCT_NODE("Cube", ReconstructGeomCube, GeomCube, scene_.geom_cubes)
        RECONSTRUCT_NODE("Capsule", ReconstructGeomCapsule, GeomCapsule, scene_.geom_capsules)
        RECONSTRUCT_NODE("Cylinder", ReconstructGeomCylinder, GeomCylinder, scene_.geom_cylinders)
        RECONSTRUCT_NODE("BasisCurves", ReconstructBasisCurves, GeomBasisCurves, scene_.geom_basis_curves)
        RECONSTRUCT_NODE("Camera", ReconstructGeomCamera, GeomCamera, scene_.geom_cameras)
        RECONSTRUCT_NODE("Shader", ReconstructShader, Shader, scene_.shaders)
        RECONSTRUCT_NODE("Material", ReconstructMaterial, Material, scene_.materials)

        RECONSTRUCT_NODE("Scope", ReconstructScope, Scope, scene_.scopes)

        RECONSTRUCT_NODE("SphereLight", ReconstructLuxSphereLight, LuxSphereLight, scene_.lux_sphere_lights)
        RECONSTRUCT_NODE("SphereLight", ReconstructLuxDomeLight, LuxDomeLight, scene_.lux_dome_lights)

        RECONSTRUCT_NODE("SkelRoot", ReconstructSkelRoot, SkelRoot, scene_.skel_roots)
        RECONSTRUCT_NODE("Skeleton", ReconstructSkeleton, Skeleton, scene_.skeletons)
        } else {
          PUSH_ERROR_AND_RETURN(" TODO: " + prim_type);
        }
      }
    } else {
      // Store properties to GPrim.
      // TODO: Use Class?
      GPrim gprim;
      if (!ReconstructGPrim(props, references, &gprim)) {
        PushError("Failed to reconstruct GPrim.");
        return false;
      }
      gprim.name = node_name;
      gprim.prim_type = prim_type;

      if (PathStackDepth() == 1) {
        // root node
        _gprims.push_back(gprim);
      }

    }

    PopPath();

    return true;
  }

  bool ReconstructGPrim(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GPrim *gprim);

  bool ReconstructGeomSphere(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomSphere *sphere);

  bool ReconstructGeomCone(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCone *cone);

  bool ReconstructGeomCube(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCube *cube);

  bool ReconstructGeomCapsule(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCapsule *capsule);

  bool ReconstructGeomCylinder(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCylinder *cylinder);

  bool ReconstructXform(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Xform *xform);

  bool ReconstructGeomMesh(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomMesh *mesh);

  bool ReconstructBasisCurves(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomBasisCurves *curves);

  bool ReconstructGeomCamera(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      GeomCamera *curves);

  bool ReconstructShader(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Shader *shader);

  bool ReconstructMaterial(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Material *material);

  bool ReconstructPreviewSurface(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      PreviewSurface *surface);

  bool ReconstructUVTexture(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      UVTexture *texture);

  bool ReconstructPrimvarReader_float2(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      PrimvarReader_float2 *reader_float2);

  bool ReconstructLuxSphereLight(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      LuxSphereLight *light);

  bool ReconstructLuxDomeLight(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      LuxDomeLight *light);

  bool ReconstructScope(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Scope *scope);

  bool ReconstructSkelRoot(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      SkelRoot *skelroot);

  bool ReconstructSkeleton(
      const std::map<std::string, Property> &properties,
      std::vector<std::pair<ListEditQual, Reference>> &references,
      Skeleton *skeleton);

  bool CheckHeader() { return ParseMagicHeader(); }

  void ImportScene(tinyusdz::Scene &scene) { _scene = scene; }

  bool HasPath(const std::string &path) {
    // TODO
    TokenizedPath tokPath(path);
    (void)tokPath;
    return false;
  }

  ///
  /// Parser entry point
  ///
  bool Parse(LoadState state = LOAD_STATE_TOPLEVEL) {
    _sub_layered = (state == LOAD_STATE_SUBLAYER);
    _referenced = (state == LOAD_STATE_REFERENCE);
    _payloaded = (state == LOAD_STATE_PAYLOAD);

    bool header_ok = ParseMagicHeader();
    if (!header_ok) {
      PushError("Failed to parse USDA magic header.\n");
      return false;
    }

    // global meta.
    bool has_meta = ParseWorldMeta();
    if (has_meta) {
      // TODO: Process meta info
    } else {
      // no meta info accepted.
    }

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

      std::string tok;
      if (!ReadToken(&tok)) {
        PushError("Token expected.\n");
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

  std::vector<GPrim> GetGPrims() { return _gprims; }

  std::string GetDefaultPrimName() const { return _defaultPrim; }

  std::string GetError() {
    if (err_stack.empty()) {
      return std::string();
    }

    std::stringstream ss;
    while (!err_stack.empty()) {
      ErrorDiagnositc diag = err_stack.top();

      ss << "Near line " << diag.line_row << ", col " << diag.line_col << ": ";
      ss << diag.err << "\n";

      err_stack.pop();
    }

    return ss.str();
  }

  std::string GetWarning() {
    if (warn_stack.empty()) {
      return std::string();
    }

    std::stringstream ss;
    while (!warn_stack.empty()) {
      ErrorDiagnositc diag = warn_stack.top();

      ss << "Near line " << diag.line_row << ", col " << diag.line_col << ": ";
      ss << diag.err << "\n";

      warn_stack.pop();
    }

    return ss.str();
  }

 private:
  bool IsRegisteredPrimAttrType(const std::string &ty) {
    return _registered_prim_attr_types.count(ty);
  }

  void RegisterPrimAttrTypes() {
    _registered_prim_attr_types.insert("int");

    _registered_prim_attr_types.insert("float");
    _registered_prim_attr_types.insert("float2");
    _registered_prim_attr_types.insert("float3");
    _registered_prim_attr_types.insert("float4");

    _registered_prim_attr_types.insert("double");
    _registered_prim_attr_types.insert("double2");
    _registered_prim_attr_types.insert("double3");
    _registered_prim_attr_types.insert("double4");

    _registered_prim_attr_types.insert("normal3f");
    _registered_prim_attr_types.insert("point3f");
    _registered_prim_attr_types.insert("texCoord2f");
    _registered_prim_attr_types.insert("vector3f");
    _registered_prim_attr_types.insert("color3f");

    _registered_prim_attr_types.insert("matrix4d");

    _registered_prim_attr_types.insert("token");
    _registered_prim_attr_types.insert("string");
    _registered_prim_attr_types.insert("bool");

    _registered_prim_attr_types.insert("rel");
    _registered_prim_attr_types.insert("asset");

    _registered_prim_attr_types.insert("dictionary");

    //_registered_prim_attr_types.insert("custom");

    // TODO: array type
  }

  void PushError(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.line_row = _line_row;
    diag.line_col = _line_col;
    diag.err = msg;
    err_stack.push(diag);
  }

  // This function is used to cancel recent parsing error.
  void PopError() {
    if (!err_stack.empty()) {
      err_stack.pop();
    }
  }

  void PushWarn(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.line_row = _line_row;
    diag.line_col = _line_col;
    diag.err = msg;
    warn_stack.push(diag);
  }

  // This function is used to cancel recent parsing warning.
  void PopWarn() {
    if (!warn_stack.empty()) {
      warn_stack.pop();
    }
  }

  bool IsBuiltinMeta(const std::string &name) {
    return _builtin_metas.count(name) ? true : false;
  }

  bool IsNodeArg(const std::string &name) {
    return _node_args.count(name) ? true : false;
  }

  void RegisterNodeArgs() {
    _node_args["kind"] = VariableDef("string", "kind");
    _node_args["references"] = VariableDef("ref[]", "references");
    _node_args["inherits"] = VariableDef("path", "inherits");
    _node_args["assetInfo"] = VariableDef("dictionary", "assetInfo");
    _node_args["customData"] = VariableDef("dictionary", "customData");
    _node_args["variants"] = VariableDef("dictionary", "variants");
    _node_args["variantSets"] = VariableDef("string", "variantSets");
    _node_args["payload"] = VariableDef("ref[]", "payload");
    _node_args["specializes"] = VariableDef("path[]", "specializes");
    _node_args["active"] = VariableDef(value::kBool, "active");

    // ListOp
    _node_args["apiSchemas"] = VariableDef("string[]", "apiSchemas");
  }

  nonstd::optional<VariableDef> GetNodeArg(const std::string &arg) {
    if (_node_args.count(arg)) {
      return _node_args.at(arg);
    }

    return nonstd::nullopt;
  }

  void RegisterBuiltinMeta() {
    _builtin_metas["doc"] = VariableDef("string", "doc");
    _builtin_metas["metersPerUnit"] = VariableDef("float", "metersPerUnit");
    _builtin_metas["defaultPrim"] = VariableDef("string", "defaultPrim");
    _builtin_metas["upAxis"] = VariableDef("string", "upAxis");
    _builtin_metas["timeCodesPerSecond"] =
        VariableDef("float", "timeCodesPerSecond");
    _builtin_metas["customLayerData"] =
        VariableDef("object", "customLayerData");
    _builtin_metas["subLayers"] = VariableDef("ref[]", "subLayers");
  }

  void RegisterNodeTypes() {
    _node_types.insert("Xform");
    _node_types.insert("Sphere");
    _node_types.insert("Cube");
    _node_types.insert("Cylinder");
    _node_types.insert("BasisCurves");
    _node_types.insert("Mesh");
    _node_types.insert("Scope");
    _node_types.insert("Material");
    _node_types.insert("Shader");
    _node_types.insert("SphereLight");
    _node_types.insert("DomeLight");
    _node_types.insert("Camera");
    _node_types.insert("SkelRoot");
    _node_types.insert("Skeleton");
  }

  ///
  /// -- Members --
  ///

  const tinyusdz::StreamReader *_sr = nullptr;

  std::map<std::string, VariableDef> _builtin_metas;
  std::set<std::string> _node_types;
  std::set<std::string> _registered_prim_attr_types;
  std::map<std::string, VariableDef> _node_args;

  std::stack<ErrorDiagnositc> err_stack;
  std::stack<ErrorDiagnositc> warn_stack;
  std::stack<ParseState> parse_stack;

  int _line_row{0};
  int _line_col{0};

  float _version{1.0f};

  std::string _base_dir;  // Used for importing another USD file

  nonstd::optional<tinyusdz::Scene> _scene;  // Imported scene.

  // "class" defs
  std::map<std::string, Klass> _klasses;

  std::stack<std::string> _path_stack;

  // Cache of loaded `references`
  // <filename, {defaultPrim index, list of root nodes in referenced usd file}>
  std::map<std::string, std::pair<uint32_t, std::vector<GPrim>>>
      _reference_cache;

  // toplevel "def" defs
  std::vector<GPrim> _gprims;

  // load flags
  bool _sub_layered{false};
  bool _referenced{false};
  bool _payloaded{false};

  std::string _defaultPrim;

};  // namespace usda

// == impl ==
bool USDAParser::Impl::ReconstructGPrim(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, GPrim *gprim) {
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  // Update props;
  for (auto item : properties) {
    if (item.second.is_rel) {
      PUSH_WARN("TODO: rel");
    } else {
      gprim->props[item.first].attrib = item.second.attrib;
    }
  }

  //
  // Resolve append references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructXform(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, Xform *xform) {
  (void)xform;

  // ret = (basename, suffix, isTimeSampled?)
  auto Split =
      [](const std::string &str) -> std::tuple<std::string, std::string, bool> {
    bool isTimeSampled{false};

    std::string s = str;

    const std::string tsSuffix = ".timeSamples";

    if (endsWith(s, tsSuffix)) {
      isTimeSampled = true;
      // rtrim
      s = s.substr(0, s.size() - tsSuffix.size());

    }

    // TODO: Support multiple namespace?
    std::string suffix;
    if (s.find_last_of(':') != std::string::npos) {
      suffix = s.substr(s.find_last_of(':') + 1);
    }

    std::string basename = s;
    if (s.find_last_of(':') != std::string::npos) {
      basename = s.substr(0, s.find_last_of(':'));
    }

    return std::make_tuple(basename, suffix, isTimeSampled);
  };

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend) {
    }
  }


  // Lookup xform values from `xformOpOrder`
  if (properties.count("xformOpOrder")) {

    // array of string
    auto prop = properties.at("xformOpOrder");
    if (prop.is_rel) {
      PUSH_WARN("TODO: Rel type for `xformOpOrder`");
    } else {
#if 0
      if (auto parr = value::as_vector<std::string>(&attrib->var)) {
        for (const auto &item : *parr) {
          // remove double-quotation
          std::string identifier = item;
          identifier.erase(
              std::remove(identifier.begin(), identifier.end(), '\"'),
              identifier.end());

          auto tup = Split(identifier);
          auto basename = std::get<0>(tup);
          auto suffix = std::get<1>(tup);
          auto isTimeSampled = std::get<2>(tup);
          (void)isTimeSampled;

          XformOp op;

          std::string target_name = basename;
          if (!suffix.empty()) {
            target_name += ":" + suffix;
          }

          if (!properties.count(target_name)) {
            PushError("Property '" + target_name +
                       "' not found in Xform node.");
            return false;
          }

          auto targetProp = properties.at(target_name);

          if (basename == "xformOp:rotateZ") {
            if (auto targetAttr = nonstd::get_if<PrimAttrib>(&targetProp)) {
              if (auto p = value::as_basic<float>(&targetAttr->var)) {
                std::cout << "xform got it "
                          << "\n";
                op.op = XformOp::OpType::ROTATE_Z;
                op.suffix = suffix;
                op.value = (*p);

                xform->xformOps.push_back(op);
              }
            }
          }
        }
      }
      PushError("`xformOpOrder` must be an array of string type.");
#endif
      (void)Split;
    }

  } else {
    //std::cout << "no xformOpOrder\n";
  }

#if 0
    for (const auto &prop : properties) {


      if (prop.first == "xformOpOrder") {
        if (!prop.second.IsArray()) {
          PushError("`xformOpOrder` must be an array type.");
          return false;
        }

        for (const auto &item : prop.second.array) {
          if (auto p = nonstd::get_if<std::string>(&item)) {
            // TODO
            //XformOp op;
            //op.op =
          }
        }

      } else if (std::get<0>(tup) == "xformOp:rotateZ") {

        if (prop.second.IsTimeSampled()) {

        } else if (prop.second.IsFloat()) {
          if (auto p = nonstd::get_if<float>(&prop.second.value)) {
            XformOp op;
            op.op = XformOp::OpType::ROTATE_Z;
            op.precision = XformOp::PrecisionType::PRECISION_FLOAT;
            op.value = *p;

            std::cout << "rotateZ value = " << *p << "\n";

          } else {
            PushError("`xformOp:rotateZ` must be an float type.");
            return false;
          }
        } else {
          PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                     "\n");
        }

      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
#endif

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructGeomSphere(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomSphere *sphere) {
  (void)sphere;

  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference radius = " << (*p) << "\n";
                sphere->radius = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      // if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
      //   sphere->materialBinding.materialBinding = prel->path;
      // } else {
      //   PushError("`material:binding` must be 'rel' type.");
      //   return false;
      // }
    } else {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        if (prop.first == "radius") {
          // const tinyusdz::PrimAttrib &attr = prop.second.attrib;
          // if (auto p = value::as_basic<double>(&attr->var)) {
          //   sphere->radius = *p;
          // } else {
          //   PushError("`radius` must be double type.");
          //   return false;
          // }
        } else {
          PUSH_ERROR_AND_RETURN("TODO: type: " + prop.first);
        }
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
          // if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
          //   if (prop.first == "radius") {
          //     if (auto p = value::as_basic<double>(&attr->var)) {
          //       SLOG_INFO << "append reference radius = " << (*p) << "\n";
          //       sphere->radius = *p;
          //     }
          //   }
          // }
        }
      }
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructGeomCone(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCone *cone) {
  (void)properties;
  (void)cone;
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          (void)prop;
#if 0
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
#endif
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cone->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cone->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference radius = " << (*p) << "\n";
                cone->radius = *p;
              }
            } else if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference height = " << (*p) << "\n";
                cone->height = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAParser::Impl::ReconstructGeomCube(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCube *cube) {
  (void)properties;
  (void)cube;
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cube->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "size") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cube->size = *p;
        } else {
          PushError("`size` must be double type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }
#endif

#if 0
  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "size") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference size = " << (*p) << "\n";
                cube->size = *p;
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAParser::Impl::ReconstructGeomCapsule(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCapsule *capsule) {
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (prop.second.is_rel) {
            PUSH_WARN("TODO: Rel");
          } else {
            // const PrimAttrib &attrib = prop.second.attrib;
#if 0
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SLOG_INFO << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  LOG_WARN("Invalid axis token: " + p->value);
                }
              }
            }
#endif
          }
        }
      }
    }
  }

#if 0
  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        capsule->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          capsule->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            capsule->axis = Axis::X;
          } else if (p->value == "y") {
            capsule->axis = Axis::Y;
          } else if (p->value == "z") {
            capsule->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference height = " << (*p) << "\n";
                capsule->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference radius = " << (*p) << "\n";
                capsule->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SLOG_INFO << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  capsule->axis = Axis::X;
                } else if (p->value == "y") {
                  capsule->axis = Axis::Y;
                } else if (p->value == "z") {
                  capsule->axis = Axis::Z;
                } else {
                  LOG_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAParser::Impl::ReconstructGeomCylinder(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCylinder *cylinder) {
#if 0
  //
  // Resolve prepend references
  //
  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "prepend reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SLOG_INFO << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  LOG_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.first == "material:binding") {
      if (auto prel = nonstd::get_if<Rel>(&prop.second)) {
        cylinder->materialBinding.materialBinding = prel->path;
      } else {
        PushError("`material:binding` must be 'rel' type.");
        return false;
      }
    } else if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
      if (prop.first == "height") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->height = *p;
        } else {
          PushError("`height` must be double type.");
          return false;
        }
      } else if (prop.first == "radius") {
        if (auto p = value::as_basic<double>(&attr->var)) {
          cylinder->radius = *p;
        } else {
          PushError("`radius` must be double type.");
          return false;
        }
      } else if (prop.first == "axis") {
        if (auto p = value::as_basic<Token>(&attr->var)) {
          if (p->value == "x") {
            cylinder->axis = Axis::X;
          } else if (p->value == "y") {
            cylinder->axis = Axis::Y;
          } else if (p->value == "z") {
            cylinder->axis = Axis::Z;
          }
        } else {
          PushError("`axis` must be token type.");
          return false;
        }
      } else {
        PushError(std::to_string(__LINE__) + " TODO: type: " + prop.first +
                   "\n");
        return false;
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      const Reference &asset_ref = std::get<1>(ref);

      std::string filepath = asset_ref.asset_path;
      if (!io::IsAbsPath(filepath)) {
        filepath = io::JoinPath(_base_dir, filepath);
      }

      if (_reference_cache.count(filepath)) {
        LOG_INFO("Got a cache: filepath = " + filepath);

        const auto root_nodes = _reference_cache.at(filepath);
        const GPrim &prim = std::get<1>(root_nodes)[std::get<0>(root_nodes)];

        for (const auto &prop : prim.props) {
          if (auto attr = nonstd::get_if<PrimAttrib>(&prop.second)) {
            if (prop.first == "height") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference height = " << (*p) << "\n";
                cylinder->height = *p;
              }
            } else if (prop.first == "radius") {
              if (auto p = value::as_basic<double>(&attr->var)) {
                SLOG_INFO << "append reference radius = " << (*p) << "\n";
                cylinder->radius = *p;
              }
            } else if (prop.first == "axis") {
              if (auto p = value::as_basic<Token>(&attr->var)) {
                SLOG_INFO << "prepend reference axis = " << p->value << "\n";
                if (p->value == "x") {
                  cylinder->axis = Axis::X;
                } else if (p->value == "y") {
                  cylinder->axis = Axis::Y;
                } else if (p->value == "z") {
                  cylinder->axis = Axis::Z;
                } else {
                  LOG_WARN("Invalid axis token: " + p->value);
                }
              }
            }
          }
        }
      }
    }
  }
#endif

  return true;
}

bool USDAParser::Impl::ReconstructGeomMesh(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomMesh *mesh) {
  //
  // Resolve prepend references
  //

  for (const auto &ref : references) {

    LOG_INFO("asset_path = '" + std::get<1>(ref).asset_path + "'\n");

    if ((std::get<0>(ref) == tinyusdz::ListEditQual::ResetToExplicit) ||
        (std::get<0>(ref) == tinyusdz::ListEditQual::Prepend)) {
      const Reference &asset_ref = std::get<1>(ref);

      if (endsWith(asset_ref.asset_path, ".obj")) {
        std::string err;
        GPrim gprim;

        // abs path.
        std::string filepath = asset_ref.asset_path;

        if (io::IsAbsPath(asset_ref.asset_path)) {
          // do nothing
        } else {
          if (!_base_dir.empty()) {
            filepath = io::JoinPath(_base_dir, filepath);
          }
        }

        LOG_INFO("Reading .obj file: " + filepath);

        if (!usdObj::ReadObjFromFile(filepath, &gprim, &err)) {
          PUSH_ERROR_AND_RETURN("Failed to read .obj(usdObj). err = " + err);
        }
        LOG_INFO("Loaded .obj file: " + filepath);

        mesh->visibility = gprim.visibility;
        mesh->doubleSided = gprim.doubleSided;
        mesh->orientation = gprim.orientation;

        if (gprim.props.count("points")) {
          LOG_INFO("points");
          const Property &prop = gprim.props.at("points");
          if (prop.is_rel) {
            PUSH_WARN("TODO: points Rel\n");
          } else {
            const PrimAttrib &attr = prop.attrib;
            // PrimVar
            LOG_INFO("points.type:" + attr.var.type_name());
            if (attr.var.is_scalar()) {
              auto p = attr.var.get_value<std::vector<value::point3f>>();
              if (p) {
                mesh->points = p.value();
              } else {
                PUSH_ERROR_AND_RETURN("TODO: points.type = " +
                                      attr.var.type_name());
              }
              // if (auto p = value::as_vector<value::float3>(&pattr->var)) {
              //   LOG_INFO("points. sz = " + std::to_string(p->size()));
              //   mesh->points = (*p);
              // }
            } else {
              PUSH_ERROR_AND_RETURN("TODO: timesample points.");
            }
          }
        }

      } else {
        LOG_INFO("Not a .obj file");
      }
    }
  }

  for (const auto &prop : properties) {
    if (prop.second.is_rel) {
      if (prop.first == "material:binding") {
        mesh->materialBinding.materialBinding = prop.second.rel.path;
      } else {
        PUSH_WARN("TODO: rel");
      }
    } else {
      const PrimAttrib &attr = prop.second.attrib;
      if (prop.first == "points") {
        auto p = attr.var.get_value<std::vector<value::point3f>>();
        if (p) {
          mesh->points = (*p);
        } else {
          PUSH_ERROR_AND_RETURN(
              "`GeomMesh::points` must be point3[] type, but got " +
              attr.var.type_name());
        }
      } else if (prop.first == "subdivisionScheme") {
        auto p = attr.var.get_value<std::string>();
        if (!p) {
          PUSH_ERROR_AND_RETURN(
              "Invalid type for \'subdivisionScheme\'. expected \'STRING\' but "
              "got " +
              attr.var.type_name());
        } else {
          LOG_INFO("subdivisionScheme = " + (*p));
          if (p->compare("none") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::None;
          } else if (p->compare("catmullClark") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::CatmullClark;
          } else if (p->compare("bilinear") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Bilinear;
          } else if (p->compare("loop") == 0) {
            mesh->subdivisionScheme = SubdivisionScheme::Loop;
          } else {
            PUSH_ERROR_AND_RETURN("Unknown subdivision scheme: " + (*p));
          }
        }
      } else {
        LOG_WARN(" TODO: prop: " + prop.first);
      }
    }
  }

  //
  // Resolve append references
  // (Overwrite variables with the referenced one).
  //
  for (const auto &ref : references) {
    if (std::get<0>(ref) == tinyusdz::ListEditQual::Append) {
      // TODO
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructBasisCurves(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomBasisCurves *curves) {
  for (const auto &prop : properties) {
    if (prop.first == "points") {
      if (prop.second.is_rel) {
        PUSH_WARN("TODO: Rel");
      } else {
        // const PrimAttrib &attrib = prop.second.attrib;
#if 0  // TODO
        attrib.
        attrib.IsFloat3() && !prop.second.IsArray()) {
        PushError("`points` must be float3 array type.");
        return false;
      }

      const std::vector<float3> p =
          nonstd::get<std::vector<float3>>(prop.second.value);

      curves->points.resize(p.size() * 3);
      memcpy(curves->points.data(), p.data(), p.size() * 3);
#endif
      }

    } else if (prop.first == "curveVertexCounts") {
#if 0  // TODO
      if (!prop.second.IsInt() && !prop.second.IsArray()) {
        PushError("`curveVertexCounts` must be int array type.");
        return false;
      }

      const std::vector<int32_t> p =
          nonstd::get<std::vector<int32_t>>(prop.second.value);

      curves->curveVertexCounts.resize(p.size());
      memcpy(curves->curveVertexCounts.data(), p.data(), p.size());
#endif

    } else {
      PUSH_WARN("TODO: " << prop.first);
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructGeomCamera(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    GeomCamera *camera) {
  for (const auto &prop : properties) {
    if (prop.first == "focalLength") {
      // TODO
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructLuxSphereLight(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxSphereLight *light) {
  // TODO: Implement
  for (const auto &prop : properties) {
    if (prop.first == "radius") {
      // TODO
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

#define PARSE_PROPERTY_BEGIN if (0) {
// TODO(syoyo): TimeSamples, Reference
#define PARSE_PROPERTY(__prop, __name, __ty, __target)             \
  }                                                                \
  else if (__prop.first == __name) {                               \
    const PrimAttrib &attr = __prop.second.attrib;                 \
    if (auto v = attr.var.get_value<__ty>()) {                     \
      __target = v.value();                                        \
    } else {                                                       \
      PUSH_ERROR_AND_RETURN("Type mismatch. "                      \
                            << __name << " expects "               \
                            << value::TypeTrait<__ty>::type_name); \
    }

//#define PARSE_PROPERTY_END }

bool USDAParser::Impl::ReconstructLuxDomeLight(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    LuxDomeLight *light) {
  // TODO: Implement
  for (const auto &prop : properties) {
    PARSE_PROPERTY_BEGIN
    PARSE_PROPERTY(prop, "guideRadius", float, light->guideRadius)
    PARSE_PROPERTY(prop, "inputs:color", value::color3f, light->color)
    PARSE_PROPERTY(prop, "inputs:intensity", float, light->intensity)
  }
  else {
    DCOUT("TODO: " << prop.first);
  }
}

return true;
}  // namespace usda

bool USDAParser::Impl::ReconstructScope(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references, Scope *scope) {
  (void)scope;

  // TODO: Implement
  DCOUT("Implement Scope");

  return true;
}

bool USDAParser::Impl::ReconstructSkelRoot(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    SkelRoot *root) {
  (void)root;

  // TODO: Implement
  DCOUT("Implement SkelRoot");

  return true;
}

bool USDAParser::Impl::ReconstructSkeleton(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Skeleton *skel) {
  (void)skel;

  // TODO: Implement
  DCOUT("Implement Skeleton");

  return true;
}

bool USDAParser::Impl::ReconstructShader(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Shader *shader) {
  for (const auto &prop : properties) {
    if (prop.first == "info:id") {
      const PrimAttrib &attr = prop.second.attrib;

      auto p = attr.var.get_value<std::string>();
      if (p) {
        if (p->compare("UsdPreviewSurface") == 0) {
          PreviewSurface surface;
          if (!ReconstructPreviewSurface(properties, references, &surface)) {
            PUSH_WARN("TODO: reconstruct PreviewSurface.");
          }
          shader->value = surface;
        } else if (p->compare("UsdUVTexture") == 0) {
          UVTexture texture;
          if (!ReconstructUVTexture(properties, references, &texture)) {
            PUSH_WARN("TODO: reconstruct UVTexture.");
          }
          shader->value = texture;
        } else if (p->compare("UsdPrimvarReader_float2") == 0) {
          PrimvarReader_float2 preader;
          if (!ReconstructPrimvarReader_float2(properties, references,
                                               &preader)) {
            PUSH_WARN(
                "TODO: reconstruct PrimvarReader_float2.");
          }
          shader->value = preader;
        } else {
          PUSH_ERROR_AND_RETURN("Invalid or Unsupported Shader id: " + (*p));
        }
      }
    } else {
      //std::cout << "TODO: " << prop.first << "\n";
    }
  }

  return true;
}

bool USDAParser::Impl::ReconstructMaterial(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    Material *material) {
  (void)properties;
  (void)references;
  (void)material;

  PUSH_WARN("TODO: Implement Material.");

  return true;
}

bool USDAParser::Impl::ReconstructPreviewSurface(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    PreviewSurface *surface) {
  // TODO:
  return false;
}

bool USDAParser::Impl::ReconstructUVTexture(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    UVTexture *texture) {
  // TODO:
  return false;
}

bool USDAParser::Impl::ReconstructPrimvarReader_float2(
    const std::map<std::string, Property> &properties,
    std::vector<std::pair<ListEditQual, Reference>> &references,
    PrimvarReader_float2 *preader) {
  // TODO:
  return false;
}

// 'None'
bool USDAParser::Impl::MaybeNone() {
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

//
// -- impl ReadBasicType
//

bool USDAParser::Impl::ReadBasicType(value::token *value) {

  std::string str;  
  if (ReadStringLiteral(&str)) {
    (*value) = value::token(str);
  } 

  return false;
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::token> *value) {
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

bool USDAParser::Impl::ReadBasicType(value::matrix4f *value) {
  if (value) {
    return ParseMatrix4f(value->m);
  } else {
    return false;
  }
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::matrix4f> *value) {
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

bool USDAParser::Impl::ReadBasicType(value::matrix2d *value) {
  if (value) {
    return ParseMatrix2d(value->m);
  } else {
    return false;
  }
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::matrix2d> *value) {
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

bool USDAParser::Impl::ReadBasicType(value::matrix3d *value) {
  if (value) {
    return ParseMatrix3d(value->m);
  } else {
    return false;
  }
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::matrix3d> *value) {
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

bool USDAParser::Impl::ReadBasicType(value::matrix4d *value) {
  if (value) {
    return ParseMatrix4d(value->m);
  } else {
    return false;
  }
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::matrix4d> *value) {
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

bool USDAParser::Impl::ReadBasicType(std::string *value) {
  return ReadStringLiteral(value);
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<std::string> *value) {
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

bool USDAParser::Impl::ReadBasicType(bool *value) {

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
  _line_col++;

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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<bool> *value) {
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

bool USDAParser::Impl::ReadBasicType(int *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  // bool negative = false;
  {
    char sc;
    if (!_sr->read1(&sc)) {
      return false;
    }
    _line_col++;

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

bool USDAParser::Impl::ReadBasicType(uint64_t *value) {
  std::stringstream ss;

  // head character
  bool has_sign = false;
  bool negative = false;
  {
    char sc;
    if (!_sr->read1(&sc)) {
      return false;
    }
    _line_col++;

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
#else
  (*value) = std::stoull(ss.str());
#endif

  // std::cout << "read int ok\n";

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::float2 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::float3 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::float4 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::double2 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::double3 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::double4 *value) {
  return ParseBasicTypeTuple(value);
}

bool USDAParser::Impl::ReadBasicType(value::point3f *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<float> values;
  if (!SepBy1BasicType<float>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->x = values[0];
  value->y = values[1];
  value->z = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::normal3f *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<float> values;
  if (!SepBy1BasicType<float>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->x = values[0];
  value->y = values[1];
  value->z = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::color3f *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<float> values;
  if (!SepBy1BasicType<float>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->r = values[0];
  value->g = values[1];
  value->b = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::color3d *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<double> values;
  if (!SepBy1BasicType<double>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->r = values[0];
  value->g = values[1];
  value->b = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::color4f *value) {
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

  if (values.size() != 4) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(4) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->r = values[0];
  value->g = values[1];
  value->b = values[2];
  value->a = values[3];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::color4d *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<double> values;
  if (!SepBy1BasicType<double>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 4) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(4) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->r = values[0];
  value->g = values[1];
  value->b = values[2];
  value->a = values[3];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::point3d *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<double> values;
  if (!SepBy1BasicType<double>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->x = values[0];
  value->y = values[1];
  value->z = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::normal3d *value) {
  if (!Expect('(')) {
    return false;
  }
  // std::cout << "got (\n";

  std::vector<double> values;
  if (!SepBy1BasicType<double>(',', &values)) {
    return false;
  }

  // std::cout << "try to parse )\n";

  if (!Expect(')')) {
    return false;
  }

  if (values.size() != 3) {
    std::string msg = "The number of tuple elements must be " +
                      std::to_string(3) + ", but got " +
                      std::to_string(values.size()) + "\n";
    PUSH_ERROR_AND_RETURN(msg);
  }

  value->x = values[0];
  value->y = values[1];
  value->z = values[2];

  return true;
}

bool USDAParser::Impl::ReadBasicType(value::texcoord2f *value) {
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

bool USDAParser::Impl::ReadBasicType(
    nonstd::optional<value::texcoord2f> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::float2> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::float3> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::float4> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::double2> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::double3> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::double4> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::point3f> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::point3d> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::normal3f> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::normal3d> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::color3f> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::color4f> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::color3d> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<value::color4d> *value) {
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

bool USDAParser::Impl::ReadBasicType(nonstd::optional<int> *value) {
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

//
// -- impl ReadTimeSampleData

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::float2> *out_value) {
  nonstd::optional<std::array<float, 2>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::float3> *out_value) {
  nonstd::optional<std::array<float, 3>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::float4> *out_value) {
  nonstd::optional<std::array<float, 4>> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(nonstd::optional<float> *out_value) {
  nonstd::optional<float> value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(nonstd::optional<double> *out_value) {
  nonstd::optional<double> value;
  if (!ReadBasicType(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::double2> *out_value) {
  nonstd::optional<value::double2> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::double3> *out_value) {
  nonstd::optional<value::double3> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::double4> *out_value) {
  nonstd::optional<value::double4> value;
  if (!ParseBasicTypeTuple(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
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

bool USDAParser::Impl::ReadTimeSampleData(
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

bool USDAParser::Impl::ReadTimeSampleData(
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

bool USDAParser::Impl::ReadTimeSampleData(
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

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<std::vector<value::matrix4f>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
    return true;
  }

  std::vector<value::matrix4f> value;
  if (!ParseMatrix4fArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::matrix4f> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
  }

  value::matrix4f value;
  if (!ParseMatrix4f(value.m)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<std::vector<value::matrix4d>> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
  }

  std::vector<value::matrix4d> value;
  if (!ParseMatrix4dArray(&value)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadTimeSampleData(
    nonstd::optional<value::matrix4d> *out_value) {
  if (MaybeNone()) {
    (*out_value) = nonstd::nullopt;
  }

  value::matrix4d value;
  if (!ParseMatrix4d(value.m)) {
    return false;
  }

  (*out_value) = value;

  return true;
}

bool USDAParser::Impl::ReadBasicType(float *value) {
  // -inf, inf, nan
  {
    float v;
    if (MaybeNonFinite(&v)) {
      (*value) = v;
      return true;
    }
  }

  std::string value_str;
  std::string err;
  if (!LexFloat(&value_str, &err)) {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += err;
    }
    PushError(msg);

    return false;
  }

  auto flt = ParseFloat(value_str);
  if (flt) {
    (*value) = flt.value();
  } else {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += flt.error() + "\n";
    }
    PushError(msg);
    return false;
  }

  return true;
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<float> *value) {
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

bool USDAParser::Impl::ReadBasicType(double *value) {
  // -inf, inf, nan
  {
    double v;
    if (MaybeNonFinite(&v)) {
      (*value) = v;
      return true;
    }
  }

  std::string value_str;
  std::string err;
  if (!LexFloat(&value_str, &err)) {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += err;
    }
    PushError(msg);

    return false;
  }

  auto flt = ParseDouble(value_str);
  if (!flt) {
    std::string msg = "Failed to parse float value literal.\n";
    msg += flt.error();
    PushError(msg);
    return false;
  } else {
    (*value) = flt.value();
  }

  return true;
}

bool USDAParser::Impl::ReadBasicType(nonstd::optional<double> *value) {
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

//
// --
//

bool IsUSDA(const std::string &filename, size_t max_filesize) {
  // TODO: Read only first N bytes
  std::vector<uint8_t> data;
  std::string err;

  if (!io::ReadWholeFile(&data, &err, filename, max_filesize)) {
    return false;
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::usda::USDAParser parser(&sr);

  return parser.CheckHeader();
}

///
/// -- USDAParser
///
USDAParser::USDAParser(StreamReader *sr) { _impl = new Impl(sr); }

USDAParser::~USDAParser() { delete _impl; }

bool USDAParser::CheckHeader() { return _impl->CheckHeader(); }

bool USDAParser::Parse(LoadState state) { return _impl->Parse(state); }

void USDAParser::SetBaseDir(const std::string &dir) {
  return _impl->SetBaseDir(dir);
}

std::vector<GPrim> USDAParser::GetGPrims() { return _impl->GetGPrims(); }

std::string USDAParser::GetDefaultPrimName() const {
  return _impl->GetDefaultPrimName();
}

std::string USDAParser::GetError() { return _impl->GetError(); }
std::string USDAParser::GetWarning() { return _impl->GetWarning(); }

}  // namespace tinyusdz

}  // namespace tinyusdz
