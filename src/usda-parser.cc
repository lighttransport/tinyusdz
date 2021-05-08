// SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <vector>
#include <iterator>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#if 0  // TODO(syoyo): Use lexy
#include "lexy/dsl.hpp"
#include "lexy/input/file.hpp"
#include "lexy/lexeme.hpp"
#include "lexy/parse.hpp"
#include "report_error.hpp"
#endif

#include <ryu/ryu.h>
#include <ryu/ryu_parse.h>

#include <nonstd/variant.hpp>

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <simple-serialize.hh>
#include <stream-reader.hh>

#if 0
namespace grammar {

namespace dsl = lexy::dsl;
constexpr auto ws = dsl::ascii::blank;

struct string
{
  struct invalid_char
  {
    static LEXY_CONSTEVAL auto name() {
      return "invalid character in string literal";
    }
  };

  static constexpr auto rule = [] {
        auto code_point = dsl::try_<invalid_char>(dsl::code_point - dsl::ascii::control);
        auto escape     = dsl::backslash_escape //
                          .lit_c<'"'>()
                          .lit_c<'\\'>()
                          .lit_c<'/'>()
                          .lit_c<'b'>(dsl::value_c<'\b'>)
                          .lit_c<'f'>(dsl::value_c<'\f'>)
                          .lit_c<'n'>(dsl::value_c<'\n'>)
                          .lit_c<'r'>(dsl::value_c<'\r'>)
                          .lit_c<'t'>(dsl::value_c<'\t'>)
                          .rule(dsl::lit_c<'u'> >> dsl::code_point_id<4>);

        // String of code_point with specified escape sequences, surrounded by ".
        return dsl::quoted[ws](code_point, escape);

  }();

  static constexpr auto list = lexy::as_string<std::string, lexy::utf8_encoding>;
};

} // namespace grammer
#endif

namespace tinyusdz {

namespace usda {

// https://stackoverflow.com/questions/13777987/c-error-handling-downside-of-using-stdpair-or-stdtuple-for-returning-err
template <class T>
struct Result {
 public:
  enum Status { Success, Error };

  // Feel free to change the default behavior... I use implicit
  // constructors for type T for syntactic sugar in return statements.
  Result(T resultValue) : v(resultValue), s(Success) {}
  explicit Result(Status status, std::string _errMsg = std::string())
      : v(), s(status), errMsg(_errMsg) {}
  Result() : s(Error), v() {}  // Error without message

  // Explicit error with message
  static Result error(std::string errMsg) { return Result(Error, errMsg); }

  // Implicit conversion to type T
  operator T() const { return v; }
  // Explicit conversion to type T
  T value() const { return v; }

  Status status() const { return s; }
  bool isError() const { return s == Error; }
  bool isSuccessful() const { return s == Success; }
  std::string errorMessage() const { return errMsg; }

 private:
  T v;
  Status s;

  // if you want to provide error messages:
  std::string errMsg;
};

}  // namespace usda

static void test() {
  int i;
  bool b;
  uint32_t ui;
  int64_t i64;
  uint64_t ui64;
  float f;
  double d;
  char c;
  std::string s;
  std::map<std::string, int> map0;
  std::vector<float> fvec;
  std::list<float> flist;
  std::array<float, 3> fvec3;
  std::vector<std::array<float, 3>> float3v;
  std::vector<short> sv;

  simple_serialize::ObjectHandler h;
  h.add_property("i", &i);
  h.add_property("b", &b);
  h.add_property("ui", &ui);
  h.add_property("i64", &i64);
  h.add_property("ui64", &ui64);
  h.add_property("f", &f);
  h.add_property("d", &d);
  h.add_property("c", &c);
  h.add_property("s", &s);
  h.add_property("fvec", &fvec);
  h.add_property("flist", &flist);
  h.add_property("map0", &map0);
  h.add_property("fvec3", &fvec3);
  h.add_property("float3v", &float3v);

  // simple_serialize::Handler<double> dh(&d);
  // simple_serialize::Handler<std::vector<std::array<float, 3>>> dh(&float3v);
  // simple_serialize::Handler<std::map<std::string, int>> dh(&map0);
  simple_serialize::Handler<std::vector<short>> dh(&sv);
  simple_serialize::Parse test;

  // std::vector<std::array<float, 3>> ref = {{1,2,3}, {4,5,6}};
  // std::map<std::string, int> ref0;
  std::vector<short> ref1 = {1, 4, 5};

  bool ret = test.SetValue(ref1, dh);
  std::cout << "ret = " << ret << "\n";

  for (size_t x = 0; x < sv.size(); x++) {
    std::cout << "val = " << sv[x] << "\n";
  }

  // for (size_t x = 0; x < float3v.size(); x++) {
  //  std::cout << "val = " << float3v[x][0] << ", " << float3v[x][1] << ", " <<
  //  float3v[x][2] << "\n";
  //}
}

struct ErrorDiagnositc {
  std::string err;
  int line_row = -1;
  int line_col = -1;
};

struct Rel {
  // TODO: Implement
  std::string path;
};

using Value = nonstd::variant<int, float, std::string, Rel>;

class Variable {
 public:
  std::string type;
  std::string name;
  Value value;

