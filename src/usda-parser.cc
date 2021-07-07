// SPDX-License-Identifier: MIT
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
#include <thread>
#include <vector>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <ryu/ryu.h>
#include <ryu/ryu_parse.h>

#include <nonstd/variant.hpp>
#include <nonstd/expected.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "simple-serialize.hh"
#include "stream-reader.hh"
#include "prim-types.hh"
#include "tinyusdz.hh"

namespace tinyusdz {

namespace usda {

struct ErrorDiagnositc {
  std::string err;
  int line_row = -1;
  int line_col = -1;
};

struct Rel {
  // TODO: Implement
  std::string path;

  friend std::ostream &operator<<(std::ostream &os, const Rel &rel);
};

std::ostream &operator<<(std::ostream &os, const Rel &rel) {
  os << rel.path;

  return os;
}

typedef std::array<float, 2> float2;
typedef std::array<float, 3> float3;
typedef std::array<float, 4> float4;

typedef std::array<double, 2> double2;
typedef std::array<double, 3> double3;
typedef std::array<double, 4> double4;

// monostate = could be `Object` type in Variable class.
// If you want to add more items, you need to generate nonstd::variant file,
// since nonstd::variant has a limited number of types to use.
using Value = nonstd::variant<nonstd::monostate,
                              bool, int, float, double,
                              float2,
                              float3,
                              float4,
                              std::vector<float3>,
                              std::string, Rel>;

class Variable {
 public:
  std::string type;
  std::string name;

  // scalar type
  Value value;

  // compound types
  typedef std::vector<Value> Array;
  typedef std::map<std::string, Variable> Object;
  Array array;
  Object object;

  template <typename T>
  bool is() const {
    return value.index() == Value::index_of<T>();
  }

  bool IsEmpty() const { return type.empty() && is<nonstd::monostate>(); }

  bool IsArray() const { return array.size(); }

  bool IsBool() const { return type == "bool" && is<bool>(); }

  bool IsInt() const { return type == "int" && is<int>(); }

  bool IsFloat() const { return type == "float" && is<float>(); }
  bool IsFloat2() const { return type == "float2" && is<float2>(); }
  bool IsFloat3() const { return type == "float3" && is<float3>(); }
  bool IsFloat4() const { return type == "float4" && is<float4>(); }

  bool IsDouble() const { return type == "double" && is<double>(); }

  bool IsString() const { return type == "string" && is<std::string>(); }

  bool IsRel() const { return type == "rel" && is<Rel>(); }

  bool IsObject() const { return type == "object" && is<nonstd::monostate>(); }

  bool valid() const {
    // FIXME: Make empty valid?
    bool ok = IsBool() || IsInt() || IsFloat() || IsDouble() || IsString() ||
              IsRel() || IsObject();
    return ok;
  }

  Variable() = default;
  Variable(std::string ty, std::string n) : type(ty), name(n) {}

  // friend std::ostream &operator<<(std::ostream &os, const Object &obj);
  friend std::ostream &operator<<(std::ostream &os, const Variable &var);

  // friend std::string str_object(const Object &obj, int indent = 0); // string
  // representation of Object.
};

namespace {

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
    ss << Indent(indent + 1) << item.second.type << " " << item.first << " = ";

    if (item.second.IsObject()) {
      std::string str = str_object(item.second.object, indent + 1);
      ss << str;
    } else {
      // Value
      ss << item.second;
    }

    ss << "\n";
  }

  ss << Indent(indent) + "}";

  return ss.str();
}

}  // namespace

std::ostream &operator<<(std::ostream &os, const Variable &var) {
  if (var.is<bool>()) {
    os << nonstd::get<bool>(var.value);
  } else if (var.is<int>()) {
    os << nonstd::get<int>(var.value);
  } else if (var.is<float>()) {
    os << nonstd::get<float>(var.value);
  } else if (var.is<double>()) {
    os << nonstd::get<double>(var.value);
  } else if (var.is<std::string>()) {
    os << nonstd::get<std::string>(var.value);
  } else if (var.is<Rel>()) {
    os << nonstd::get<Rel>(var.value);
  } else if (var.IsObject()) {
    os << str_object(var.object, /* indent */ 0);
  } else if (var.IsEmpty()) {
    os << "[Variable is empty]";
  } else {
    os << "[???Variable???]";
  }

  return os;
}

inline bool isChar(char c) { return std::isalpha(int(c)); }

inline bool startsWith(const std::string &str, const std::string &t) {
  return (str.size() >= t.size()) &&
         std::equal(std::begin(t), std::end(t), std::begin(str));
}

inline bool endsWith(const std::string &str, const std::string &suffix) {
  return (str.size() >= suffix.size()) &&
         (str.find(suffix, str.size() - suffix.size()) != std::string::npos);
}

