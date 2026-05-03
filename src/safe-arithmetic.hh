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

// Checked multiplication for size_t (or any unsigned integral type)
// Returns false if overflow would occur
template <typename T>
inline typename std::enable_if<std::is_unsigned<T>::value, bool>::type
checked_mul(T a, T b, T* result) {
  if (b != 0 && a > std::numeric_limits<T>::max() / b) {
    return false;  // overflow would occur
  }
  *result = a * b;
  return true;
}

// Checked multiplication for size_t with three operands
template <typename T>
inline typename std::enable_if<std::is_unsigned<T>::value, bool>::type
checked_mul3(T a, T b, T c, T* result) {
  T tmp;
  if (!checked_mul(a, b, &tmp)) return false;
  return checked_mul(tmp, c, result);
}

// Convenience wrapper: compute size with overflow check
// Returns true on success, false on overflow
inline bool safe_size(size_t count, size_t element_size, size_t* out) {
  return checked_mul(count, element_size, out);
}

// Convenience wrapper for three-way size calculation
inline bool safe_size3(size_t a, size_t b, size_t c, size_t* out) {
  return checked_mul3(a, b, c, out);
}

// Checked addition
template <typename T>
inline typename std::enable_if<std::is_unsigned<T>::value, bool>::type
checked_add(T a, T b, T* result) {
  if (a > std::numeric_limits<T>::max() - b) {
    return false;  // overflow would occur
  }
  *result = a + b;
  return true;
}

// Special handling for uint64_t to size_t conversion with multiplication
// Used in crate reader where n is uint64_t but we need size_t bytes
inline bool safe_uint64_to_size_t_mul(uint64_t n, size_t element_size, size_t* out) {
  // First check if n fits in size_t
  if (n > std::numeric_limits<size_t>::max()) {
    return false;
  }
  size_t count = static_cast<size_t>(n);
  return checked_mul(count, element_size, out);
}

// Check if uint64_t value exceeds size_t max
inline bool uint64_fits_size_t(uint64_t v) {
  return v <= std::numeric_limits<size_t>::max();
}

}  // namespace safe
}  // namespace tinyusdz
