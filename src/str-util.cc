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
      s += "\\\\";
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

}  // namespace tinyusdz