inline bool contains(const std::string &str, char c) {
  return str.find(c) == std::string::npos;
}

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
      if (exponent > std::numeric_limits<int>::max()/10) {
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


static nonstd::expected<float, std::string> ParseFloat(const std::string &s) {
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
}

static nonstd::expected<double, std::string> ParseDouble(const std::string &s) {
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
}

#if 0
template<typename T>
struct LexResult
{
  uint64_t n_chars; // # of characters read

  T value;
};

// Re-entrant LexFloat
static nonstd::expected<LexResult<std::string>, std::string> LexFloatR(
  const tinyusdz::StreamReader *sr) {

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
    if (!sr->read1(&sc)) {
      return nonstd::make_unexpected("Failed to read a character");
    }

    ss << sc;

    // sign, '.' or [0-9]
    if ((sc == '+') || (sc == '-')) {
      has_sign = true;

      char c;
      if (!sr->read1(&c)) {
        return nonstd::make_unexpected("Failed to read a character");
      }

      if (c == '.') {
        // ok. something like `+.7`, `-.53`
        leading_decimal_dots = true;
        ss << c;

      } else {
        // unwind and continue
        sr->seek_from_current(-1);
      }

    } else if ((sc >= '0') && (sc <= '9')) {
      // ok
    } else if (sc == '.') {
      // ok
      leading_decimal_dots = true;
    } else {
      return nonstd::make_unexpected("Sign or `.` or 0-9 expected.");
    }
  }

  (void)has_sign;

  // 1. Read the integer part
  char curr;
  if (!leading_decimal_dots) {
    // std::cout << "1 read int part: ss = " << ss.str() << "\n";

    while (!sr->eof()) {
      if (!sr->read1(&curr)) {
        return nonstd::make_unexpected("Failed to read a character");
      }

      // std::cout << "1 curr = " << curr << "\n";
      if ((curr >= '0') && (curr <= '9')) {
        // continue
        ss << curr;

      } else {
        sr->seek_from_current(-1);
        break;
      }
    }
  }

  if (sr->eof()) {
    LexResult<std::string> ret;
    ret.n_chars = ss.str().size();
    ret.value = ss.str();
    return std::move(ret);
  }

  if (!sr->read1(&curr)) {
    return nonstd::make_unexpected("Failed to read a character");
  }

  // std::cout << "before 2: ss = " << ss.str() << ", curr = " << curr <<
  // "\n";

  // 2. Read the decimal part
  if (curr == '.') {
    ss << curr;

    while (!sr->eof()) {
      if (!sr->read1(&curr)) {
        return nonstd::make_unexpected("Failed to read a character");
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
    sr->seek_from_current(-1);

    LexResult<std::string> ret;
    ret.n_chars = ss.str().size();
    ret.value = ss.str();
    return std::move(ret);
  }

  if (sr->eof()) {
    LexResult<std::string> ret;
    ret.n_chars = ss.str().size();
    ret.value = ss.str();
    return std::move(ret);
  }

  // 3. Read the exponent part
  bool has_exp_sign{false};
  if ((curr == 'e') || (curr == 'E')) {
    ss << curr;

    if (!sr->read1(&curr)) {
      return nonstd::make_unexpected("Failed to read a character");
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
      std::string msg = "Empty E is not allowed. curr = " + ss.str();
      return nonstd::make_unexpected(msg);
    }

    while (!sr->eof()) {
      if (!sr->read1(&curr)) {
        return nonstd::make_unexpected("Failed to read a character");
      }

      if ((curr >= '0') && (curr <= '9')) {
        // ok
        ss << curr;

      } else if ((curr == '+') || (curr == '-')) {
        if (has_exp_sign) {
          // No multiple sign characters
          std::string msg = "No multiple exponential sign characters.";
          return nonstd::make_unexpected(msg);
        }

        ss << curr;
        has_exp_sign = true;
      } else {
        // end
        sr->seek_from_current(-1);
        break;
      }
    }
  } else {
    sr->seek_from_current(-1);
  }

  LexResult<std::string> ret;
  ret.n_chars = ss.str().size();
  ret.value = ss.str();
  return std::move(ret);
}
#endif

//
// TODO: multi-threaded value array parser.
//
// Assumption.
//   - Assume input string is one-line(no newline) and startsWith/endsWith brackets(e.g. `((1,2),(3, 4))`)
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

class USDAParser {
 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  USDAParser(tinyusdz::StreamReader *sr) : _sr(sr) {
    _RegisterBuiltinMeta();
    _RegisterNodeTypes();
    _RegisterPrimAttrTypes();
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

  bool ParseDefArgs(std::map<std::string, Variable> *args) {
    // '(' args ')'

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

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::string token;
    if (!ReadToken(&token)) {
      return false;
    }

    if (token != "kind") {
      _PushError("Currently only `kind` is supported.\n");
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

    std::string value;
    if (!ReadStringLiteral(&value)) {
      return false;
    }

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (!Expect(')')) {
      return false;
    }

    Variable kind_var("string", "kind");
    kind_var.value = value;

    (*args)["kind"] = kind_var;

    std::cout << "100: kind = " << value << "\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
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
          _PushError("Failed to parse dict element.");
          return false;
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

        if ((token != "interpolation") && (token != "customData")) {
          _PushError(
              "Currently only `interpolation` or `customData` is supported but "
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

          Variable var("string", "interpolation");
          var.value = value;

          assert(var.valid());

          (*out_meta)["interpolation"] = var;
        } else if (token == "customData") {
          std::map<std::string, Variable> dict;

          std::cout << "Parse customData\n";

          if (!ParseDict(&dict)) {
            std::cout << "dict parse fail\n";
            return false;
          }

          Variable var("object", "customData");
          var.object = dict;

          assert(var.valid());

          (*out_meta)["customData"] = var;

          std::cout << "Got customData = " << var << "\n";

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
        _PushError("`type` identifier expected but got non-identifier\n");
        return false;
      }

      // `type_name` is then overwritten.
    }

    if (!_IsRegisteredPrimAttrType(type_name)) {
      _PushError("Unknown or unsupported primtive attribute type `" +
                 type_name + "`\n");
      return false;
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
          _PushError("Invalid syntax found.\n");
          return false;
        }

      } else {
        if (!Rewind(1)) {
          return false;
        }
      }
    }

    std::cout << "array_qual " << array_qual << "\n";

    if (!SkipWhitespace()) {
      return false;
    }

    std::string primattr_name;
    if (!ReadPrimAttrIdentifier(&primattr_name)) {
      _PushError("Failed to parse primAttr identifier.\n");
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
      std::string value;
      if (!ReadStringLiteral(&value)) {
        _PushError("Failed to parse string literal.\n");
        return false;
      }

      std::cout << "string = " << value << "\n";

    } else {
      _PushError("Unimplemented or unsupported type: " + type_name + "\n");
      return false;
    }

    (void)uniform_qual;

    return true;
  }

  bool ParseDictElement(std::string *out_key, Variable *out_var) {
    // dict_element: type (array_qual?) name '=' value
    //           ;

    std::string type_name;

    if (!ReadIdentifier(&type_name)) {
      return false;
    }

    if (!SkipWhitespace()) {
      return false;
    }

    if (!_IsRegisteredPrimAttrType(type_name)) {
      _PushError("Unknown or unsupported type `" + type_name + "`\n");
      return false;
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
          _PushError("Invalid syntax found.\n");
          return false;
        }

      } else {
        if (!Rewind(1)) {
          return false;
        }
      }
    }

    std::cout << "array_qual " << array_qual << "\n";

    if (!SkipWhitespace()) {
      return false;
    }

    std::string key_name;
    if (!ReadIdentifier(&key_name)) {
      // string literal is also supported. e.g. "0"
      if (ReadStringLiteral(&key_name)) {
        // ok
      } else {
        _PushError("Failed to parse dictionary key identifier.\n");
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
    // TODO(syoyo): Refactror and implement value parser dispatcher.
    //
    Variable var;
    if (type_name == "matrix4d") {
      double m[4][4];
      if (!ParseMatrix4d(m)) {
        _PushError("Failed to parse value with type `matrix4d`.\n");
        return false;
      }

      std::cout << "matrix4d = \n";
      std::cout << m[0][0] << ", " << m[0][1] << ", " << m[0][2] << ", "
                << m[0][3] << "\n";
      std::cout << m[1][0] << ", " << m[1][1] << ", " << m[1][2] << ", "
                << m[1][3] << "\n";
      std::cout << m[2][0] << ", " << m[2][1] << ", " << m[2][2] << ", "
                << m[2][3] << "\n";
      std::cout << m[3][0] << ", " << m[3][1] << ", " << m[3][2] << ", "
                << m[3][3] << "\n";
    } else if (type_name == "bool") {
      if (array_qual) {
        // Assume people only use array access to vector<bool>
        std::vector<bool> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError(
              "Failed to parse array of string literal for `uniform "
              "bool[]`.\n");
          return false;
        }
      } else {
        bool value;
        if (!ReadBasicType(&value)) {
          _PushError("Failed to parse value for `uniform bool`.\n");
          return false;
        }
        std::cout << "bool value = " << value << "\n";

        var.type = "bool";
        var.value = value;
      }
    } else if (type_name == "token") {
      if (array_qual) {
        std::vector<std::string> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError(
              "Failed to parse array of string literal for `uniform "
              "token[]`.\n");
          return false;
        }
      } else {
        std::string value;  // TODO: Path
        if (!ReadPathIdentifier(&value)) {
          _PushError("Failed to parse path identifier for `token`.\n");
          return false;
        }
        std::cout << "Path identifier = " << value << "\n";
      }
    } else if (type_name == "int") {
      if (array_qual) {
        std::vector<int> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError("Failed to parse int array.\n");
          return false;
        }
      } else {
        int value;
        if (!ReadBasicType(&value)) {
          _PushError("Failed to parse int value.\n");
          return false;
        }
      }
    } else if (type_name == "float2") {
      if (array_qual) {
        std::vector<std::array<float, 2>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse float2 array.\n");
          return false;
        }
        std::cout << "float2 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ")\n";
        }
      } else {
        std::array<float, 2> value;
        if (!ParseBasicTypeTuple<float, 2>(&value)) {
          _PushError("Failed to parse float2.\n");
          return false;
        }
        std::cout << "float3 = (" << value[0] << ", " << value[1] << ")\n";
      }

      // optional: interpolation parameter
      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "float3") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse float3 array.\n");
          return false;
        }
        std::cout << "float3 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse float3.\n");
          return false;
        }
        std::cout << "float3 = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }
    } else if (type_name == "color3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse color3f array.\n");
          return false;
        }
        std::cout << "color3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse color3f.\n");
          return false;
        }
        std::cout << "color3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }
      if (meta.count("customData")) {
        std::cout << "has customData\n";
      }

    } else if (type_name == "double3") {
      if (array_qual) {
        std::vector<std::array<double, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse double3 array.\n");
          return false;
        }
        std::cout << "double3 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<double, 3> value;
        if (!ParseBasicTypeTuple<double, 3>(&value)) {
          _PushError("Failed to parse double3.\n");
          return false;
        }
        std::cout << "double3 = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "normal3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse normal3f array.\n");
          return false;
        }
        std::cout << "normal3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse normal3f.\n");
          return false;
        }
        std::cout << "normal3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "point3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse point3f array.\n");
          return false;
        }
        std::cout << "point3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse point3f.\n");
          return false;
        }
        std::cout << "point3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "texCoord2f") {
      if (array_qual) {
        std::vector<std::array<float, 2>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse texCoord2f array.\n");
          return false;
        }
        std::cout << "texCoord2f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ")\n";
        }
      } else {
        std::array<float, 2> value;
        if (!ParseBasicTypeTuple<float, 2>(&value)) {
          _PushError("Failed to parse texCoord2f.\n");
          return false;
        }
        std::cout << "texCoord2f = (" << value[0] << ", " << value[1] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "rel") {
      Rel rel;
      if (ParseRel(&rel)) {
        _PushError("Failed to parse rel.\n");
      }

      std::cout << "rel: " << rel.path << "\n";

    } else if (type_name == "dictionary") {
      if (array_qual) {
        _PushError("`dictionary[]` is not supported.");
        return false;
      }

      std::map<std::string, Variable> dict;
      if (!ParseDict(&dict)) {
        _PushError("Failed to parse `dictionary` data.");
        return false;
      }

      var.type = "object";
      var.name = key_name;
      var.object = dict;

      std::cout << "Dict = " << var << "\n";

      assert(var.valid());

      std::cout << "dict = " << var << "\n";

      // 'todos'
    } else {
      _PushError("TODO: Implement value parser for type: " + type_name + "\n");
      return false;
    }

    // TODO
    (*out_key) = key_name;
    (*out_var) = var;

    return true;
  }

  bool ParsePrimAttr(std::map<std::string, Variable> *props) {
    // prim_attr : uniform type (array_qual?) name '=' value interpolation?
    //           | type (array_qual?) name '=' value interpolation?
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
        _PushError("`type` identifier expected but got non-identifier\n");
        return false;
      }

      // `type_name` is then overwritten.
    }

    if (!_IsRegisteredPrimAttrType(type_name)) {
      _PushError("Unknown or unsupported primtive attribute type `" +
                 type_name + "`\n");
      return false;
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
          _PushError("Invalid syntax found.\n");
          return false;
        }

      } else {
        if (!Rewind(1)) {
          return false;
        }
      }
    }

    std::cout << "array_qual " << array_qual << "\n";

    if (!SkipWhitespace()) {
      return false;
    }

    std::string primattr_name;
    if (!ReadPrimAttrIdentifier(&primattr_name)) {
      _PushError("Failed to parse primAttr identifier.\n");
      return false;
    }

    if (!SkipWhitespace()) {
      return false;
    }

    // output node?
    if (type_name == "token" && hasOutputs(primattr_name) && !hasConnect(primattr_name)) {
      // ok
      return true;
    }

    if (!Expect('=')) {
      return false;
    }

    if (!SkipWhitespace()) {
      return false;
    }

    //
    // TODO(syoyo): Refactror and implement value parser dispatcher.
    //
    if (type_name == "matrix4d") {
      double m[4][4];
      if (!ParseMatrix4d(m)) {
        _PushError("Failed to parse value with type `matrix4d`.\n");
        return false;
      }

      std::cout << "matrix4d = \n";
      std::cout << m[0][0] << ", " << m[0][1] << ", " << m[0][2] << ", "
                << m[0][3] << "\n";
      std::cout << m[1][0] << ", " << m[1][1] << ", " << m[1][2] << ", "
                << m[1][3] << "\n";
      std::cout << m[2][0] << ", " << m[2][1] << ", " << m[2][2] << ", "
                << m[2][3] << "\n";
      std::cout << m[3][0] << ", " << m[3][1] << ", " << m[3][2] << ", "
                << m[3][3] << "\n";
    } else if (type_name == "bool") {
      if (array_qual) {
        // Assume people only use array access to vector<bool>
        std::vector<bool> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError(
              "Failed to parse array of string literal for `uniform "
              "bool[]`.\n");
        }
      } else {
        bool value;
        if (!ReadBasicType(&value)) {
          _PushError("Failed to parse value for `uniform bool`.\n");
        }
        std::cout << "bool value = " << value << "\n";
      }
    } else if (type_name == "token") {
      if (array_qual) {
        if (!uniform_qual) {
          _PushError("TODO: token[]\n");
          return false;
        }

        std::vector<std::string> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError(
              "Failed to parse array of string literal for `uniform "
              "token[]`.\n");
        }
      } else {
        if (uniform_qual) {
          std::cout << "uniform_qual\n";
          std::string value;
          if (!ReadStringLiteral(&value)) {
            _PushError("Failed to parse string literal for `uniform token`.\n");
          }
          std::cout << "StringLiteral = " << value << "\n";
        } else if (hasConnect(primattr_name)) {
          std::cout << "hasConnect\n";
          std::string value;  // TODO: Path
          if (!ReadPathIdentifier(&value)) {
            _PushError("Failed to parse path identifier for `token`.\n");
          }
          std::cout << "Path identifier = " << value << "\n";

        } else if (hasOutputs(primattr_name)) {
          std::cout << "output\n";
          // Output node.
          // OK
        } else {
          std::cout << "??? " << primattr_name << "\n";
          std::string value;
          if (!ReadStringLiteral(&value)) {
            _PushError("Failed to parse string literal for `token`.\n");
          }
        }
      }
    } else if (type_name == "int") {
      if (array_qual) {
        std::vector<int> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError("Failed to parse int array.\n");
        }
      } else {
        int value;
        if (!ReadBasicType(&value)) {
          _PushError("Failed to parse int value.\n");
        }
      }
    } else if (type_name == "float") {
      if (array_qual) {
        std::vector<float> value;
        if (!ParseBasicTypeArray(&value)) {
          _PushError("Failed to parse float array.\n");
        }
        std::cout << "float = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << value[i] << "\n";
        }
      } else if (hasConnect(primattr_name)) {
        std::string value;  // TODO: Path
        if (!ReadPathIdentifier(&value)) {
          _PushError("Failed to parse path identifier for `token`.\n");
          return false;
        }
        std::cout << "Path identifier = " << value << "\n";
      } else {
        float value;
        if (!ReadBasicType(&value)) {
          _PushError("Failed to parse float.\n");
        }
        std::cout << "float = " << value << "\n";
      }

      // optional: interpolation parameter
      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }
    } else if (type_name == "float2") {
      if (array_qual) {
        std::vector<std::array<float, 2>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse float2 array.\n");
        }
        std::cout << "float2 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ")\n";
        }
      } else {
        std::array<float, 2> value;
        if (!ParseBasicTypeTuple<float, 2>(&value)) {
          _PushError("Failed to parse float2.\n");
        }
        std::cout << "float3 = (" << value[0] << ", " << value[1] << ")\n";
      }

      // optional: interpolation parameter
      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "float3") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse float3 array.\n");
        }
        std::cout << "float3 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }

        Variable var;
        var.value = value;
        (*props)[primattr_name] = var;
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse float3.\n");
        }
        std::cout << "float3 = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";

      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }
    } else if (type_name == "color3f") {
      if (array_qual) {
        // TODO: connection
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse color3f array.\n");
        }
        std::cout << "color3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else if (hasConnect(primattr_name)) {
        std::string value;  // TODO: Path
        if (!ReadPathIdentifier(&value)) {
          _PushError("Failed to parse path identifier for `token`.\n");
          return false;
        }
        std::cout << "Path identifier = " << value << "\n";
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse color3f.\n");
        }
        std::cout << "color3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }
      if (meta.count("customData")) {
        std::cout << "has customData\n";
      }

    } else if (type_name == "double3") {
      if (array_qual) {
        std::vector<std::array<double, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse double3 array.\n");
        }
        std::cout << "double3 = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<double, 3> value;
        if (!ParseBasicTypeTuple<double, 3>(&value)) {
          _PushError("Failed to parse double3.\n");
        }
        std::cout << "double3 = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "normal3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse normal3f array.\n");
        }
        std::cout << "normal3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else if (hasConnect(primattr_name)) {
        std::string value;  // TODO: Path
        if (!ReadPathIdentifier(&value)) {
          _PushError("Failed to parse path identifier for `token`.\n");
          return false;
        }
        std::cout << "Path identifier = " << value << "\n";
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse normal3f.\n");
        }
        std::cout << "normal3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "point3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse point3f array.\n");
        }
        std::cout << "point3f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ", "
                    << value[i][2] << ")\n";
        }
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse point3f.\n");
        }
        std::cout << "point3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }
      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "texCoord2f") {
      if (array_qual) {
        std::vector<std::array<float, 2>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse texCoord2f array.\n");
        }
        std::cout << "texCoord2f = \n";
        for (size_t i = 0; i < value.size(); i++) {
          std::cout << "(" << value[i][0] << ", " << value[i][1] << ")\n";
        }
      } else {
        std::array<float, 2> value;
        if (!ParseBasicTypeTuple<float, 2>(&value)) {
          _PushError("Failed to parse texCoord2f.\n");
        }
        std::cout << "texCoord2f = (" << value[0] << ", " << value[1] << ")\n";
      }

      std::map<std::string, Variable> meta;
      if (!ParseAttrMeta(&meta)) {
        _PushError("Failed to parse PrimAttr meta.");
        return false;
      }

      if (meta.count("interpolation")) {
        std::cout << "interpolation: "
                  << nonstd::get<std::string>(meta.at("interpolation").value)
                  << "\n";
      }

    } else if (type_name == "rel") {
      Rel rel;
      if (ParseRel(&rel)) {
        _PushError("Failed to parse rel.\n");
      }

      std::cout << "rel: " << rel.path << "\n";

      // 'todos'
    } else {
      _PushError("TODO: Implement value parser for type: " + type_name + "\n");
      return false;
    }

    (void)uniform_qual;

    return true;
  }

  bool ReadBasicType(std::string *value);
  bool ReadBasicType(int *value);
  bool ReadBasicType(float *value);
  bool ReadBasicType(double *value);
  bool ReadBasicType(bool *value);

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
        _PushError("Not starting with the value of requested type.\n");
        return false;
      }

      result->push_back(value);
    }

    // std::cout << "sep: " << sep << "\n";

    while (!_sr->eof()) {
      // sep
      if (!SkipWhitespaceAndNewline()) {
        // std::cout << "ws failure\n";
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        std::cout << "read1 failure\n";
        return false;
      }

      // std::cout << "sep c = " << c << "\n";

      if (c != sep) {
        // end
        // std::cout << "sepBy1 end\n";
        _sr->seek_from_current(-1);  // unwind single char
        break;
      }

      if (!SkipWhitespaceAndNewline()) {
        // std::cout << "ws failure\n";
        return false;
      }

      // std::cout << "go to read int\n";

      T value;
      if (!ReadBasicType(&value)) {
        break;
      }

      result->push_back(value);
    }

    // std::cout << "result.size " << result->size() << "\n";

    if (result->empty()) {
      _PushError("Empty array.\n");
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
        _PushError("Not starting with the tuple value of requested type.\n");
        return false;
      }

      result->push_back(value);
    }

    while (!_sr->eof()) {
      if (!SkipWhitespaceAndNewline()) {
        // std::cout << "ws failure\n";
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        // std::cout << "read1 failure\n";
        return false;
      }

      // std::cout << "sep c = " << c << "\n";

      if (c != sep) {
        // end
        std::cout << "sepBy1 end\n";
        _sr->seek_from_current(-1);  // unwind single char
        break;
      }

      if (!SkipWhitespaceAndNewline()) {
        // std::cout << "ws failure\n";
        return false;
      }

      // std::cout << "go to read int\n";

      std::array<T, N> value;
      if (!ParseBasicTypeTuple<T, N>(&value)) {
        break;
      }

      result->push_back(value);
    }

    // std::cout << "result.size " << result->size() << "\n";

    if (result->empty()) {
      _PushError("Empty array.\n");
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
    // std::cout << "got [\n";

    if (!SepBy1BasicType<T>(',', result)) {
      return false;
    }

    // std::cout << "try to parse ]\n";

    if (!Expect(']')) {
      // std::cout << "not ]\n";

      return false;
    }
    // std::cout << "got ]\n";

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
    // std::cout << "got (\n";

    std::vector<T> values;
    if (!SepBy1BasicType<T>(',', &values)) {
      return false;
    }

    // std::cout << "try to parse )\n";

    if (!Expect(')')) {
      return false;
    }

    if (values.size() != N) {
      std::string msg = "The number of tuple elements must be " +
                        std::to_string(N) + ", but got " +
                        std::to_string(result->size()) + "\n";
      _PushError(msg);
      return false;
    }

    for (size_t i = 0; i < N; i++) {
      (*result)[i] = values[i];
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
      _PushError("# of rows in matrix4d must be 4, but got " +
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
  /// Parse the array of tuple(e.g. `float3`: [(0, 1, 2), (2, 3, 4), ...] )
  ///
  template <typename T, size_t N>
  bool ParseTupleArray(std::vector<std::array<T, N>> *result) {
    (void)result;

    if (!Expect('[')) {
      return false;
    }
    std::cout << "got [\n";

    if (!SepBy1TupleType<T, N>(',', result)) {
      return false;
    }

    if (!Expect(']')) {
      std::cout << "not ]\n";

      return false;
    }
    std::cout << "got ]\n";

    return true;
  }

  bool ReadStringLiteral(std::string *literal) {
    std::stringstream ss;

    char c0;
    if (!_sr->read1(&c0)) {
      return false;
    }

    if (c0 != '"') {
      std::cout << "c0 = " << c0 << "\n";
      ErrorDiagnositc diag;
      diag.err = "String literal expected but it does not start with '\"'\n";
      diag.line_col = _line_col;
      diag.line_row = _line_row;

      err_stack.push(diag);
      return false;
    }

    ss << "\"";

    while (!_sr->eof()) {
      char c;
      if (!_sr->read1(&c)) {
        // this should not happen.
        std::cout << "read err\n";
        return false;
      }

      ss << c;

      if (c == '"') {
        break;
      }
    }

    (*literal) = ss.str();

    if (literal->back() != '"') {
      ErrorDiagnositc diag;
      diag.err = "String literal expected but it does not end with '\"'\n";
      diag.line_col = _line_col;
      diag.line_row = _line_row;

      err_stack.push(diag);
      return false;
    }

    _line_col += literal->size();

    return true;
  }

  bool ReadPrimAttrIdentifier(std::string *token) {
    // Example: xformOp:transform

    std::stringstream ss;

    std::cout << "readtoken\n";

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
          _PushError("PrimAttr name must not starts with `:`\n");
          return false;
        }
      } else if (c == '.') {  // delimiter for `connect`
        // '.' must lie in the middle of string literal
        if (ss.str().size() == 0) {
          _PushError("PrimAttr name must not starts with `.`\n");
          return false;
        }
      } else if (!std::isalpha(int(c))) {
        _sr->seek_from_current(-1);
        break;
      }

      _line_col++;

      std::cout << c << "\n";
      ss << c;
    }

    // ':' must lie in the middle of string literal
    if (ss.str().back() == ':') {
      _PushError("PrimAttr name must not ends with `:`\n");
      return false;
    }

    // '.' must lie in the middle of string literal
    if (ss.str().back() == '.') {
      _PushError("PrimAttr name must not ends with `.`\n");
      return false;
    }

    // Currently we only support '.connect'
    std::string tok = ss.str();

    if (contains(tok, '.')) {
      if (endsWith(tok, ".connect")) {
        _PushError(
            "Must ends with `.connect` when a name contains punctuation `.`");
        return false;
      }
    }

    (*token) = ss.str();
    std::cout << "primAttr identifier = " << (*token) << "\n";
    return true;
  }

  bool ReadIdentifier(std::string *token) {
    // identifier = (`_` | [a-zA-Z]) (`_` | [a-zA-Z0-9]+)
    std::stringstream ss;

    // std::cout << "readtoken\n";

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

      // std::cout << c << "\n";
      ss << c;
    }

    (*token) = ss.str();
    // std::cout << "ReadIdentifier: token = " << (*token) << "\n";
    return true;
  }

  bool ReadPathIdentifier(std::string *path_identifier) {
    // path_identifier = `<` string `>`
    std::stringstream ss;

    if (!Expect('<')) {
      return false;
    }

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

    (*path_identifier) = ss.str();
    std::cout << "PathIdentifier: " << (*path_identifier) << "\n";

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
      _PushError(msg);

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
      char magic[5];
      if (!_sr->read(5, 5, reinterpret_cast<uint8_t *>(magic))) {
        // eol
        return false;
      }

      if ((magic[0] == '#') && (magic[1] == 'u') && (magic[2] == 's') &&
          (magic[3] == 'd') && (magic[4] == 'a')) {
        // ok
      } else {
        ErrorDiagnositc diag;
        diag.line_row = _line_row;
        diag.line_col = _line_col;
        diag.err = "Magic header must be `#usda` but got `" +
                   std::string(magic, 5) + "`\n";
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

  bool ParseMetaValue(const std::string &vartype, const std::string &varname,
                      Variable *outvar) {
    (void)outvar;

    if (vartype == "string") {
      std::string value;
      std::cout << "read string literal\n";
      if (!ReadStringLiteral(&value)) {
        std::string msg = "String literal expected for `" + varname + "`.\n";
        _PushError(msg);
        return false;
      }
    } else if (vartype == "int[]") {
      std::vector<int> values;
      if (!ParseBasicTypeArray<int>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; _PushError(msg);
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "int[" << i << "] = " << values[i] << "\n";
      }
    } else if (vartype == "float[]") {
      std::vector<float> values;
      if (!ParseBasicTypeArray<float>(&values)) {
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "float[" << i << "] = " << values[i] << "\n";
      }
    } else if (vartype == "float3[]") {
      std::vector<std::array<float, 3>> values;
      if (!ParseTupleArray<float, 3>(&values)) {
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "float[" << i << "] = " << values[i][0] << ", "
                  << values[i][1] << ", " << values[i][2] << "\n";
      }
    } else if (vartype == "float") {
      std::string fval;
      std::string ferr;
      if (!LexFloat(&fval, &ferr)) {
        std::string msg =
            "Floating point literal expected for `" + varname + "`.\n";
        if (!ferr.empty()) {
          msg += ferr;
        }
        _PushError(msg);
        return false;
      }
      //std::cout << "float : " << fval << "\n";
      auto ret = ParseFloat(fval);
      //if (!ParseFloat(fval, &value, &ferr)) {
      if (!ret) {
        std::string msg =
            "Failed to parse floating point literal for `" + varname + "`.\n";
        if (!ferr.empty()) {
          msg += ret.error();
        }
        _PushError(msg);
        return false;
      }
      std::cout << "parsed float : " << ret.value() << "\n";

    } else if (vartype == "int3") {
      std::array<int, 3> values;
      if (!ParseBasicTypeTuple<int, 3>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; _PushError(msg);
        return false;
      }

      for (size_t i = 0; i < values.size(); i++) {
        std::cout << "int[" << i << "] = " << values[i] << "\n";
      }
    } else if (vartype == "object") {
      if (!Expect('{')) {
        _PushError("'{' expected.\n");
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
          std::cout << "End of compound meta\n";
          break;
        } else {
          if (!Rewind(1)) {
            return false;
          }

          if (!ParseCustomMetaValue()) {
            _PushError("Failed to parse meta definition.\n");
            return false;
          }
        }
      }

      return true;
    }

    return true;
  }

  // metadata_opt := string_literal '\n'
  //              |  var '=' value '\n'
  //
  bool ParseMetaOpt() {
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
        std::cout << "note = " << note << "\n";

        return true;
      }
    }

    std::string varname;
    if (!ReadIdentifier(&varname)) {
      std::cout << "token " << varname;
      return false;
    }

    if (!_IsBuiltinMeta(varname)) {
      std::string msg =
          "'" + varname + "' is not a builtin Metadata variable.\n";
      _PushError(msg);
      return false;
    }

    if (!Expect('=')) {
      _PushError("'=' expected in Metadata line.\n");
      return false;
    }
    SkipWhitespace();

    Variable &refvar = _builtin_metas.at(varname);
    Variable var;
    if (!ParseMetaValue(refvar.type, refvar.name, &var)) {
      _PushError("Failed to parse meta value.\n");
      return false;
    }
