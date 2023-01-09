// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <array>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace tinyusdz {

enum class CharEncoding
{
  None,
  UTF8,
  UTF8BOM, // UTF8 + BOM
  UTF16LE  // UTF16 LE(Windows Unicode)
};

inline bool hasNewline(const std::string &str) {
  for (size_t i = 0; i < str.size(); i++) {
    if ((str[i] == '\r') || (str[i] == '\n')) {
      return true;
    }
  }

  return false;
}

inline bool startsWith(const std::string &str, const std::string &t) {
  return (str.size() >= t.size()) &&
         std::equal(std::begin(t), std::end(t), std::begin(str));
}

inline bool endsWith(const std::string &str, const std::string &suffix) {
  return (str.size() >= suffix.size()) &&
         (str.find(suffix, str.size() - suffix.size()) != std::string::npos);
}

inline std::string removePrefix(const std::string &str,
                                const std::string &prefix) {
  if (startsWith(str, prefix)) {
    return str.substr(prefix.length());
  }
  return str;
}

inline std::string removeSuffix(const std::string &str,
                                const std::string &suffix) {
  if (endsWith(str, suffix)) {
    return str.substr(0, str.length() - suffix.length());
  }
  return str;
}

inline bool contains(const std::string &str, char c) {
  return str.find(c) != std::string::npos;
}

inline size_t counts(const std::string &str, char c) {
  size_t cnt = 0;
  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == c) {
      cnt++;
    }
  }
  return cnt;
}

// Remove the beginning and the ending delimiter(s) from input string
// e.g. "mystring" -> mystring
// no error for an input string which does not contain `delim` in both side.
inline std::string unwrap(const std::string &str,
                          const std::string &delim = "\"") {
  size_t n = delim.size();

  if (str.size() < n) {
    return str;
  }

  std::string s = str;

  if (s.substr(0, n) == delim) {
    s.erase(0, n);
  }

  if (s.substr(s.size() - n) == delim) {
    s.erase(s.size() - n);
  }

  return s;
}

inline std::string unwrap(const std::string &str, const std::string &l,
                          const std::string &r) {
  return removePrefix(removeSuffix(str, r), l);
}

inline std::string quote(const char *s, const std::string &quote_str = "\"") {
  return quote_str + std::string(s) + quote_str;
}

inline std::string quote(const std::string &s,
                         const std::string &quote_str = "\"") {
  return quote_str + s + quote_str;
}

inline std::string wquote(const std::string &s,
                          const std::string &quote_lstr = "\"",
                          const std::string &quote_rstr = "\"") {
  return quote_lstr + s + quote_rstr;
}

#if 0
template<typename It>
inline It quote(const It& v, const std::string &quote_str = "\"") {

  It dst;

  for (typename It::const_iterator it = v.begin(); it != v.end(); ++it) {
    dst.emplace_back(quote((*it), quote_str));
  }

  return dst;
}
#else
inline std::vector<std::string> quote(const std::vector<std::string> &vs,
                                      const std::string &quote_str = "\"") {
  std::vector<std::string> dst;

  for (const auto &item : vs) {
    dst.emplace_back(quote(item, quote_str));
  }

  return dst;
}
#endif

// Python like join  ", ".join(v)
template <typename It>
inline std::string join(const std::string &sep, const It &v) {
  std::ostringstream oss;
  if (!v.empty()) {
    typename It::const_iterator it = v.begin();
    oss << *it++;
    for (typename It::const_iterator e = v.end(); it != e; ++it)
      oss << sep << *it;
  }
  return oss.str();
}

// To avoid splitting toooo large input text(e.g. few GB).
inline std::vector<std::string> split(
    const std::string &str, const std::string &sep,
    const uint32_t kMaxItems = std::numeric_limits<int32_t>::max() / 100) {
  size_t s;
  size_t e = 0;

  size_t count = 0;
  std::vector<std::string> result;

  while ((s = str.find_first_not_of(sep, e)) != std::string::npos) {
    e = str.find(sep, s);
    result.push_back(str.substr(s, e - s));
    if (count > kMaxItems) {
      break;
    }
  }

  return result;
}

//
// "{name=varname}"
//
// => ["name", "varname"]
//
// "{name=}"
//
// => ["name", ""]
//
// Return false when input string is not a variant element
//
inline bool tokenize_variantElement(const std::string &elementName, std::array<std::string, 2> *result = nullptr) {

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
      (*result)[0] = toks[0];
      (*result)[1] = std::string();
    }
    return true;
  } else if (toks.size() == 2) {
    if (result) {
      (*result)[0] = toks[0];
      (*result)[1] = toks[1];
    }
    return true;
  } else {
    return false;
  }

}

