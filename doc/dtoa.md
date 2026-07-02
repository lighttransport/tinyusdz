# dtoa — float→string for the USDA writer

How `src/next` turns `float`/`double` into the shortest round-trippable decimal
string that OpenUSD `usdcat` emits, and how the zmij SIMD fast path makes it ~2×
faster on realistic data.

## Notation contract (usdcat)

The writer must be **byte-identical to `usdcat`** (roundtrip tests depend on it).
The notation (pxr `pxr_double_conversion` `ToShortest`/`ToShortestSingle`):

- Shortest decimal that round-trips the value (float: ≤9 sig digits, double: ≤17).
- **Fixed** notation for output decimal exponent in `[-6, 15)`, **scientific**
  outside.
- Exponent: leading `-` only (**no** `+`), minimal digits, **no** zero padding
  (`1e-7`, not `1e-07`).
- Integer-valued floats print with **no** trailing `.0` (`1.0` → `1`, `100.0` → `100`).
- `-0` is preserved; `nan` / `inf` / `-inf`.

Examples: `0.3`, `1.5`, `13.944`, `0.000001` (=1e-6, fixed), `1e-7` (scientific),
`1e15` (scientific), `-0`, `nan`.

## Architecture

`src/next/writer/dtoa.{hh,cc}` exposes `dtos()`, `dtos_append()`, `dtos_to()`
(hot path, into a caller buffer) and `format_g()` (`%g` for layer-meta doubles).
All shortest-mode entry points funnel through `dtoa_impl(v, buf)`:

```
dtoa_impl(v, buf):
    if g_zmij_fast_dtoa:
        e = zmij::write_usd_fast(buf, v)   # SIMD fast path, usdcat notation
        if e: return e                     # handled
    return dtoa_impl_t(v, buf, max_digits) # dragonbox fallback (handles everything)
```

- **Fast path** — `zmij::write_usd_fast` (a `LOCAL ADDITION` in the vendored
  `src/external/zmij/zmij.cc`). It reuses zmij's fixed-notation SIMD block (BCD
  digit extraction + a single-shuffle decimal-point placement) **verbatim** for the
  leading-decimal-exponent window where zmij's fixed notation is byte-identical to
  usdcat's: `dec_exp ∈ [-4, min(14, max_fixed_dec_exp)]`. That window covers
  essentially all authored coordinates/normals/UVs. It returns `nullptr` for
  everything else (special values, subnormals, scientific, and the `[-6,-5]` fixed
  bucket zmij renders scientific).
- **Fallback** — the original **dragonbox** renderer (`jkj::dragonbox::to_decimal`
  + a hand-written usdcat digit layout, `dtoa_impl_t`). It handles every remaining
  value. This is the pre-existing, already-usdcat-correct code, so the integrated
  `dtos_to` is byte-identical to the old one for *all* inputs.

### Buffer sizing (important)

The SIMD fast path writes **16-byte chunks that run past the logical end** of the
string. Every destination buffer must therefore provide slack:

- `kDtoaBufSize = 48` (in `dtoa.hh`) is the required minimum for any `dtos_to`
  destination (was 24/32 for the old dragonbox-only path).
- The value printer's `ChunkedStream` scalar buffers and the internal
  `dtos`/`dtos_append` buffers were grown to `kDtoaBufSize`.

### Escape hatch

`TINYUSDZ_NO_ZMIJ_DTOA=1` (read once into a cached global) forces the
dragonbox-only path — for A/B timing or platform safety. The fast path is proven
byte-identical, so this only affects speed.

## Benchmark & conformance harness

`sandbox/dtoa/` is a standalone harness that compared the digit cores (Dragonbox
vs Ryū vs zmij) and validated the fast path. It vendors ryu + zmij under
`third_party/`, renders each core's digits through a shared usdcat renderer, and:

- **Conformance (exhaustive):** `make check` verifies every candidate is
  byte-identical to the production `dtos_to` across **all 2³² float bit patterns +
  1e9 random doubles + curated edge cases** (0 mismatches). This is the gate any
  dtoa change must clear.
- **Throughput:** `make run_bench` / `run_bench_native` report ns/value over
  realistic-USD, random-bit-pattern, and `float3[]` workloads.

Key findings that shaped the design (see `sandbox/dtoa/README.md`):

1. A plain digit-core swap (Dragonbox→zmij, same scalar renderer) is only ~1.1×.
2. A scalar "lean" renderer does **not** beat the tuned dragonbox layout.
3. The 2× comes only from zmij's **SIMD** digit-placement — but that speed is
   inseparable from its layout, so we **reuse** its fixed block within the window
   where it already matches usdcat rather than regenerating SIMD tables.

## Results

