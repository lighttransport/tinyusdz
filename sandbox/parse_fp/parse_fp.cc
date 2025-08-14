#include <vector>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>

#include <random>

#include "fast_float/fast_float.h"

std::string gen_floatarray(size_t n, bool delim_at_end) {

  std::stringstream ss;

  std::random_device rd;

  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    double f = dist(engine);
    ss << std::to_string(f);
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

std::string gen_float2array(size_t n, bool delim_at_end) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    double f1 = dist(engine);
    double f2 = dist(engine);
    ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ")";
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

std::string gen_float3array(size_t n, bool delim_at_end) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    double f1 = dist(engine);
    double f2 = dist(engine);
    double f3 = dist(engine);
    ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ", " << std::to_string(f3) << ")";
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

std::string gen_float4array(size_t n, bool delim_at_end) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    double f1 = dist(engine);
    double f2 = dist(engine);
    double f3 = dist(engine);
    double f4 = dist(engine);
    ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ", " << std::to_string(f3) << ", " << std::to_string(f4) << ")";
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

std::string gen_matrix3d_array(size_t n, bool delim_at_end) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    ss << "(";
    for (int j = 0; j < 9; j++) {
      double f = dist(engine);
      ss << std::to_string(f);
      if (j < 8) {
        ss << ", ";
      }
    }
    ss << ")";
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

std::string gen_matrix4d_array(size_t n, bool delim_at_end) {
  std::stringstream ss;
  std::random_device rd;
  std::mt19937 engine(rd());
  std::uniform_real_distribution<> dist(-10000.0, 10000.0);

  ss << "[";
  for (size_t i = 0; i < n; i++) {
    ss << "(";
    for (int j = 0; j < 16; j++) {
      double f = dist(engine);
      ss << std::to_string(f);
      if (j < 15) {
        ss << ", ";
      }
    }
    ss << ")";
    if (delim_at_end) {
      ss << ",";
    } else if (i < (n-1)) {
      ss << ",";
    }
  }
  ss << "]";

  return ss.str();
}

struct Lexer {

  void init(const char *_p_begin, const char *_p_end, size_t row = 0, size_t column = 0) {
    p_begin = _p_begin;
    p_end = _p_end;
    curr = p_begin;
    row_ = row;
    column_ = column;
  }

  void skip_whitespaces() {

    while (!eof()) {

      char s = *curr;
      if ((s == ' ') || (s == '\t') || (s == '\f') || (s == '\n') || (s == '\r') || (s == '\v')) {
        curr++;
        column_++;

        if (s == '\r') {

          // '\r\n'?
          if (!eof()) {
            char c{'\0'};
            look_char1(&c);
            if (c == '\n') {
              curr++;
            }
          } 
          row_++;
          column_ = 0;
        } else if (s == '\n') {
          row_++;
          column_ = 0;
        }
      }
      break;
    }   

  }

  bool skip_until_delim_or_close_paren(const char delim, const char close_paren) {

    while (!eof()) {

      char s = *curr;
      if ((s == delim) || (s == close_paren)) {
        return true;
      }

      curr++;
        column_++;

        if (s == '\r') {

          // '\r\n'?
          if (!eof()) {
            char c{'\0'};
            look_char1(&c);
            if (c == '\n') {
              curr++;
            }
          } 
          row_++;
          column_ = 0;
        } else if (s == '\n') {
          row_++;
          column_ = 0;
        }
    }   

    return false;
  }

  bool char1(char *result) {
    if (eof()) {
      return false;
    }
    *result = *curr;
    curr++;

    column_++;

    if ((*result == '\r') || (*result == '\n')) {
       row_++;
       column_ = 0;
     }

    return true;
  }

  bool look_char1(char *result) {
    if (eof()) {
      return false;
    }
    *result = *curr;

    return true;
  }

  bool consume_char1() {
    if (eof()) {
      return false;
    }
    char c = *curr;
    curr++;

    if ((c == '\r') || (c == '\n')) {
       row_++;
       column_ = 0;
     }

    return true;
  }

  inline bool eof() const {
    return (curr >= p_end);
  }

#if 0
  inline bool unwind_char1() {
    if (curr <= p_begin) {
      return false;
    }

    curr--;
    return true;
  }
#endif

  bool lex_float(uint16_t &len, bool &truncated) {

    // truncate too large fp string
    // (e.g. "0.100000010000000100000010000..."
    constexpr size_t n_trunc_chars = 256; // 65535 at max.

    size_t n = 0;
    bool has_sign = false;
    bool has_exponential = false;
    bool has_dot = false;

    // oneOf [0-9, eE, -+]
    while (!eof() || (n < n_trunc_chars)) {
      char c;
      look_char1(&c);
      if ((c == '-') || (c == '+')) {
        if (has_sign) {
          return false;
        }
        has_sign = true;
      } else if (c == '.') {
        if (has_dot) {
          return false;
        }
        has_dot = true;
      } else if ((c == 'e') || (c == 'E')) {
        if (has_exponential) {
          return false;
        }
        has_exponential = true;
      } else if ((c >= '0') && (c <= '9')) {
      } else {
        break;
      }

      consume_char1();
      n++;
    }

    if (n == 0) {
      len = 0;
      return false;
    }

    truncated = (n >= n_trunc_chars);

    len = uint16_t(n);
    return true;
  }

  void push_error(const std::string &msg) {
    err_ += msg + " (near line " + std::to_string(row_) + ", column " + std::to_string(column_) + ")\n";
  }

  std::string get_error() const {
    return err_;
  }

  const char *p_begin{nullptr};
  const char *p_end{nullptr};

  const char *curr{nullptr};

  size_t row_{0};
  size_t column_{0};

 private:
  std::string err_;
};


struct fp_lex_span
{
  const char *p_begin{nullptr};
  uint16_t length{0};
};

template<size_t N>
struct vec_lex_span
{
  fp_lex_span vspans[N];
};

struct matrix3d_lex_span
{
  fp_lex_span mspans[9];  // 3x3 = 9 elements
};

struct matrix4d_lex_span
{
  fp_lex_span mspans[16]; // 4x4 = 16 elements
};

// '[' + fp0 + "," + fp1 + ", " ... ']'
// allow_delim_at_last is true: '[' + fp0 + "," + fp1 + ", " ... "," + ']'
bool lex_float_array(
  const char *p_begin,
  const char *p_end,
  std::vector<fp_lex_span> &result,
  std::string &err,
  const bool allow_delim_at_last = true,
  const char delim = ',',
  const char open_paren = '[',
  const char close_paren = ']') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
  
    return false;
  }

  Lexer lexer;
  lexer.p_begin = p_begin;
  lexer.p_end = p_end;
  lexer.curr = p_begin;

  
  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    fp_lex_span sp;
    sp.p_begin = lexer.curr;

    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Input is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    sp.length = length;

    if (truncated) {
      // skip until encountering delim or close_paren.
      if (!lexer.skip_until_delim_or_close_paren(delim, close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }
  

    result.emplace_back(std::move(sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool lex_vec2_array(
  Lexer &lexer,
  std::string &err,
  vec_lex_span<2> &result,
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  // '('
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short for vector.\n";
      return false;
    }

    if (c != vec_open_paren) {
      err = "Vector does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse first float
  {
    result.vspans[0].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("First element is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.vspans[0].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', vec_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }
  }

  lexer.skip_whitespaces();

  // ','
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected comma delimiter.\n";
      return false;
    }

    if (c != ',') {
      err = "Expected comma delimiter between vector elements.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse second float
  {
    result.vspans[1].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Second element is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.vspans[1].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', vec_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }
  }

  lexer.skip_whitespaces();

  // ')'
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected closing parenthesis.\n";
      return false;
    }

    if (c != vec_close_paren) {
      err = "Expected closing parenthesis for vector.\n";
      return false;
    }
  }

  return true;
}

bool lex_vec3_array(
  Lexer &lexer,
  std::string &err,
  vec_lex_span<3> &result,
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  // '('
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short for vector.\n";
      return false;
    }

    if (c != vec_open_paren) {
      err = "Vector does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse three floats
  for (int i = 0; i < 3; i++) {
    result.vspans[i].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Element " + std::to_string(i) + " is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.vspans[i].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', vec_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }

    lexer.skip_whitespaces();

    if (i < 2) {  // Need comma after first two elements
      char c;
      if (!lexer.char1(&c)) {
        err = "Expected comma delimiter.\n";
        return false;
      }

      if (c != ',') {
        err = "Expected comma delimiter between vector elements.\n";
        return false;
      }

      lexer.skip_whitespaces();
    }
  }

  // ')'
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected closing parenthesis.\n";
      return false;
    }

    if (c != vec_close_paren) {
      err = "Expected closing parenthesis for vector.\n";
      return false;
    }
  }

  return true;
}

bool lex_vec4_array(
  Lexer &lexer,
  std::string &err,
  vec_lex_span<4> &result,
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  // '('
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short for vector.\n";
      return false;
    }

    if (c != vec_open_paren) {
      err = "Vector does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse four floats
  for (int i = 0; i < 4; i++) {
    result.vspans[i].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Element " + std::to_string(i) + " is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.vspans[i].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', vec_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }

    lexer.skip_whitespaces();

    if (i < 3) {  // Need comma after first three elements
      char c;
      if (!lexer.char1(&c)) {
        err = "Expected comma delimiter.\n";
        return false;
      }

      if (c != ',') {
        err = "Expected comma delimiter between vector elements.\n";
        return false;
      }

      lexer.skip_whitespaces();
    }
  }

  // ')'
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected closing parenthesis.\n";
      return false;
    }

    if (c != vec_close_paren) {
      err = "Expected closing parenthesis for vector.\n";
      return false;
    }
  }

  return true;
}

