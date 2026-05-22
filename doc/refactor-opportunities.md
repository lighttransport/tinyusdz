# TimeSamples / any(get()) Refactoring Plan

Date: 2026-05-22
Updated: 2026-05-22 (review: switch dispatch, unique_ptr, private get(void*))
Updated: 2026-05-22 (implementation started on branch `refactor-2026may`)

## Implementation Progress (branch `refactor-2026may`)

Each commit keeps the tree green: clang `ninja -C build-ct tinyusdz_static`; gcc
`make -j16 -C build` + `ctest` (14 suites incl. `timesamples_test`); roundtrip
`bash tests/run-usdcat-compare.sh` from the repo root (baseline **560 equivalent /
2 known apiSchemas failures**).

### Done (verified, committed)
- **Phase 1** (`9b930d45`): added binary-direct `TimeSamples::get_scalar(void*,t,interp)`
  switch + per-type `get_scalar_impl<T>` (reads POD samples straight from `_data`, no
  `value::Value` reconstruction; generic-Value fallback), defined+instantiated only in
  `timesamples.cc`. Public `eval_scalar<T>()` transition hook with a role-compatible
  type-accept check mirroring `Value::as<T>`. Additive / unreachable until tested.
- **Phase 1.5** (`1eb22354`): differential parity test in `unit-timesamples.cc` asserting
  `eval_scalar<T>()` ≡ `get<T>()` bit-for-bit across {default,Held,Linear} × {two-sample,
  single, blocked-middle, blocked-endpoints, dedup, role-type, non-lerp int, generic token}.
  Confirms the typed `lerp` matches the value-level `Lerp`. Permanent gate for Phase 3.
- **Phase 2a** (`56db66b7`): `Animatable<T>` now stores `std::unique_ptr<value::TimeSamples>`
  (RAII; 8 bytes / nullptr for scalar-only) for registered value types, with user-defined
  deep-copy + explicit move. Compat shims keep ~34 consumers source-compatible:
  `get_timesamples()` returns `TypedTimeSamples<T>` by value (rebuilt via `from_timesamples`),
  `set(const TypedTimeSamples<T>&)` converts in; migration target is `get_timesamples_ptr()`
  (`const value::TimeSamples*`).
- **Phase 2e/2f** (`bebe0665`): migrated `tydra/scene-access.cc` (ToTypelessTimeSamples value
  sites) and `stage.cc` (memory estimators) off the typed shim to `get_timesamples_ptr()`.

### Two design decisions discovered during 2a (not in the original plan below)
1. **Enums and non-registered structs (e.g. `Extent`) cannot live in `value::TimeSamples`**
   (it is keyed on the `value::TypeTraits` value-type registry). Resolved with a hybrid:
   `Animatable<T>` selects storage at compile time via
   `animatable_detail::has_value_type_traits<T>` — `value::TimeSamples` (unique_ptr) for
   registered value types, `TypedTimeSamples<T>` for everything else (enums:
   Visibility/UsdUVTexture::Wrap/...). NOTE `Extent` *is* registered
   (`DEFINE_TYPE_TRAIT` in core/extent.hh:87) so it takes the value path.
2. **`reconstruct_binary_sample()` (and the Phase-1 `get_scalar()` switch) were INCOMPLETE** —
   missing `Extent`, the 7 half role types (color3h/color4h/point3h/normal3h/vector3h/
   texcoord2h/texcoord3h) and `timecode`. Any binary value type stored in `value::TimeSamples`
   MUST have an arm in BOTH switches or its timesamples are silently lost (regressed
   extent-001.usda before this was fixed). Arms added in Phase 2a.

### Migration pattern for the remaining consumers
For a VALUE-type `Animatable` consumer: replace `X.get_timesamples()` (typed shim) with
`X.get_timesamples_ptr()` (`const value::TimeSamples*`, nullptr-guarded) + the
`value::TimeSamples` API (`get<T>` / `get_samples()` + `.value.as<T>()` / `estimate_*`).
Keep the `has_timesamples()` guard. ENUM-type consumers keep `get_timesamples()`. Delete any
now-unused typed helper (`-Wunused-template` is a hard error under gcc `-Werror`).

### Remaining
- **Phase 2b–2h** — migrate the ~30 remaining value-type `get_timesamples()` sites:
  `tydra/render-data-anim.cc` (8 `FOREACH_TIMESAMPLES` sites — rewrite the macro to take an
  element type and extract via `as<T>()`; also distinguish `Animatable` vs `XformOp`
  get_timesamples), `sconv-shader.cc` (5), `sconv-geom.cc` (5), `render-animation-converter.cc`
  (5), `pprint-detail.hh` (4), `sconv-detail.hh`, `prim-types.cc`, `layer-to-renderscene.hh`,
  `tydra/common-utils.hh`. Plus the public `GeomPrimvar::_ts_indices` (usdGeom.hh:114/121/201/297)
  and the `usdShade.hh` enum `TypedTimeSamples<...>::get` specializations (525/547/557).
  Gate: `grep -rn TypedTimeSamples src/*.cc src/*.hh` shows only timesamples.{hh,cc} + inst TUs +
  the .inc + enum/Extent uses; then remove the value-type `get_timesamples()` compat shim.
