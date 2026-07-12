// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// AOUSD Unicode identifier and path validation for the standalone next core.
// Reuses TinyUSDZ's generated Unicode XID tables (`src/unicode-xid.hh`).

#pragma once

#include "unicode-xid.hh"

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyusdz {
namespace next {

namespace identifier_detail {

inline bool DecodeUtf8(const char* data, size_t size, size_t offset,
                       uint32_t* codepoint, size_t* width) {
  if (!data || !codepoint || !width || offset >= size) return false;
  const uint8_t c0 = static_cast<uint8_t>(data[offset]);
  if (c0 <= 0x7f) {
    *codepoint = c0;
    *width = 1;
    return true;
  }
  size_t n = 0;
  uint32_t cp = 0;
  uint32_t minimum = 0;
  if (c0 >= 0xc2 && c0 <= 0xdf) {
    n = 2;
    cp = c0 & 0x1f;
    minimum = 0x80;
  } else if (c0 >= 0xe0 && c0 <= 0xef) {
    n = 3;
    cp = c0 & 0x0f;
    minimum = 0x800;
  } else if (c0 >= 0xf0 && c0 <= 0xf4) {
    n = 4;
    cp = c0 & 0x07;
    minimum = 0x10000;
  } else {
    return false;
  }
  if (offset + n > size) return false;
  for (size_t i = 1; i < n; ++i) {
    const uint8_t cx = static_cast<uint8_t>(data[offset + i]);
    if ((cx & 0xc0) != 0x80) return false;
    cp = (cp << 6) | (cx & 0x3f);
  }
  if (cp < minimum || cp > 0x10ffff ||
      (cp >= 0xd800 && cp <= 0xdfff)) {
    return false;
  }
  *codepoint = cp;
  *width = n;
  return true;
}

inline bool IsStart(uint32_t cp) {
  return cp == static_cast<uint32_t>('_') || unicode_xid::is_xid_start(cp);
}

inline bool IsContinue(uint32_t cp) {
  return unicode_xid::is_xid_continue(cp);
}

inline bool ParseIdentifier(const std::string& text, size_t* pos) {
  if (!pos || *pos >= text.size()) return false;
  uint32_t cp = 0;
  size_t width = 0;
  if (!DecodeUtf8(text.data(), text.size(), *pos, &cp, &width) ||
      !IsStart(cp)) {
    return false;
  }
  *pos += width;
  while (*pos < text.size()) {
    const size_t before = *pos;
    if (!DecodeUtf8(text.data(), text.size(), *pos, &cp, &width) ||
        !IsContinue(cp)) {
      *pos = before;
      break;
    }
    *pos += width;
  }
  return true;
}

inline void SkipSpaces(const std::string& text, size_t* pos) {
  while (*pos < text.size() &&
         (text[*pos] == ' ' || text[*pos] == '\t')) {
    ++*pos;
  }
}

inline bool ParseVariantSelection(const std::string& text, size_t* pos) {
  if (*pos >= text.size() || text[*pos] != '{') return false;
  ++*pos;
  SkipSpaces(text, pos);
  if (!ParseIdentifier(text, pos)) return false;
  SkipSpaces(text, pos);
  if (*pos >= text.size() || text[*pos] != '=') return false;
  ++*pos;
  SkipSpaces(text, pos);
  if (*pos < text.size() && text[*pos] != '}') {
    bool saw = false;
    if (text[*pos] == '.') {
      ++*pos;
      saw = true;
    }
    while (*pos < text.size() && text[*pos] != '}') {
      const char c = text[*pos];
      if (c == ' ' || c == '\t') break;
      if (c == '|' || c == '-') {
        ++*pos;
        saw = true;
        continue;
      }
      uint32_t cp = 0;
      size_t width = 0;
      if (!DecodeUtf8(text.data(), text.size(), *pos, &cp, &width) ||
          !IsContinue(cp)) {
        return false;
      }
      *pos += width;
      saw = true;
    }
    if (!saw) return false;
  }
  SkipSpaces(text, pos);
  if (*pos >= text.size() || text[*pos] != '}') return false;
  ++*pos;
  return true;
}

inline bool ParsePropertyName(const std::string& text, size_t* pos) {
  if (!ParseIdentifier(text, pos)) return false;
  while (*pos < text.size() && text[*pos] == ':') {
    ++*pos;
    if (!ParseIdentifier(text, pos)) return false;
  }
  return true;
}

inline bool ParsePathElements(const std::string& text, size_t* pos,
                              bool prim_required,
                              bool at_most_one_property = false) {
  bool have_prim = false;
  if (*pos < text.size() && text[*pos] != '.') {
    if (!ParseIdentifier(text, pos)) return false;
    have_prim = true;
    while (*pos < text.size()) {
      if (text[*pos] == '/') {
        ++*pos;
        if (!ParseIdentifier(text, pos)) return false;
        continue;
      }
      if (text[*pos] == '{') {
        do {
          if (!ParseVariantSelection(text, pos)) return false;
        } while (*pos < text.size() && text[*pos] == '{');
        // AOUSD also permits a prim element immediately after a selection.
        if (*pos < text.size() && text[*pos] != '.') {
          const size_t save = *pos;
          if (ParseIdentifier(text, pos)) continue;
          *pos = save;
        }
      }
      break;
    }
  }
  if (prim_required && !have_prim) return false;
  bool have_property = false;
  size_t property_count = 0;
  while (*pos < text.size() && text[*pos] == '.') {
    if (at_most_one_property && property_count != 0) return false;
    ++*pos;
    if (!ParsePropertyName(text, pos)) return false;
    have_property = true;
    ++property_count;
  }
  return have_prim || have_property;
}

}  // namespace identifier_detail

inline bool IsValidIdentifier(const std::string& text) {
  size_t pos = 0;
  return identifier_detail::ParseIdentifier(text, &pos) && pos == text.size();
}

inline bool IsValidNamespacedIdentifier(const std::string& text) {
  size_t pos = 0;
  return identifier_detail::ParsePropertyName(text, &pos) && pos == text.size();
}

/// Validate the AOUSD §8 textual Path production (angle brackets excluded).
inline bool IsValidPathString(const std::string& text) {
  if (text.empty()) return false;
  size_t pos = 0;
  if (text == "." || text == ".." || text == "/") return true;
  bool prim_required = false;
  if (text[0] == '/') {
    pos = 1;
    prim_required = true;
  } else if (text.size() >= 2 && text[0] == '.' && text[1] == '.') {
    pos = 2;
    if (pos == text.size()) return true;
    // The normative examples require repeated parent traversal (`../..`,
    // `../../Sibling`) even though the compact PEG spells out only one `..`.
    while (pos + 3 <= text.size() && text.compare(pos, 3, "/..") == 0 &&
           (pos + 3 == text.size() || text[pos + 3] == '/')) {
      pos += 3;
      if (pos == text.size()) return true;
    }
    if (text[pos] != '/') return false;
    ++pos;
  }
  if (pos >= text.size()) return false;
  if (!identifier_detail::ParsePathElements(text, &pos, prim_required,
                                             prim_required)) {
    return false;
  }
  return pos == text.size();
}

}  // namespace next
}  // namespace tinyusdz