  typedef std::map<std::string, Value> Object;
  Object object;

  bool IsObject() const {
    return type == "object";
  }

  Variable() = default;
  Variable(std::string ty, std::string n) : type(ty), name(n) {}
};

inline bool isChar(char c) { return std::isalpha(int(c)); }

inline bool startsWith(const std::string &str, const std::string &t) {
  return (str.size() >= t.size()) &&
         std::equal(std::begin(t), std::end(t), std::begin(str));
}

inline bool endsWith(const std::string &str, const std::string &suffix) {
  return (str.size() >= suffix.size()) &&
         (str.find(suffix, str.size() - suffix.size()) != std::string::npos);
}

inline bool contains(const std::string &str, char c)
{
  return str.find(c) == std::string::npos;
}

inline bool hasConnection(const std::string &str) {
  return endsWith(str, ".connection");
}

static usda::Result<float> ParseFloatR(const std::string &s) {
  float value;

  // Pase with Ryu.
  Status stat = s2f_n(s.data(), int(s.size()), &value);
  if (stat == SUCCESS) {
    return value;
  }

  if (stat == INPUT_TOO_SHORT) {
    return usda::Result<float>::error(
        "Input floating point literal is too short");
  } else if (stat == INPUT_TOO_LONG) {
    return usda::Result<float>::error(
        "Input floating point literal is too long");
  } else if (stat == MALFORMED_INPUT) {
    return usda::Result<float>::error("Malformed input floating point literal");
  }

  return usda::Result<float>::error("Unexpected floating point literal input");
}

static usda::Result<double> ParseDoubleR(const std::string &s) {
  double value;

  // Pase with Ryu.
  Status stat = s2d_n(s.data(), int(s.size()), &value);
  if (stat == SUCCESS) {
    return value;
  }

  if (stat == INPUT_TOO_SHORT) {
    return usda::Result<double>::error(
        "Input floating point literal is too short");
  } else if (stat == INPUT_TOO_LONG) {
    return usda::Result<double>::error(
        "Input floating point literal is too long");
  } else if (stat == MALFORMED_INPUT) {
    return usda::Result<double>::error(
        "Malformed input floating point literal");
  }

  return usda::Result<double>::error("Unexpected floating point literal input");
}

inline bool ParseFloat(const std::string &s, float *value, std::string *err) {
  //std::cout << "Parse float: " << s << "\n";
  // Pase with Ryu.
  Status stat = s2f_n(s.data(), int(s.size()), value);
  if (stat == SUCCESS) {
    return true;
  }

  if (stat == INPUT_TOO_SHORT) {
    (*err) = "Input floating point literal is too short\n";
  } else if (stat == INPUT_TOO_LONG) {
    (*err) = "Input floating point literal is too long\n";
  } else if (stat == MALFORMED_INPUT) {
    (*err) = "Malformed input floating point literal\n";
  }

  return false;
}

class USDAParser {
 public:
  struct ParseState {
    int64_t loc{-1};  // byte location in StreamReder
  };

