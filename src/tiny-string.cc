// SPDX-License-Identifier: MIT
// Copyright 2024-Present Light Transport Entertainment Inc.
#include "tiny-string.hh"

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

bool print_float_array(std::vector<float> &v,
  std::string &dst, const char delimiter) {


  // TODO
  (void)v;
  (void)dst;
  (void)delimiter;

  return false;
}


}


} // namespace tinyusdz