///
/// Test if str contains " or '
///
/// @param[in] is_double_quote true: find escaped triple double quotes. false find escaped single double quotes.
///
inline bool hasQuotes(const std::string &str, bool is_double_quote) {

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

///
/// Test if str contains """ or '''
///
/// @param[in] is_double_quote true: find escaped triple double quotes. false find escaped single double quotes.
inline bool hasTripleQuotes(const std::string &str, bool is_double_quote) {

  for (size_t i = 0; i < str.size(); i++) {
    if (i + 3 < str.size()) {
      if (is_double_quote) {
        if ((str[i+0] == '"') && (str[i+1] == '"') && (str[i+2] == '"')) {
          return true;
        }
      } else {
        if ((str[i+0] == '\'') && (str[i+1] == '\'') && (str[i+2] == '\'')) {
          return true;
        }
      }
    }
  }

  return false;
}

///
/// Test if str contains \""" or \'''
///
/// @param[in] is_double_quote true: find escaped triple double quotes. false find escaped single double quotes.
/// @param[out] n The number of escaped triple quotes
///
/// Return true immediately when an escaped triple quotes found when `n` is nullptr.
///
inline bool hasEscapedTripleQuotes(const std::string &str, bool is_double_quote, size_t *n = nullptr) {
  size_t count = 0;

  for (size_t i = 0; i < str.size(); i++) {
    if (str[i] == '\\') {
      if (i + 3 < str.size()) {
        if (is_double_quote) {
          if ((str[i+1] == '"') && (str[i+2] == '"') && (str[i+3] == '"')) {
            if (!n) { // early exit
              return true;
            }

            count++;
            i += 3;
          }
        } else {
          if ((str[i+1] == '\'') && (str[i+2] == '\'') && (str[i+3] == '\'')) {
            if (!n) { // early exit
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

inline std::string escapeSingleQuote(const std::string &str, const bool is_double_quote) {

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

// Escape backslash('\') to '\\'
inline std::string escapeBackslash(const std::string &str, const bool triple_quoted_string = false) {

  if (triple_quoted_string) {

    std::string s;

    // Do not escape \""" or \'''

    for (size_t i = 0; i < str.size(); i++) {
      if (str[i] == '\\') {
        if (i + 3 < str.size()) {
          if ((str[i+1] == '\'') && (str[i+1] == '\'') && (str[i+2] == '\'')) {
            s += "\\'''";
            i += 3;
          } else if ((str[i+1] == '"') && (str[i+1] == '"') && (str[i+2] == '"')) {
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

// Unescape backslash('\\' -> '\')
inline std::string unescapeBackslash(const std::string &str) {
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

inline std::string buildEescapedStringForUSDA(const std::string &str) {
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
  bool has_triple_single_quoted_string = hasTripleQuotes(str, false);
  bool has_triple_double_quoted_string = hasTripleQuotes(str, true);

  std::string s;

  if (has_newline || has_triple_single_quoted_string || has_triple_double_quoted_string) {

  } else {
    // single quote string.
    bool has_single_quote = hasQuotes(str, false);
    bool has_double_quote = hasQuotes(str, true);

    std::string delim = "\"";
    if (has_single_quote && has_double_quote) {
      s = escapeSingleQuote(str, true);
    } else if (has_single_quote) {
      s = escapeSingleQuote(str, false);
    } else {
      s = escapeSingleQuote(str, true);
    }
  }

  return s;
}

// TfIsValidIdentifier in pxrUSD equivalanet
inline bool isValidIdentifier(const std::string &str) {

  if (str.empty()) {
    return false;
  }

  // first char
  // [a-ZA-Z_]
  if ((('a' <= str[0]) && (str[0] <= 'z')) || (('A' <= str[0]) && (str[0] <= 'Z')) || (str[0] == '_')) {
    // ok
  } else {
    return false;
  }

  // remain chars
  // [a-ZA-Z0-9_]
  for (size_t i = 1; i < str.length(); i++) {
    if ((('a' <= str[i]) && (str[i] <= 'z')) || (('A' <= str[i]) && (str[i] <= 'Z')) || (('0' <= str[i]) && (str[i] <= '9')) || (str[i] == '_')) {
      // ok
    } else {
      return false;
    }
  }

  return true;
}


// TfMakeValidIdentifier in pxrUSD equivalanet
inline std::string makeIdentifierValid(const std::string &str) {
  std::string s;

  if (str.empty()) {
    // return '_'
    return "_";
  }

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

  return s;
}

#if 0
template<typename It>
inline std::string quote_then_join(const std::string& sep, const It& v, const std::string &quote = "\"")
{
  std::ostringstream oss;
  if (!v.empty()) {
    typename It::const_iterator it = v.begin();
    oss << wrap(*it++;
    for (typename It::const_iterator e = v.end(); it != e; ++it)
      oss << sep << *it;
  }
  return oss.str();
}
#endif

#if 0
template<typename It>
inline std::string join(const std::string& sep, It& v)
{
  std::ostringstream oss;
  if (!v.empty()) {
    typename It::iterator it = v.begin();
    oss << *it++;
    for (typename It::iterator e = v.end(); it != e; ++it)
      oss << sep << *it;
  }
  return oss.str();
}
#endif

}  // namespace tinyusdz
