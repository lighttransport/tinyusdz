// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.

#include <cmath>
#include "str-util.hh"

#include "unicode-xid.hh"
#include "common-macros.inc"

// external
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/dragonbox/dragonbox_to_chars.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

namespace tinyusdz {

std::string buildEscapedAndQuotedStringForUSDA(const std::string &str) {
  // Rule for triple quote string:
  //
  // if str contains newline
  //   if str contains """ and '''
  //      use quote """ and escape " to \\", no escape for '''
  //   elif str contains """ only
  //      use quote ''' and no escape for """
  //   elif str contains ''' only
  //      use quote """ and no escape for '''
  //   else
  //      use quote """
  //
  // Rule for single quote string
  //   if str contains " and '
  //      use quote " and escape " to \\", no escape for '
  //   elif str contains " only
  //      use quote ' and no escape for "
  //   elif str contains ' only
  //      use quote " and no escape for '
  //   else
  //      use quote "

  bool has_newline = hasNewline(str);

  std::string s;

  if (has_newline) {
    bool has_triple_single_quoted_string = hasTripleQuotes(str, false);
    bool has_triple_double_quoted_string = hasTripleQuotes(str, true);

    std::string delim = "\"\"\"";
    if (has_triple_single_quoted_string && has_triple_double_quoted_string) {
      s = escapeSingleQuote(str, true);
    } else if (has_triple_single_quoted_string) {
      s = escapeSingleQuote(str, false);
    } else if (has_triple_double_quoted_string) {
      delim = "'''";
      s = str;
    } else {
      s = str;
    }

    s = quote(escapeControlSequence(s), delim);

  } else {
    // single quote string.
    bool has_single_quote = hasQuotes(str, false);
    bool has_double_quote = hasQuotes(str, true);

    std::string delim = "\"";
    if (has_single_quote && has_double_quote) {
      s = escapeSingleQuote(str, true);
    } else if (has_single_quote) {
      s = escapeSingleQuote(str, false);
    } else if (has_double_quote) {
      delim = "'";
      s = str;
    } else {
      s = str;
    }

    s = quote(escapeControlSequence(s), delim);
  }

  return s;
}

std::string escapeControlSequence(const std::string &str) {
  std::string s;

  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == '\a') {
      s += "\\x07";
    } else if (str[i] == '\b') {
      s += "\\x08";
    } else if (str[i] == '\t') {
      s += "\\t";
    } else if (str[i] == '\v') {
      s += "\\x0b";
    } else if (str[i] == '\f') {
      s += "\\x0c";
    } else if (str[i] == '\\') {
      // skip escaping backshash for escaped quote string: \' \"
      if (i + 1 < str.size()) {
        if ((str[i + 1] == '"') || (str[i + 1] == '\'')) {
          s += str[i];
        } else {
          s += "\\\\";
        }
      } else {
        s += "\\\\";
      }
    } else {
      s += str[i];
    }
  }

  return s;
}

std::string unescapeControlSequence(const std::string &str) {
  std::string s;

  if (str.size() < 2) {
    return str;
  }

  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == '\\') {
      if (i + 1 < str.size()) {
        if (str[i + 1] == 'a') {
          s += '\a';
          i++;
        } else if (str[i + 1] == 'b') {
          s += '\b';
          i++;
        } else if (str[i + 1] == 't') {
          s += '\t';
          i++;
        } else if (str[i + 1] == 'v') {
          s += '\v';
          i++;
        } else if (str[i + 1] == 'f') {
          s += '\f';
          i++;
        } else if (str[i + 1] == 'n') {
          s += '\n';
          i++;
        } else if (str[i + 1] == 'r') {
          s += '\r';
          i++;
        } else if (str[i + 1] == '\\') {
          s += "\\";
        } else {
          // ignore backslash
        }
      } else {
        // ignore backslash
      }
    } else {
      s += str[i];
    }
  }

  return s;
}

