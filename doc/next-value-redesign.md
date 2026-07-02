# next-core: compact C-style `Value` representation

Status: **design + validated prototype** (not yet wired into `src/next`). Part of
the next-core C-style refactor (see `[[next-core-gated-lean-core]]`, the arena /
`small_vector` / `TfToken` work).

## Motivation

`next::Value` (src/next/types/value.{hh,cc}) is an any-like, type-erased USD value
container. It is already good C-style code: a `TypeId` tag + flag bits, runtime
dispatch (no public templates), SBO for scalars/vectors, and copy-on-write
`shared_ptr` for arrays/dicts. The one structural cost is the **SBO size**:

```
static constexpr size_t kSBOSize = 136;   // sized to hold matrix4d (128 B) inline
```

So `sizeof(Value) ≈ 144` bytes (136 SBO + tag/flags/size). **Every** `Value` pays
that — scalar `int`s, every element of `ValueStorage` (per-property default
values), every `TimeSampleStorage` entry, and every `Dict` value — even though
`matrix4d` (the reason for 136) is one of the *rarest* USD value types. Points,
normals, colors, tokens, and scalars dominate real scenes and all fit in ≤16 bytes.

Result: `ValueStorage`/`TimeSampleStorage`/`Dict` and any `std::vector<Value>` carry
~6–9× more bytes than the data needs, hurting footprint and cache behavior.

## Current representation (summary)

| Kind | Storage today |
|---|---|
| POD scalars/vectors/matrices | inline in the 136-byte SBO (memcpy) |
| String / Token / AssetPath | `StringStorage{std::string}` inline in SBO |
| Arrays | `shared_ptr<ArrayStorageBase>` in SBO; `VecArrayStorage<T>` with a **virtual** `clone()`; COW via `use_count()` |
| Dict | `shared_ptr<Dict>` in SBO, COW |
| Lazy crate arrays | `LazyArrayRef*` in SBO |

Dispatch for copy/move/destroy is a `switch` on `TypeId` + flags. A
function-pointer table (`types/type-info.hh`: `TypeInfo{size, align, construct,
destruct, copy, move, equals}` per `TypeId`) already exists but is not yet the
value's dispatch path.

## Proposed representation

A small discriminated value where **only small, common payloads are inline** and
everything large or non-trivial is a **COW box** referenced by a pointer.

```
class Value {                    // target: 24 bytes (16-byte payload)
  union Payload {                //  16 bytes
    uint64_t bits;               //   inline scalar (bool/int/float/double/half...)
    unsigned char inline16[16];  //   float3(12) / float4(16) / quatf(16) / color*  inline
    uint32_t token_id;           //   TfToken id — inline (4 bytes)
    Box*     box;                //   arrays, big scalars, string  (COW)
    LazyArrayRef* lazy;          //   undecoded crate array
  } p_;
  uint32_t size_;                //  4  array element count
  uint16_t type_id_;             //  2  TypeId
  uint8_t  flags_;               //  1  is_array | is_lazy | dirty | is_block
  uint8_t  pad_;                 //  1
};
```

`Box` — one heap allocation, intrusive refcount, no vtable, no `shared_ptr`
control-block allocation:

```
struct alignas(16) Box {         // header is exactly 16 B, so payload is 16-aligned
  std::atomic<uint32_t> rc;      // COW refcount
  uint16_t ty;                   // element/box TypeId (drives the free-function table)
  uint32_t elem_count;
  // payload bytes follow: matrix4d / VtArray<T> / std::string / Dict / ...
};
```

