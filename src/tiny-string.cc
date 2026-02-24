// SPDX-License-Identifier: MIT
// Copyright 2024-Present Light Transport Entertainment Inc.
#include "tiny-string.hh"
#include "value-types.hh"  // For complete matrix type definitions

#if defined(TINYUSDZ_USE_THREAD)
#include <thread>
#include <atomic>
#include <mutex>
#endif

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/fast_float/include/fast_float/fast_float.h"

#define nssv_CONFIG_USR_SV_OPERATOR  0

// TODO(syoyo): Use C++17 std::string_view when compiled with C++-17 compiler

// clang and gcc
#if defined(__EXCEPTIONS) || defined(__cpp_exceptions)

#ifdef nsel_CONFIG_NO_EXCEPTIONS
#undef nsel_CONFIG_NO_EXCEPTIONS
#endif
#ifdef nssv_CONFIG_NO_EXCEPTIONS
#undef nssv_CONFIG_NO_EXCEPTIONS
#endif

#define nsel_CONFIG_NO_EXCEPTIONS 0
#define nssv_CONFIG_NO_EXCEPTIONS 0
#else
// -fno-exceptions
#if !defined(nsel_CONFIG_NO_EXCEPTIONS)
#define nsel_CONFIG_NO_EXCEPTIONS 1
#endif

#define nssv_CONFIG_NO_EXCEPTIONS 1
#endif
#include "nonstd/string_view.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

namespace str {

struct Lexer {

  void skip_whitespaces() {

    while (!eof()) {

      char s = *curr;
      if ((s == ' ') || (s == '\t') || (s == '\f') || (s == '\n') || (s == '\r') || (s == '\v')) {
        curr++;
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
    }   

    return false;
  }

  bool char1(char *result) {
    if (eof()) {
      return false;
    }
    *result = *curr;
    curr++;

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
    curr++;

    return true;
  }

  inline bool eof() const {
    return (curr >= p_end);
  }

  inline bool unwind_char1() {
    if (curr <= p_begin) {
      return false;
    }

    curr--;
    return true;
  }

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
    err_ += msg + "\n";
  }

  std::string get_error() const {
    return err_;
  }

  const char *p_begin{nullptr};
  const char *p_end{nullptr};

  const char *curr{nullptr};

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

namespace internal {

#if 0
// '[' + fp0 + "," + fp1 + ", " ... ']'
// allow_delim_at_last is true: '[' + fp0 + "," + fp1 + ", " ... "," + ']'
static bool lex_float_array(
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
#endif

} // namespace internal

bool parse_int(const tstring_view &sv, int32_t *ret) {
  const char* str = sv.c_str();
  size_t len = sv.size();
  
  if (len == 0) {
    return false;
  }
  
  bool negative = false;
  size_t start = 0;
  
  if (str[0] == '-') {
    negative = true;
    start = 1;
  } else if (str[0] == '+') {
    start = 1;
  }
  
  if (start >= len) {
    return false;
  }
  
  int64_t result = 0;
  for (size_t i = start; i < len; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return false;
    }
    result = result * 10 + (str[i] - '0');
    
    // Check for overflow
    if (negative && result > static_cast<int64_t>(std::numeric_limits<int32_t>::max()) + 1) {
      return false;
    }
    if (!negative && result > std::numeric_limits<int32_t>::max()) {
      return false;
    }
  }
  
  *ret = static_cast<int32_t>(negative ? -result : result);
  return true;
}

bool parse_int64(const tstring_view &sv, int64_t *ret) {
  const char* str = sv.c_str();
  size_t len = sv.size();
  
  if (len == 0) {
    return false;
  }
  
  bool negative = false;
  size_t start = 0;
  
  if (str[0] == '-') {
    negative = true;
    start = 1;
  } else if (str[0] == '+') {
    start = 1;
  }
  
  if (start >= len) {
    return false;
  }
  
  uint64_t result = 0;
  for (size_t i = start; i < len; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return false;
    }
    result = result * 10ull + uint64_t(str[i] - '0');
    
    // Check for overflow
    if (negative && result > static_cast<uint64_t>(std::numeric_limits<int64_t>::max()) + 1) {
      return false;
    }
    if (!negative && result > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
      return false;
    }
  }
  