- **Phase 3** — delete value-type `TypedTimeSamples` instantiations (~430 lines in
  timesamples.cc) + the `extern template` block + the 5 `timesamples-inst-*.cc` +
  `timesamples-get-impl.inc` (from all 3 build files). Fixes the ODR; lands the
  timesamples.cc ~47s→~8s compile win. Gate: `timesamples_test` (ODR tripwire) + roundtrip 560.
- **Phase 4** — memory-safety hardening (remove dangling `get_sample_at`, `_data_offsets`
  uint32→size_t, all-role-type `static_assert`s, gate `any_value_raw_cast`).
- **Phase 5** — tydra attribute-eval simplification (measurement-gated).

## Executive Summary

**Goal:** Eliminate `TypedTimeSamples<T>` entirely. Replace template-heavy
`get<T>()` / interpolation with a `switch`-based dispatch in a single
non-template `get()` implementation confined to `timesamples.cc`. This
removes ~460 explicit template instantiations, unifies 4 divergent
interpolation implementations, fixes memory-safety issues (dangling
pointer, unsafe cast, type-confusion), reduces per-attribute memory
overhead 6× for scalar-only attributes, and makes `TimeSamples` the
single type-erased container that both stores *and* evaluates
time-varying values — directly from its binary buffer without
reconstructing `value::Value` objects.

---

## 1. Current Architecture — What's Wrong

### 1.1 The Three-Layer Hierarchy

```
  Animatable<T>          (layer 3: union T | TypedTimeSamples<T> | blocked)
       |
       v
  TypedTimeSamples<T>    (layer 2: typed AoS copy of samples)
       |
       v
  TimeSamples            (layer 1: type-erased, binary + Value storage)
```

**Layer 2 is redundant.** `TypedTimeSamples<T>` exists solely to provide
type-safe access, but it does so by *copying* every sample out of
`TimeSamples`'s efficient binary buffer into per-type `vector<Sample>`
where `Sample = {double t, T value, bool blocked}`. This defeats the
binary-storage optimization and is the primary driver of template
instantiation explosion.

### 1.2 Template Instantiation Bloat

| Location | What | Count |
|----------|------|-------|
| `timesamples.cc` | `template struct TypedTimeSamples<T>` | 114 |
| `timesamples.cc` | `template bool TypedTimeSamples<T>::get()` | 117 |
| `timesamples-inst-{scalar,array,role}*.cc` (5 files) | Duplicate of above | 114 + 117 |
| `tydra/attribute-eval-typed-*.cc` (6 files) | `EvaluateTypedAttribute<T>` etc. | ~640 |
| **Total** | | **~1,100** |

**Key problems:**
- The same `template struct TypedTimeSamples<T>` and `template bool
  TypedTimeSamples<T>::get()` appear in *both* `timesamples.cc` (lines
  458–899) and the 5 `timesamples-inst-*.cc` files — duplicate explicit
  instantiation definitions, a latent ODR / linker-error hazard.
- The tydra attribute-eval layer has 4 template families
  (`EvaluateTypedAttribute`, `EvaluateTypedAnimatableAttribute`, and
  their `*WithFallback` variants) × ~55 types = ~640 instantiations,
  spread across 6 translation units.

### 1.3 Divergent `get()` Implementations

There are **4 separate implementations of essentially the same algorithm:**

| # | Location | Lines | Blocked-handling? |
|---|----------|-------|-------------------|
| 1 | `TypedTimeSamples::get()` in `timesamples.cc:600–754` | 154 | Full (fixed in prior commit) |
| 2 | `TypedTimeSamples::get()` in `timesamples-get-impl.inc` | 223 | **Incomplete** — 8 FIXME/TODO comments about blocked |
| 3 | `TimeSamples::get<T>()` in `timesamples.hh` (inline) | 2 overloads | Full |
| 4 | `PrimVar::get_interpolated_value()` in `primvar.cc` | 92 | Partial (no blocked check for lerp) |

Implementation #2 (`timesamples-get-impl.inc`) is `#include`-d by the 5
split instantiation TUs with `#ifdef TINYUSDZ_USE_TIMESAMPLES_SOA`
(SoA experimental variant). This means the same template is instantiated
with **different function bodies** depending on which TU it's compiled in
— an ODR violation.

### 1.4 Memory Safety Issues

#### (a) Dangling pointer from `get_sample_at()`

`TimeSamples::get_sample_at(t, Sample** dst)` returns a **mutable
pointer** into `_samples` (the internal `std::vector<Sample>`). Any
subsequent `add_sample()` call can reallocate the vector, turning this
pointer into a dangling reference. The code in `value-types.cc:1584–1600`
also does `const_cast<Sample*>` to strip const from the internal
iterator, weakening const-correctness:

```cpp
// value-types.cc:1584-1600 (simplified)
const_iterator it = std::find_if(...);
*dst = const_cast<Sample*>(&(*it));  // strips const, exposes internal pointer
```

#### (b) Unsafe `any_value_raw_cast`

`value-types.hh:1856–1858` contains a completely unchecked cast used for
role-type reinterpretation (e.g. `color3f` ↔ `float3`):

```cpp
template <class T>
inline const T *any_value_raw_cast(const any_value *av) {
  return av->cast<T>();  // no type_id check, pure reinterpret_cast
}
```