  USDAParser(tinyusdz::StreamReader *sr) : _sr(sr) {
    _RegisterBuiltinMeta();
    _RegisterNodeTypes();
    _RegisterPrimAttrTypes();

    // HACK
    test();
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
      //std::cout << "1 read int part: ss = " << ss.str() << "\n";

      while (!_sr->eof()) {
        if (!_sr->read1(&curr)) {
          return false;
        }

        //std::cout << "1 curr = " << curr << "\n";
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

    //std::cout << "before 2: ss = " << ss.str() << ", curr = " << curr << "\n";

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

  bool ParseInterpolation(std::string *interpolation) {
    // '(' 'interpolation' '=' STRING_LITERAL ')'

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

    if (token != "interpolation") {
      _PushError("Currently only `interpolation` is supported.\n");
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

    (*interpolation) = value;

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

  bool ParsePrimAttr() {
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
      if (!uniform_qual) {
        _PushError("`uniform` qualifier is missing in type `bool`\n");
        return false;
      }

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
          std::string value;
          if (!ReadStringLiteral(&value)) {
            _PushError("Failed to parse string literal for `uniform token`.\n");
          }
          std::cout << "StringLiteral = " << value << "\n";
        } else {
          std::string value; // TODO: Path
          if (!ReadPathIdentifier(&value)) {
            _PushError("Failed to parse path identifier for `token`.\n");
          }
          std::cout << "Path identifier = " << value << "\n";

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
      std::string interpolation;
      ParseInterpolation(&interpolation);

      std::cout << "interpolation: " << interpolation << "\n";

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
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse float3.\n");
        }
        std::cout << "float3 = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }
    } else if (type_name == "color3f") {
      if (array_qual) {
        std::vector<std::array<float, 3>> value;
        if (!ParseTupleArray(&value)) {
          _PushError("Failed to parse color3f array.\n");
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
        }
        std::cout << "color3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      // optional:
      ParsePrimOptional();

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
      } else {
        std::array<float, 3> value;
        if (!ParseBasicTypeTuple<float, 3>(&value)) {
          _PushError("Failed to parse normal3f.\n");
        }
        std::cout << "normal3f = (" << value[0] << ", " << value[1] << ", "
                  << value[2] << ")\n";
      }

      // optional: interpolation parameter
      std::string interpolation;
      ParseInterpolation(&interpolation);

      std::cout << "interpolation: " << interpolation << "\n";

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

      // optional: interpolation parameter
      std::string interpolation;
      ParseInterpolation(&interpolation);

      std::cout << "interpolation: " << interpolation << "\n";

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

    //std::cout << "sep: " << sep << "\n";

    while (!_sr->eof()) {
      // sep
      if (!SkipWhitespaceAndNewline()) {
        //std::cout << "ws failure\n";
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        std::cout << "read1 failure\n";
        return false;
      }

      //std::cout << "sep c = " << c << "\n";

      if (c != sep) {
        // end
        //std::cout << "sepBy1 end\n";
        _sr->seek_from_current(-1);  // unwind single char
        break;
      }

      if (!SkipWhitespaceAndNewline()) {
        //std::cout << "ws failure\n";
        return false;
      }

      //std::cout << "go to read int\n";

      T value;
      if (!ReadBasicType(&value)) {
        break;
      }

      result->push_back(value);
    }

    //std::cout << "result.size " << result->size() << "\n";

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
      // sep
      if (!SkipWhitespaceAndNewline()) {
        //std::cout << "ws failure\n";
        return false;
      }

      char c;
      if (!_sr->read1(&c)) {
        //std::cout << "read1 failure\n";
        return false;
      }

      //std::cout << "sep c = " << c << "\n";

      if (c != sep) {
        // end
        //std::cout << "sepBy1 end\n";
        _sr->seek_from_current(-1);  // unwind single char
        break;
      }

      if (!SkipWhitespaceAndNewline()) {
        //std::cout << "ws failure\n";
        return false;
      }

      //std::cout << "go to read int\n";

      std::array<T, N> value;
      if (!ParseBasicTypeTuple<T, N>(&value)) {
        break;
      }

      result->push_back(value);
    }

    //std::cout << "result.size " << result->size() << "\n";

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
    //std::cout << "got [\n";

    if (!SepBy1BasicType<T>(',', result)) {
      return false;
    }

    //std::cout << "try to parse ]\n";

    if (!Expect(']')) {
      //std::cout << "not ]\n";

      return false;
    }
    //std::cout << "got ]\n";

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
    //std::cout << "got (\n";

    std::vector<T> values;
    if (!SepBy1BasicType<T>(',', &values)) {
      return false;
    }

    //std::cout << "try to parse )\n";

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
      } else if (c == ':') { // namespace
        // ':' must lie in the middle of string literal
        if (ss.str().size() == 0) {
          _PushError("PrimAttr name must not starts with `:`\n");
          return false;
        }
      } else if (c == '.') { // delimiter for `connect`
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
        _PushError("Must ends with `.connect` when a name contains punctuation `.`");
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

    std::cout << "readtoken\n";

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
    std::cout << "ReadIdentifier: token = " << (*token) << "\n";
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

      //printf("sws c = %c\n", c);

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
        //std::cout << "unwind\n";
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

  bool ParseMetaValue(const Variable &var) {
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
      std::string msg = "'" + varname + "' is not a builtin Metadata variable.\n";
      _PushError(msg);
      return false;
    }

    if (!Expect('=')) {
      _PushError("'=' expected in Metadata line.\n");
      return false;
    }
    SkipWhitespace();

    const Variable &var = _builtin_metas.at(varname);
    if (!ParseMetaValue(var)) {
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
          if (!ParsePrimAttr()) {
            return false;
          }
        }

        if (!SkipWhitespaceAndNewline()) {
          return false;
        }
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

    bool block_ok = ParseDefBlock();
    if (!block_ok) {
      _PushError("Failed to parse `def` block.\n");
      return false;
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

  bool _IsBuiltinMeta(std::string name) {
    return _builtin_metas.count(name) ? true : false;
  }

  void _RegisterBuiltinMeta() {
    _builtin_metas["doc"] = Variable("string", "doc");
    _builtin_metas["metersPerUnit"] = Variable("float", "metersPerUnit");
    _builtin_metas["defaultPrim"] = Variable("string", "defaultPrim");
    _builtin_metas["upAxis"] = Variable("string", "upAxis");
    _builtin_metas["timeCodesPerSecond"] = Variable("float", "timeCodesPerSecond");
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

  //std::cout << "ReadInt\n";

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

  //std::cout << "ReadInt token: " << ss.str() << "\n";

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

  //std::cout << "read int ok\n";

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

#if 0
    if (!ParseFloat(value_str, value, &err)) {
      std::string msg = "Failed to parse float value literal.\n";
      if (err.size()) {
        msg += err;
      }
      _PushError(msg);
      return false;
    }
#else
  usda::Result<float> flt = ParseFloatR(value_str);
  if (flt.isSuccessful()) {
    (*value) = flt.value();
  } else {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += flt.errorMessage() + "\n";
    }
    _PushError(msg);
    return false;
  }
#endif

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

#if 0
    if (!ParseFloat(value_str, value, &err)) {
      std::string msg = "Failed to parse float value literal.\n";
      if (err.size()) {
        msg += err;
      }
      _PushError(msg);
      return false;
    }
#else
  usda::Result<double> flt = ParseDoubleR(value_str);
  if (flt.isSuccessful()) {
    (*value) = flt.value();
  } else {
    std::string msg = "Failed to parse float value literal.\n";
    if (err.size()) {
      msg += flt.errorMessage() + "\n";
    }
    _PushError(msg);
    return false;
  }
#endif

  return true;
}


}  // namespace tinyusdz

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cout << "Need input.usda\n";
    exit(-1);
  }

  std::string filename = argv[1];

#if 0
  // lexy test
  {
    auto file = lexy::read_file<lexy::utf8_encoding>(argv[1]);
    if (!file)
    {
        std::fprintf(stderr, "file '%s' not found", argv[1]);
        return 1;
    }
  }
#endif

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
  tinyusdz::USDAParser parser(&sr);

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
