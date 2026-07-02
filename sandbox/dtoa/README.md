# sandbox/dtoa — USD-optimized dtoa comparison

Standalone benchmark + exhaustive conformance harness comparing shortest-round-trip
float→string ("dtoa") **cores** for the OpenUSD/`usdcat` ASCII writer:

| candidate | core algorithm | source | how it renders |
|---|---|---|---|
| **oracle** | Dragonbox | the repo's real `src/next/writer/dtoa.cc` `dtos_to` | production (golden reference) |
| **dragonbox** | Dragonbox `to_decimal` | `src/external/dragonbox` | shared `render_usd` |
| **ryu** | Ryū `f2d`/`d2d` | vendored `third_party/ryu` (Apache-2.0/BSL) | shared `render_usd` |
| **zmij** | Żmij `to_decimal` (Schubfach+xjb) | vendored `third_party/zmij` (MIT) | shared `render_usd_finite` |
| **zmij_simd** | Żmij `to_decimal` | vendored `third_party/zmij` | **zmij's fixed-notation SIMD block, emitting usdcat** (+ scalar fallback) — the 2× win |
| **zmij_fused** | Żmij `to_decimal` | vendored `third_party/zmij` | lean scalar `render_usd_fused` (a failed attempt) |
| **dragonbox_fused** | Dragonbox `to_decimal` | `src/external/dragonbox` | lean scalar `render_usd_fused` (isolates the renderer) |
| **zmij_native\*** | Żmij fused `write` | vendored `third_party/zmij` | its own Python-style string (**NOT** usdcat) |

Motivation: profiling the `src/next` USDA flatten/write path showed float→string is
the single biggest wall-clock consumer (`dtoa_impl_t<float>` ~17-21% self-time). The
current formatter already uses Dragonbox; this sandbox asks whether Ryū or Żmij's
digit cores are faster — **while staying byte-identical to `usdcat`**.

## Design

A "dtoa" does two things: (1) generate the shortest decimal digits, and (2) render
them in USD notation (`nan`/`inf`/`-0`, fixed for decimal exponent in `[-6,15)` else
scientific, no `+`/no zero-pad on the exponent, `1.0`→`1`). To compare **cores**
fairly, `render_usd.hh` factors out step (2) — copied verbatim from `dtoa.cc` plus a
trailing-zero trim so every core feeds the same canonical shortest `(significand,
exponent)`. Each candidate = *its core* → *the same `render_usd_finite`*, so any speed
difference is purely the digit algorithm.

`zmij_native*` is the exception: raw fused `zmij::write` (digit-gen + its own
rendering in one pass). It does **not** emit usdcat notation, so it is a **speed
ceiling only**, not a drop-in — it shows what a *fused* USD renderer could reach.

Three rendering strategies are compared:
- `render_usd_finite` (in `render_usd.hh`) — verbatim from the shipping `dtoa.cc`.
- `render_usd_fused` (in `render_usd.hh`) — a scalar "lean layout" attempt (place the
  point by byte moves, skip the trim loop). Byte-identical but **not** faster.
- **`zmij::write_usd_fast`** (a local addition in `third_party/zmij/zmij.cc`) — the
  winner: zmij's fixed-notation SIMD block emitting usdcat directly, `nullptr` →
  scalar fallback outside the fixed window. This is the actual "fuse zmij with usdcat".

