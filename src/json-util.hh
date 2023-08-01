// SPDX-License-Identifier: MIT
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Simple JSON parser/stringify
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace tinyusdz {

namespace json {

// Based on minijson (MIT license)
// https://github.com/mattn/minijson
//
typedef enum {
  unknown_type,
  null_type,
  boolean_type,
  number_type,
  string_type,
  array_type,
  object_type,
} json_type;

typedef enum {
  no_error,
  undefined_error,
  invalid_token_error,
  unknown_type_error,
  memory_allocation_error,
} json_error;

// TODO: Use tiny-variant or tiny-any
struct value
{
  json_type t{unknown_type};

  using Array = std::vector<value>;
  using Object = std::unordered_map<std::string, value>;

  union {
    std::nullptr_t n; // null
    bool b;
    double d;
    std::string* s;
    std::vector<value>* a;
    std::unordered_map<std::string, value> *o;
  } u;

  value() : u() {}
  value(std::nullptr_t n) : t(null_type), u() { u.n = n; }
  value(bool b) : t(boolean_type), u() { u.b = b; }
  value(double d) : t(boolean_type), u() { u.d = d; }
  value(const char* s) : t(string_type), u() { u.s = new std::string(s); }
  value(const std::string& s) : t(string_type), u() { u.s = new std::string(s); }
  value(const Array& a) : t(array_type), u() { u.a = new Array(a); }
  value(const Object& o) : t(object_type), u() { u.o = new Object(o); }
  value(const value& v) : t(v.t), u() {
    if (t == array_type) {
      u.a = new Array();
      *u.a = *v.u.a;
    } else if (t == object_type) {
      u.o = new Object();
      *u.o = *v.u.o;
    } else if (t == string_type) {
      u.s = new std::string();
      *u.s = *v.u.s;
    } else {
      u.d = v.u.d;
    }
  }

  ~value() {
    if (t == string_type) delete this->u.s;
    if (t == array_type) delete this->u.a;
    if (t == object_type) delete this->u.o;
  }

  const value& operator=(const value& v) {
    t = v.t;
    if (t == array_type) {
      u.a = new Array(*v.u.a);
    } else if (t == object_type) {
      u.o = new Object(*v.u.o);
    } else if (t == string_type) {
      u.s = new std::string(*v.u.s);
    } else {
      u.d = v.u.d;
    }
    return *this;
  }

};


bool loads(const std::string &str, value &json);
bool dumps(const value &json, std::string &str, uint32_t indent = 2);

} // namespace json

} // namespace tinyusdz
