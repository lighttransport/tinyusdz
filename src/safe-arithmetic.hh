// SPDX-License-Identifier: Apache 2.0
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// Safe arithmetic operations with overflow checking
//
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace tinyusdz {
namespace safe {

///
/// Safe multiplication: a * b -> result (as size_t)
/// Works with any integer types that can be converted to size_t.
/// Returns true and sets *out on success, false on overflow.
///
template <typename A, typename B>
inline bool mul(A a, B b, size_t* out) {
  static_assert(std::is_integral<A>::value && std::is_integral<B>::value,
                "mul requires integral types");
  size_t sa = static_cast<size_t>(a);
  size_t sb = static_cast<size_t>(b);
  if ((sb != 0) && (sa > (std::numeric_limits<size_t>::max)() / sb)) {
    return false;  // overflow would occur
  }
  *out = sa * sb;
  return true;
}

///
/// Safe multiplication with three operands: a * b * c -> result (as size_t)
/// Returns true and sets *out on success, false on overflow.
///
template <typename A, typename B, typename C>
inline bool mul3(A a, B b, C c, size_t* out) {
  static_assert(std::is_integral<A>::value && std::is_integral<B>::value && std::is_integral<C>::value,
                "mul3 requires integral types");
  size_t tmp;
  if (!mul(a, b, &tmp)) return false;
  return mul(tmp, c, out);
}

///
/// Safe addition: a + b -> result (as size_t)
/// Returns true and sets *out on success, false on overflow.
///
template <typename A, typename B>
inline bool add(A a, B b, size_t* out) {
  static_assert(std::is_integral<A>::value && std::is_integral<B>::value,
                "add requires integral types");
  size_t sa = static_cast<size_t>(a);
  size_t sb = static_cast<size_t>(b);
  if (sa > (std::numeric_limits<size_t>::max)() - sb) {
    return false;  // overflow would occur
  }
  *out = sa + sb;
  return true;
}

///
/// Safe uint64_t n -> size_t with sizeof(T) multiplication
/// For patterns like: size_t byte_count = sizeof(T) * arr.size();
/// Returns true and sets *out on success, false on overflow.
///
template <typename T>
inline bool n_to_size(uint64_t n, size_t* out) {
  // On 32-bit platforms `n` may not fit in size_t. On 64-bit they are
  // the same width, so the runtime check would be tautological — clang's
  // -Wtautological-type-limit-compare flags it. `if constexpr` does not
  // discard the body at template-definition time, so use the preprocessor.
#if SIZE_MAX < UINT64_MAX
  if (n > (std::numeric_limits<size_t>::max)()) {
    return false;
  }
#endif
  size_t count = static_cast<size_t>(n);
  return mul(count, sizeof(T), out);
}

}  // namespace safe
}  // namespace tinyusdz
