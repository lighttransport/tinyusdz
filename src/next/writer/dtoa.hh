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

#include <cstddef>
#include <cstdint>
#include <string>

namespace tinyusdz {
namespace next {

/// Shortest round-trippable decimal string for a float / double (OpenUSD
/// notation). Used by the USDA value printer.
std::string dtos(float v);
std::string dtos(double v);

/// Same as dtos(), but appends directly into `out` with no intermediate
/// std::string allocation (hot path for large numeric arrays). Byte-identical
/// to dtos().
void dtos_append(std::string& out, float v);
void dtos_append(std::string& out, double v);

/// Shortest decimal that parses back to the same IEEE binary16 bit pattern.
/// `bits` is the raw half representation; the spelling may intentionally be
/// shorter than formatting its widened float32 value (for example 0.35 rather
/// than 0.35009766).
std::string htos(uint16_t bits);
size_t htos_to(char* dst, uint16_t bits);
void htos_append(std::string& out, uint16_t bits);

/// Same as dtos(), but formats into a caller-provided buffer and returns the
/// byte count (no std::string at all). `dst` capacity must be >= 24 (float) /
/// >= 32 (double). Byte-identical to dtos(); used by the value printer to format
/// a scalar and append it to the chunk buffer in a single copy.
size_t dtos_to(char* dst, float v);
size_t dtos_to(char* dst, double v);

/// Freestanding `printf("%.*g", precision, v)` formatter (no libc / no locale):
/// round to `precision` significant digits, choose fixed vs scientific by the %g
/// rule (fixed iff -4 <= decimal-exponent < precision), strip trailing zeros,
/// scientific exponent `e±dd`. Used for the USDA layer-meta + time-sample-key
/// doubles (which previously went through ostream `setprecision`).
///
/// Contract: BYTE-IDENTICAL to `%.*g` whenever the value's shortest round-trip
/// decimal fits in `precision` significant digits — which holds for every
/// authored USD scalar (metersPerUnit, fps, time codes like 13.944, …) and is
/// verified byte-identical across the flatten test scenes. It is built on the
/// dragonbox SHORTEST digits rounded to `precision`, so for an arbitrary
/// full-precision double whose shortest needs > precision digits it can differ
/// from libc by one ULP in the last digit (a double-rounding artifact);
/// exact-in-all-cases rounding would need exact (big-integer) digit extraction.
std::string format_g(double v, int precision);
void format_g_append(std::string& out, double v, int precision);

}  // namespace next
}  // namespace tinyusdz
