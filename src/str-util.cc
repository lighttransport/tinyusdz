// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.
#include "str-util.hh"

#include "unicode-xid.hh"
#include "common-macros.inc"
#include "external/dtoa_milo.h"

#ifdef __SSE2__
#include <emmintrin.h>
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
    s = str;
  } else {

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
            i++;
          } else if (str[i + 1] == 'x') {
            // Hex escape: \xNN
            if (i + 3 < str.size()) {
              char h1 = str[i + 2];
              char h2 = str[i + 3];
              auto hex_digit = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
              };
              int d1 = hex_digit(h1);
              int d2 = hex_digit(h2);
              if (d1 >= 0 && d2 >= 0) {
                s += static_cast<char>((d1 << 4) | d2);
                i += 3;
              } else {
                // Invalid hex, keep backslash
                s += str[i];
              }
            } else {
              // Not enough characters for \xNN
              s += str[i];
            }
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
      //return std::vector<std::string>();
      utf8_chars = std::vector<std::string>();
      break;
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
      cps = std::vector<uint32_t>();
      break;
      //return std::vector<uint32_t>();
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

std::string makeIdentifierValid(const std::string &str, bool is_utf8) {
  // TODO: utf8 support
  (void)is_utf8;

  std::string s;

  if (str.empty()) {
    // return '_'
    s =  "_";
  } else {

    // first char
    // [a-ZA-Z_]
    if ((('a' <= str[0]) && (str[0] <= 'z')) || (('A' <= str[0]) && (str[0] <= 'Z')) || (str[0] == '_')) {
      s.push_back(str[0]);
    } else {
      s.push_back('_');
    }

    // remain chars
    // [a-ZA-Z0-9_]
    for (size_t i = 1; i < str.length(); i++) {
      if ((('a' <= str[i]) && (str[i] <= 'z')) || (('A' <= str[i]) && (str[i] <= 'Z')) || (('0' <= str[i]) && (str[i] <= '9')) || (str[i] == '_')) {
        s.push_back(str[i]);
      } else {
        s.push_back('_');
      }
    }
  }

  return s;
}

double atof(const char *p) {
  // TODO: Use from_chars
  return std::atof(p);
}

double atof(const std::string &s) {
  return atof(s.c_str());
}

/*
   base64.cpp and base64.h

   Copyright (C) 2004-2008 René Nyffenegger

   This source code is provided 'as-is', without any express or implied
   warranty. In no event will the author be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this source code must not be misrepresented; you must not
      claim that you wrote the original source code. If you use this source code
      in a product, an acknowledgment in the product documentation would be
      appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
      misrepresented as being the original source code.

   3. This notice may not be removed or altered from any source distribution.

   René Nyffenegger rene.nyffenegger@adp-gmbh.ch

*/

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wconversion"
#endif

#ifdef __SSE2__
#else
static inline bool is_base64(unsigned char c) {
  return (isalnum(c) || (c == '+') || (c == '/'));
}
#endif

#ifdef __SSE2__
#else
// Fallback implementation (original)
static std::string base64_encode_scalar(unsigned char const *bytes_to_encode,
                                       unsigned int in_len) {
  std::string ret;
  int i = 0;
  int j = 0;
  unsigned char char_array_3[3];
  unsigned char char_array_4[4];

  const char *base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  while (in_len--) {
    char_array_3[i++] = *(bytes_to_encode++);
    if (i == 3) {
      char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
      char_array_4[1] =
          ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
      char_array_4[2] =
          ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
      char_array_4[3] = char_array_3[2] & 0x3f;

      for (i = 0; (i < 4); i++) ret += base64_chars[char_array_4[i]];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 3; j++) char_array_3[j] = '\0';

    char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
    char_array_4[1] =
        ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
    char_array_4[2] =
        ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);

    for (j = 0; (j < i + 1); j++) ret += base64_chars[char_array_4[j]];

    while ((i++ < 3)) ret += '=';
  }

  return ret;
}
#endif

