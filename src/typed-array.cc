// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Syoyo Fujita.
// Copyright 2025 - Present, Light Transport Entertainment Inc.

///
/// @file typed-array.cc
/// @brief TypedArray and TypedArrayView explicit template instantiations
///

#include "typed-array.hh"

namespace tinyusdz {

// Explicit template instantiations for commonly used types
// This helps reduce compilation time by pre-instantiating frequently used types

// TypedArray instantiations
template class TypedArray<uint8_t>;
template class TypedArray<uint16_t>;
template class TypedArray<uint32_t>;
template class TypedArray<uint64_t>;
template class TypedArray<int8_t>;
template class TypedArray<int16_t>;
template class TypedArray<int32_t>;
template class TypedArray<int64_t>;
template class TypedArray<float>;
template class TypedArray<double>;

// TypedArrayView instantiations (both const and non-const)
template class TypedArrayView<uint8_t>;
template class TypedArrayView<const uint8_t>;
template class TypedArrayView<uint16_t>;
template class TypedArrayView<const uint16_t>;
template class TypedArrayView<uint32_t>;
template class TypedArrayView<const uint32_t>;
template class TypedArrayView<uint64_t>;
template class TypedArrayView<const uint64_t>;
template class TypedArrayView<int8_t>;
template class TypedArrayView<const int8_t>;
template class TypedArrayView<int16_t>;
template class TypedArrayView<const int16_t>;
template class TypedArrayView<int32_t>;
template class TypedArrayView<const int32_t>;
template class TypedArrayView<int64_t>;
template class TypedArrayView<const int64_t>;
template class TypedArrayView<float>;
template class TypedArrayView<const float>;
template class TypedArrayView<double>;
template class TypedArrayView<const double>;

}  // namespace tinyusdz
