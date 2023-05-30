// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment, Inc.
#include "str-util.hh"

#include "common-macros.inc"

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
        if ((str[i+1] == '"') || (str[i+1] == '\'')) {
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
        if (str[i+1] == 'a') {
          s += '\a'; i++;      
        } else if (str[i+1] == 'b') {
          s += '\b'; i++;      
        } else if (str[i+1] == 't') {
          s += '\t'; i++;      
        } else if (str[i+1] == 'v') {
          s += '\v'; i++;      
        } else if (str[i+1] == 'f') {
          s += '\f'; i++;      
        } else if (str[i+1] == 'n') {
          s += '\n'; i++;      
        } else if (str[i+1] == 'r') {
          s += '\r'; i++;      
        } else if (str[i+1] == '\\') {
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

bool hasEscapedTripleQuotes(const std::string &str, bool is_double_quote, size_t *n) {
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

std::string escapeSingleQuote(const std::string &str, const bool is_double_quote) {

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

std::string escapeBackslash(const std::string &str, const bool triple_quoted_string) {

  if (triple_quoted_string) {

    std::string s;

    // Do not escape \""" or \'''

    for (size_t i = 0; i < str.size(); i++) {
      if (str[i] == '\\') {
        if (i + 3 < str.size()) {
          if ((str[i+1] == '\'') && (str[i+2] == '\'') && (str[i+3] == '\'')) {
            s += "\\'''";
            i += 3;
          } else if ((str[i+1] == '"') && (str[i+2] == '"') && (str[i+3] == '"')) {
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

bool tokenize_variantElement(const std::string &elementName, std::array<std::string, 2> *result) {

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
bool makeUniqueName(std::multiset<std::string> &nameSet, const std::string &name, std::string *unique_name) {
  if (!unique_name) {
    return false;
  }

  if (nameSet.count(name) == 0) {
    (*unique_name) = name;
    return 0;
  }

  // Simply add number

  const size_t kMaxLoop = 1024; // to avoid infinite loop.

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

}  // namespace tinyusdz
