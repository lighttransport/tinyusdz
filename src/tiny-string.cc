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
    if (negative && result > static_cast<int64_t>((std::numeric_limits<int32_t>::max)()) + 1) {
      return false;
    }
    if (!negative && result > (std::numeric_limits<int32_t>::max)()) {
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
    if (negative && result > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)()) + 1) {
      return false;
    }
    if (!negative && result > static_cast<uint64_t>((std::numeric_limits<int64_t>::max)())) {
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
    if (result > (std::numeric_limits<uint32_t>::max)()) {
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
    if (result > ((std::numeric_limits<uint64_t>::max)() - uint64_t(str[i] - '0')) / 10) {
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


// Skip whitespace and '#' line comments
static inline const char* skip_whitespace(const char *p, const char *end) {
  while (p < end) {
    if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    } else if (*p == '#') {
      // Skip comment to end of line
      while (p < end && *p != '\n' && *p != '\r') {
        p++;
      }
    } else {
      break;
    }
  }
  return p;
}

// Helper: parse a single float/double from pointer, advancing past the number
template<typename T>
static inline bool parse_single(const char **p, const char *end, T *value) {
  const char *start = *p;
  auto result = fast_float::from_chars(start, end, *value);
  if (result.ec != std::errc{} || result.ptr == start) {
    return false;
  }
  *p = result.ptr;
  return true;
}

// Scalar array parser: [val, val, ...]
// Works for float, double (via fast_float) and int (via parse_int).
template<typename T, typename ParseFn>
static bool parse_scalar_array_impl(const tstring_view &sv, std::vector<T> *result,
                                    ParseFn parse_fn) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++;

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true;

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    const char *num_start = p;
    while (p < end && *p != ',' && *p != ']' && *p != '#' &&
           *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') {
      p++;
    }
    if (p <= num_start) return false;

    T value;
    if (!parse_fn(num_start, p, &value)) return false;
    result->push_back(value);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') {
      p++;
    } else if (p < end && *p == ']') {
      break;
    }
  }
  return true;
}

bool parse_float_array(const tstring_view &sv, std::vector<float> *result) {
  return parse_scalar_array_impl<float>(sv, result,
    [](const char *start, const char *end, float *val) -> bool {
      auto r = fast_float::from_chars(start, end, *val);
      return r.ec == std::errc{};
    });
}

bool parse_double_array(const tstring_view &sv, std::vector<double> *result) {
  return parse_scalar_array_impl<double>(sv, result,
    [](const char *start, const char *end, double *val) -> bool {
      auto r = fast_float::from_chars(start, end, *val);
      return r.ec == std::errc{};
    });
}

bool parse_int_array(const tstring_view &sv, std::vector<int32_t> *result) {
  return parse_scalar_array_impl<int32_t>(sv, result,
    [](const char *start, const char *end, int32_t *val) -> bool {
      tstring_view num_view(start, size_t(end - start));
      return parse_int(num_view, val);
    });
}

// Tuple array parser: [(v0, v1, ...), (v0, v1, ...), ...]
// VecT must support operator[] for element access.
template<typename VecT, size_t N, typename ParseFn>
static bool parse_tuple_array_impl(const tstring_view &sv, std::vector<VecT> *result,
                                   ParseFn parse_fn) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++;

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true;

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    if (*p != '(') return false;
    p++;

    VecT vec{};
    for (size_t i = 0; i < N; i++) {
      p = skip_whitespace(p, end);
      if (!parse_fn(&p, end, &vec[i])) return false;
      p = skip_whitespace(p, end);
      if (i < N - 1) {
        if (p >= end || *p != ',') return false;
        p++;
      }
    }

    p = skip_whitespace(p, end);
    if (p >= end || *p != ')') return false;
    p++;

    result->push_back(vec);

    p = skip_whitespace(p, end);
    if (p < end && *p == ',') p++;
  }
  return true;
}

