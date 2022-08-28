/// Copyright 2021-present Syoyo Fujita.
/// MIT license.
///
/// Type-erasure technique for Attribute/PrimVar(Primitive Variables), a Value class which can have 30+ different types(and can be compound-types(e.g. 1D/2D array, dictionary).
/// Neigher std::any nor std::variant is applicable for such usecases, so write our own, handy typesystem.
///
#pragma once

#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <type_traits>
#include <vector>
#include <cmath>

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

// TODO(syoyo): Use C++17 std::optional, std::string_view when compiled with C++-17 compiler

// clang and gcc
#if defined(__EXCEPTIONS) || defined(__cpp_exceptions)
#define nsel_CONFIG_NO_EXCEPTIONS 0
#define nssv_CONFIG_NO_EXCEPTIONS 0
#else
// -fno-exceptions
#define nsel_CONFIG_NO_EXCEPTIONS 1
#define nssv_CONFIG_NO_EXCEPTIONS 1
#endif
#include "nonstd/optional.hpp"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include "value-types.hh"

namespace tinyusdz {
namespace primvar {

struct PrimVar {
  // For scalar(default) value, times.size() == 0, and values.size() == 1
  value::TimeSamples var;

  bool is_scalar() const {
    return (var.times.size() == 0) && (var.values.size() == 1);
  }

  bool is_timesample() const {
    return (var.times.size() > 0) && (var.times.size() == var.values.size());
  }

  bool is_valid() const { return is_scalar() || is_timesample(); }

  std::string type_name() const {
    if (!is_valid()) {
      return std::string();
    }
    return var.values[0].type_name();
  }

  uint32_t type_id() const {
    if (!is_valid()) {
      return value::TYPE_ID_INVALID;
    }

    return var.values[0].type_id();
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_value() const {

    if (!is_scalar()) {
      return nonstd::nullopt;
    }

    if (value::TypeTrait<T>::type_id == var.values[0].type_id()) {
      //return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
      auto pv = linb::any_cast<const T>(&var.values[0]);
      if (pv) {
        return (*pv);
      }
      return nonstd::nullopt;
    } else if (value::TypeTrait<T>::underlying_type_id == var.values[0].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      //return *reinterpret_cast<const T *>(var.values[0].value());
      
      // TODO: strict type check.
      return *linb::cast<const T>(&var.values[0]);
    }
    return nonstd::nullopt;
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_ts_value(size_t idx) const {

    if (is_timesample()) {
      return nonstd::nullopt;
    }

    if (idx >= var.times.size()) {
      return nonstd::nullopt;
    }

    if (value::TypeTrait<T>::type_id == var.values[idx].type_id()) {
      //return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
      auto pv = linb::any_cast<const T>(&var.values[idx]);
      if (pv) {
        return (*pv);
      }
      return nonstd::nullopt;
    } else if (value::TypeTrait<T>::underlying_type_id == var.values[idx].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      //return *reinterpret_cast<const T *>(var.values[0].value());
      
      // TODO: strict type check.
      return *linb::cast<const T>(&var.values[idx]);
    }
    return nonstd::nullopt;
  }

  // Returns nullptr when type-mismatch.
  template <class T>
  const T* as() const {

    if (!is_scalar()) {
      return nullptr;
    }

    if (value::TypeTrait<T>::type_id == var.values[0].type_id()) {
      //return std::move(*reinterpret_cast<const T *>(var.values[0].value()));
      return linb::any_cast<const T>(&var.values[0]);
    } else if (value::TypeTrait<T>::underlying_type_id == var.values[0].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      // TODO: strict type check.
      return *linb::cast<const T>(&var.values[0]);
    }

    return nullptr;
  }

  template <class T>
  void set_scalar(const T &v) {
    var.times.clear();
    var.values.clear();

    var.values.push_back(v);
  }

  void set_timesamples(const value::TimeSamples &v) {
    var = v;
  }
  
  void set_timesamples(value::TimeSamples &&v) {
    var = std::move(v);
  }

};


} // namespace primvar
} // namespace tinyusdz