#if 0
    if (var.type == "string") {
      std::string value;
      std::cout << "read string literal\n";
      if (!ReadStringLiteral(&value)) {
        std::string msg = "String literal expected for `" + var.name + "`.\n";
        _PushError(msg);
        return false;
      }
    } else if (var.type == "int[]") {
      std::vector<int> values;
      if (!ParseBasicTypeArray<int>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; _PushError(msg);
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
        _PushError(msg);
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
        _PushError(msg);
        return false;
      }
      std::cout << "parsed float : " << value << "\n";

    } else if (var.type == "int3") {
      std::array<int, 3> values;
      if (!ParseBasicTypeTuple<int, 3>(&values)) {
        // std::string msg = "Array of int values expected for `" + var.name +
        // "`.\n"; _PushError(msg);
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

  // Parse meta
  // meta = ( metadata_opt )
  //      | empty
  //      ;
  bool ParseMeta() {
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

        if (!ParseMetaOpt()) {
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

  bool Char1(char *c) { return _sr->read1(c); }

  bool Rewind(size_t offset) {
    if (!_sr->seek_from_current(-int64_t(offset))) {
      return false;
    }

    return true;
  }

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
  /// Parse `def` block.
  /// `def Xform "root" optional_arg? { ... }
  ///
  /// optional_arg = '(' args ')'
  ///
  bool ParseDefBlock() {
    std::string def;

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    if (!ReadToken(&def)) {
      return false;
    }

    if (def != "def") {
      _PushError("`def` is expected.");
      return false;
    }

    std::cout << "def = " << def << "\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::string prim_type;

    if (!ReadToken(&prim_type)) {
      return false;
    }

    if (!_node_types.count(prim_type)) {
      std::string msg =
          "`" + prim_type +
          "` is not a defined Prim type(or not supported in TinyUSDZ)\n";
      _PushError(msg);
      return false;
    }

    std::cout << "prim_type: " << prim_type << "\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::string node_name;
    if (!ReadStringLiteral(&node_name)) {
      return false;
    }

    std::cout << "prim node name: " << node_name << "\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::map<std::string, Variable> args;
    ParseDefArgs(&args);

    if (!Expect('{')) {
      std::cout << "???\n";
      return false;
    }
    std::cout << "bbb\n";

    if (!SkipWhitespaceAndNewline()) {
      return false;
    }

    std::cout << "aaa\n";

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
        std::cout << "End of block\n";
        break;
      } else {
        if (!Rewind(1)) {
          return false;
        }

        std::string tok;
        if (!ReadToken(&tok)) {
          return false;
        }

        std::cout << "token = " << tok << ", size = " << tok.size() << "\n";

        if (!Rewind(tok.size())) {
          return false;
        }

        if (tok == "def") {
          std::cout << "rec\n";
          // recusive call
          if (!ParseDefBlock()) {
            std::cout << "rec failed\n";
            return false;
          }
        } else {
          // Assume PrimAttr
          std::map<std::string, Variable> props;
          if (!ParsePrimAttr(&props)) {
            return false;
          }

          if (prim_type == "GeomMesh") {
            GeomMesh mesh;
            std::cout << "Reconstruct GeomMesh\n";
            if (!ReconstructGeomMesh(props, &mesh)) {
              _PushError("Failed to reconstruct GeomMesh.");
              return false;
            }
          } else if (prim_type == "BasisCurves") {

	  } else {
            std::cout << "TODO:" << prim_type << "\n";
	  }
        }

        if (!SkipWhitespaceAndNewline()) {
          return false;
        }
      }
    }

    return true;
  }

  bool ReconstructGeomMesh(
    const std::map<std::string, Variable> &properties,
    GeomMesh *mesh) {

    for (const auto &prop : properties) {
      if (prop.first == "points") {
        if (!prop.second.IsFloat3()) {
          _PushError("`points` must be float3 type.");
          return false;
        }

        const std::vector<float3> p = nonstd::get<std::vector<float3>>(prop.second.value);

        mesh->points.resize(p.size() * 3);
        memcpy(mesh->points.data(), p.data(), p.size() * 3);

      } else {
       
      }
    }

    return true;
  }

  bool ReconstructBasisCurves(
    const std::map<std::string, Variable> &properties,
    GeomBasisCurves *curves) {

    for (const auto &prop : properties) {
      if (prop.first == "points") {
        if (!prop.second.IsFloat3() && !prop.second.IsArray()) {
          _PushError("`points` must be float3 array type.");
          return false;
        }

        const std::vector<float3> p = nonstd::get<std::vector<float3>>(prop.second.value);

        curves->points.resize(p.size() * 3);
        memcpy(curves->points.data(), p.data(), p.size() * 3);

      } else if (prop.first == "curveVertexCounts") {
        if (!prop.second.IsInt() && !prop.second.IsArray()) {
          _PushError("`curveVertexCounts` must be int array type.");
          return false;
        }

        const std::vector<int32_t> p = nonstd::get<std::vector<int32_t>>(prop.second.value);

        curves->curveVertexCounts.resize(p.size());
        memcpy(curves->curveVertexCounts.data(), p.data(), p.size());

      } else {
        std::cout << "TODO: " << prop.first << "\n";
      }
    }

    return true;
  }


  bool Parse() {
    bool header_ok = ParseMagicHeader();
    if (!header_ok) {
      _PushError("Failed to parse USDA magic header.\n");
      return false;
    }

    bool has_meta = ParseMeta();
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

      bool block_ok = ParseDefBlock();
      if (!block_ok) {
        _PushError("Failed to parse `def` block.\n");
        return false;
      }
    }

    return true;
  }

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

 private:
  bool _IsRegisteredPrimAttrType(const std::string &ty) {
    return _registered_prim_attr_types.count(ty);
  }

  void _RegisterPrimAttrTypes() {
    _registered_prim_attr_types.insert("float");
    _registered_prim_attr_types.insert("int");

    _registered_prim_attr_types.insert("float2");
    _registered_prim_attr_types.insert("float3");
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

    _registered_prim_attr_types.insert("dictionary");

    // TODO: array type
  }

  void _PushError(const std::string &msg) {
    ErrorDiagnositc diag;
    diag.line_row = _line_row;
    diag.line_col = _line_col;
    diag.err = msg;
    err_stack.push(diag);
  }

  // This function is used to cancel recent parsing error.
  void _PopError() {
    if (!err_stack.empty()) {
      err_stack.pop();
    }
  }

  bool _IsBuiltinMeta(std::string name) {
    return _builtin_metas.count(name) ? true : false;
  }

  void _RegisterBuiltinMeta() {
    _builtin_metas["doc"] = Variable("string", "doc");
    _builtin_metas["metersPerUnit"] = Variable("float", "metersPerUnit");
    _builtin_metas["defaultPrim"] = Variable("string", "defaultPrim");
    _builtin_metas["upAxis"] = Variable("string", "upAxis");
    _builtin_metas["timeCodesPerSecond"] =
        Variable("float", "timeCodesPerSecond");
    _builtin_metas["customLayerData"] = Variable("object", "customLayerData");
    _builtin_metas["test"] = Variable("int[]", "test");
    _builtin_metas["testt"] = Variable("int3", "testt");
    _builtin_metas["testf"] = Variable("float", "testf");
    _builtin_metas["testfa"] = Variable("float[]", "testfa");
    _builtin_metas["testfta"] = Variable("float3[]", "testfta");
  }

  void _RegisterNodeTypes() {
    _node_types.insert("Xform");
    _node_types.insert("Sphere");
    _node_types.insert("Mesh");
    _node_types.insert("Scope");
    _node_types.insert("Material");
    _node_types.insert("Shader");
    _node_types.insert("SphereLight");
    _node_types.insert("Camera");
  }

  const tinyusdz::StreamReader *_sr = nullptr;

  std::map<std::string, Variable> _builtin_metas;
  std::set<std::string> _node_types;
  std::set<std::string> _registered_prim_attr_types;

  std::stack<ErrorDiagnositc> err_stack;
  std::stack<ParseState> parse_stack;

  int _line_row{0};
  int _line_col{0};

  float _version{1.0f};
};