bool hasQuotes(const std::string &str, bool is_double_quote) {
  for (size_t i = 0; i < str.size(); i++) {
    if (is_double_quote) {
      if (str[i] == '"') {
        return true;
      }
    } else {
      if (str[i] == '\'') {
        return true;
      }
    }
  }

  return false;
}

bool hasTripleQuotes(const std::string &str, bool is_double_quote) {
  for (size_t i = 0; i < str.size(); i++) {
    if (i + 3 < str.size()) {
      if (is_double_quote) {
        if ((str[i + 0] == '"') && (str[i + 1] == '"') && (str[i + 2] == '"')) {
          return true;
        }
      } else {
        if ((str[i + 0] == '\'') && (str[i + 1] == '\'') &&
            (str[i + 2] == '\'')) {
          return true;
        }
      }
    }
  }

  return false;
}

bool hasEscapedTripleQuotes(const std::string &str, bool is_double_quote,
                            size_t *n) {
  size_t count = 0;

  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == '\\') {
      if (i + 3 < str.size()) {
        if (is_double_quote) {
          if ((str[i + 1] == '"') && (str[i + 2] == '"') &&
              (str[i + 3] == '"')) {
            if (!n) {  // early exit
              return true;
            }

            count++;
            i += 3;
          }
        } else {
          if ((str[i + 1] == '\'') && (str[i + 2] == '\'') &&
              (str[i + 3] == '\'')) {
            if (!n) {  // early exit
              return true;
            }
            count++;
            i += 3;
          }
        }
      }
    }
  }

  if (n) {
    (*n) = count;
  }

  return count > 0;
}

std::string escapeSingleQuote(const std::string &str,
                              const bool is_double_quote) {
  std::string s;

  if (is_double_quote) {
    for (size_t i = 0; i < str.size(); i++) {
      if (str[i] == '"') {
        s += "\\\"";
      } else {
        s += str[i];
      }
    }
  } else {
    for (size_t i = 0; i < str.size(); i++) {
      if (str[i] == '\'') {
        s += "\\'";
      } else {
        s += str[i];
      }
    }
  }

  return s;
}

std::string escapeBackslash(const std::string &str,
                            const bool triple_quoted_string) {
  if (triple_quoted_string) {
    std::string s;

    // Do not escape \""" or \'''

    for (size_t i = 0; i < str.size(); i++) {
      if (str[i] == '\\') {
        if (i + 3 < str.size()) {
          if ((str[i + 1] == '\'') && (str[i + 2] == '\'') &&
              (str[i + 3] == '\'')) {
            s += "\\'''";
            i += 3;
          } else if ((str[i + 1] == '"') && (str[i + 2] == '"') &&
                     (str[i + 3] == '"')) {
            s += "\\\"\"\"";
            i += 3;
          } else {
            s += "\\\\";
          }
        } else {
          s += "\\\\";
        }
      } else {
        s += str[i];
      }
    }

    return s;
  } else {
    const std::string bs = "\\";
    const std::string bs_escaped = "\\\\";

    std::string s = str;

    std::string::size_type pos = 0;
    while ((pos = s.find(bs, pos)) != std::string::npos) {
      s.replace(pos, bs.length(), bs_escaped);
      pos += bs_escaped.length();
    }

    return s;
  }
}

std::string unescapeBackslash(const std::string &str) {
  std::string s = str;

  std::string bs = "\\\\";
  std::string bs_unescaped = "\\";

  std::string::size_type pos = 0;
  while ((pos = s.find(bs, pos)) != std::string::npos) {
    s.replace(pos, bs.length(), bs_unescaped);
    pos += bs_unescaped.length();
  }

  return s;
}

