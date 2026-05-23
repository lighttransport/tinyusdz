// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - Present, Syoyo Fujita.
//
// animatable.hh - Animatable<T> template for scalar and timeSamples values
//
#pragma once

#include <memory>
#include <type_traits>

#include "timesamples.hh"

namespace tinyusdz {

namespace animatable_detail {
// True iff value::TypeTraits<T> is a complete specialization (i.e. T is a
// registered value type that value::TimeSamples can store). False for enums and
// non-registered structs such as Extent — those keep a typed TypedTimeSamples<T>.
template <typename T, typename = void>
struct has_value_type_traits : std::false_type {};
template <typename T>
struct has_value_type_traits<
    T, std::void_t<decltype(value::TypeTraits<T>::type_id())>>
    : std::true_type {};
}  // namespace animatable_detail

//
// Scalar(default) and/or TimeSamples
//
// TimeSamples storage is type-dependent:
//  - value types (have a value::TypeTraits registration): stored type-erased in a
//    heap-allocated value::TimeSamples owned via unique_ptr (RAII; nullptr for the
//    common scalar-only case -> 8 bytes instead of an always-present container).
//  - enum types (no TypeTraits): kept in a typed TypedTimeSamples<T>, since the
//    type-erased value::TimeSamples is keyed on the value-type registry. This is a
//    small set of schema enums; the bulk value-type instantiations are gone.
//
template <typename T>
struct Animatable {
 private:
  // Use the typed store (TypedTimeSamples<T>) for types value::TimeSamples
  // cannot hold (enums, non-registered structs like Extent); use the type-erased
  // unique_ptr<value::TimeSamples> for registered value types.
  static constexpr bool kTyped =
      !animatable_detail::has_value_type_traits<T>::value;
  using TimeSampleStore =
      typename std::conditional<kTyped, TypedTimeSamples<T>,
                                std::unique_ptr<value::TimeSamples>>::type;

 public:
  bool is_blocked() const { return _blocked; }

  bool is_timesamples() const {
    if (is_blocked()) {
      return false;
    }
    if (_has_value) {
      return false;
    }
    if constexpr (kTyped) {
      return !_ts.empty();
    } else {
      return _ts && !_ts->empty();
    }
  }

  bool is_scalar() const {
    if (is_blocked()) {
      return false;
    }
    if constexpr (kTyped) {
      return _ts.empty();
    } else {
      return !_ts || _ts->empty();
    }
  }

  ///
  /// Get value at specific time.
  ///
  bool get(double t, T *v,
           const value::TimeSampleInterpolationType tinerp =
               value::TimeSampleInterpolationType::Linear) const {
    if (!v) {
      return false;
    }

    if (is_blocked()) {
      return false;
    }

    if (value::TimeCode(t).is_default()) {
      if (has_value()) {
        (*v) = _value;
        return true;
      }
    }

    if (has_timesamples()) {
      if constexpr (kTyped) {
        return _ts.get(v, t, tinerp);
      } else {
        return _ts->get(v, t, tinerp);
      }
    }

    if (has_default()) {
      return get_scalar(v);
    }

    return false;
  }

  ///
  /// Get scalar(default) value.
  ///
  bool get_scalar(T *v) const {
    if (!v) {
      return false;
    }

    if (is_blocked()) {
      return false;
    } else if (has_value()) {
      (*v) = _value;
      return true;
    }

    // timesamples
    return false;
  }

  bool get_default(T *v) const {
    return get_scalar(v);
  }

  // TimeSamples
  // void set(double t, const T &v);

  void add_sample(const double t, const T &v) {
    if constexpr (kTyped) {
      _ts.add_sample(t, v);
    } else {
      if (!_ts) _ts.reset(new value::TimeSamples());
      _ts->add_sample(t, v);
    }
  }

  // Add None(ValueBlock) sample to timesamples
  void add_blocked_sample(const double t) {
    if constexpr (kTyped) {
      _ts.add_blocked_sample(t);
    } else {
      if (!_ts) _ts.reset(new value::TimeSamples());
      _ts->template add_blocked_sample<T>(t);
    }
  }