// SSE2-optimized base64 encode implementation
#ifdef __SSE2__
static std::string base64_encode_sse(unsigned char const *bytes_to_encode, unsigned int in_len) {
  if (in_len == 0) return std::string();

  const char base64_chars[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
  };

  // Calculate output size
  const size_t output_len = ((in_len + 2) / 3) * 4;
  std::string result;
  result.reserve(output_len);

  size_t input_pos = 0;

  // Process 12 bytes at a time using SSE2 (produces 16 base64 characters)
  while (input_pos + 12 <= in_len) {
    // Load 12 input bytes (will process as 4 groups of 3 bytes each)
    alignas(16) uint8_t input_block[16] = {0};

    // Copy 12 bytes, leaving last 4 bytes as zero padding
    for (int i = 0; i < 12; i++) {
      input_block[i] = bytes_to_encode[input_pos + i];
    }

    // Load input data into SSE register (currently unused but reserved for future vectorization)
    (void)_mm_load_si128(reinterpret_cast<const __m128i*>(input_block));

    // Process 4 groups of 3 bytes each
    alignas(16) uint8_t output_indices[16];

    for (int group = 0; group < 4; group++) {
      int base_idx = group * 3;

      // Extract 3 bytes for this group
      uint8_t b0 = input_block[base_idx];
      uint8_t b1 = input_block[base_idx + 1];
      uint8_t b2 = input_block[base_idx + 2];

      // Convert 3 bytes to 4 base64 indices
      output_indices[group * 4] = (b0 >> 2) & 0x3F;
      output_indices[group * 4 + 1] = ((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F);
      output_indices[group * 4 + 2] = ((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03);
      output_indices[group * 4 + 3] = b2 & 0x3F;
    }

    // Convert indices to base64 characters using table lookup
    for (int i = 0; i < 16; i++) {
      result.push_back(base64_chars[output_indices[i]]);
    }

    input_pos += 12;
  }

  // Handle remaining bytes with scalar code
  while (input_pos + 3 <= in_len) {
    uint8_t b0 = bytes_to_encode[input_pos];
    uint8_t b1 = bytes_to_encode[input_pos + 1];
    uint8_t b2 = bytes_to_encode[input_pos + 2];

    result.push_back(base64_chars[(b0 >> 2) & 0x3F]);
    result.push_back(base64_chars[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);
    result.push_back(base64_chars[((b1 & 0x0F) << 2) | ((b2 >> 6) & 0x03)]);
    result.push_back(base64_chars[b2 & 0x3F]);

    input_pos += 3;
  }

  // Handle final 1-2 bytes if present
  if (input_pos < in_len) {
    uint8_t b0 = bytes_to_encode[input_pos];
    uint8_t b1 = (input_pos + 1 < in_len) ? bytes_to_encode[input_pos + 1] : 0;

    result.push_back(base64_chars[(b0 >> 2) & 0x3F]);
    result.push_back(base64_chars[((b0 & 0x03) << 4) | ((b1 >> 4) & 0x0F)]);

    if (input_pos + 1 < in_len) {
      result.push_back(base64_chars[((b1 & 0x0F) << 2)]);
    } else {
      result.push_back('=');
    }
    result.push_back('=');
  }

  return result;
}
#endif // __SSE2__

std::string base64_encode(unsigned char const *bytes_to_encode,
                          unsigned int in_len) {
#ifdef __SSE2__
  // Use SSE2 optimized version if available
  return base64_encode_sse(bytes_to_encode, in_len);
#else
  // Use scalar fallback implementation
  return base64_encode_scalar(bytes_to_encode, in_len);
#endif
}

// SSE2-optimized base64 decode implementation
#ifdef __SSE2__
static std::string base64_decode_sse(std::string const &encoded_string) {
  const size_t input_len = encoded_string.size();
  if (input_len == 0) return std::string();

  // Lookup table for base64 decoding (256 entries, -1 for invalid chars)
  static const int8_t decode_table[256] = {
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,62, -1,-1,-1,63,
    52,53,54,55, 56,57,58,59, 60,61,-1,-1, -1,-2,-1,-1,
    -1, 0, 1, 2,  3, 4, 5, 6,  7, 8, 9,10, 11,12,13,14,
    15,16,17,18, 19,20,21,22, 23,24,25,-1, -1,-1,-1,-1,
    -1,26,27,28, 29,30,31,32, 33,34,35,36, 37,38,39,40,
    41,42,43,44, 45,46,47,48, 49,50,51,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
  };

  // Calculate output size (remove padding)
  size_t padding = 0;
  if (input_len >= 1 && encoded_string[input_len - 1] == '=') padding++;
  if (input_len >= 2 && encoded_string[input_len - 2] == '=') padding++;

  const size_t output_len = (input_len * 3) / 4 - padding;
  std::string result;
  result.reserve(output_len);

  const uint8_t* input = reinterpret_cast<const uint8_t*>(encoded_string.data());
  size_t input_pos = 0;

  // Process 16 bytes at a time using SSE2
  while (input_pos + 16 <= input_len) {
    // Load 16 input bytes
    __m128i input_chunk = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + input_pos));

    // Decode using lookup table (split into two 8-byte chunks for table lookup)
    alignas(16) uint8_t input_bytes[16];
    _mm_store_si128(reinterpret_cast<__m128i*>(input_bytes), input_chunk);

    alignas(16) int8_t decoded[16];
    bool valid = true;

    for (int i = 0; i < 16; i++) {
      decoded[i] = decode_table[input_bytes[i]];
      if (decoded[i] < 0 && input_bytes[i] != '=') {
        valid = false;
        break;
      }
    }

    if (!valid) break; // Fall back to scalar processing for invalid chars

    // Pack groups of 4 decoded bytes into 3 output bytes
    for (int group = 0; group < 4; group++) {
      if (input_pos + group * 4 + 3 >= input_len) break;

      int base_idx = group * 4;
      if (decoded[base_idx] >= 0 && decoded[base_idx + 1] >= 0 &&
          decoded[base_idx + 2] >= 0 && decoded[base_idx + 3] >= 0) {

        uint32_t combined = (static_cast<uint32_t>(decoded[base_idx]) << 18) |
                           (static_cast<uint32_t>(decoded[base_idx + 1]) << 12) |
                           (static_cast<uint32_t>(decoded[base_idx + 2]) << 6) |
                           static_cast<uint32_t>(decoded[base_idx + 3]);

        result.push_back(static_cast<char>((combined >> 16) & 0xFF));
        result.push_back(static_cast<char>((combined >> 8) & 0xFF));
        result.push_back(static_cast<char>(combined & 0xFF));
      }
    }

    input_pos += 16;
  }

  // Process remaining bytes with scalar code
  while (input_pos + 4 <= input_len) {
    uint8_t a = input[input_pos];
    uint8_t b = input[input_pos + 1];
    uint8_t c = input[input_pos + 2];
    uint8_t d = input[input_pos + 3];

    if (a == '=' || b == '=') break;

    int8_t da = decode_table[a];
    int8_t db = decode_table[b];
    int8_t dc = decode_table[c];
    int8_t dd = decode_table[d];

    if (da < 0 || db < 0) break;

    uint32_t combined = (static_cast<uint32_t>(da) << 18) |
                       (static_cast<uint32_t>(db) << 12);

    result.push_back(static_cast<char>((combined >> 16) & 0xFF));

    if (c != '=' && dc >= 0) {
      combined |= static_cast<uint32_t>(dc) << 6;
      result.push_back(static_cast<char>((combined >> 8) & 0xFF));

      if (d != '=' && dd >= 0) {
        combined |= static_cast<uint32_t>(dd);
        result.push_back(static_cast<char>(combined & 0xFF));
      }
    }

    input_pos += 4;
  }

  return result;
}
#endif // __SSE2__

// Fallback implementation (original)
std::string base64_decode(std::string const &encoded_string) {
#ifdef __SSE2__
  // Use SSE2 optimized version if available
  return base64_decode_sse(encoded_string);
#else
  // Original scalar implementation
  int in_len = static_cast<int>(encoded_string.size());
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  const std::string base64_chars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz"
      "0123456789+/";

  while (in_len-- && (encoded_string[in_] != '=') &&
         is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++)
        char_array_4[i] =
            static_cast<unsigned char>(base64_chars.find(char_array_4[i]));

      char_array_3[0] =
          (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] =
          ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++) ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++) char_array_4[j] = 0;

    for (j = 0; j < 4; j++)
      char_array_4[j] =
          static_cast<unsigned char>(base64_chars.find(char_array_4[j]));

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] =
        ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
  }

  return ret;