bool tokenize_variantElement(const std::string &elementName,
                             std::array<std::string, 2> *result) {
  std::vector<std::string> toks;

  // Ensure ElementPath is quoted with '{' and '}'
  if (startsWith(elementName, "{") && endsWith(elementName, "}")) {
    // ok
  } else {
    return false;
  }

  // Remove variant quotation
  std::string name = unwrap(elementName, "{", "}");

  toks = split(name, "=");
  if (toks.size() == 1) {
    if (result) {
      // ensure '=' and newline does not exist.
      if (counts(toks[0], '=') || hasNewline(toks[0])) {
        return false;
      }

      (*result)[0] = toks[0];
      (*result)[1] = std::string();
    }
    return true;
  } else if (toks.size() == 2) {
    if (result) {
      // ensure '=' and newline does not exist.
      if (counts(toks[0], '=') || hasNewline(toks[0])) {
        return false;
      }

      if (counts(toks[1], '=') || hasNewline(toks[1])) {
        return false;
      }

      (*result)[0] = toks[0];
      (*result)[1] = toks[1];
    }
    return true;
  } else {
    return false;
  }
}

bool is_variantElementName(const std::string &name) {
  return tokenize_variantElement(name);
}

///
/// Simply add number suffix to make unique string.
///
/// - plane -> plane1
/// - sphere1 -> sphere11
/// - xform4 -> xform41
///
///
bool makeUniqueName(std::multiset<std::string> &nameSet,
                    const std::string &name, std::string *unique_name) {
  if (!unique_name) {
    return false;
  }

  if (nameSet.count(name) == 0) {
    (*unique_name) = name;
    return 0;
  }

  // Simply add number

  const size_t kMaxLoop = 1024;  // to avoid infinite loop.

  std::string new_name = name;

  size_t cnt = 0;
  while (cnt < kMaxLoop) {
    size_t i = nameSet.count(new_name);
    if (i == 0) {
      // This should not happen though.
      return false;
    }

    new_name += std::to_string(i);

    if (nameSet.count(new_name) == 0) {
      (*unique_name) = new_name;
      return true;
    }

    cnt++;
  }

  return false;
}

namespace detail {

inline uint32_t utf8_len(const unsigned char c) {
      if (c <= 127) {
        // ascii
        return 1;
      } else if ((c & 0xE0) == 0xC0) {
        return 2;
      } else if ((c & 0xF0) == 0xE0) {
        return 3;
      } else if ((c & 0xF8) == 0xF0) {
        return 4;
      }

      // invalid
      return 0;
}

inline std::string extract_utf8_char(const std::string &str, uint32_t start_i,
                                     int &len) {
  len = 0;

  if ((start_i + 1) > str.size()) {
    len = 0;
    return std::string();
  }

  unsigned char c = static_cast<unsigned char>(str[start_i]);

  if (c <= 127) {
    // ascii
    len = 1;
    return str.substr(start_i, 1);
  } else if ((c & 0xE0) == 0xC0) {
    if ((start_i + 2) > str.size()) {
      len = 0;
      return std::string();
    }
    len = 2;
    return str.substr(start_i, 2);
  } else if ((c & 0xF0) == 0xE0) {
    if ((start_i + 3) > str.size()) {
      len = 0;
      return std::string();
    }
    len = 3;
    return str.substr(start_i, 3);
  } else if ((c & 0xF8) == 0xF0) {
    if ((start_i + 4) > str.size()) {
      len = 0;
      return std::string();
    }
    len = 4;
    return str.substr(start_i, 4);
  } else {
    // invalid utf8
    len = 0;
    return std::string();
  }
}

inline uint32_t to_codepoint(const char *s, uint32_t &char_len) {
  if (!s) {
    char_len = 0;
    return ~0u;
  }

  char_len = detail::utf8_len(static_cast<unsigned char>(s[0]));
  if (char_len == 0) {
    return ~0u;
  }

  uint32_t code = 0;
  if (char_len == 1) {
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    if (s0 > 0x7f) {
      return ~0u;
    }
    code = uint32_t(s0) & 0x7f;
  } else if (char_len == 2) {
    // 11bit: 110y-yyyx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);

    if (((s0 & 0xe0) == 0xc0) && ((s1 & 0xc0) == 0x80)) {
      code = (uint32_t(s0 & 0x1f) << 6) | (s1 & 0x3f);
    } else {
      return ~0u;
    }
  } else if (char_len == 3) {
    // 16bit: 1110-yyyy 10yx-xxxx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);
    unsigned char s2 = static_cast<unsigned char>(s[2]);
    if (((s0 & 0xf0) == 0xe0) && ((s1 & 0xc0) == 0x80) &&
        ((s2 & 0xc0) == 0x80)) {
      code =
          (uint32_t(s0 & 0xf) << 12) | (uint32_t(s1 & 0x3f) << 6) | (s2 & 0x3f);
    } else {
      return ~0u;
    }
  } else if (char_len == 4) {
    // 21bit: 1111-0yyy 10yy-xxxx 10xx-xxxx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);
    unsigned char s2 = static_cast<unsigned char>(s[2]);
    unsigned char s3 = static_cast<unsigned char>(s[3]);
    if (((s0 & 0xf8) == 0xf0) && ((s1 & 0xc0) == 0x80) &&
        ((s2 & 0xc0) == 0x80) && ((s2 & 0xc0) == 0x80)) {
      code = (uint32_t(s0 & 0x7) << 18) | (uint32_t(s1 & 0x3f) << 12) |
             (uint32_t(s2 & 0x3f) << 6) | uint32_t(s3 & 0x3f);
    } else {
      return ~0u;
    }
  } else {
    // ???
    char_len = 0;
    return ~0u;
  }

  return code;
}

}  // namespace detail

