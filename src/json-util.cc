// SPDX-License-Identifier: MIT
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "json-util.hh"

// #include "external/sajson.h"

namespace tinyusdz {
namespace json {

// Based on minijson: https://github.com/mattn/minijson

#if 0
#define MINIJSON_SKIP(i)                \
  while (*i && strchr("\r\n \t", *i)) { \
    i++;                                \
  }
#endif

template <typename Iter>
inline json_error parse_object(Iter& i, value& v) {
  value::Object o;
  i++;
  _MINIJSON_SKIP(i);
  if (*i != '\x7d') {
    while (*i) {
      value vk, vv;
      json_error e = parse_string(i, vk);
      if (e != no_error) return e;
      _MINIJSON_SKIP(i);
      if (*i != ':') return invalid_token_error;
      i++;
      e = parse_any(i, vv);
      if (e != no_error) return e;
#if defined(_MSC_VER) && _MSC_VER <= 6000
      string s;
      vk.get(s);
      o[s] = vv;
#else
      // TODO
      //o[vk.get<std::string>()] = vv;
#endif
      _MINIJSON_SKIP(i);
      if (*i == '\x7d') break;
      if (*i != ',') return invalid_token_error;
      i++;
      _MINIJSON_SKIP(i);
#ifdef MINIJSON_LIBERAL
      if (*i == '\x7d') break;
#endif
    }
  }
  v = value(o);
  i++;
  return no_error;
}

}  // namespace json
}  // namespace tinyusdz
