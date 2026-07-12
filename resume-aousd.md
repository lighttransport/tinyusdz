# Resume: AOUSD Core conformance for TinyUSDZ `next`

Handoff for a fresh coding-agent session. The AOUSD supplemental **composition**
gate is fully closed (138/138); this file lists what remains and how to work on it.

## Orientation

- **Repo / branch:** `/mnt/nvme02/work/tinyusdz-repo/physics-2026`, branch `physics-2026-fix2`.
- **Module under work:** `src/next/` — a standalone C++14 USD core (parser, crate,
  pcp composition, eval, validation, writer). It is NOT built by the top-level
  CMake; it has its own project + tests.
- **Conformance report (read first):** `doc/ousd-vs-tusdz.md`. Every subsection ends
  with a `**Remaining:**` note; the closing summary (~line 778) names the critical path.
- **ARKit/USDZ checker notes:** `doc/openusd-usdz.md`.

### Build & test

```sh
# Debug build (tests use assert(); a Release build compiles them out — do not use -DNDEBUG)
cmake --build build-next -j16
ctest --test-dir build-next --output-on-failure        # 32/32 must pass

# Threaded build — exercises the parallel-warm merge path (findings that only bite under threads)
cmake --build build-next-threaded-debug -j16 && ctest --test-dir build-next-threaded-debug
```

### The supplemental corpus (the conformance oracle)

- **Location:** `/tmp/mxpv-openusd/vendor/core-spec-supplemental-release_dec2025`
  — **ephemeral `/tmp`; re-fetch if gone.** It is intentionally NOT vendored.
- `build-next` is already configured with `-DTINYUSDZ_AOUSD_SUPPLEMENTAL_ROOT` pointing there.
- Run the gate directly:

```sh
python3 tests/next/run-aousd-supplemental.py \
  --suite-root /tmp/mxpv-openusd/vendor/core-spec-supplemental-release_dec2025 \
  --next-usdcat build-next/next_usdcat \
  --aousd-test build-next/test_aousd_conformance \
  --max-composition-fail 0                 # ratchet is 0; keep it there
```

- Each case dir has readable `usda/` copies of its crate `.usd` layers, a `pcp.json`
  (expected composed prim paths / prim stacks / child names / prohibited names /
  attribute connections), and a `pcp.txt` (pxr prim-index dump — very informative).
- Reproduce one case:
  `cd <case dir> && build-next/next_usdcat -f --instance-mode native usda/root.usd`
  (add `--require-prim /Some/Path` to fail loudly on a missing prim).

### Wider regression gates (run before finishing any change)

```sh
bash tests/run-usdcat-compare.sh    # legacy roundtrip; BASELINE = 413 equiv / 0 diff (USDA), 222/1 (USDC)
                                    #   the 1 USDC diff + 2 USDA parse errors are PRE-EXISTING legacy-parser gaps
cmake --build build -j16            # main lib must still build (next compiles into it)
```

## Composition-engine map (for the composition items below)

The `next` PCP composition lives in `src/next/pcp/`. Key pieces after the 22-gap work:

- `namespace-mapping.hh` — `NamespaceMapping` is a longest-prefix-first SET of
  (source→target) pairs (pxr `PcpMapFunction`): one per arc + one per relocate.
- `prim-index.hh` — `StackRelocates` (per-layer-stack namespace edits) + `LayerStack`.
- `cache-layer-stack.inc` — `BuildStackRelocates` (validity rules + raw-path normalization).
- `cache-arc-expansion.inc` — `ProcessArc`, `ExpandArcs`, `ExpandList`,
  `SourcesForSite` / `DeriveChildSources` / `SourcesForRelocateSource`
  (relocate-aware source derivation), implied-class block (`Src::arc_chain`).
- `cache-compose.inc` — `ComposeOpinions` / `ComposeChildNames` (post-relocation child list).
- `cache-specs-instances.inc` — `SelectVariants` / `FallbackSelection`, instance keys.
- `cache-parallel-merge.inc` — `MergeSources` (remaps worker stack/map/arc_chain indices;
  skips stack-qualified `@N:`/`!N:` cache keys).

---

## Remaining gaps (pick one; each is independently workable)

Ordered roughly by concreteness/value. Each says: what's missing, where, how to verify.

### 1. Sampled-value oracle for value resolution  *(most concrete, high value)*
The supplemental `value_resolution` cases currently only assert that the 8 entry
layers **load/compose** (`run-aousd-supplemental.py`, `value_resolution` branch —
it just checks `next_usdcat -f` returns 0). The corpus ships expected **resolved
sample values** (value-clip bracketing, layer offsets, interpolation) that are NOT
checked. Translate those assertions and compare `next`'s resolved values (via
`src/next/eval/` attribute-eval / value-clip) against the corpus expectations.
- Files: `tests/next/run-aousd-supplemental.py` (add a value-comparison mode),
  `src/next/eval/value-clip.{cc,hh}`, `src/next/eval/attribute-eval.{cc,hh}`.