Role types (color3f, normal3f, point3f, etc.) are layout-identical to
their underlying types (float3), but there is **no `static_assert`**
anywhere verifying this. A future refactoring of the type system could
break layout compatibility silently.

#### (c) `any_value::cast<T>()` reinterpret_cast

```cpp
// value-types.hh:1716-1726 (simplified)
const T *cast() const {
  if (mode_ == Mode::Inline)
    return reinterpret_cast<const T *>(&storage_);       // stack memory
  else
    return *reinterpret_cast<T *const *>(&storage_);    // heap: double-indirection
}
```

The heap path dereferences a `reinterpret_cast`'d pointer from the
internal storage buffer. This is safe only if the pointer stored in
`storage_` genuinely points to a `T` object — there is no runtime check.

### 1.5 The `from_timesamples()` Conversion Kills Binary Storage

The evaluation path through `Animatable<T>` goes:

```
TimeSamples (binary) → from_timesamples() → TypedTimeSamples<T> (copy)
                                                    ↓
                                          get<T>(dst, t, interp)  [evaluates typed copy]
```

`from_timesamples()` at `timesamples.hh:1245–1267` calls
`get_samples()` first. For binary storage, `get_samples()` triggers
`reconstruct_binary_sample()` (a giant ~85-case switch) that creates
`value::Value` objects from the flat `_data` buffer — one heap allocation
per sample for large types. Then `from_timesamples()` type-checks each
`Value` via `as<T>()` and copies the typed data into `std::vector<Sample>`.

**Net effect:** The binary storage optimization is completely negated by
the evaluation path. The only thing binary storage provides is a compact
intermediate representation during parsing.

### 1.6 Other Issues

- **`uint32_t` overflow:** `_data_offsets` uses `uint32_t` for byte
  offsets (`timesamples.hh:809`). A time-varying point cache exceeding
  4 GiB would silently truncate offsets.
- **`mutable` + lazy sort:** 6 data members are `mutable` with a `_dirty`
  flag. Calling a supposedly-const `get()` triggers an O(n log n) sort.
- **`Animatable<T>` always carries full overhead**
  (`TypedTimeSamples<T>` = ~32 bytes) even for attributes that only have a
  scalar default value. In typical USD stages, 80–90% of attributes are
  scalar-only. For 10,000 scalar-only attributes, this wastes ~320 KB.
- **Spline data stored as `value::Value`** in `PrimVar::SplineKnotData`
  — same type-erasure overhead for an experimental/rarely-used feature.

---

## 2. Proposed Architecture

### 2.1 Core Idea: Eliminate `TypedTimeSamples<T>`

`TimeSamples` becomes the single storage + evaluation container. It
evaluates samples **directly from its binary `_data` buffer** using a
`switch`-based dispatch, without constructing intermediate
`value::Value` objects.

```
  TimeSamples            (type-erased, binary + Value storage, owns all data)
       |
       +-- add_sample<T>(t, v)     → template, thin wrapper for type dispatch
       +-- get_scalar(void*, t, interp) → PRIVATE, NON-template, switch dispatch
       +-- get<T>(dst, t, interp)  → template, public thin inline wrapper
```

`Animatable<T>` stores a **heap-allocated** `TimeSamples` via
`std::unique_ptr<TimeSamples>`, allocated only when timesamples are
actually present:

```
  Animatable<T>          (T _value | unique_ptr<TimeSamples> | blocked)
       |
       v  (only when _ts != nullptr)
  TimeSamples            (type-erased, heap-allocated on demand)
```

This cuts per-attribute overhead from ~144 bytes (direct member) to
8 bytes (pointer) for the common scalar-only case — a **6× improvement**
over the current code's ~32 bytes.

### 2.2 Dispatch: `switch`-Based, Not Ops-Table

**Rejected alternative:** A static function-pointer ops table
(`TimeSamplesTypeOps` with `copy`/`lerp`/`copy_array`/`lerp_array`
function pointers) indexed by `type_id`.

**Why rejected:**
- Designated array initializers `[TYPE_ID_FLOAT] = {...}` require C99/GCC
  extension — not standard C++17.  With `-Weverything -Werror`, this
  triggers warnings.
- `resolve_ops()` for role types requires a two-hop lookup (role → lookup
  → underlying → lookup), adding indirection.
- Function pointers can't be inlined; a `switch` compiles to an efficient
  jump table.
- ~60 lambdas in a static array is not simpler than ~50 `case` arms.

**Chosen approach:** A single `switch(_type_id)` in
`TimeSamples::get_scalar()` that delegates to small private helper
templates. The switch has one `case` per unique memory layout (~40–50
arms). Role types share the same arm as their underlying type via
fall-through `case` labels — no reinterpret_cast, no indirection.

