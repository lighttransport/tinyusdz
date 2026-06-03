# TimeSamples / Animatable Refactor — COMPLETE

Date: 2026-05-22 (plan) → completed on branch `refactor-2026may`.

**Status: DONE.** `TypedTimeSamples<T>` has been deleted entirely. `value::TimeSamples`
is now the single type-erased container that both *stores* and *evaluates* time-varying
values, reading scalars directly from its binary `_data` buffer. All phases (1–5) landed;
the green gate held throughout (clang + gcc builds, `ctest` 14 suites incl.
`timesamples_test` as the ODR tripwire, roundtrip `tests/run-usdcat-compare.sh` at
**560 equivalent / 2 known apiSchemas failures**).

This file is kept as a record of what was wrong and what shipped; it is no longer an
active TODO. For the broader memory/performance picture see
[memory-and-performance.md](memory-and-performance.md).

---

## What shipped

### Single evaluation path
- `value::TimeSamples::get<T>()` (`timesamples.hh`) is a **thin per-`T` forwarder**, not a
  re-instantiated interpolator. It does a role-compatible type-id acceptance check
  (mirrors `Value::as<T>`: exact id, or role-underlying layout, scalar or array) and then:
  - **scalar `T`** → `get_scalar(void*, t, interp)` — a non-template `switch(_type_id)` that
    reads POD samples straight from the flat `_data` buffer (no `value::Value`
    reconstruction). Role types share the underlying arm via fall-through `case` labels.
  - **array / non-binary `T`** (string, token, dict, AssetPath) → `get_value_at()`, a
    once-compiled generic `get_samples()` + `Lerp` value path, then cast out.
- The per-type binary evaluator `get_scalar_impl<T>` and the `get_scalar()` switch live
  only in `timesamples-eval.cc` (split out of `timesamples.cc` for parallel compile).
  Held/Linear and blocked-sample handling live entirely in these non-template cores — the
  4 previously-divergent `get()` implementations are unified into one.

### `Animatable<T>` storage unified on `unique_ptr<value::TimeSamples>`
- `src/core/animatable.hh`: `_ts` is now `std::unique_ptr<value::TimeSamples>` — 8 bytes /
  `nullptr` for the common scalar-only attribute, instead of an always-present container.
- Enums (and other non-registered types) are stored as their **underlying `int64`** in the
  same type-erased container via trivial `to_store`/`from_store` casts at the
  `Animatable` boundary; `animatable_detail::has_value_type_traits<T>` selects whether the
  stored element is `T` (registered value type) or `int64` (enum). Enum→token rendering
  stays at the write/pprint layer (which knows the concrete enum). This replaced the
  earlier hybrid that kept a *second* per-type `TypedTimeSamples<T>` for enums.
- Migration target API is `get_timesamples_ptr()` (`const value::TimeSamples*`,
  nullptr-guarded) + `set_timesamples(value::TimeSamples&&/const&)`. The old typed
  `get_timesamples()` / `set(const TypedTimeSamples<T>&)` compat shims are gone.

### Deletions (fixes the ODR + the compile-time hog)
- `struct TypedTimeSamples<T>` (was in `timesamples.hh`) — **deleted**.
- `src/timesamples-get-impl.inc` — **deleted** (it carried a *different, incomplete*
  `get()` body than `timesamples.cc`; both were emitted as COMDAT and merged arbitrarily
  by the linker — a real ODR bug, now gone).
- The 5 `src/timesamples-inst-{scalar,scalar-role,array,array-role,array-basic}.cc` split
  instantiation TUs — **deleted** (and removed from CMake; meson/xmake never listed them).
- The `extern template struct TypedTimeSamples<T>` block in `timesamples.hh` — deleted.
- Dead tydra per-type *animatable* evaluation files (6 of them:
  `attribute-eval-typed-animatable-*`) — deleted; the live
  `EvaluateTypedAnimatableAttribute<T>` calls the now-thin `Animatable<T>::get()`
  directly. The scalar/array split of the *non-animatable* typed eval
  (`attribute-eval-typed-inst-{scalar,array}.cc`) was deliberately kept — that ~108-type ×
  4-function matrix is split for parallelism and merging would hurt build time.
- Dead helper APIs `PrimVar::set_typed_timesamples`, `Attribute::set_typed_timesamples`,
  `tydra::utils::ExtractAnimatableData` — deleted (zero callers).

### Memory-safety hardening (Phase 4)
- `get_sample_at()` (both the `value::TimeSamples` and `TypedTimeSamples` versions) —
  **deleted**. The former returned `const_cast<Sample*>(&*it)` — a raw pointer into a
  reallocatable `std::vector` (a dangling footgun). The one test user was rewritten to
  `has_sample_at()` + `get<float>()` at the exact sample time.
- `value::TimeSamples::_data_offsets`: `uint32_t` → `size_t`, and `BLOCKED_OFFSET`
  `UINT32_MAX` → `SIZE_MAX` — removes the 4 GiB byte-offset ceiling (a >4 GiB point cache
  no longer truncates). `_array_counts` stays `uint32_t` (it holds element counts).
- `DEFINE_ROLE_TYPE_TRAIT` now `static_assert`s `sizeof` **and** `alignof`
  `(role) == (underlying)` for every role type (`define-type-trait.inc`), auto-covering all
  ~23 current and any future ones. This makes the shared `get_scalar` switch arms and the
  role path of `any_value_raw_cast` provably layout-safe — there is no more silent
  reinterpret without a compile-time guard.
- `any_value_raw_cast` (the unchecked force-cast) was moved into `value::detail` so it
  cannot be reached without intent; the 8 internal callers (`Value::as`/`get` role
  branches) qualify it. The **checked** `any_value_cast` stays public (external callers in
  crate-writer / c-tinyusd). It was hardened/relocated, not removed.

---

## Measured results

| Metric | Before | After |
|--------|-------:|------:|
| `TypedTimeSamples<T>` struct + `get()` explicit instantiations | ~231 (+ duplicated across 5 inst TUs) | **0** |
| `timesamples.cc` isolated compile (clang) | ~47 s | 16.6 s (Phase 3) → 8.6 s → ~5.6/3.5 s after `timesamples-eval.cc` split |
| split instantiation TUs | 5 | 0 (deleted) |
| `libtinyusdz_static.a` | 48,718,744 B | 48,607,894 B |
| ODR hazard (`.cc` body ≠ `.inc` body) | present | fixed (`.inc` deleted) |
| `_data_offsets` ceiling | 4 GiB (uint32) | none (size_t) |

Runtime: scalar evaluation now reads directly from `_data` (zero `value::Value`
reconstruction, zero per-call heap allocation); parsed binary data moves into
`Animatable<T>` via `set_timesamples(&&)` with no intermediate `TypedTimeSamples<T>` copy;
zero-copy dedup is preserved (`duplicate_sample()` shares a `_data_offsets` byte range).

---

## Residual / out of scope

- `PrimVar` keeps its own `value::TimeSamples _ts` and a rewritten
  `get_interpolated_value()` (operates on `get_samples()` + `Lerp`); it no longer depends
  on `TypedTimeSamples<T>`.
- The non-animatable tydra typed-eval scalar/array split TUs remain (kept for parallel
  compile, see above).
- Spline data is still stored as `value::Value` in `PrimVar::SplineKnotData`
  (experimental/rare; not part of this refactor).
- The token `TypedTimeSamples` now survives only as: comments, the unrelated parser method
  `AsciiParser::ParseTypedTimeSamples` (parses typed timesamples *text* into a
  `value::TimeSamples`), and a stale SoA comment in `crate-reader.hh`.
