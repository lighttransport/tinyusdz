# zmij (vendored)

Żmij — a fast `double`/`float` → shortest-decimal string conversion algorithm
(Schubfach + xjb) by Victor Zverovich. Used by `src/next/writer/dtoa.cc` as the
usdcat-notation SIMD fast path.

- **Upstream:** https://github.com/vitaut/zmij
- **License:** MIT (see `LICENSE`), Copyright (c) 2025 Victor Zverovich.
- **Vendored:** 2026-07 (`zmij.h`, `zmij.cc` only; the C impl, tests and build
  files are not vendored).

## Local modifications

All local edits are marked with a `LOCAL ADDITION` / `NOTE (tinyusdz …)` comment
so they can be re-applied when re-syncing upstream:

1. **`zmij::to_decimal(float)`** — a float overload mirroring the upstream
   `to_decimal(double)` (upstream ships the `double` overload only; USD writes
   mostly `float3`).
2. **`to_decimal` de-`inline`d** — so it links as a real symbol from the
   separately-compiled `zmij.cc`.
3. **`zmij::write_usd_fast<Float>`** — emits OpenUSD/`usdcat` notation directly by
   reusing zmij's fixed-notation SIMD block for the exponent window where zmij's
   fixed output already matches usdcat, returning `nullptr` otherwise (the caller
   falls back to the dragonbox renderer).

## Validation

Correctness of `write_usd_fast` (byte-identity to usdcat across all 2³² floats +
1e9 doubles) and its speedup are covered by the standalone harness in
`sandbox/dtoa/` and the unit/fuzz tests `tests/next/test_dtoa.cc` /
`tests/fuzzer/next_dtoa_fuzzmain.cc`. See `doc/dtoa.md`.