bool parse_float2_array(const tstring_view &sv, std::vector<tinyusdz::value::float2> *result) {
  return parse_tuple_array_impl<tinyusdz::value::float2, 2>(sv, result, parse_single<float>);
}
bool parse_float3_array(const tstring_view &sv, std::vector<tinyusdz::value::float3> *result) {
  return parse_tuple_array_impl<tinyusdz::value::float3, 3>(sv, result, parse_single<float>);
}
bool parse_float4_array(const tstring_view &sv, std::vector<tinyusdz::value::float4> *result) {
  return parse_tuple_array_impl<tinyusdz::value::float4, 4>(sv, result, parse_single<float>);
}
bool parse_point3f_array(const tstring_view &sv, std::vector<tinyusdz::value::point3f> *result) {
  return parse_tuple_array_impl<tinyusdz::value::point3f, 3>(sv, result, parse_single<float>);
}
bool parse_normal3f_array(const tstring_view &sv, std::vector<tinyusdz::value::normal3f> *result) {
  return parse_tuple_array_impl<tinyusdz::value::normal3f, 3>(sv, result, parse_single<float>);
}
bool parse_double2_array(const tstring_view &sv, std::vector<tinyusdz::value::double2> *result) {
  return parse_tuple_array_impl<tinyusdz::value::double2, 2>(sv, result, parse_single<double>);
}
bool parse_double3_array(const tstring_view &sv, std::vector<tinyusdz::value::double3> *result) {
  return parse_tuple_array_impl<tinyusdz::value::double3, 3>(sv, result, parse_single<double>);
}
bool parse_double4_array(const tstring_view &sv, std::vector<tinyusdz::value::double4> *result) {
  return parse_tuple_array_impl<tinyusdz::value::double4, 4>(sv, result, parse_single<double>);
}

// Matrix array parser: [((r00, r01, ...), (r10, r11, ...), ...), ...]
// MatT must have mat.m[row][col] access.
template<typename MatT, size_t N, typename ParseFn>
static bool parse_matrix_array_impl(const tstring_view &sv, std::vector<MatT> *result,
                                    ParseFn parse_fn) {
  if (!result) return false;
  result->clear();
  if (sv.size() == 0) return false;

  const char *p = sv.c_str();
  const char *end = p + sv.size();

  p = skip_whitespace(p, end);
  if (p >= end || *p != '[') return false;
  p++;

  p = skip_whitespace(p, end);
  if (p < end && *p == ']') return true;

  while (p < end) {
    p = skip_whitespace(p, end);
    if (p >= end) break;
    if (*p == ']') break;

    if (*p != '(') return false;
    p++;

    MatT mat{};
    for (size_t i = 0; i < N; i++) {
      p = skip_whitespace(p, end);
      if (p >= end || *p != '(') return false;
      p++;

      for (size_t j = 0; j < N; j++) {
        p = skip_whitespace(p, end);
        if (!parse_fn(&p, end, &mat.m[i][j])) return false;
        p = skip_whitespace(p, end);
        if (j < N - 1) {
          if (p >= end || *p != ',') return false;
          p++;
        }
      }

      p = skip_whitespace(p, end);
      if (p >= end || *p != ')') return false;
      p++;

      p = skip_whitespace(p, end);
      if (i < N - 1 && p < end && *p == ',') p++;
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

bool parse_matrix2f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2f> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix2f, 2>(sv, result, parse_single<float>);
}
bool parse_matrix3f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3f> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix3f, 3>(sv, result, parse_single<float>);
}
bool parse_matrix4f_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4f> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix4f, 4>(sv, result, parse_single<float>);
}
bool parse_matrix2d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix2d> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix2d, 2>(sv, result, parse_single<double>);
}
bool parse_matrix3d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix3d> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix3d, 3>(sv, result, parse_single<double>);
}
bool parse_matrix4d_array(const tstring_view &sv, std::vector<tinyusdz::value::matrix4d> *result) {
  return parse_matrix_array_impl<tinyusdz::value::matrix4d, 4>(sv, result, parse_single<double>);
}

}


} // namespace tinyusdz