bool lex_matrix3d_array(
  Lexer &lexer,
  std::string &err,
  matrix3d_lex_span &result,
  const char matrix_open_paren = '(',
  const char matrix_close_paren = ')') {

  // '('
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short for matrix3d.\n";
      return false;
    }

    if (c != matrix_open_paren) {
      err = "Matrix3d does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse nine floats
  for (int i = 0; i < 9; i++) {
    result.mspans[i].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Element " + std::to_string(i) + " is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.mspans[i].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', matrix_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }

    lexer.skip_whitespaces();

    if (i < 8) {  // Need comma after first eight elements
      char c;
      if (!lexer.char1(&c)) {
        err = "Expected comma delimiter.\n";
        return false;
      }

      if (c != ',') {
        err = "Expected comma delimiter between matrix elements.\n";
        return false;
      }

      lexer.skip_whitespaces();
    }
  }

  // ')'
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected closing parenthesis.\n";
      return false;
    }

    if (c != matrix_close_paren) {
      err = "Expected closing parenthesis for matrix3d.\n";
      return false;
    }
  }

  return true;
}

bool lex_matrix4d_array(
  Lexer &lexer,
  std::string &err,
  matrix4d_lex_span &result,
  const char matrix_open_paren = '(',
  const char matrix_close_paren = ')') {

  // '('
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short for matrix4d.\n";
      return false;
    }

    if (c != matrix_open_paren) {
      err = "Matrix4d does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  // Parse sixteen floats
  for (int i = 0; i < 16; i++) {
    result.mspans[i].p_begin = lexer.curr;
    uint16_t length{0};
    bool truncated{false};

    if (!lexer.lex_float(length, truncated)) {
      lexer.push_error("Element " + std::to_string(i) + " is not a floating point literal.");
      err = lexer.get_error();
      return false;
    }

    result.mspans[i].length = length;

    if (truncated) {
      if (!lexer.skip_until_delim_or_close_paren(',', matrix_close_paren)) {
        lexer.push_error("Failed to seek to delimiter or closing parenthesis character.");
        err = lexer.get_error();
        return false;
      }
    }

    lexer.skip_whitespaces();

    if (i < 15) {  // Need comma after first fifteen elements
      char c;
      if (!lexer.char1(&c)) {
        err = "Expected comma delimiter.\n";
        return false;
      }

      if (c != ',') {
        err = "Expected comma delimiter between matrix elements.\n";
        return false;
      }

      lexer.skip_whitespaces();
    }
  }

  // ')'
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Expected closing parenthesis.\n";
      return false;
    }

    if (c != matrix_close_paren) {
      err = "Expected closing parenthesis for matrix4d.\n";
      return false;
    }
  }

  return true;
}