Files: `render_usd.hh` (scalar renderers), `dtoa_candidates.hh` (adapters),
`ryu_decimal.h`/`ryu_{f,d}2dec.c` (expose Ryū's internal decimal),
`conformance.cc` (byte-identity gate, 6 candidates), `bench.cc` (throughput).
Local vendored-source edits (all marked `LOCAL ADDITION`): `zmij::to_decimal(float)`,
non-`inline` `to_decimal`, and `zmij::write_usd_fast<Float>` (the usdcat SIMD path).

Local edits to vendored sources (documented inline): added `zmij::to_decimal(float)`
(upstream only ships the `double` overload; USD writes mostly `float3`) and dropped
`inline` on `to_decimal` so it links from a separately-compiled `zmij.cc`.

## Build & run

```bash
make                 # conformance + bench + bench_native
make check           # EXHAUSTIVE gate: all 2^32 floats + 1e9 doubles (minutes)
make check_quick     # curated + 10M random floats + 10M doubles (seconds)
make run_bench       # release-faithful (-O3 -DNDEBUG -fno-exceptions -fno-rtti)
make run_bench_native# + -march=native (cores' full BMI/lzcnt potential)
```

## Results

### Conformance — PASS (exhaustive)

All six candidates — `dragonbox`, `ryu`, `zmij`, `zmij_fused`, `dragonbox_fused`,
and **`zmij_simd`** — are **byte-identical to the oracle** across **all 2³² float bit
patterns + 1,000,000,000 random doubles + curated edge cases** (0 mismatches). The
SIMD fast path `zmij_simd` is provably a drop-in for usdcat notation. (`zmij_native*`
is not gated — non-usdcat by design.)

### Throughput (ns per value, best-of-5; this machine)

Realistic USD values (coords/normals/uvs — the dominant real case). All conformant
candidates emit identical usdcat bytes; `zmij_native*` is the non-usdcat ceiling.

Release-faithful (`-O3 -DNDEBUG -fno-exceptions -fno-rtti`, no `-march=native` — how
the shipping lib builds):

| candidate | float | double | float3[] (per triple) |
|---|--:|--:|--:|
| **dragonbox** (current, `render_usd_finite`) | 34.9 | 53.7 | 116.9 |
| **zmij_simd** (SIMD fixed + fallback) | **18.6 (1.88×)** | **22.2 (2.43×)** | **68.5 (1.71×)** |
| zmij (core swap, same renderer) | 31.8 (1.10×) | 47.4 (1.14×) | 107.2 (1.09×) |
| zmij_fused (scalar lean layout) | 36.8 (0.95×) | 52.5 (1.03×) | 116.8 (1.00×) |
| ryu | 42.8 (0.82×) | 58.1 (0.93×) | 141.1 (0.83×) |
| zmij_native\* (fused, NON-usdcat) | 18.5 (1.90×) | 28.9 (1.87×) | 67.6 (1.73×) |

`-march=native` (candidate cores' full potential): `zmij_simd` float **1.99×**,
double **2.78×**, float3[] **1.91×**. **`zmij_simd` matches (and on doubles beats) the
`zmij_native` ceiling while emitting exact usdcat.** On adversarial *random bit
patterns* `zmij_simd` is ~0.9–1.0× (they fall out of the fixed window to the scalar
fallback) — that distribution barely occurs in real `.usda`.

## Findings

1. **`zmij_simd` reaches the 2× ceiling with exact usdcat output** — 1.7–2.8× over the
   current dragonbox on realistic float/double/vector data, byte-identical across the
   entire float domain + 1e9 doubles. This is the headline result.
2. **How:** zmij's fixed-notation path already writes plain fixed digits with **no
   trailing `.0`** (like usdcat). For the leading-exponent window where zmij-fixed ==
   usdcat-fixed (`dec_exp ∈ [-4, min(14, max_fixed)]` — where essentially all
   coords/normals/uvs live), `zmij_simd` runs zmij's SIMD BCD + single-shuffle
   decimal-point placement **verbatim**; for the rare out-of-window / scientific /
   special values it returns `nullptr` and the scalar `usd_dtos_zmij` fallback (also
   conformant) takes over. No SIMD table regeneration was needed — the win came from
   *reusing* zmij's fixed block within its valid range.
3. **A plain core swap (`zmij`) is only ~1.1×**, and the **naive scalar fusion
   (`zmij_fused`) does not help** (~0.95–1.0×) — `render_usd_finite` is already
   well-tuned, so the speed had to come from zmij's SIMD digit placement, not a
   scalar rewrite. **Ryū is slower** throughout.

## Recommendation / integration notes

Adopt the `zmij_simd` shape in the real value printer: zmij's fixed-notation SIMD
block for the common exponent window + the existing scalar usdcat renderer as the
fallback. Caveats for integration into `src/next/writer`:
- **Output buffer slack:** the SIMD block writes 16-byte chunks past the logical end,
  so the scalar stack buffer must grow from 32 → **≥ 40 bytes** (`dtos_to`'s callers).
- **Portability:** the fast path uses zmij's SSE4.1/SSE2/NEON code; it already has a
  scalar BCD fallback inside zmij, so non-SIMD targets stay correct (just slower).
- **Gate:** any integration must re-clear this exhaustive conformance harness.

No changes were made to `src/next` in this sandbox.