  *ret = negative ? -static_cast<int64_t>(result) : static_cast<int64_t>(result);
  return true;
}

bool parse_uint(const tstring_view &sv, uint32_t *ret) {
  const char* str = sv.c_str();
  size_t len = sv.size();
  
  if (len == 0) {
    return false;
  }
  
  size_t start = 0;
  if (str[0] == '+') {
    start = 1;
  }
  
  if (start >= len) {
    return false;
  }
  
  uint64_t result = 0;
  for (size_t i = start; i < len; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return false;
    }
    result = result * 10 + uint64_t(str[i] - '0');
    
    // Check for overflow
    if (result > std::numeric_limits<uint32_t>::max()) {
      return false;
    }
  }
  
  *ret = static_cast<uint32_t>(result);
  return true;
}

bool parse_uint64(const tstring_view &sv, uint64_t *ret) {
  const char* str = sv.c_str();
  size_t len = sv.size();
  
  if (len == 0) {
    return false;
  }
  
  size_t start = 0;
  if (str[0] == '+') {
    start = 1;
  }
  
  if (start >= len) {
    return false;
  }
  
  uint64_t result = 0;
  for (size_t i = start; i < len; i++) {
    if (str[i] < '0' || str[i] > '9') {
      return false;
    }
    
    // Check for overflow before multiplication
    if (result > (std::numeric_limits<uint64_t>::max() - uint64_t(str[i] - '0')) / 10) {
      return false;
    }
    
    result = result * 10 + uint64_t(str[i] - '0');
  }
  
  *ret = result;
  return true;
}

bool parse_float(const tstring_view &sv, float *ret) {
  auto result = fast_float::from_chars(sv.c_str(), sv.c_str() + sv.size(), *ret);
  return result.ec == std::errc{};
}

bool parse_double(const tstring_view &sv, double *ret) {
  auto result = fast_float::from_chars(sv.c_str(), sv.c_str() + sv.size(), *ret);
  return result.ec == std::errc{};
}

bool parse_float_array(const tstring_view &sv, std::vector<float> *result, const char delimiter) {
  if (!result) {
    return false;
  }

  result->clear();

  if (sv.size() == 0) {
    return false;
  }

  const char *p = sv.c_str();
  const char *end = p + sv.size();
  
  // Skip leading whitespace and '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  if (p >= end || *p != '[') {
    return false;
  }
  p++; // skip '['
  
  // Skip whitespace after '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  // Handle empty array
  if (p < end && *p == ']') {
    return true;
  }
  
  while (p < end) {
    // Skip whitespace
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    if (p >= end) break;
    
    // Check for closing bracket
    if (*p == ']') {
      break;
    }
    
    // Find the end of the number
    const char *num_start = p;
    while (p < end && *p != delimiter && *p != ']' && 
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    
    if (p == num_start) {
      return false; // No number found
    }
    
    // Parse the number
    float value;
    auto parse_result = fast_float::from_chars(num_start, p, value);
    if (parse_result.ec != std::errc{}) {
      return false;
    }
    
    result->push_back(value);
    
    // Skip whitespace after number
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    // Handle delimiter or end
    if (p < end && *p == delimiter) {
      p++; // skip delimiter
    } else if (p < end && *p == ']') {
      break; // end of array
    }
  }
  
  return true;
}