- **Correctness:** exhaustively byte-identical (all 2³² floats + 1e9 doubles);
  26/26 next ctests pass (thread + nothread); TSan-clean on the parallel write;
  flatten output identical with the fast path on vs off on ALab (1.3 GB), two large
  proprietary scenes, and a 4.25 GB float-heavy USDA rewrite (also idempotent).
- **Pure dtoa (`sandbox/dtoa`, realistic USD, release-faithful):** float **1.88×**,
  double **2.43×**, `float3[]` **1.71×** vs the current dragonbox — matching zmij's
  own (non-usdcat) fused ceiling while emitting exact usdcat.
- **End-to-end USDA write:** ~3–5%. The write is **parallel** and dtoa is only a
  fraction of it (the rest is buffer `memmove` + structural text), so the 2× on the
  primitive dilutes to a few percent at the whole-file level. Larger for any
  dtoa-bound or single-threaded consumer of `dtos`.

## Testing

- `tests/next/test_dtoa.cc` (ctest `next_test_dtoa`): notation contract, round-trip
  over millions of random floats/doubles (spanning fast path + fallback), the three
  APIs agree byte-for-byte, and a buffer-canary overshoot check. Pass `exhaustive`
  as argv[1] to also sweep all 2³² floats.
- `tests/next/test_writer.cc` (ctest `next_test_writer`): stream-vs-string and
  `dtos_to`-vs-`dtos_append` byte-identity.
- `tests/fuzzer/next_dtoa_fuzzmain.cc` (`fuzz_next_dtoa`, built with
  `-DTINYUSDZ_NEXT_BUILD_FUZZERS=ON`): libFuzzer + ASAN/UBSAN. The input bytes are
  the float/double bit patterns, so it explores nan/inf/subnormal/huge/tiny values
  directly. Per value it checks: memory safety (formats into an *exact*
  `kDtoaBufSize` heap buffer, so ASAN's redzone catches any SIMD overshoot),
  round-trip bit-equality, three-API agreement, and no `+`/NUL in the output.

## Future enhancements

Ordered roughly by value/effort:

1. **Widen the SIMD fast path.** Two buckets currently fall to the scalar fallback:
   the `[-6,-5]` low-fixed bucket (usdcat fixed, zmij scientific) and `[15, …]`
   (usdcat scientific, zmij fixed). A usdcat **scientific** SIMD path (adapt zmij's
   `write_exp_float_simd` / exponent tables to the unpadded, no-`+` usdcat exponent
   format) would take scientific-heavy data (very small/large magnitudes, some time
   samples) off the scalar path.
2. **Attack the new write bottleneck.** With dtoa ~2×, the USDA write is now bound
   by `ChunkedStream` buffer assembly (`memmove`) and structural text. Profile the
   parallel write again and reduce copies / grow chunk granularity — this is where
   the next end-to-end write win lives, not in dtoa.
3. **`format_g` fast path.** Layer-meta / time-sample-key doubles still use the
   dragonbox `%g` renderer (`dtoa_g_impl`). Low volume, but a zmij-backed variant
   would remove the last dragonbox-only scalar hot spot if it ever matters.
4. **Scalar `half`/`float16`.** Scalar half currently isn't handled in the value
   printer's scalar switch (only half *arrays* are, via widening). Add a scalar
   `Half` case that widens to float and formats via `dtos_to`.
5. **Portability sweep.** The fast path exercises zmij's SSE4.1/SSE2 and NEON code
   (with a scalar BCD fallback inside zmij). Re-run the exhaustive gate on ARM/NEON
   and on an SSE2-only x86 target; confirm the `kDtoaBufSize` slack holds on each.
6. **Track upstream zmij.** zmij is slated to replace Dragonbox inside {fmt}. When
   upstream stabilizes, re-sync the vendored copy and re-apply the `LOCAL ADDITION`
   markers (`to_decimal(float)`, non-`inline` `to_decimal`, `write_usd_fast`); the
   exhaustive gate re-validates the re-sync.
7. **Consider retiring the dragonbox dependency** if a full usdcat SIMD renderer
   (fixed + scientific) lands — the fallback would become a small scalar usdcat
   renderer over zmij digits, dropping one vendored library.

## Files

- `src/next/writer/dtoa.{hh,cc}` — public API, `kDtoaBufSize`, fast-path wiring,
  dragonbox fallback, `TINYUSDZ_NO_ZMIJ_DTOA` escape hatch.
- `src/external/zmij/` — vendored zmij (MIT) with the `write_usd_fast` addition.
- `src/next/writer/value-printer.cc` — `ChunkedStream` (uses `dtos_to`); buffers
  grown to `kDtoaBufSize`.
- `sandbox/dtoa/` — the standalone benchmark + exhaustive conformance harness.
- `tests/next/test_dtoa.cc` — unit test (ctest `next_test_dtoa`).
- `tests/fuzzer/next_dtoa_fuzzmain.cc` — libFuzzer harness (`fuzz_next_dtoa`).