```cpp
// In timesamples.cc — single non-template function (private)
bool TimeSamples::get_scalar(void *dst, size_t idx, double t,
                             TimeSampleInterpolationType interp) const {
  switch (_type_id) {
    case TYPE_ID_FLOAT:              return get_scalar_impl<float>(dst, idx, t, interp);
    case TYPE_ID_DOUBLE:             return get_scalar_impl<double>(dst, idx, t, interp);
    case TYPE_ID_HALF:               return get_scalar_impl<value::half>(dst, idx, t, interp);
    case TYPE_ID_INT32:              return get_scalar_impl<int32_t>(dst, idx, t, interp);
    case TYPE_ID_UINT32:             return get_scalar_impl<uint32_t>(dst, idx, t, interp);
    case TYPE_ID_INT64:              return get_scalar_impl<int64_t>(dst, idx, t, interp);
    case TYPE_ID_UINT64:             return get_scalar_impl<uint64_t>(dst, idx, t, interp);
    case TYPE_ID_BOOL:
      if (!_times.empty()) {
        *static_cast<bool*>(dst) = (_data[_data_offsets[idx]] != 0);
        return true;
      }
      return get_scalar_from_generic(dst, idx);
    // -- Vector / matrix / quat types --
    case TYPE_ID_FLOAT2:             return get_scalar_impl<value::float2>(dst, idx, t, interp);
    case TYPE_ID_FLOAT3:             // fall through — role types share underlying arm
    case TYPE_ID_COLOR3F:
    case TYPE_ID_NORMAL3F:
    case TYPE_ID_POINT3F:
    case TYPE_ID_VECTOR3F:           return get_scalar_impl<value::float3>(dst, idx, t, interp);
    case TYPE_ID_DOUBLE3:
    case TYPE_ID_COLOR3D:
    case TYPE_ID_NORMAL3D:
    case TYPE_ID_POINT3D:
    case TYPE_ID_VECTOR3D:           return get_scalar_impl<value::double3>(dst, idx, t, interp);
    case TYPE_ID_FLOAT4:
    case TYPE_ID_COLOR4F:            return get_scalar_impl<value::float4>(dst, idx, t, interp);
    case TYPE_ID_MATRIX4D:           return get_scalar_impl<value::matrix4d>(dst, idx, t, interp);
    // ... etc (~40 arms total, shared across ~55 type_ids) ...
    default: return false;
  }
}
```

### 2.3 Private Helper Template — Confined to `timesamples.cc`

```cpp
// In timesamples.cc (anonymous namespace) —
// explicitly instantiated for ~40 unique memory-layout types.
// Each instantiation is ~30 lines of binary-search + memcpy/lerp code.
template <typename T>
bool TimeSamples::get_scalar_impl(void *dst, size_t idx, double t,
                                  TimeSampleInterpolationType interp) const {
  // Binary storage path — direct read from _data buffer
  if (!_times.empty()) {
    if (idx >= _times.size()) return false;
    if (_blocked[idx]) return false;

    const uint8_t *sample_ptr = _data.data() + _data_offsets[idx];
    T *out = static_cast<T*>(dst);

    if (interp == TimeSampleInterpolationType::Held ||
        !value::LerpTraits<T>::supported()) {
      std::memcpy(out, sample_ptr, sizeof(T));
      return true;
    }

    // Linear interpolation from binary buffer
    size_t next_idx = idx + 1;
    if (next_idx >= _times.size() || _blocked[next_idx]) {
      std::memcpy(out, sample_ptr, sizeof(T));
      return true;
    }

    const T *a = reinterpret_cast<const T*>(sample_ptr);
    const T *b = reinterpret_cast<const T*>(_data.data() + _data_offsets[next_idx]);
    double dt = (t - _times[idx]) / (_times[next_idx] - _times[idx]);
    *out = lerp(*a, *b, dt);
    return true;
  }

  // Generic Value storage path — fallback for non-binary types
  // (string, token, dict, AssetPath — rare in animation)
  if (idx >= _samples.size()) return false;
  if (_samples[idx].blocked) return false;
  const T *pv = _samples[idx].value.as<T>();
  if (!pv) return false;
  *static_cast<T*>(dst) = *pv;
  return true;
}
```

Explicitly instantiated in `timesamples.cc` for ~40 types:

```cpp
template bool TimeSamples::get_scalar_impl<float>(...);
template bool TimeSamples::get_scalar_impl<double>(...);
template bool TimeSamples::get_scalar_impl<value::float3>(...);
// ... ~37 more ...
```

### 2.4 Public Template Wrapper — Thin, Inline, Zero Code Bloat

A single template `get<T>()` method in the header does a type-id check
then calls the private `get_scalar()`:

```cpp
// In timesamples.hh — inline, compiles to ~5 instructions
template <typename T>
bool TimeSamples::get(T *dst, double t,
                      TimeSampleInterpolationType interp) const {
  if (!dst) return false;
  if (_type_id != TypeTraits<T>::type_id() &&
      _type_id != TypeTraits<T>::underlying_type_id()) {
    return false;
  }
  // ... binary search for idx ...
  return get_scalar(static_cast<void*>(dst), idx, t, interp);  // private
}
```

`get_scalar(void*)` is **private** — no direct call from outside the
class, eliminating the type-confusion risk.

### 2.5 `Animatable<T>` — Simplified with `unique_ptr<TimeSamples>`