bool parse_double_array(const tstring_view &sv, std::vector<double> *result, const char delimiter) {
  if (!result) {
    return false;
  }

  result->clear();

  if (sv.size() == 0) {
    return false;
  }

  const char *p = sv.c_str();
  const char *end = p + sv.size();
  
  // Skip leading whitespace and '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  if (p >= end || *p != '[') {
    return false;
  }
  p++; // skip '['
  
  // Skip whitespace after '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  // Handle empty array
  if (p < end && *p == ']') {
    return true;
  }
  
  while (p < end) {
    // Skip whitespace
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    if (p >= end) break;
    
    // Check for closing bracket
    if (*p == ']') {
      break;
    }
    
    // Find the end of the number
    const char *num_start = p;
    while (p < end && *p != delimiter && *p != ']' && 
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    
    if (p == num_start) {
      return false; // No number found
    }
    
    // Parse the number
    double value;
    auto parse_result = fast_float::from_chars(num_start, p, value);
    if (parse_result.ec != std::errc{}) {
      return false;
    }
    
    result->push_back(value);
    
    // Skip whitespace after number
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    // Handle delimiter or end
    if (p < end && *p == delimiter) {
      p++; // skip delimiter
    } else if (p < end && *p == ']') {
      break; // end of array
    }
  }
  
  return true;
}

bool parse_int_array(const tstring_view &sv, std::vector<int32_t> *result, const char delimiter) {
  if (!result) {
    return false;
  }

  result->clear();

  if (sv.size() == 0) {
    return false;
  }

  const char *p = sv.c_str();
  const char *end = p + sv.size();
  
  // Skip leading whitespace and '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  if (p >= end || *p != '[') {
    return false;
  }
  p++; // skip '['
  
  // Skip whitespace after '['
  while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
    p++;
  }
  
  // Handle empty array
  if (p < end && *p == ']') {
    return true;
  }
  
  while (p < end) {
    // Skip whitespace
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    if (p >= end) break;
    
    // Check for closing bracket
    if (*p == ']') {
      break;
    }
    
    // Find the end of the number
    const char *num_start = p;
    while (p < end && *p != delimiter && *p != ']' && 
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    
    if (p <= num_start) {
      return false; // No number found
    }
    
    // Parse the number
    int32_t value;
    tstring_view num_view(num_start, size_t(p - num_start));
    if (!parse_int(num_view, &value)) {
      return false;
    }
    
    result->push_back(value);
    
    // Skip whitespace after number
    while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) {
      p++;
    }
    
    // Handle delimiter or end
    if (p < end && *p == delimiter) {
      p++; // skip delimiter
    } else if (p < end && *p == ']') {
      break; // end of array
    }
  }
  
  return true;
}

bool print_float_array(std::vector<float> &v,
  std::string &dst, const char delimiter) {

  // TODO
  (void)v;
  (void)dst;
  (void)delimiter;

  return false;
}

// Helper function to skip whitespace
static inline const char* skip_whitespace(const char *p, const char *end) {
  while (p < end) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    } else if (*p == '#') {
      // Skip comment to end of line
      while (p < end && *p != '\n') p++;
    } else {
      break;
    }
  }
  return p;
}