#endif // __SSE2__
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

/*
   -- end base64.cpp and base64.h
*/

bool GlobMatch(const std::string &pattern, const std::string &str) {
  // Simple glob matching with * (any chars) and ? (single char)
  // Uses dynamic programming approach

  size_t p = 0;  // pattern index
  size_t s = 0;  // string index
  size_t starIdx = std::string::npos;
  size_t matchIdx = 0;

  while (s < str.size()) {
    if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == str[s])) {
      // Current characters match, or pattern has ?
      p++;
      s++;
    } else if (p < pattern.size() && pattern[p] == '*') {
      // Star found, remember position
      starIdx = p;
      matchIdx = s;
      p++;
    } else if (starIdx != std::string::npos) {
      // Mismatch, but we have a previous star
      // Backtrack: try matching one more character with the star
      p = starIdx + 1;
      matchIdx++;
      s = matchIdx;
    } else {
      // No match and no star to backtrack
      return false;
    }
  }

  // Check remaining pattern characters (should all be *)
  while (p < pattern.size() && pattern[p] == '*') {
    p++;
  }

  return p == pattern.size();
}

bool GlobMatchPath(const std::string &pattern, const std::string &path) {
  // Glob matching for paths with ** support for recursive matching
  // ** matches zero or more path segments (including /)
  // * matches any characters except /
  // ? matches single character except /

  size_t p = 0;
  size_t s = 0;
  size_t starStarIdx = std::string::npos;
  size_t starStarMatchIdx = 0;
  size_t starIdx = std::string::npos;
  size_t matchIdx = 0;

  while (s < path.size()) {
    // Check for **
    if (p + 1 < pattern.size() && pattern[p] == '*' && pattern[p + 1] == '*') {
      starStarIdx = p;
      starStarMatchIdx = s;
      p += 2;
      // Skip trailing / after **
      if (p < pattern.size() && pattern[p] == '/') {
        p++;
      }
      continue;
    }

    if (p < pattern.size() && pattern[p] == '*' &&
        (p + 1 >= pattern.size() || pattern[p + 1] != '*')) {
      // Single * - matches any except /
      starIdx = p;
      matchIdx = s;
      p++;
    } else if (p < pattern.size() &&
               ((pattern[p] == '?' && path[s] != '/') || pattern[p] == path[s])) {
      p++;
      s++;
    } else if (starIdx != std::string::npos && path[s] != '/') {
      // Backtrack to single star
      p = starIdx + 1;
      matchIdx++;
      s = matchIdx;
    } else if (starStarIdx != std::string::npos) {
      // Backtrack to double star
      p = starStarIdx + 2;
      if (p < pattern.size() && pattern[p] == '/') {
        p++;
      }
      starStarMatchIdx++;
      s = starStarMatchIdx;
      starIdx = std::string::npos;
    } else {
      return false;
    }
  }

  // Check remaining pattern
  while (p < pattern.size()) {
    if (pattern[p] == '*') {
      p++;
    } else if (p + 1 < pattern.size() && pattern[p] == '*' && pattern[p + 1] == '*') {
      p += 2;
    } else {
      break;
    }
  }

  return p == pattern.size();
}

char *dtoa(float f, char *buf) {
  // For float, use simple sprintf for now
  // dtoa_milo is optimized for double and doesn't work well with float
  int n = snprintf(buf, 384, "%.9g", static_cast<double>(f));
  if (n < 0 || n >= 384) {
    buf[0] = '0';
    return &buf[1];
  }
  return &buf[n];
}

char *dtoa(double d, char *buf) {
  // Use dtoa_milo for double precision
  return dtoa_milo(d, buf);
}

}  // namespace tinyusdz
