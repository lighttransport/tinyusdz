// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// sandbox/dtoa — candidate dtoa adapters, all producing OpenUSD notation via the
// shared usddtoa::render_usd_finite (except zmij_native, a raw speed ceiling).
//
//   oracle        : the repo's real src/next/writer dtos_to (golden reference)
//   dragonbox     : jkj::dragonbox::to_decimal  -> render_usd_finite  (== baseline)
//   ryu           : ryu f2d/d2d (via ryu_*dec)  -> render_usd_finite
//   zmij          : zmij::to_decimal            -> render_usd_finite
//   zmij_native   : raw zmij::write (NOT USD notation; a fusion-speed ceiling)
//
// Each `usd_dtos_<x>(char* dst, T v)` returns the byte count. `dst` capacity
// must be >= 32 (matches the real value printer's stack buffer).

#pragma once

#include <cstddef>
#include <cmath>

#include "render_usd.hh"

// --- oracle: the repo's production dtoa -------------------------------------
#include "../../src/next/writer/dtoa.hh"

// --- dragonbox (baseline core) ----------------------------------------------
#include "../../src/external/dragonbox/dragonbox.h"

// --- ryu core (digits+exp via vendored ryu) ---------------------------------
#include "ryu_decimal.h"

// --- zmij core + native -----------------------------------------------------
#include "zmij.h"

namespace usddtoa {

// ---- oracle ----------------------------------------------------------------
inline std::size_t usd_dtos_oracle(char* dst, float v) {
  return tinyusdz::next::dtos_to(dst, v);
}
inline std::size_t usd_dtos_oracle(char* dst, double v) {
  return tinyusdz::next::dtos_to(dst, v);
}

// ---- dragonbox core -> shared renderer -------------------------------------
inline std::size_t usd_dtos_dragonbox(char* dst, float v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  auto r = jkj::dragonbox::to_decimal(v);
  return render_usd_finite(dst, r.significand, r.exponent, std::signbit(v), 9);
}
inline std::size_t usd_dtos_dragonbox(char* dst, double v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  auto r = jkj::dragonbox::to_decimal(v);
  return render_usd_finite(dst, r.significand, r.exponent, std::signbit(v), 17);
}

// ---- ryu core -> shared renderer -------------------------------------------
inline std::size_t usd_dtos_ryu(char* dst, float v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  ryu_dec32 r = ryu_f2dec(v);
  return render_usd_finite(dst, r.sig, r.exp, r.neg != 0, 9);
}
inline std::size_t usd_dtos_ryu(char* dst, double v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  ryu_dec64 r = ryu_d2dec(v);
  return render_usd_finite(dst, r.sig, r.exp, r.neg != 0, 17);
}

// ---- zmij core -> shared renderer ------------------------------------------
inline std::size_t usd_dtos_zmij(char* dst, float v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  zmij::dec_fp r = zmij::to_decimal(v);
  return render_usd_finite(dst, static_cast<uint64_t>(r.sig), r.exp, r.negative,
                           9);
}
inline std::size_t usd_dtos_zmij(char* dst, double v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  zmij::dec_fp r = zmij::to_decimal(v);
  return render_usd_finite(dst, static_cast<uint64_t>(r.sig), r.exp, r.negative,
                           17);
}

// ---- FUSED: core -> lean usdcat layout (render_usd_fused) ------------------
// The point of the exercise: zmij's fast digit core + a usdcat renderer tailored
// to it (no trim loop, no integer re-decomposition). dragonbox_fused isolates the
// renderer win from the core win.
inline std::size_t usd_dtos_zmij_fused(char* dst, float v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  zmij::dec_fp r = zmij::to_decimal(v);
  return render_usd_fused(dst, static_cast<uint64_t>(r.sig), r.exp, r.negative, 9);
}
inline std::size_t usd_dtos_zmij_fused(char* dst, double v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  zmij::dec_fp r = zmij::to_decimal(v);
  return render_usd_fused(dst, static_cast<uint64_t>(r.sig), r.exp, r.negative, 17);
}
inline std::size_t usd_dtos_dragonbox_fused(char* dst, float v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  auto r = jkj::dragonbox::to_decimal(v);
  return render_usd_fused(dst, r.significand, r.exponent, std::signbit(v), 9);
}
inline std::size_t usd_dtos_dragonbox_fused(char* dst, double v) {
  std::size_t n;
  if (try_special(dst, v, &n)) return n;
  auto r = jkj::dragonbox::to_decimal(v);
  return render_usd_fused(dst, r.significand, r.exponent, std::signbit(v), 17);
}

// ---- zmij SIMD (fixed-notation fast path + scalar fallback) ----------------
// The real fusion: zmij's fixed-notation SIMD block emitting usdcat directly for
// the common exponent window, falling back to the scalar usdcat path only for
// the rare edge/scientific cases. Byte-identical to the oracle (exhaustive gate).
inline std::size_t usd_dtos_zmij_simd(char* dst, float v) {
  char* e = zmij::write_usd_fast(dst, v);
  if (e) return static_cast<std::size_t>(e - dst);
  return usd_dtos_zmij(dst, v);  // scalar fallback (conformant)
}
inline std::size_t usd_dtos_zmij_simd(char* dst, double v) {
  char* e = zmij::write_usd_fast(dst, v);
  if (e) return static_cast<std::size_t>(e - dst);
  return usd_dtos_zmij(dst, v);
}

// ---- zmij native (raw fused write; NOT USD notation) -----------------------
// Speed ceiling only: this is zmij's own Python-style output (e.g. "1e-07"),
// which does NOT match usdcat. Not run through the conformance gate.
inline std::size_t usd_dtos_zmij_native(char* dst, float v) {
  char* e = zmij::write(dst, 32, v);
  return static_cast<std::size_t>(e - dst);
}
inline std::size_t usd_dtos_zmij_native(char* dst, double v) {
  char* e = zmij::write(dst, 32, v);
  return static_cast<std::size_t>(e - dst);
}

}  // namespace usddtoa