// Helper function to parse a single float from a pointer, advancing the pointer
static inline bool parse_single_float(const char **p, const char *end, float *value) {
  const char *start = *p;
  // Find end of number
  while (*p < end && (**p != ',' && **p != ')' && **p != ']' &&
         **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r')) {
    (*p)++;
  }
  auto result = fast_float::from_chars(start, *p, *value);
  return result.ec == std::errc{};
}

// Helper function to parse a single double from a pointer, advancing the pointer
static inline bool parse_single_double(const char **p, const char *end, double *value) {
  const char *start = *p;
  // Find end of number
  while (*p < end && (**p != ',' && **p != ')' && **p != ']' &&
         **p != ' ' && **p != '\t' && **p != '\n' && **p != '\r')) {
    (*p)++;
  }
  auto result = fast_float::from_chars(start, *p, *value);
  return result.ec == std::errc{};
}

// Parse float2 array: [(1, 2), (3, 4), ...]
bool parse_float2_array(const tstring_view &sv, std::vector<tinyusdz::value::float2> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::float2 vec;
    for (size_t i = 0; i < 2; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_float(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i == 0) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse float3 array: [(1, 2, 3), (4, 5, 6), ...]
bool parse_float3_array(const tstring_view &sv, std::vector<tinyusdz::value::float3> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::float3 vec;
    for (size_t i = 0; i < 3; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_float(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i < 2) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse float4 array: [(1, 2, 3, 4), (5, 6, 7, 8), ...]
bool parse_float4_array(const tstring_view &sv, std::vector<tinyusdz::value::float4> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::float4 vec;
    for (size_t i = 0; i < 4; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_float(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i < 3) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse double2 array: [(1, 2), (3, 4), ...]
bool parse_double2_array(const tstring_view &sv, std::vector<tinyusdz::value::double2> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::double2 vec;
    for (size_t i = 0; i < 2; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_double(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i == 0) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse double3 array: [(1, 2, 3), (4, 5, 6), ...]
bool parse_double3_array(const tstring_view &sv, std::vector<tinyusdz::value::double3> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::double3 vec;
    for (size_t i = 0; i < 3; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_double(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i < 2) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse double4 array: [(1, 2, 3, 4), (5, 6, 7, 8), ...]
bool parse_double4_array(const tstring_view &sv, std::vector<tinyusdz::value::double4> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::double4 vec;
    for (size_t i = 0; i < 4; i++) {
      p = skip_whitespace(p, end);
      if (!parse_single_double(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i < 3) {
        if (p >= end || *p != ',') return false;
        p++; // skip ','
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip ')'

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse matrix2f array: [((r00, r01), (r10, r11)), ...]
bool parse_matrix2f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2f> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix2f mat;
    for (size_t i = 0; i < 2; i++) {
      p = skip_whitespace(p, end);
      // Expect row '('
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 2; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_float(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 1) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++; // skip row ')'

      p = skip_whitespace(p, end);
      if (i < 1 && p < end && *p == ',') p++; // skip comma between rows
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++; // skip outer ')'

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++; // skip optional delimiter
  }

  return true;
}

// Parse matrix3f array: [((r00, r01, r02), (r10, r11, r12), (r20, r21, r22)), ...]
bool parse_matrix3f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3f> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix3f mat;
    for (size_t i = 0; i < 3; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 3; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_float(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 2) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < 2 && p < end && *p == ',') p++;
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }

  return true;
}

// Parse matrix4f array: [((r00,..,r03), (r10,..,r13), (r20,..,r23), (r30,..,r33)), ...]
bool parse_matrix4f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4f> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix4f mat;
    for (size_t i = 0; i < 4; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 4; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_float(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 3) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < 3 && p < end && *p == ',') p++;
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }

  return true;
}

// Parse matrix2d array: [((r00, r01), (r10, r11)), ...]
bool parse_matrix2d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2d> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix2d mat;
    for (size_t i = 0; i < 2; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 2; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_double(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 1) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < 1 && p < end && *p == ',') p++;
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }

  return true;
}

// Parse matrix3d array: [((r00, r01, r02), (r10, r11, r12), (r20, r21, r22)), ...]
bool parse_matrix3d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3d> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix3d mat;
    for (size_t i = 0; i < 3; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 3; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_double(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 2) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < 2 && p < end && *p == ',') p++;
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }

  return true;
}

// Parse matrix4d array: [((r00,..,r03), (r10,..,r13), (r20,..,r23), (r30,..,r33)), ...]
bool parse_matrix4d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4d> *result) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++; // skip '['

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true; // empty array

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    // Expect outer '('
    if (*p != '(') return false;
    p++;

    tinyusdz::value::matrix4d mat;
    for (size_t i = 0; i < 4; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < 4; j++) {
        p = skip_whitespace(p, end);
        if (!parse_single_double(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < 3) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < 3 && p < end && *p == ',') p++;
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(mat);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }

  return true;
}

}


} // namespace tinyusdz
