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
#ifndef nsel_CONFIG_NO_EXCEPTIONS
#define nsel_CONFIG_NO_EXCEPTIONS 1
#endif

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
  value::TimeSamples _var;

  bool is_scalar() const {
    return (_var.times.size() == 0) && (_var.values.size() == 1);
  }

  bool is_timesample() const {
    return (_var.times.size() > 0) && (_var.times.size() == _var.values.size());
  }

  bool is_valid() const { return is_scalar() || is_timesample(); }

  std::string type_name() const {
    if (!is_valid()) {
      return std::string();
    }
    return _var.values[0].type_name();
  }

  uint32_t type_id() const {
    if (!is_valid()) {
      return value::TYPE_ID_INVALID;
    }

    return _var.values[0].type_id();
  }

  // Type-safe way to get concrete value.
  // NOTE: This consumes lots of stack size(rougly 1000 bytes),
  // If you need to handle multiple types, use as() insted.
  // 
  template <class T>
  nonstd::optional<T> get_value() const {

    if (!is_scalar()) {
      return nonstd::nullopt;
    }

    return _var.values[0].get_value<T>();
  }

  nonstd::optional<double> get_ts_time(size_t idx) const {

    if (!is_timesample()) {
      return nonstd::nullopt;
    }

    if (idx >= _var.times.size()) {
      return nonstd::nullopt;
    }

    return _var.times[idx];
  }

  // Type-safe way to get concrete value.
  template <class T>
  nonstd::optional<T> get_ts_value(size_t idx) const {

    if (!is_timesample()) {
      return nonstd::nullopt;
    }

    if (idx >= _var.times.size()) {
      return nonstd::nullopt;
    }

#if 0
    if (value::TypeTraits<T>::type_id == _var.values[idx].type_id()) {
      //return std::move(*reinterpret_cast<const T *>(_var.values[0].value()));
      auto pv = linb::any_cast<const T>(&_var.values[idx]);
      if (pv) {
        return (*pv);
      }
      return nonstd::nullopt;
    } else if (value::TypeTraits<T>::underlying_type_id == _var.values[idx].underlying_type_id()) {
      // `roll` type. Can be able to cast to underlying type since the memory
      // layout does not change.
      //return *reinterpret_cast<const T *>(_var.values[0].value());

      // TODO: strict type check.
      return *linb::cast<const T>(&_var.values[idx]);
    }
    return nonstd::nullopt;
#else
    return _var.values[idx].get_value<T>();
#endif
  }

  // Check if specific a TimeSample value for a specified index is ValueBlock or not.
  nonstd::optional<bool> is_ts_value_blocked(size_t idx) const {

    if (!is_timesample()) {
      return nonstd::nullopt;
    }

    if (idx >= _var.times.size()) {
      return nonstd::nullopt;
    }

    if (auto pv = _var.values[idx].get_value<value::ValueBlock>()) {
      return true;
    }

    return false;
  }

  // Returns nullptr when type-mismatch.
  template <class T>
  const T* as() const {

    if (!is_scalar()) {
      return nullptr;
    }

    return _var.values[0].as<T>();

    return nullptr;
  }

  template <class T>
  void set_scalar(const T &v) {
    _var.times.clear();
    _var.values.clear();

    _var.values.push_back(v);
  }

  void set_timesamples(const value::TimeSamples &v) {
    _var = v;
  }

  void set_timesamples(value::TimeSamples &&v) {
    _var = std::move(v);
  }

  template <typename T>
  void set_ts_value(double t, const T &v) {
    if (is_scalar()) {
      _var.times.clear();
      _var.values.clear();
    }
    _var.times.push_back(t);
    _var.values.push_back(v);
  }

  size_t num_timesamples() const {
    if (is_timesample()) {
      return _var.times.size();
    }
    return 0;
  }

  const value::TimeSamples &var() const {
    return _var;
  }
  
  value::TimeSamples &var() {
    return _var;
  }

};


} // namespace primvar
} // namespace tinyusdz