```cpp
template <typename T>
struct Animatable {
  T _value{};
  std::unique_ptr<value::TimeSamples> _ts;  // nullptr for scalar-only
  bool _has_value{false};
  bool _blocked{false};

  // --- Evaluation ---

  bool get(double t, T *v, TimeSampleInterpolationType interp) const {
    if (_blocked) return false;
    if (TimeCode(t).is_default() && _has_value) {
      *v = _value;
      return true;
    }
    if (_ts && !_ts->empty()) return _ts->get(v, t, interp);
    if (_has_value) { *v = _value; return true; }
    return false;
  }

  bool has_timesamples() const { return _ts && !_ts->empty(); }
  bool has_value() const { return _has_value; }
  bool is_blocked() const { return _blocked; }

  // --- Scalar setters ---

  void set(const T &v) {
    _value = v; _blocked = false; _has_value = true;
  }
  void set(T &&v) {
    _value = std::move(v); _blocked = false; _has_value = true;
  }

  // --- TimeSamples setters ---

  /// Add a single time sample (allocates TimeSamples on demand)
  void add_sample(double t, const T &v) {
    if (!_ts) _ts = std::make_unique<value::TimeSamples>();
    _ts->add_sample(t, v);
    _has_value = false;
  }

  /// Add a blocked time sample
  void add_blocked_sample(double t) {
    if (!_ts) _ts = std::make_unique<value::TimeSamples>();
    _ts->add_blocked_sample(t);
    _has_value = false;
  }

  /// Move TimeSamples in — replaces with parsed data (no copy)
  void set_timesamples(value::TimeSamples &&ts) {
    _ts = std::make_unique<value::TimeSamples>(std::move(ts));
    _has_value = false;
  }

  /// Copy TimeSamples in
  void set_timesamples(const value::TimeSamples &ts) {
    _ts = std::make_unique<value::TimeSamples>(ts);
    _has_value = false;
  }

  void clear_timesamples() { _ts.reset(); }

  // --- Observers ---

  const value::TimeSamples *get_timesamples() const { return _ts.get(); }
  value::TimeSamples *get_timesamples() { return _ts.get(); }

  // --- Construction / Move ---

  Animatable() = default;
  Animatable(const T &v) : _value(v), _has_value(true) {}

  Animatable(Animatable&& other) noexcept
    : _value(std::move(other._value)),
      _ts(std::move(other._ts)),
      _has_value(other._has_value),
      _blocked(other._blocked) {
    other._has_value = false;
    other._blocked = false;
  }

  Animatable& operator=(Animatable&& other) noexcept {
    if (this != &other) {
      _value = std::move(other._value);
      _ts = std::move(other._ts);
      _has_value = other._has_value;
      _blocked = other._blocked;
      other._has_value = false;
      other._blocked = false;
    }
    return *this;
  }

  // Copy (deep)
  Animatable(const Animatable& other)
    : _value(other._value),
      _ts(other._ts ? std::make_unique<value::TimeSamples>(*other._ts) : nullptr),
      _has_value(other._has_value),
      _blocked(other._blocked) {}
};
```