std::vector<std::string> to_utf8_chars(const std::string &str) {
  std::vector<std::string> utf8_chars;
  size_t sz = str.size();

  for (size_t i = 0; i <= sz;) {
    int len = 0;
    std::string s = detail::extract_utf8_char(str, uint32_t(i), len);
    if (len == 0) {
      // invalid char
      return std::vector<std::string>();
    }

    i += uint64_t(len);
    utf8_chars.push_back(s);
  }

  return utf8_chars;
}

uint32_t to_utf8_code(const std::string &s) {
  if (s.empty() || (s.size() > 4)) {
    return ~0u;  // invalid
  }

  // TODO: endianness.
  uint32_t code = 0;
  if (s.size() == 1) {
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    if (s0 > 0x7f) {
      return ~0u;
    }
    code = uint32_t(s0) & 0x7f;
  } else if (s.size() == 2) {
    // 11bit: 110y-yyyx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);

    if (((s0 & 0xe0) == 0xc0) && ((s1 & 0xc0) == 0x80)) {
      code = (uint32_t(s0 & 0x1f) << 6) | (s1 & 0x3f);
    } else {
      return ~0u;
    }
  } else if (s.size() == 3) {
    // 16bit: 1110-yyyy 10yx-xxxx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);
    unsigned char s2 = static_cast<unsigned char>(s[2]);
    if (((s0 & 0xf0) == 0xe0) && ((s1 & 0xc0) == 0x80) &&
        ((s2 & 0xc0) == 0x80)) {
      code =
          (uint32_t(s0 & 0xf) << 12) | (uint32_t(s1 & 0x3f) << 6) | (s2 & 0x3f);
    } else {
      return ~0u;
    }
  } else {
    // 21bit: 1111-0yyy 10yy-xxxx 10xx-xxxx 10xx-xxxx
    unsigned char s0 = static_cast<unsigned char>(s[0]);
    unsigned char s1 = static_cast<unsigned char>(s[1]);
    unsigned char s2 = static_cast<unsigned char>(s[2]);
    unsigned char s3 = static_cast<unsigned char>(s[3]);
    if (((s0 & 0xf8) == 0xf0) && ((s1 & 0xc0) == 0x80) &&
        ((s2 & 0xc0) == 0x80) && ((s2 & 0xc0) == 0x80)) {
      code = (uint32_t(s0 & 0x7) << 18) | (uint32_t(s1 & 0x3f) << 12) |
             (uint32_t(s2 & 0x3f) << 6) | uint32_t(s3 & 0x3f);
    } else {
      return ~0u;
    }
  }

  return code;
}