bool lex_float2_array(
  const char *p_begin,
  const char *p_end,
  std::vector<vec_lex_span<2>> &result,
  std::string &err,
  bool allow_delim_at_last = true, 
  const char delim = ',', 
  const char arr_open_paren = '[',
  const char arr_close_paren = ']',
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
  
    return false;
  }

  Lexer lexer;
  lexer.init(p_begin, p_end);

  
  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != arr_open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == arr_close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    vec_lex_span<2> v_sp;
    if (!lex_vec2_array(lexer, err, v_sp)) {
      err = lexer.get_error();
      return false;
    }

    result.emplace_back(std::move(v_sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool lex_float3_array(
  const char *p_begin,
  const char *p_end,
  std::vector<vec_lex_span<3>> &result,
  std::string &err,
  bool allow_delim_at_last = true, 
  const char delim = ',', 
  const char arr_open_paren = '[',
  const char arr_close_paren = ']',
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
    return false;
  }

  Lexer lexer;
  lexer.init(p_begin, p_end);

  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != arr_open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == arr_close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    vec_lex_span<3> v_sp;
    if (!lex_vec3_array(lexer, err, v_sp, vec_open_paren, vec_close_paren)) {
      err = lexer.get_error();
      return false;
    }

    result.emplace_back(std::move(v_sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool lex_float4_array(
  const char *p_begin,
  const char *p_end,
  std::vector<vec_lex_span<4>> &result,
  std::string &err,
  bool allow_delim_at_last = true, 
  const char delim = ',', 
  const char arr_open_paren = '[',
  const char arr_close_paren = ']',
  const char vec_open_paren = '(',
  const char vec_close_paren = ')') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
    return false;
  }

  Lexer lexer;
  lexer.init(p_begin, p_end);

  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != arr_open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == arr_close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    vec_lex_span<4> v_sp;
    if (!lex_vec4_array(lexer, err, v_sp, vec_open_paren, vec_close_paren)) {
      err = lexer.get_error();
      return false;
    }

    result.emplace_back(std::move(v_sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool lex_matrix3d_array_parser(
  const char *p_begin,
  const char *p_end,
  std::vector<matrix3d_lex_span> &result,
  std::string &err,
  bool allow_delim_at_last = true, 
  const char delim = ',', 
  const char arr_open_paren = '[',
  const char arr_close_paren = ']',
  const char matrix_open_paren = '(',
  const char matrix_close_paren = ')') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
    return false;
  }

  Lexer lexer;
  lexer.init(p_begin, p_end);

  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != arr_open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == arr_close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    matrix3d_lex_span m_sp;
    if (!lex_matrix3d_array(lexer, err, m_sp, matrix_open_paren, matrix_close_paren)) {
      err = lexer.get_error();
      return false;
    }

    result.emplace_back(std::move(m_sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool lex_matrix4d_array_parser(
  const char *p_begin,
  const char *p_end,
  std::vector<matrix4d_lex_span> &result,
  std::string &err,
  bool allow_delim_at_last = true, 
  const char delim = ',', 
  const char arr_open_paren = '[',
  const char arr_close_paren = ']',
  const char matrix_open_paren = '(',
  const char matrix_close_paren = ')') {

  if (p_begin >= p_end) {
    err = "Invalid input\n";
    return false;
  }

  Lexer lexer;
  lexer.init(p_begin, p_end);

  // '['
  {
    char c;
    if (!lexer.char1(&c)) {
      err = "Input too short.\n";
      return false;
    }

    if (c != arr_open_paren) {
      err = "Input does not begin with open parenthesis character.\n";
      return false;
    }
  }

  lexer.skip_whitespaces();

  while (!lexer.eof()) {

    bool prev_is_delim = false;

    // is ','?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Invalid character found.");
        err = lexer.get_error();
        return false;
      } 

      if (c == delim) {
        // Array element starts with delimiter, e.g. '[ ,'
        if (result.empty()) {
          lexer.push_error("Array element starts with the delimiter character.");
          err = lexer.get_error();
          return false;
        }
        prev_is_delim = true;
        lexer.consume_char1();
      }

      lexer.skip_whitespaces();
    }

    // is ']'?
    {
      char c;
      if (!lexer.look_char1(&c)) {
        lexer.push_error("Failed to read a character.");
        err = lexer.get_error();
        return false;
      }

      if (c == arr_close_paren) {
        if (prev_is_delim) {
          if (allow_delim_at_last) {
            // ok
            return true;
          } else {
            lexer.push_error("Delimiter character is not allowed before the closing parenthesis character.");
            err = lexer.get_error();
            return false;
          }
        } else {
          // ok
          return true;
        }
      }
    }

    matrix4d_lex_span m_sp;
    if (!lex_matrix4d_array(lexer, err, m_sp, matrix_open_paren, matrix_close_paren)) {
      err = lexer.get_error();
      return false;
    }

    result.emplace_back(std::move(m_sp));

    lexer.skip_whitespaces();
  }

  return true;
}

bool do_parse(
  uint32_t nthreads,
  const std::vector<fp_lex_span> &spans,
  std::vector<double> &results) {

  auto start = std::chrono::steady_clock::now();

  results.resize(spans.size());

  if (spans.size() > (1024*128)) {

    nthreads = (std::min)((std::max)(1u, nthreads), 256u);

    std::mutex mutex;
    std::atomic<size_t> cnt(0);
    std::atomic<bool> parse_failed{false};
    std::vector<std::thread> threads;

    for (uint32_t i = 0; i < nthreads; i++) {
      threads.emplace_back(std::thread([&] {

        size_t j; 

        while ((j = cnt++) < results.size()) {

          double fp;
          auto answer = fast_float::from_chars(spans[i].p_begin, spans[i].p_begin + spans[i].length, fp);
          if (answer.ec != std::errc()) { parse_failed = true; }

          results[j] = fp;
        }

      }));

    }

    for (auto &&th : threads) {
      th.join();
    }

    if (parse_failed) {
      std::cerr << "parsing failure\n";
      return false;
    }

  } else {

    for (size_t i = 0; i < spans.size(); i++) {

      //std::string s(spans[i].p_begin, spans[i].length);
      double fp;
      auto answer = fast_float::from_chars(spans[i].p_begin, spans[i].p_begin + spans[i].length, fp);
      if (answer.ec != std::errc()) { std::cerr << "parsing failure\n"; return false; }

      results[i] = fp;
    }
  }
  auto end = std::chrono::steady_clock::now();

  std::cout << "n threasd: " << nthreads << "\n";
  std::cout << "n elems: " << spans.size() << "\n";
  std::cout << "parse time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";

  return true;
}

int main(int argc, char **argv)
{
  uint32_t nthreads = 1;
  bool delim_at_end = true;
  size_t n = 1024*16;  // Smaller default for testing
  int test_type = 0;  // 0: float, 1: float2, 2: float3, 3: float4, 4: matrix3d, 5: matrix4d
  
  if (argc > 1) {
    n = std::stoi(argv[1]);
  }
  if (argc > 2) {
    delim_at_end = std::stoi(argv[2]) > 0;
  }
  if (argc > 3) {
    nthreads = std::stoi(argv[3]);
  }
  if (argc > 4) {
    test_type = std::stoi(argv[4]);
  }

  std::cout << "Testing ";
  switch (test_type) {
    case 0: std::cout << "float array"; break;
    case 1: std::cout << "float2 array"; break;
    case 2: std::cout << "float3 array"; break;
    case 3: std::cout << "float4 array"; break;
    case 4: std::cout << "matrix3d array"; break;
    case 5: std::cout << "matrix4d array"; break;
    default: std::cout << "float array (default)"; test_type = 0; break;
  }
  std::cout << " with " << n << " elements\n";

  if (test_type == 0) {
    // Test float array (original)
    std::vector<fp_lex_span> lex_results;
    lex_results.reserve(n);

    std::string input = gen_floatarray(n, delim_at_end);
    auto start = std::chrono::steady_clock::now();

    std::string err;
    if (!lex_float_array(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n elems " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";

    std::vector<double> parse_results;
    parse_results.reserve(n);
    do_parse(nthreads, lex_results, parse_results);
    
  } else if (test_type == 1) {
    // Test float2 array
    std::vector<vec_lex_span<2>> lex_results;
    lex_results.reserve(n);

    std::string input = gen_float2array(n, delim_at_end);
    std::cout << "Sample input: " << input.substr(0, 200) << "...\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string err;
    if (!lex_float2_array(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n float2 vectors: " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";
    
  } else if (test_type == 2) {
    // Test float3 array
    std::vector<vec_lex_span<3>> lex_results;
    lex_results.reserve(n);

    std::string input = gen_float3array(n, delim_at_end);
    std::cout << "Sample input: " << input.substr(0, 200) << "...\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string err;
    if (!lex_float3_array(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n float3 vectors: " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";
    
  } else if (test_type == 3) {
    // Test float4 array
    std::vector<vec_lex_span<4>> lex_results;
    lex_results.reserve(n);

    std::string input = gen_float4array(n, delim_at_end);
    std::cout << "Sample input: " << input.substr(0, 200) << "...\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string err;
    if (!lex_float4_array(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n float4 vectors: " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";
    
  } else if (test_type == 4) {
    // Test matrix3d array
    std::vector<matrix3d_lex_span> lex_results;
    lex_results.reserve(n);

    std::string input = gen_matrix3d_array(n, delim_at_end);
    std::cout << "Sample input: " << input.substr(0, 200) << "...\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string err;
    if (!lex_matrix3d_array_parser(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n matrix3d matrices: " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";
    
  } else if (test_type == 5) {
    // Test matrix4d array
    std::vector<matrix4d_lex_span> lex_results;
    lex_results.reserve(n);

    std::string input = gen_matrix4d_array(n, delim_at_end);
    std::cout << "Sample input: " << input.substr(0, 200) << "...\n";
    
    auto start = std::chrono::steady_clock::now();
    std::string err;
    if (!lex_matrix4d_array_parser(input.c_str(), input.c_str() + input.size(), lex_results, err)) {
      std::cerr << "parse error\n";
      std::cerr << err << "\n";
      return -1;
    }
    auto end = std::chrono::steady_clock::now();

    std::cout << "n matrix4d matrices: " << lex_results.size() << "\n";
    std::cout << "size " << input.size() << "\n";
    std::cout << "lex time: " << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() << " [ms]\n";
  }

  return 0;
}
