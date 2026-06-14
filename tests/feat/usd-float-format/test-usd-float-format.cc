// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//
// Regression: opt-in OpenUSD-compatible float formatting (SetUSDFloatFormat).
//
// OpenUSD (pxr_double_conversion ToShortest/ToShortestSingle) prints floats with
// FIXED notation for decimal exponents in [-6, 15) and scientific outside, with
// NO '+' on positive exponents and NO zero-padding -- e.g. `0.000001`, `1e-7`,
// `0.0000019073486`, `1.234567890123456e15`. tinyusdz's default (shortest &
// robust dragonbox) switches to scientific below 1e-4 and pads the exponent to
// two digits with a sign -- `1e-06`, `1.9073486e-06`, `2.0887073e-07`.
//
// Both produce the same shortest round-trip DIGITS; only the notation differs.
// Default must stay byte-identical; the opt-in must reproduce usdcat's notation.

#include <iostream>
#include <string>

#include "str-util.hh"

using namespace tinyusdz;

namespace {

int g_failures = 0;

void expect(const std::string &got, const std::string &want,
            const std::string &what) {
  if (got != want) {
    std::cerr << "FAIL: " << what << " : got '" << got << "' want '" << want
              << "'\n";
    ++g_failures;
  }
}

}  // namespace

int main() {
  // (1) DEFAULT (shortest & robust) -- existing tinyusdz notation.
  SetUSDFloatFormat(false);
  expect(dtos(0.01), "0.01", "default 0.01");
  expect(dtos(0.0001), "0.0001", "default 1e-4");
  expect(dtos(1e-5), "1e-05", "default 1e-5 padded scientific");
  expect(dtos(1e-6), "1e-06", "default 1e-6 padded scientific");
  expect(dtos(1.9073486e-6), "1.9073486e-06", "default small padded");
  expect(dtos(2.0887073e-7), "2.0887073e-07", "default small padded2");
  expect(dtos(1234567890123456.0), "1234567890123456", "default 1.2e15 fixed");

  // (2) OPT-IN OpenUSD format -- matches usdcat.
  SetUSDFloatFormat(true);
  expect(dtos(0.01), "0.01", "usd 0.01");
  expect(dtos(0.0001), "0.0001", "usd 1e-4");
  expect(dtos(1e-5), "0.00001", "usd 1e-5 fixed");
  expect(dtos(1e-6), "0.000001", "usd 1e-6 fixed");
  expect(dtos(1e-7), "1e-7", "usd 1e-7 unpadded scientific");
  expect(dtos(1.9073486e-6), "0.0000019073486", "usd small fixed");
  expect(dtos(2.0887073e-7), "2.0887073e-7", "usd small unpadded scientific");
  expect(dtos(6.67572e-5), "0.0000667572", "usd 6.67e-5 fixed");
  expect(dtos(1234567890123456.0), "1.234567890123456e15",
         "usd 1.2e15 unpadded scientific");
  expect(dtos(0.3333333333333333), "0.3333333333333333", "usd 1/3 fixed");

  // float path (ToShortestSingle): same thresholds.
  expect(dtos(1.0e-7f), "1e-7", "usd float 1e-7 unpadded");
  expect(dtos(0.000001f), "0.000001", "usd float 1e-6 fixed");

  SetUSDFloatFormat(false);  // restore global

  // (3) Default restored.
  expect(dtos(1e-6), "1e-06", "default restored after opt-in");

  if (g_failures == 0) {
    std::cout << "test-usd-float-format: ALL PASS\n";
    return 0;
  }
  std::cerr << "test-usd-float-format: " << g_failures << " failure(s)\n";
  return 1;
}