//
// Specializations
//
bool USDAParser::ReadBasicType(std::string *value) {
  return ReadStringLiteral(value);
}

bool USDAParser::ReadBasicType(bool *value) {
  std::stringstream ss;

  std::cout << "ReadBool\n";

  // '0' or '1'

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
    _PushError("'0' or '1' expected.\n");
    return false;
  }
}

bool USDAParser::ReadBasicType(int *value) {
  std::stringstream ss;

  // std::cout << "ReadInt\n";

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
      _PushError("Sign or 0-9 expected.\n");
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
    _PushError("Integer value expected but got sign character only.\n");
    return false;
  }

  if ((ss.str().size() > 1) && (ss.str()[0] == '0')) {
    _PushError("Zero padded integer value is not allowed.\n");
    return false;
  }

  // std::cout << "ReadInt token: " << ss.str() << "\n";

  // TODO(syoyo): Use ryu parse.
  try {
    (*value) = std::stoi(ss.str());
  } catch (const std::invalid_argument &e) {
    (void)e;
    _PushError("Not an integer literal.\n");
    return false;
  } catch (const std::out_of_range &e) {
    (void)e;
    _PushError("Integer value out of range.\n");
    return false;
  }

  // std::cout << "read int ok\n";

  return true;
}

bool USDAParser::ReadBasicType(float *value) {
  std::string value_str;
  std::string err;
  if (!LexFloat(&value_str, &err)) {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += err;
    }
    _PushError(msg);

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
    _PushError(msg);
    return false;
  }

  return true;
}

