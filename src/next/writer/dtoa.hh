// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - dragonbox-based float-to-string (dtoa)
//
// Same shortest-round-trippable formatter tinyusdz uses in `src/str-util.cc`
// (`dtos`), ported into the standalone next module. Emits OpenUSD-compatible
// notation (pxr_double_conversion ToShortest/ToShortestSingle): the shortest
// decimal that round-trips the value, fixed for decimal exponents in [-6, 15)
// and scientific outside, with NO `+` on positive exponents, NO zero-padding,
// and `-0` preserved -- matching `usdcat` byte-for-byte.

#pragma once

#include <string>

namespace tinyusdz {
namespace next {

/// Shortest round-trippable decimal string for a float / double (OpenUSD
/// notation). Used by the USDA value printer.
std::string dtos(float v);
std::string dtos(double v);

}  // namespace next
}  // namespace tinyusdz