#if 0
std::string to_utf8_char(const uint32_t code) {

  if (code < 128) {
    std::string s = static_cast<char>(code);
    return s;
  }
  // TODO

}
#endif

bool is_valid_utf8(const std::string &str) {
  // TODO: Consider UTF-BOM?
  for (size_t i = 0; i < str.size();) {
    uint32_t len = detail::utf8_len(*reinterpret_cast<const unsigned char *>(&str[i]));
    if (len == 0) {
      return false;
    }
    i += len;
  }
  return true;
}

std::vector<uint32_t> to_codepoints(const std::string &str) {

  std::vector<uint32_t> cps;

  for (size_t i = 0; i < str.size(); ) {
    uint32_t char_len;
    uint32_t cp = detail::to_codepoint(str.c_str() + i, char_len);

    if ((cp > kMaxUTF8Codepoint) || (char_len == 0)) {
      return std::vector<uint32_t>();
    }

    cps.push_back(cp);

    i += char_len;
  }

  return cps;
}

bool is_valid_utf8_identifier(const std::string &str) {
  // First convert to codepoint values.
  std::vector<uint32_t> codepoints = to_codepoints(str);

  if (codepoints.empty()) {
    return false;
  }

  // (XID_Start|_) (XID_Continue|_)+
  
  if ((codepoints[0] != '_') && !unicode_xid::is_xid_start(codepoints[0])) {
    return false;
  }

  for (size_t i = 1; i < codepoints.size(); i++) {
    if ((codepoints[i] != '_') && !unicode_xid::is_xid_continue(codepoints[i])) {
      return false;
    }
  }

  return true; 
}

// ----------------------------------------------------------------------
// based on fmtlib
// Copyright (c) 2012 - present, Victor Zverovich and {fmt} contributors
// MIT license.
//

namespace internal {

// TOOD: Use builtin_clz insturction?
// T = uint32 or uint64
template <typename T>
inline int count_digits(T n) {
  int count = 1;
  for (;;) {
    // Integer division is slow so do it for a group of four digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    if (n < 10) return count;
    if (n < 100) return count + 1;
    if (n < 1000) return count + 2;
    if (n < 10000) return count + 3;
    n /= 10000u;
    count += 4;
  }
}

// Converts value in the range [0, 100) to a string.
// GCC generates slightly better code when value is pointer-size.
inline auto digits2(size_t value) -> const char* {
  // Align data since unaligned access may be slower when crossing a
  // hardware-specific boundary.
  alignas(2) static const char data[] =
      "0001020304050607080910111213141516171819"
      "2021222324252627282930313233343536373839"
      "4041424344454647484950515253545556575859"
      "6061626364656667686970717273747576777879"
      "8081828384858687888990919293949596979899";
  return &data[value * 2];
}

// Writes a two-digit value to out.
inline void write2digits(char* out, size_t value) {
  // if (!is_constant_evaluated() && std::is_same<Char, char>::value &&
  //     !FMT_OPTIMIZE_SIZE) {
  //   memcpy(out, digits2(value), 2);
  //   return;
  // }
  *out++ = static_cast<char>('0' + value / 10);
  *out = static_cast<char>('0' + value % 10);
}

// Writes the exponent exp in the form "[+-]d{2,3}" to buffer.
static char* write_exponent(int exp, char* out) {
  // FMT_ASSERT(-10000 < exp && exp < 10000, "exponent out of range");
  if (exp < 0) {
    *out++ = '-';
    exp = -exp;
  } else {
    *out++ = '+';
  }
  auto uexp = static_cast<uint32_t>(exp);
  // if (is_constant_evaluated()) {
  //   if (uexp < 10) *out++ = '0';
  //   return format_decimal<Char>(out, uexp, count_digits(uexp));
  // }
  if (uexp >= 100u) {
    const char* top = digits2(uexp / 100);
    if (uexp >= 1000u) *out++ = top[0];
    *out++ = static_cast<char>(top[1]);
    uexp %= 100;
  }
  const char* d = digits2(uexp);
  *out++ = static_cast<char>(d[0]);
  *out++ = static_cast<char>(d[1]);
  return out;
}

inline char* fill_n(char* p, int n, char c) {
  for (int i = 0; i < n; i++, p++) {
    *p = c;
  }
  return p;
}

inline void format_decimal_impl(char* out, uint64_t value, uint32_t size) {
  // FMT_ASSERT(size >= count_digits(value), "invalid digit count");
  unsigned n = size;
  while (value >= 100) {
    // Integer division is slow so do it for a group of two digits instead
    // of for every digit. The idea comes from the talk by Alexandrescu
    // "Three Optimization Tips for C++". See speed-test for a comparison.
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value % 100));
    value /= 100;
  }
  if (value >= 10) {
    n -= 2;
    write2digits(out + n, static_cast<unsigned>(value));
  } else {
    out[--n] = static_cast<char>('0' + value);
  }
  //return out + n;
}