**Alignment is load-bearing** (the prototype's UBSan run caught this): the box
header must be padded so the payload begins on a 16-byte boundary, or `matrix4d`
/ double-vector loads are misaligned UB. `alignas(16)` on `Box` makes
`sizeof(Box)==16`, so the payload starts correctly.

### Inline vs boxed placement

| Placement | Types (payload ≤ 16 B) |
|---|---|
| **Inline** (no alloc) | bool, int/uint, int64/uint64, half, float, double; `TfToken` (id); vec2/3/4 h/f/i; vec2d; quatf; color3f/4f; point3f; normal3f; texcoord2f/3f |
| **Boxed** (COW) | vec3d(24), vec4d(32), quatd(32), matrix2/3/4 f/d; String / AssetPath; all arrays; Dict |

The inline width is a **tuning knob**. 16-byte payload (Value = 24 B) boxes
`vec3d`; widening to 24-byte payload (Value = 32 B) inlines `vec3d`/`quatd` too
(relevant because `xformOp:translate` and double points are common) at the cost of
8 more bytes per Value. Recommended default: **16-byte payload / 24-byte Value**;
revisit `vec3d` inlining with a footprint measurement on xform-heavy scenes.

Two wins fall out of prior next-core work: **`TfToken` makes `token` values inline
4-byte ids** (today `token` is a heap `std::string` shared with String), and the
same interning removes the per-value token string allocation.

### Dispatch: function-pointer table, not virtual / not switch

Reuse and extend `types/type-info.hh`. Give each boxed `TypeId` a
`clone`/`destroy` (and existing `copy`/`move`/`equals`) function pointer; `Value`
copy/move/destroy/hash become table lookups indexed by `type_id_`. This drops the
per-array-type vtable (`ArrayStorageBase::clone`) and the central `switch`, and
makes new types data-driven — the C-style dispatch the module already favors
(cf. the runtime `ValueParser`/`type-info` tables).

## Memory impact

- `sizeof(Value)`: **144 → 24 bytes** (6×; the prototype hits 16 with an 8-byte
  payload). `std::vector<Value>` and `Dict::entries` shrink by the same factor.
- Array *element* data is unchanged (it already lives in one contiguous buffer);
  the win is on the *handles* and on non-array scalar values.
- `Box` replaces `shared_ptr` (saves the separate control-block allocation and
  gives a tighter header) while keeping the same VtArray-style COW semantics that
  make compose/flatten/Clone cheap.

## Preserved invariants (must not regress)

- **Byte pass-through / `dirty_`**: lazy crate arrays stay undecoded; the
  `is_lazy`/`dirty` flags and `LazyArrayRef*` path must survive re-encode. This is
  the [[next-flatten-rss-cut]] / [[lazy-valuerep-lowmem]] behavior.
- **COW**: copy shares (refcount bump); first mutable access detaches
  (`DetachArray`/`DetachDict` equivalents on `Box`).
- **`is_block`** (`= None`) and empty vs block distinction.
- **Public API stability**: keep the `Make*` factories and `as_*` accessors
  identical so parser/crate/writer/schema/eval callers are unchanged. `as_token()`
  keeps returning a stable `const std::string&` (via `TfToken::str()` / the box).

## Migration plan (staged, byte-parity-gated)

1. **[LANDED]** Swap `shared_ptr<ArrayStorageBase>` (16 B + control block +
   vtable) for an intrusive-refcounted `ArrayBox` (atomic rc + function-pointer
   `clone`/`destroy`) behind an 8-byte `ArrayHandle` wrapper — no `Value` layout
   change, COW semantics unchanged. Handle **16 → 8 bytes**. Deferred-array
   keepalive converted to an intrusive retain + `shared_ptr<void>` release
   deleter. Verified: `next` ctests 25/25 byte-parity; ASan clean; TSan clean on
   parallel compose (`test_pcp_parallel`) — the atomic refcount is race-free.
   (`src/next/types/value.cc`.)
2. **[LANDED]** Scalar `token` now stores an inline 4-byte `TfToken` id (removed
   from `UsesStringStorage`; flows through the inline-POD copy/move/destroy paths
   with explicit `==`/hash/`raw_bytes` id branches). `as_token()` returns the
   stable table string, so callers/writer/crate are unchanged. Verified: 25/25
   byte-parity + a scalar-token USDA roundtrip; ASan clean.
3. **[LANDED]** Shrank the SBO **136 → 32 bytes** (was sized for `matrix4d`). The
   four oversized matrix scalars (`matrix3f`/`4f`/`3d`/`4d`) move to a share-only
   COW `ScalarBox` (intrusive rc, immutable so no detach; `alignas(16)` payload);
   everything ≤32 (all vectors, `matrix2f`/`2d`, `std::string`=32, `shared_ptr`
   Dict=16, 8-byte array handle) stays inline. `data_ptr`/`raw_data` (the crate
   writer's matrix byte source), `as_matrix3f/4f/3d/4d`, `==`/hash/`raw_bytes`,
   `MakeFromRaw`, and copy/destroy route through the box. **`sizeof(Value)`
   144 → 48** (3×). Verified: 25/25 byte-parity; matrix scalars byte-identical
   through **USDA and USDC**; ASan clean; TSan clean on parallel compose.
4. **[LANDED — revised]** The original framing ("route copy/move/destroy through
   the `TypeInfo` op table") did not apply: `Value` dispatches by STORAGE CATEGORY
   (inline POD / string / array / dict / boxed scalar), which is cleaner than a
   per-`TypeId` table for this storage model, and there is no per-type `switch` in
   copy/move/destroy to delete. The actual dead weight was the inverse — a
   per-`TypeId` `construct/destruct/copy/move/equals` function-pointer table in
   `TypeInfo` that was **never called**, yet instantiated ~300 `Pod*`/`Generic*`
   template functions. Removed those five fields + the helpers + the unused
   `COMPLEX_TYPE_INFO` macro (`type-info.{hh,cc}`). `type-info.cc`: compile
   **−34%** (0.47→0.31s), object file **−66%** (53→18 KB). Behavior-preserving
   (25/25 byte-parity). `GetTypeSize`/`GetTypeName` keep using the retained
   `size`/`name` fields.

5. **[LANDED]** Shrank the SBO further **32 → 16 bytes** by boxing `String`/
   `AssetPath` (32-byte `std::string` → a COW `StringBox` with an 8-byte handle;
   copy shares, `as_string()` detaches) and the double-vectors/quats that no
   longer fit (`vec3d`/`vec4d`/`quatd`/`matrix2d` join the already-boxed matrices
   via `ScalarBox` + a generic `store_scalar_payload`). `TfToken` (4 B),
   `shared_ptr<Dict>` (16 B), and array/box handles (8 B) still inline.
   **`sizeof(Value)` 48 → 32.** Profiled on real assets: 91–97% of property
   Values stay inline (win −24 B each), 3–9% box (double3/string; +16 B + 1
   alloc). Measured on an 80k-value scene: **RSS −5.6%**, malloc **+2.1%**
   (one box per `double3` scalar). Bonus: `String` copies are now COW-shared
   through composition instead of deep-copied. Byte-parity perfect through USDA
   **and** USDC; 25/25; ASan + TSan clean.
6. **[LANDED]** Reached the **24-byte** target: `storage_` `alignas(16)->alignas(8)`
   (inline types are now <= double, 8-aligned; boxed matrices are 16-aligned in
   their `ScalarBox`) and packed the four flag bools into `uint8_t : 1` bitfields.
   C++17 forbids bitfield default-member-initializers, so `Value()` initializes
   them and every typed ctor delegates to `Value()` (copy/move set them via
   `copy_from`/`move_from`). **`sizeof(Value)` 32 -> 24** (48 -> 24 overall,
   -50%); another -2.6% RSS (-8.8% cumulative on the 80k-value scene). Byte-parity
   perfect; 25/25; ASan + UBSan (alignment) + TSan clean.

Each step is independently revertable and keeps `sizeof` assertions + the
byte-identical USDA/USDC roundtrip suite green.

## Risks / open questions

- `matrix4d`/`vec3d` now allocate — measure the alloc-count trade on xform/matrix
  heavy scenes; tune the inline width (24 vs 32 B Value) with data.
- `dirty_`/lazy interaction with a boxed representation needs the same
  bit-exact re-encode tests the current path has.
- Threaded interning + intrusive `rc` are both `std::atomic`; keep the
  freeze/lock-free-read discipline of the name tables for token reads.

## Prototype

`scratchpad/value2_proto.cc` (standalone, no deps) validates the mechanism:
inline scalars, COW `Box` for `matrix4d`, COW float array, inline `TfToken`-style
id, `memcpy`-based copy/move with refcount share, and the alignment fix.

```
sizeof(proto::Value) = 16 bytes  (vs current 144)
vector<Value>(1000) payload = 16000 bytes (was 144000)
all prototype assertions passed        # + ASan/UBSan clean
```