  // Scalar
  void set(const T &v) {
    _value = v;
    _blocked = false;
    _has_value = true;
  }

  // Move overload for scalar - avoids copy for large vectors
  void set(T &&v) {
    _value = std::move(v);
    _blocked = false;
    _has_value = true;
  }

  void set_default(const T &v) {
    set(v);
  }

  void set_default(T &&v) {
    set(std::move(v));
  }

  // --- TimeSamples setters (target API; type-erased value::TimeSamples) ---
  // Only meaningful for value types. For enum types these are no-ops (enum
  // attributes use the TypedTimeSamples<T> setters below); they are never called
  // with a value::TimeSamples for an enum-valued attribute.
  void set_timesamples(value::TimeSamples &&ts) {
    if constexpr (!kTyped) {
      _ts.reset(new value::TimeSamples(std::move(ts)));
    } else {
      (void)ts;
    }
  }

  void set_timesamples(const value::TimeSamples &ts) {
    if constexpr (!kTyped) {
      _ts.reset(new value::TimeSamples(ts));
    } else {
      (void)ts;
    }
  }

  void clear_scalar() {
    _has_value = false;
  }

  void clear_timesamples() {
    if constexpr (kTyped) {
      _ts.samples().clear();
    } else {
      _ts.reset();
    }
  }

  bool has_value() const {
    return _has_value;
  }

  bool has_default() const {
    return has_value();
  }

  bool has_timesamples() const {
    if constexpr (kTyped) {
      return _ts.size() > 0;
    } else {
      return _ts && (_ts->size() > 0);
    }
  }

  // [Compat — Phase 2] Typed view of the timesamples. Existing consumers bind
  // this by const-ref (the temporary's lifetime is extended). For value types
  // it rebuilds a typed copy from the type-erased store; for enum types it copies
  // the typed store. Value-type consumers migrate to get_timesamples_ptr() and
  // this is removed in Phase 3.
  TypedTimeSamples<T> get_timesamples() const {
    if constexpr (kTyped) {
      return _ts;
    } else {
      TypedTimeSamples<T> r;
      if (_ts) {
        r.from_timesamples(*_ts);
      }
      return r;
    }
  }

  // Migration target: direct access to the type-erased storage (value types
  // only; nullptr for enum-valued attributes or when there are no timesamples).
  const value::TimeSamples *get_timesamples_ptr() const {
    if constexpr (kTyped) {
      return nullptr;
    } else {
      return _ts.get();
    }
  }
  value::TimeSamples *get_timesamples_ptr() {
    if constexpr (kTyped) {
      return nullptr;
    } else {
      return _ts.get();
    }
  }

  /// Get const reference to the scalar/default value (no copy).
  /// Only valid when has_default() is true.
  const T &get_scalar_ref() const { return _value; }

  Animatable() = default;

  Animatable(const T &v) {
    set(v);
  }

  // Deep copy (the unique_ptr store makes the implicit copy ops deleted for the
  // value-type case).
  Animatable(const Animatable &other)
      : _value(other._value),
        _has_value(other._has_value),
        _blocked(other._blocked) {
    if constexpr (kTyped) {
      _ts = other._ts;
    } else {
      if (other._ts) {
        _ts.reset(new value::TimeSamples(*other._ts));
      }
    }
  }

  Animatable &operator=(const Animatable &other) {
    if (this != &other) {
      _value = other._value;
      _has_value = other._has_value;
      _blocked = other._blocked;
      if constexpr (kTyped) {
        _ts = other._ts;
      } else {
        _ts.reset(other._ts ? new value::TimeSamples(*other._ts) : nullptr);
      }
    }
    return *this;
  }

  Animatable(Animatable &&) noexcept = default;
  Animatable &operator=(Animatable &&) noexcept = default;

 private:
  // scalar
  T _value{};
  bool _has_value{false};
  bool _blocked{false};

  // timesamples (type-erased unique_ptr for value types; typed for enum types)
  TimeSampleStore _ts;
};

}  // namespace tinyusdz