inline char* format_decimal(char* out, uint64_t value, uint32_t num_digits) {
  format_decimal_impl(out, value, num_digits);
  return out + num_digits;
}

inline char* write_significand_e(char* out, uint64_t significand,
                                 int significand_size, int exponent) {
  out = format_decimal(out, significand, uint32_t(significand_size));
  return fill_n(out, exponent, '0');
}

inline char* write_significand(char* out, uint64_t significand,
                               int significand_size, int integral_size,
                               char decimal_point) {
  if (!decimal_point) return format_decimal(out, significand, uint32_t(significand_size));
  out += significand_size + 1;
  char* end = out;
  int floating_size = significand_size - integral_size;
  for (int i = floating_size / 2; i > 0; --i) {
    out -= 2;
    write2digits(out, static_cast<std::size_t>(significand % 100));
    significand /= 100;
  }
  if (floating_size % 2 != 0) {
    *--out = static_cast<char>('0' + significand % 10);
    significand /= 10;
  }
  *--out = decimal_point;
  format_decimal(out - integral_size, significand, uint32_t(integral_size));
  return end;
}

// Use dragonbox algorithm to print floating point value.
// Use to_deciamal and do human-readable pretty printing for some value range(e.g. print 1e-3 as 0.001) 
// 
// exp_upper: (15 + 1) for double, (6+1) for float
static char* dtoa_dragonbox(const double f, char* buf, int exp_upper = 16) {
  //const int spec_precision = -1;  // unlimited

  bool is_negative = std::signbit(f);

  auto ret = jkj::dragonbox::to_decimal(f);

  // print human-readable float for the value in range [1e-exp_lower, 1e+exp_upper]
  const int exp_lower = -4;
  char exp_char = 'e';
  char zero_char = '0';

  auto significand = ret.significand;
  int significand_size = count_digits(significand);

  //size_t size = size_t(significand_size) + (is_negative ? 1u : 0u);

  int output_exp = ret.exponent + significand_size - 1;
  bool use_exp_format = (output_exp < exp_lower) || (output_exp >= exp_upper);

  char decimal_point = '.';
  if (use_exp_format) {
    int num_zeros = 0;
    if (significand_size == 1) {
      decimal_point = '\0';
    }
    //auto abs_output_exp = output_exp >= 0 ? output_exp : -output_exp;
    //int exp_digits = 2;
    //if (abs_output_exp >= 100) exp_digits = abs_output_exp >= 1000 ? 4 : 3;

    //size += (decimal_point ? 1u : 0u) + 2u + size_t(exp_digits);

    if (is_negative) {
      *buf++ = '-';
    }

    buf =
        write_significand(buf, significand, significand_size, 1, decimal_point);

    if (num_zeros > 0) buf = fill_n(buf, num_zeros, zero_char);
    *buf++ = exp_char;
    return write_exponent(output_exp, buf);
  }

  int exp = ret.exponent + significand_size;
  if (ret.exponent >= 0) {
    // 1234e5 -> 123400000[.0+]
    //size += static_cast<size_t>(ret.exponent);
    //int num_zeros = spec_precision - exp;
    // abort_fuzzing_if(num_zeros > 5000);
    // if (specs.alt()) {
    //   ++size;
    //   if (num_zeros <= 0 && specs.type() != presentation_type::fixed)
    //     num_zeros = 0;
    //   if (num_zeros > 0) size += size_t(num_zeros);
    // }
    // auto grouping = Grouping(loc, specs.localized());
    // size += size_t(grouping.count_separators(exp));
    // return write_padded<Char, align::right>(out, specs, size, [&](iterator
    // it) {
    //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
    //   it = write_significand<Char>(it, significand, significand_size,
    //                                f.exponent, grouping);
    //   if (!specs.alt()) return it;
    //   *it++ = decimal_point;
    //   return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
    // });

    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand_e(buf, significand, significand_size,
                               ret.exponent);

  } else if (exp > 0) {
    // 1234e-2 -> 12.34[0+]
    // int num_zeros = specs.alt() ? spec_precision - significand_size : 0;
    // size += 1 + static_cast<unsigned>(max_of(num_zeros, 0));
    //size += 1;
    // auto grouping = Grouping(loc, specs.localized());
    // size += size_t(grouping.count_separators(exp));
    // return write_padded<Char, align::right>(out, specs, size, [&](iterator
    // it) {
    //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
    //   it = write_significand(it, significand, significand_size, exp,
    //                          decimal_point, grouping);
    //   return num_zeros > 0 ? detail::fill_n(it, num_zeros, zero) : it;
    // });
    if (is_negative) {
      *buf++ = '-';
    }

    return write_significand(buf, significand, significand_size, exp,
                             decimal_point);
  }
  // 1234e-6 -> 0.001234
  int num_zeros = -exp;
  // if (significand_size == 0 && specs.precision >= 0 &&
  //     specs.precision < num_zeros) {
  //   num_zeros = spec_precision;
  // }
  bool pointy = num_zeros != 0 || significand_size != 0;  // || specs.alt();
  //size += 1u + (pointy ? 1u : 0u) + size_t(num_zeros);
  // return write_padded<Char, align::right>(out, specs, size, [&](iterator it)
  // {
  //   if (s != sign::none) *it++ = detail::getsign<Char>(s);
  //   *it++ = zero;
  //   if (!pointy) return it;
  //   *it++ = decimal_point;
  //   it = detail::fill_n(it, num_zeros, zero);
  //   return write_significand<Char>(it, significand, significand_size);
  // });

  if (is_negative) {
    *buf++ = '-';
  }

  *buf++ = zero_char;

  if (!pointy) return buf;
  *buf++ = decimal_point;
  buf = fill_n(buf, num_zeros, zero_char);

  return format_decimal(buf, significand, uint32_t(significand_size));
}

static char* dtoa_dragonbox(const float f, char* buf) {
  return dtoa_dragonbox(double(f), buf, 7);
}

} // namespace internal

char *dtoa(float f, char *buffer) {
  return internal::dtoa_dragonbox(f, buffer);
}

char *dtoa(double f, char *buffer) {
  return internal::dtoa_dragonbox(f, buffer);
}

}  // namespace tinyusdz