bool USDAParser::ReadBasicType(double *value) {
  std::string value_str;
  std::string err;
  if (!LexFloat(&value_str, &err)) {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += err;
    }
    _PushError(msg);

    return false;
  }

  auto flt = ParseDouble(value_str);
  if (!flt) {
    std::string msg = "Failed to parse float value literal.\n";
    msg += flt.error();
    _PushError(msg);
    return false;
  } else {
    (*value) = flt.value();
  }

  return true;
}

}  // namespace usda

}  // namespace tinyusdz

#if defined(USDA_MAIN)
int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usda\n";
    exit(-1);
  }

  std::string filename = argv[1];

  std::vector<uint8_t> data;
  {
    // TODO(syoyo): Support UTF-8 filename
    std::ifstream ifs(filename.c_str(), std::ifstream::binary);
    if (!ifs) {
      std::cerr << "Failed to open file: " << filename << "\n";
      return -1;
    }

    // TODO(syoyo): Use mmap
    ifs.seekg(0, ifs.end);
    size_t sz = static_cast<size_t>(ifs.tellg());
    if (int64_t(sz) < 0) {
      // Looks reading directory, not a file.
      std::cerr << "Looks like filename is a directory : \"" << filename
                << "\"\n";
      return -1;
    }

    data.resize(sz);

    ifs.seekg(0, ifs.beg);
    ifs.read(reinterpret_cast<char *>(&data.at(0)),
             static_cast<std::streamsize>(sz));
  }

  tinyusdz::StreamReader sr(data.data(), data.size(), /* swap endian */ false);
  tinyusdz::usda::USDAParser parser(&sr);

  {
    bool ret = parser.Parse();

    if (!ret) {
      std::cerr << "Failed to parse .usda: \n";
      std::cerr << parser.GetError() << "\n";
    } else {
      std::cout << "ok\n";
    }
  }

  return 0;
}
#endif