- Verify: new assertions pass on all 8 cases; `doc/ousd-vs-tusdz.md:288` note updated.

### 2. Composition specifier edge case  *(concrete bug, no current test)*
Code-review finding, left un-fixed: in `cache-compose.inc` (~line 98-106,
`ComposeOpinions`), a composed prim whose strongest spec-bearing source is a
direct-inherit `class` gets **downgraded to `def`** when a WEAKER Inherit source's
spec is a `def` (`specifier_from_direct_inherit` is set from the first spec-bearing
source, which can itself be an inherit). Repro: `/World/Foo` inherits `/_C`;
`/_C` is `class _C { class Child {} }` and `_C` inherits `/_D` where
`_D` is `class _D { def Child {} }`. Composed `/World/Foo/Child` should stay an
abstract `class` but is emitted as a concrete `def`.
- Fix: the specifier should be the strongest authored opinion's, with the
  direct-inherit-over-`over` promotion only applying when the def opinion is at
  least as strong. Add a `test_pcp.cc` regression first (in-memory `LayerBuilder`).

### 3. Data-type round-trip generated from the normative table
Today `test_aousd_conformance.cc` guards every public `TypeId` in the implementation
registry. Generate cases from the normative §-spec type table across every
scalar / array / default / time-sampled / dictionary context instead.
- `opaque`/`group` non-block values are correctly unrepresentable — keep that.
- Verify: expanded matrix passes; `doc/ousd-vs-tusdz.md:158` note updated.

### 4. Elective authored-state + list-op breadth
- Not every elective layer/prim/property field has an authored-state bit; add one
  (or store fields generically) so explicit-empty vs unauthored vs default is
  distinguishable for ALL core fields, not the reviewed subset.
- `apiSchemas` / `variantSetNames` ordered + generated multi-layer combinations
  aren't registry-driven (`cache-arc-listops.inc`, `schema-registry`).
- Missing differentials: deleted/blocked values, dictionary type-conflict.
- Typed extension-field storage doesn't cover variant/spec categories outside the
  Layer/Prim/Property model (also limits `diff/layer-diff.cc`).
- Files: `src/next/layer/prim-spec.{cc,hh}`, `src/next/crate/crate-writer-*.inc`,
  `src/next/schema/schema-registry.{cc,hh}`. Docs: `:210 :222 :417 :427`.

### 5. General `SdfVariableExpression` function grammar
Only direct/quoted **asset-path** substitution is implemented
(`src/next/composition/expression-variables.cc`, `EvaluateAssetPathExpression`).
The general expression **function language** (string ops, conditionals, etc.) is not.
Define substitution policy + implement the grammar.
- Docs: `:361 :365`.

### 6. Identifier validation at authoring boundaries
The shared UTF-8/XID validator runs on parser + validator paths but NOT on every
programmatic authoring boundary (`Path`/name construction from untrusted strings).
Wire it in; generate cases from every grammar production incl. malformed UTF-8 and
noncharacter/private-use boundaries.
- Files: `src/next/prim/identifier.hh`, `src/next/prim/path.*`. Docs: `:168 :182`.

### 7. Non-core OpenUSD domain-schema breadth  *(large, product-driven)*
Schema checks are structural for schemas `next` knows — not a plugin registry.
Volume/field, Hermite/TetMesh/NURBS-patch, render-settings, shader-node breadth are
product parity, not AOUSD Core. Prioritize from application requirements; label as
product parity, not conformance.

### Housekeeping (not conformance, but tracked)
- 3 legacy-parser roundtrip fixtures fail (`aousd-namespace-order.usda`,
  `aousd-unknown-property-metadata.usda`, one USDC variant) — the LEGACY binary,
  which `next` does not feed. XFAIL; only relevant if touching `src/usda-*`.
- Oracle pinning: OpenUSD source checkout says `v26.03-278`, installed headers say
  26.05 — rebuild + pin ONE oracle before trusting differential comparisons.

## Working rules

- Keep the composition ratchet at **0**. Any change that regresses a supplemental
  case (composition/data_types/file_formats/value_resolution) fails the gate.
- Add a regression FIRST (in-memory `LayerBuilder` in `tests/next/test_pcp.cc` or a
  fixture under `tests/next/fixtures/`, NOT the shared legacy corpus).
- After any `src/next/pcp` change, run BOTH `build-next` and
  `build-next-threaded-debug` ctest (the parallel-warm merge path is thread-only).
- Diagnosis is cheap: `next_usdcat -f --instance-mode native <case>/usda/root.usd`
  diffed against `<case>/pcp.json` tells you exactly what's missing.
- Reference memory: `[[aousd-supplemental-composition]]`, `[[legacy-next-deep-audit-2026-07]]`.