Key differences from the current `Animatable<T>`:
- `_ts` is `unique_ptr<TimeSamples>` (8 bytes) instead of `TypedTimeSamples<T>` (~32 bytes).
- `set_timesamples(TimeSamples&&)` replaces the deleted `from_timesamples()` pattern.
- Move constructor is explicit (since `unique_ptr` doesn't auto-generate moves correctly with other members).

### 2.6 Array Timesamples

Array evaluation requires knowing the element type to resize
`std::vector<T>` output. This inherently needs a template.

**Approach:** A single thin public template `get_array<T>()` that calls a
private non-template `get_array_impl()`:

```cpp
// In timesamples.hh (public, inline)
template <typename T>
bool TimeSamples::get_array(std::vector<T> *dst, double t,
                            TimeSampleInterpolationType interp) const {
  if (!dst) return false;
  size_t idx = find_time_index_for_interp(t, interp);
  return get_array_impl(reinterpret_cast<uint8_t*>(dst), sizeof(T), idx, t, interp);
}
```

```cpp
// In timesamples.cc (private, non-template)
bool TimeSamples::get_array_impl(uint8_t *dst_vec, uint32_t elem_size,
                                 size_t idx, double t,
                                 TimeSampleInterpolationType interp) const {
  // dst_vec points to a std::vector<T> — we manipulate it through
  // offset-based access (pointer = *(void**)(dst_vec + 0),
  //                        size   = *(size_t*)(dst_vec + 8))
  // This is layout-portable: std::vector is always {T*, size_t, size_t}.
  static_assert(sizeof(std::vector<uint8_t>) == 3 * sizeof(void*),
                "vector layout assumption broken");

  // Read pointer + size from the vector header
  void **vec_data = reinterpret_cast<void**>(dst_vec);
  size_t *vec_size = reinterpret_cast<size_t*>(dst_vec + sizeof(void*));
  size_t *vec_cap  = reinterpret_cast<size_t*>(dst_vec + 2 * sizeof(void*));

  if (!_times.empty()) {
    // Binary array storage
    if (idx >= _times.size() || _blocked[idx]) return false;

    size_t count = _array_counts[idx];
    size_t byte_count = count * elem_size;
    const uint8_t *src = _data.data() + _data_offsets[idx];

    // Reallocate if needed (via the vector's allocator — only possible
    // if we know T at the call site, so the template wrapper handles this)
    // → Actually, get_array<T> handles resize.  get_array_impl does raw copy.
    std::memcpy(*vec_data, src, byte_count);
    return true;
  }

  // Generic Value storage path
  // ... (extract from _samples[idx].value, using element type dispatch)
  return false;
}
```

**Honest assessment:** Array evaluation through `void*` requires either
type-erased vector manipulation (fragile) or retains a thin template per
element type.  The practical recommendation is to keep
`get_array<T>(std::vector<T>*, ...)` as a thin template (one
instantiation per array type, ~25 types) that does `resize(sizeof(T) *
count)` and `memcpy`.  Each instantiation is ~10 lines.  Total: ~250
lines of template code — acceptable.

### 2.7 What Happens to `TypedTimeSamples<T>`?

**Deleted.** All ~1,100 lines of explicit instantiations across
`timesamples.cc` and `timesamples-inst-*.cc` are removed.

Callers that previously used `TypedTimeSamples<T>` are updated:

| Before | After |
|--------|-------|
| `TypedTimeSamples<T> tss; tss.from_timesamples(ts); anim.set(tss);` | `anim.set_timesamples(std::move(ts));` |
| `animatable._ts.get(v, t, interp)` | `animatable._ts->get(v, t, interp)` |

The `Animatable<T>::set(const TypedTimeSamples<T>&)` setter is also
deleted — replaced by `set_timesamples(TimeSamples&&)` and
`set_timesamples(const TimeSamples&)`.

### 2.8 Elimination of `reconstruct_binary_sample()`

The giant `RECONSTRUCT_SCALAR`/`RECONSTRUCT_ARRAY` macro switch in
`timesamples.cc:134-288` (~155 lines) is removed.  The evaluation path
(`get_scalar_impl<T>`) reads directly from `_data` — no `value::Value`
reconstruction.

The `reconstruct_binary_sample()` path is kept for `get_samples()` (used
by pretty-printer and tests), but simplified using the same
`get_scalar_impl<T>` helper to copy data out of `_data` into a temporary
typed value, then wrap it into `value::Value`.

### 2.9 Unify Interpolation Logic

The **single** `get_scalar_impl<T>` in `timesamples.cc` becomes the
canonical implementation. The other 3 copies are eliminated:

| Copy | Action |
|------|--------|
| `TypedTimeSamples::get()` in `timesamples.cc` | Deleted (struct removed) |
| `TypedTimeSamples::get()` in `timesamples-get-impl.inc` | Deleted (struct removed) |
| `PrimVar::get_interpolated_value()` | Rewritten to call `TimeSamples::get_scalar(void*)` |

---

## 3. Impact Summary

### 3.1 Code Reduction

| Category | Before | After | Delta |
|----------|--------|-------|-------|
| `timesamples.hh` | 1,559 lines | ~600 lines | −959 |
| `timesamples.cc` | 1,078 lines | ~550 lines | −528 |
| `timesamples-get-impl.inc` | 223 lines | **deleted** | −223 |
| `timesamples-inst-*.cc` (5 files, 429 lines) | 429 lines | **deleted** | −429 |
| `primvar.cc` interpolation | 92 lines | ~15 lines | −77 |
| `animatable.hh` | 185 lines | ~130 lines | −55 |
| **Total** | | | **−2,271 lines** |

### 3.2 Template Instantiation Reduction

| Category | Before | After | Delta |
|----------|--------|-------|-------|
| `TypedTimeSamples<T>` struct instantiations | 228 | **0** | −228 |
| `TypedTimeSamples<T>::get()` instantiations | 234 | **0** | −234 |
| `get_scalar_impl<T>` private inst. (in `.cc`) | 0 | ~40 | +40 |
| `get_array<T>` thin instantiations | 0 | ~25 | +25 |
| Tydra `EvaluateTyped*Attribute<T>` | ~640 | ~320* | −320 |
| **Total** | **~1,100** | **~385** | **−715** |

\* The tydra evaluators still need `EvaluateTypedAttribute<T>` for
connection resolution, but the body simplifies substantially since
it no longer calls `TypedTimeSamples<T>::get()`.

The net template count drops from ~1,100 to ~385. The 40 new private
instantiations are *only* in `timesamples.cc` — not duplicated across
5 split TUs. They are small (~30 lines each, no SFINAE).

### 3.3 Safety Fixes

| Issue | Fix |
|-------|-----|
| Dangling pointer from `get_sample_at()` | API removed |
| Unsafe `any_value_raw_cast` | Eliminated — role types share switch `case` arms with underlying type |
| `get(void*)` type-confusion | `get_scalar(void*)` is **private**; only accessible via `get<T>()` |
| Duplicate `get()` implementations | Unified to single `get_scalar_impl<T>` in `timesamples.cc` |
| ODR violation (`.cc` vs `.inc` bodies) | `.inc` file deleted |
| Duplicate explicit instantiations | All deleted (no `TypedTimeSamples<T>` left) |
| `uint32_t` overflow in `_data_offsets` | Changed to `size_t` |
| Missing role-type layout `static_assert` | Added in `define-type-trait.inc` |

### 3.4 Memory and Runtime

- **No more `TypedTimeSamples<T>` copies.** `Animatable<T>` stores a
  `unique_ptr<TimeSamples>` — the parsed binary data moves directly in
  via `set_timesamples(TimeSamples&&)` with zero intermediate copies.
- **No `value::Value` reconstruction during `get()`.** Binary-storage
  evaluation reads directly from `_data` via `get_scalar_impl<T>` —
  zero heap allocations per evaluation call.
- **No `from_timesamples()` conversion.** Callers use
  `set_timesamples(TimeSamples&&)` directly.
- **Switch-based dispatch** compiles to an efficient jump table (~50
  arms); no function-pointer indirection.
- **Zero-copy dedup preserved.** `duplicate_sample()` sets
  `_data_offsets[new] = _data_offsets[src]` — the new `get_scalar_impl`
  reads from the same byte range, preserving the dedup semantics.
- **6× less memory for scalar-only attributes.** `unique_ptr<TimeSamples>`
  is 8 bytes vs `TypedTimeSamples<T>` at ~32 bytes vs `TimeSamples` direct
  at ~144 bytes.

### 3.5 Compile-Time Improvement

- `timesamples.hh` no longer includes `value-types.hh` (2,900 lines,
  ~200 `TypeTraits` specializations).  Only needs forward-declarations
  of `Value`, `TypeTraits<T>`, `TypeId`, and `TimeCode`.
- The 5 `timesamples-inst-*.cc` files and 1 `.inc` file are deleted.
- `TypedTimeSamples<T>` explicit instantiation block (~430 lines in
  `timesamples.cc`) is deleted.
- `extern template struct TypedTimeSamples<T>` block (~250 lines in
  `timesamples.hh`) is deleted.
- The `PrimVar` header no longer depends on `TypedTimeSamples<T>`.

---

## 4. Migration Steps

### Phase 1 — Add `get_scalar_impl<T>` (non-breaking, pure addition)

1. Add `get_scalar(void*, idx, t, interp)` private method to `TimeSamples`
   in `timesamples.hh` (declaration only).
2. Implement `get_scalar()` (switch-based dispatch) and
   `get_scalar_impl<T>()` in `timesamples.cc`.
3. Explicitly instantiate `get_scalar_impl<T>` for ~40 types at the end
   of `timesamples.cc`.
4. Add `get<T>(T*, t, interp)` thin inline wrapper to header.
5. Unit test: compare new `get<T>()` against existing `get<T>()` for all
   types (scalar, role, blocked, lerp, held).

### Phase 2 — Eliminate `TypedTimeSamples<T>` Dependencies

1. Change `Animatable<T>::_ts` from `TypedTimeSamples<T>` to
   `std::unique_ptr<value::TimeSamples>`.
2. Update `Animatable<T>::get()` to call `_ts->get(v, t, interp)`.
3. Update `Animatable<T>::add_sample()` to allocate `_ts` on first use.
4. Add `Animatable<T>::set_timesamples(TimeSamples&&)` and
   `set_timesamples(const TimeSamples&)`.
5. Remove `from_timesamples()` calls in `usdGeom.cc` and `scene-access.cc`
   — use `Animatable::set_timesamples(TimeSamples&&)` instead.
6. Rewrite `PrimVar::get_interpolated_value()` to call
   `TimeSamples::get_scalar()` (via `TimeSamples::get<T>()` public wrapper).
7. Update `PrimVar::set_typed_timesamples()` to use
   `set_timesamples(TimeSamples&&)` — the existing conversion from
   `TypedTimeSamples<T>` to `TimeSamples` can be simplified to a direct
   move if the data source is already a `TimeSamples`.

### Phase 3 — Delete `TypedTimeSamples<T>`

1. Remove `struct TypedTimeSamples<T>` from `timesamples.hh`.
2. Remove all `template struct TypedTimeSamples<T>` and
   `template bool TypedTimeSamples<T>::get()` explicit instantiations
   from `timesamples.cc` (lines 458–899, ~430 lines).
3. Delete `timesamples-get-impl.inc`.
4. Remove `template struct TypedTimeSamples<T>` from the 5
   `timesamples-inst-*.cc` files, then delete the files.
5. Remove all `extern template struct TypedTimeSamples<T>` declarations
   from `timesamples.hh` (lines 1288–1539, ~250 lines).
6. Update CMakeLists.txt to remove `timesamples-inst-*.cc` from build.

### Phase 4 — Cleanup

1. Simplify `reconstruct_binary_sample()` using `get_scalar_impl<T>`
   (for `get_samples()` backward compat path used by pprinter/tests).
2. Remove `get_sample_at()` and any other raw-pointer-exposing methods.
3. Add `static_assert` for role-type layout compatibility:
   `static_assert(sizeof(color3f) == sizeof(float3))` in `define-type-trait.inc`.
4. Change `_data_offsets` from `vector<uint32_t>` to `vector<size_t>`.
5. Extract `TypeId` enum to `src/core/type-id.hh` to reduce include
   dependencies.
6. Evaluate removing `any_value_raw_cast` from all non-parser paths
   (parser/reader legitimately needs it for role-type construction).

### Phase 5 — Tydra Evaluation Simplification (separate follow-up)

1. Merge the 3 `.inc` files (`attribute-eval-typed-impl.inc`,
   `attribute-eval-typed-animatable-impl.inc`,
   `attribute-eval-typed-animatable-fallback-impl.inc`) into a single
   implementation.
2. Update `EvaluateTypedAnimatableAttribute<T>` to call
   `Animatable<T>::get()` (already simplified — thin wrapper).
3. Reduce the 6 instantiation TUs toward 2 (scalar + array).

---

## 5. Risk Assessment

| Risk | Mitigation |
|------|------------|
| Regression in blocked-sample handling | Extensive test coverage already exists (6 blocked-sample test cases). New `get_scalar_impl` must pass all before old code deletion. |
| `unique_ptr` move semantics in `Animatable<T>` | Explicit move constructor/assignment defined; tested with valgrind/ASan. |
| Switch-based dispatch slower than inline template | Compiler generates jump table (O(1)). The switch is hit once per `get()` call; binary search + memcpy/lerp dominate. |
| Role-type layout compatibility (C++ spec) | `static_assert` per role type verifies `sizeof(role) == sizeof(underlying)` at compile time. |
| `PrimVar` backward compat | `get_interpolated_value()` rewritten but maintains identical semantics; existing tests verify. |
| Array timesamples need template | Thin `get_array<T>()` template (~10 lines per inst.) is acceptable; ~25 instantiations total. |
| `get_samples()` reconstruction path | Kept for debug/pprint; uses same `get_scalar_impl<T>` to avoid duplicate implementation. |
| Generic storage path (string/token/dict) | Rare in animation; falls through to `_samples[idx].value.as<T>()` — unchanged from current behavior. |

---

## 6. Concrete API Changes

### `timesamples.hh` — Before / After

```cpp
// BEFORE (1559 lines)
struct TimeSamples {
  // ... storage ...
  template <typename T> bool get(T*, double, interp);          // 2 SFINAE overloads (public)
  template <typename T> bool add_sample(double, const T&);     // SFINAE
  bool get_sample_at(double, Sample**);                         // dangling risk
  const std::vector<Sample>& get_samples() const;               // triggers reconstruct
  // ...
};

template <typename T>
struct TypedTimeSamples { /* 178 lines */ };

extern template struct TypedTimeSamples<bool>;     // ~250 lines
// ... (~115 more) ...

// AFTER (~600 lines)
struct TimeSamples {
  // ... storage (unchanged) ...
  template <typename T> bool get(T*, double, interp);           // thin inline (public)
  template <typename T> bool get_array(std::vector<T>*, double, interp); // thin inline (public)
  template <typename T> bool add_sample(double, const T&);       // thin wrapper
  // get_sample_at() REMOVED
  // ...
private:
  bool get_scalar(void*, size_t idx, double t, TimeSampleInterpolationType);  // NON-template (switch)
  bool get_scalar_from_generic(void*, size_t idx);                            // fallback for non-binary
  template <typename T> bool get_scalar_impl(void*, size_t idx, double t, interp); // private, instantiated in .cc
};
// TypedTimeSamples<T> DELETED
// extern template decls DELETED
```

### `animatable.hh` — Before / After

```cpp
// BEFORE
template <typename T>
struct Animatable {
  TypedTimeSamples<T> _ts;     // ~32 bytes, always present
  // ...
};

// AFTER
template <typename T>
struct Animatable {
  std::unique_ptr<value::TimeSamples> _ts;  // 8 bytes, nullptr for scalar-only
  // + set_timesamples(TimeSamples&&)
  // + set_timesamples(const TimeSamples&)
  // + explicit move constructor/assignment
  // ...
};
```

---

## 7. Files Affected

| File | Action |
|------|--------|
| `src/timesamples.hh` | Remove `TypedTimeSamples<T>` + extern decls; add private `get_scalar` / `get_scalar_impl`; thin `get<T>()` and `get_array<T>()` wrappers |
| `src/timesamples.cc` | Remove explicit instantiations (~430 lines); add `get_scalar()` switch + `get_scalar_impl<T>` (~40 instantiations); simplify `reconstruct_binary_sample` |
| `src/timesamples-get-impl.inc` | **Delete** |
| `src/timesamples-inst-scalar.cc` | **Delete** |
| `src/timesamples-inst-scalar-role.cc` | **Delete** |
| `src/timesamples-inst-array.cc` | **Delete** |
| `src/timesamples-inst-array-role.cc` | **Delete** |
| `src/timesamples-inst-array-basic.cc` | **Delete** |
| `src/timesamples-pprint.hh` | Update to use new API (no `TypedTimeSamples`) |
| `src/timesamples-pprint.cc` | Update to use new API |
| `src/core/animatable.hh` | Replace `TypedTimeSamples<T>` with `unique_ptr<TimeSamples>`, add move semantics, `set_timesamples()` |
| `src/primvar.hh` | Remove `TypedTimeSamples` dependency from `set_typed_timesamples()` |
| `src/primvar.cc` | Rewrite `get_interpolated_value()` to call `TimeSamples::get<T>()` |
| `src/value-types.hh` | Remove `any_value_raw_cast` from public API |
| `src/define-type-trait.inc` | Add `static_assert` for role-type layout compatibility |
| `src/usdGeom.cc` | Replace `from_timesamples()` with `set_timesamples(TimeSamples&&)` |
| `src/tydra/scene-access.cc` | Replace `from_timesamples()` with `set_timesamples(TimeSamples&&)` |
| `src/tydra/attribute-eval-*.cc` | Update to simplified `Animatable<T>::get()` |
| `CMakeLists.txt` | Remove `timesamples-inst-*.cc` from build |
| `tests/unit/unit-timesamples.h` | Add tests for new `get_scalar` path |
| `tests/unit/unit-timesamples.cc` | Add tests for new `get_scalar` path |
