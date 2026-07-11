# AOUSD Core 1.0.1, OpenUSD, and TinyUSDZ `next`/Tydra-next review

| Review input | Pinned value |
|---|---|
| Review date | 2026-07-11 |
| TinyUSDZ revision | `f7679e6763feab7e84a5be66def3382a68ae5369` (`physics-2026-fix2`, dirty working tree) |
| OpenUSD source revision | `2095fafafd033fa23386d7ec6d58c7cc33974518` (`v26.03-278-g2095fafaf`) |
| OpenUSD comparison tool | `../OpenUSD/dist/bin/usdcat`; installed headers report `PXR_VERSION 2605` |
| Normative specification | [AOUSD Core Specification 1.0.1](../aousd/core/1.0.1/core_spec.md), source revision `b7bc21a`, 2025-12-12 |
| Vendored specification provenance | `aousd/specifications-public` at `2f9e746c4fbd7f48d6d2c9ac568133fe398bbfc0` |

The vendored Markdown and [license](../aousd/core/1.0.1/LICENSE) are exact copies from that checkout. Their SHA-256 digests are `b2feeb2c900befe224a372b2d60d0ddcc6e35048ab54c32c17d5c085b44c642d` and `9cc97638cf0185884ac800144b6246c7772f94ff2cc70686afa9574aaea4fa2b`, respectively.

## Executive conclusion

TinyUSDZ `next` is a substantial, security-conscious USD implementation, and its tested USDA, USDC, USDZ, composition, validation, and Tydra conversion surfaces are much broader than a minimal reader. It is nevertheless **not currently conformant with AOUSD Core Specification 1.0.1** under the specification's section 4 compliance definition.

That conclusion is not based merely on missing OpenUSD APIs. It follows from confirmed normative behavior differences in each of the three mandatory compliance areas:

- **Composed Stage Population:** recursive dictionary composition, relationship list-op composition, property/child ordering, and some specifier/custom-field rules differ from the normative result.
- **Value Resolution:** core value-clip resolution and schema fallbacks are absent, default-time behavior differs, and interpolation coverage is incomplete.
- **Format Implementations:** syntactically valid Unicode identifiers and splines are not supported; unsupported spline syntax is silently discarded; some foundational values are lost; invalid paths are accepted; and USDC 0.12 spline values are not implemented.

The most urgent problem is not feature breadth but **silent semantic or authored-data loss**. Loading and rewriting a valid layer can currently succeed while dropping spline properties or a `frame4d` value. Composition can succeed while dropping weaker dictionary entries or relationship targets. Until unsupported normative constructs either round-trip correctly or fail closed with a clear error, `next` should not be described as a general lossless USD editor or an AOUSD-conformant implementation.

The review also found strong foundations worth preserving: bounded parsing, no-exception error paths, broad Crate structure support, disciplined USDZ writing, meaningful composition coverage, extensive differential tests, and Tydra diagnostics that generally expose renderer-level degradation instead of hiding it.

## Scope and interpretation

This review covers:

- `src/next` core data model, USDA/USDC/USDZ I/O, composition, resource resolution, validation, and value evaluation;
- `src/tydra/next` render-scene conversion and access behavior;
- relevant legacy TinyUSDZ behavior where `tusdcat` provides a useful second implementation point;
- the checked-out OpenUSD source and `usdcat` as the expected behavior oracle where the AOUSD text is ambiguous;
- the normative AOUSD Core Specification 1.0.1 stored in this repository.

It does **not** require TinyUSDZ to reproduce every OpenUSD library, plugin, Python binding, imaging subsystem, or command-line option. Those are OpenUSD parity questions, not automatically AOUSD conformance requirements. Conversely, a small API surface can still be conformant if its implemented stage, resolution, and format behavior meets section 4.

The AOUSD supplemental test repository was inspected for coverage categories but was not executed through an adapter in this review. Its samples are useful reference material; the Core Specification remains normative.

### Principal implementation evidence

The findings below are grounded primarily in these implementation areas:

| Concern | TinyUSDZ source |
|---|---|
| USDA property parsing and unsupported suffix handling | [`ascii-parser-prim.cc`](../src/next/parser/ascii-parser-prim.cc) |
| Foundational type registry and values | [`type-id.hh`](../src/next/types/type-id.hh), [`type-info.cc`](../src/next/types/type-info.cc), [`value.hh`](../src/next/types/value.hh) |
| Path representation | [`path.hh`](../src/next/prim/path.hh), [`path.cc`](../src/next/prim/path.cc) |
| Composition and flattening | [`composition.cc`](../src/next/composition/composition.cc), [`flatten.cc`](../src/next/pipeline/flatten.cc), [`pcp/`](../src/next/pcp/) |
| Attribute evaluation and interpolation | [`attribute-eval.cc`](../src/next/eval/attribute-eval.cc), [`interpolation.cc`](../src/next/types/interpolation.cc) |
| Crate reading and feature decoding | [`crate-reader.cc`](../src/next/crate/crate-reader.cc), [`crate-reader-decode.cc`](../src/next/crate/crate-reader-decode.cc), [`crate-format.hh`](../src/next/crate/crate-format.hh) |
| Asset resolution | [`asset-resolver.cc`](../src/next/resolver/asset-resolver.cc) |
| USDZ I/O | [`usdz-reader.cc`](../src/next/reader/usdz-reader.cc), [`usdz-writer.cc`](../src/next/writer/usdz-writer.cc) |
| Semantic validation | [`usd-validation.cc`](../src/next/validation/usd-validation.cc) |
| Tydra render conversion | [`render-converter.cc`](../src/tydra/next/render-converter.cc), [`render-data.hh`](../src/tydra/next/render-data.hh) |

### Status and priority vocabulary

| Marker | Meaning |
|---|---|
| Conformant/strong | Reviewed behavior agrees with the normative requirement or shows unusually complete support. |
| Partial | Useful support exists, but required cases or exact semantics are missing. |
| Non-conformant | A confirmed normative input or operation produces a different result. |
| OpenUSD-only gap | Difference from the reviewed OpenUSD revision outside AOUSD Core 1.0.1. |
| P0 | Blocks a conformance claim, causes silent data loss, or invalidates a core operation. |
| P1 | Material interoperability or correctness difference needing near-term repair. |
| P2 | Important completeness/API/diagnostic gap without demonstrated silent core-data loss. |
| P3 | Tooling, documentation, or lower-priority parity improvement. |

## Compliance overview

AOUSD Core 1.0.1 section 4 requires exact results in three categories, not merely successful parsing.

| Compliance area | Status | Principal evidence |
|---|---|---|
| Composed Stage Population | **Non-conformant** | Dictionaries replace instead of recursively composing; relationship list ops lose weaker targets; explicit child/property order is not represented; custom-field resolution differs. |
| Value Resolution | **Non-conformant** | General value clips and schema fallbacks are absent; default-time and interpolation behavior differ. |
| USDA Format Implementation | **Non-conformant** | Unicode XID identifiers rejected; splines silently skipped; order statements not preserved; some values are silently lost; invalid paths accepted. |
| USDC Format Implementation | **Non-conformant** | Reader accepts version 0.12 but does not implement spline value type 59; reference `customData` is ignored; some encodings are explicitly unsupported. |
| Generic `.usd` forwarding | **Partial** | Format detection and forwarding exist; correctness inherits the selected USDA/USDC gaps. |
| USDZ Format Implementation | **Strong, with reader caveats** | Writer uses stored ZIP32 entries, 64-byte alignment, root-first layout, no encryption, and no archive comment. Reader is deliberately more permissive, which the specification allows. |

### Chapter-level matrix

| AOUSD Core area | `next` assessment | Notes |
|---|---|---|
| Foundational data types (§6) | Partial | Broad scalar/vector/matrix/array support, but no complete `frame4d`, `opaque`, `group`, path-expression, spline, or role/fallback behavior. |
| Document and layer data model (§7) | Partial | Strong spec containers and metadata coverage, but unknown property fields and several normative authored fields cannot be represented losslessly. |
| Paths and identifiers (§8) | Non-conformant | ASCII-oriented identifier validation rejects valid Unicode; raw path strings accept invalid repeated separators; order semantics are incomplete. |
| Asset resolution (§9) | Partial | Filesystem/package/search-path and callback resolution are useful; URI schemes, anonymous identifiers, and variable/expression substitution are not general core features. |
| Composition (§10) | Partial | Sublayers, references, payloads, variants, inherits, specializes, relocates, cycles, and prototypes have meaningful coverage; exact field composition still differs. |
| Composed stage population (§11) | Partial | Population works for many scenes, but authored ordering and some field/specifier algorithms do not match the required squashed result. |
| Value resolution (§12) | Non-conformant | Strength resolution is substantial; list ops, clips, schema fallback, DefaultTime, forwarding, and interpolation have confirmed gaps. |
| Schemas (§13) | Partial | Typed convenience views exist, but there is no general prim-definition/schema registry that supplies built-in properties and fallbacks. |
| Color (§14) | Partial | Color metadata and Tydra colorspace handling exist; full normative authored/fallback semantics are not supplied by core. |
| Collections (§15) | Partial | Data structures and validation exist, but this review did not find a complete schema/fallback-backed collection evaluation implementation. |
| Core formats (§16) | Mixed | USDZ writer is strong; USDA and USDC have conformance-blocking gaps described below. |

## Confirmed conformance findings

### AOUSD-FMT-001 — Spline syntax is silently discarded (P0)

**Normative area:** USDA Spline grammar, §§7.4.2.4, 12.3, 12.6, and USDC 0.12 type support.

The repository fixture `tests/usda/spline_basic.usda` contains five spline properties. `next_usdcat` accepts it but emits five warnings of the form `Unknown attribute property: spline`. Rewriting the layer produces an otherwise valid file whose `Splines` prim is empty. OpenUSD rewrites the same input with all five splines present.

This violates both data-model requirements and the format compatibility rule that an implementation must not simply ignore unsupported grammar. It is particularly severe because the operation reports success.

The implementation evidence is consistent across layers:

- the USDA parser recognizes the unknown property form and skips it;
- the core value system has no usable spline value representation;
- Crate type identifier 59 is declared, but corresponding read/write/value behavior is not implemented;
- value resolution has no spline evaluation path.

**Fix needed:**

1. Immediately make unsupported spline input a hard, source-located error in lossless load/rewrite modes.
2. Add spline data types, curve/knot/tangent parameters, typed storage, equality, and serialization.
3. Implement USDA parse/write and USDC 0.12 type-59 read/write.
4. Implement spline value resolution, including extrapolation and loop behavior required by §12.6.
5. Add OpenUSD-differential USDA and USDC round trips for every interpolation/extrapolation mode and for malformed splines.

### AOUSD-TYPE-001 — Foundational declared values can be lost (P0)

**Normative area:** §6 Foundational Data Types and format bijection.

A layer containing `frame4d frame = identity`, plus `opaque` and `group` declarations, loads successfully in `next`. On rewrite, the `frame4d` declaration remains but its authored value is gone. OpenUSD retains the value. `TypeId` and the USDA type table do not provide complete representations for these normative types.

Preserving only a declared type name is not sufficient: a successful format implementation must reproduce the authored field value.

**Fix needed:** add first-class representations and codecs for every AOUSD foundational type, with a generated coverage test that enumerates the specification's type table across scalar, array, default, time-sampled, dictionary, USDA, and USDC forms. Until then, reject an authored value whose declared type has no decoder.

### AOUSD-USDA-001 — Valid Unicode identifiers are rejected (P1)

**Normative area:** §§7.3.3 and 8 identifier rules; USDA identifiers use Unicode XID semantics.

The valid prim declaration `def Xform "München"` is accepted by OpenUSD and rejected by both `next_usdcat` and legacy `tusdcat`. The lexer and validators use byte-oriented `std::isalpha`/`std::isalnum` or ASCII ranges, not validated UTF-8 plus Unicode `XID_Start`/`XID_Continue` behavior.

One validation path takes the opposite approximation and accepts all non-ASCII bytes. That avoids some false negatives but also accepts malformed UTF-8 and characters outside the normative classes. Neither approximation implements the grammar.

**Fix needed:** centralize UTF-8 decoding and XID validation, use it in the lexer, path parser, authoring API, and validator, and test normalization-sensitive and malformed byte sequences. Do not use locale-dependent C character classification for USD identifiers.

### AOUSD-PATH-001 — Invalid paths are accepted as opaque strings (P1)

**Normative area:** §8.3 Path Grammar.

A relationship target `</Root//Child>` loads successfully in `next`; OpenUSD rejects the layer. The core `Path` representation is largely a string wrapper, and USDA path references do not pass through a complete grammar validator.

Permissive invalid paths can later affect namespace lookup, composition, relationship forwarding, and security policy decisions.

**Fix needed:** implement one canonical parsed-path validator and require it at every untrusted construction boundary. Preserve a separate explicitly named unchecked/internal constructor only where measured performance justifies it. Add the AOUSD valid/invalid path examples as table-driven tests.

### AOUSD-ORDER-001 — Prim and property ordering is not preserved or composed (P1)

**Normative area:** §8.2 path-element ordering and §11.3 ordered prim/property children.

The OpenUSD parser fixture `81_namespace_reorder.usda` is accepted by `next`, but rewriting loses root prim order and `reorder` statements for property and name children. The layer has child indices, but no complete authored `primOrder`/`propertyOrder` representation. `PropIndex::sort()` sorts interned numeric identifiers, which is neither authored order nor the AOUSD path-element ordering algorithm.

There is also an ecosystem wording issue worth separating from the implementation defect: an AOUSD example uses `reorder propertyChildren`, while the reviewed OpenUSD parser expects `reorder properties`. This should be tracked as a specification/OpenUSD grammar clarification. `next` currently supplies neither spelling's required semantics.

**Fix needed:** represent order fields as authored list ops, preserve them in USDA/USDC, apply the §11.3 ordering algorithm after namespace composition, and use deterministic AOUSD path ordering only for otherwise unordered residual children.

### AOUSD-COMP-001 — Dictionaries replace instead of recursively composing (P0)

**Normative area:** §§6.6.2 and 12.2 dictionary field resolution.

In a two-layer probe, the weaker layer authored:

```usda
customData = { dictionary nested = { int a = 1; int b = 2 }; int weak = 3 }
```

and the stronger layer authored a stronger `nested.a` plus `strong = 4`. OpenUSD recursively composes the dictionary, retaining `nested.b` and `weak`. `next` flattening retains only the stronger dictionary.

The current local-opinion copying logic copies a dictionary only when the destination has none; it does not recursively merge dictionary entries by strength.

**Fix needed:** implement a shared recursive dictionary composition routine for all dictionary-valued fields, including nested dictionaries and value blocks, and drive it from field metadata rather than one-off `customData` code. Add multi-layer, nested, deleted/blocked, and type-conflict differentials.

### AOUSD-COMP-002 — Relationship list-op composition loses weaker targets (P0)

**Normative area:** §§12.2 and 12.4 list-op resolution for relationship targets and attribute connections.

With a weaker `rel r = </A>` and a stronger `prepend rel r = </B>`, OpenUSD resolves `[</B>, </A>]`. `next` flattening produces only `</B>`. When a stronger relationship already exists, the current composition path skips the weaker relationship rather than applying the list operation.

The problem generalizes: the core models selected arc and relationship edit forms, but it does not yet expose a generic, fully ordered list-op value and resolution engine for all fields that permit one. `apiSchemas` qualifier support is simplified and does not cover the full combination behavior.

**Fix needed:** introduce one specification-driven list-op implementation supporting explicit, add, prepend, append, delete, and reorder semantics; use it for targets, connections, API schemas, and metadata fields identified by the schema/field registry.

### AOUSD-COMP-003 — Non-positive sublayer scale is applied instead of rejected (P1)

**Normative area:** §10.3.1.1 Layer Offsets.

For a sublayer offset `(offset = 10; scale = -2)` and a weaker sample at time 1, `next` flattening moves the sample to time 8. OpenUSD warns that the offset is invalid and uses the default/identity mapping, leaving the sample at time 1. The specification treats a scale less than or equal to zero as a composition error. TinyUSDZ validation only rejects zero, and composition applies a negative scale.

**Fix needed:** reject every non-finite or `scale <= 0` layer scale before composition and apply the specified default behavior while retaining a structured diagnostic.

### AOUSD-COMP-004 — `custom` does not resolve as a logical OR (P1)

**Normative area:** §12.2 attribute field resolution.

Given a weaker `custom int a = 1` and stronger `int a = 2`, OpenUSD's composed result remains `custom int a = 2`; `next` emits `int a = 2`. AOUSD specifies `custom` as true if any contributing opinion is true. The composition code updates selected flags on an existing slot but does not accumulate the custom flag.

**Fix needed:** move field combination rules into declarative metadata and implement `custom` as logical OR independent of the winning default value.

### AOUSD-VR-001 — General core value-clip resolution is absent (P0)

**Normative area:** §12.3.4 Value Clips.

The core stores clip metadata dictionaries but does not resolve ordinary attribute queries over clip sets. Tydra offers an optional animation-conversion bake path with a loader callback and sample cap, which is useful for rendering, but it is not stage value resolution and cannot make core queries conformant.

The AOUSD supplemental material includes basic, advanced, multiple-set, timing, and other clip cases. Those cases need a core resolver independent of Tydra.

**Fix needed:** implement clip-set metadata composition, manifest use, asset loading, active/times mapping, interpolation, fallback to ordinary samples/defaults, cycle/error handling, and caching in the core value resolver. Tydra should consume that resolver rather than own a separate partial semantics.

### AOUSD-VR-002 — Default-time queries consult time samples (P1)

**Normative area:** §12.3 Value Resolution.

`UsdPrim::GetPropertyValue` returns the earliest time sample when no authored default exists and describes this as OpenUSD parity. AOUSD instead defines a DefaultTime query as consulting the default value and then the schema fallback; it does not substitute the earliest sample. The evaluation API also lacks a distinct DefaultTime sentinel: `EvalOptions.time = 0` is always a numeric time.

**Fix needed:** introduce an explicit time-query type distinguishing DefaultTime from numeric time, remove earliest-sample substitution from default queries, and integrate schema fallback. Keep a separately named convenience function if callers genuinely need “default or earliest sample.”

### AOUSD-VR-003 — Interpolation coverage and held fallback are incomplete (P1)

**Normative area:** §12.6 Value Interpolation.

`TimeInterpolator` covers float/double/timecode scalars, several float/double vectors, `quatf`/`quatd`, matrix3/4, and arrays. It lacks complete behavior for scalar half, `quath`, half vectors, `matrix2d`, unsigned character, and integer-vector/other held-only values. For unsupported linear types, AOUSD requires held behavior; direct `AttributeEval` paths can instead return failure or empty.

Tydra's `ValueAtOrDefault` can mask some cases with a fallback call, but renderer convenience behavior is not core conformance.

**Fix needed:** dispatch interpolation from the complete type registry. Implement linear interpolation only for normative interpolatable types and automatically fall back to held for every other supported type. Add exhaustive type-by-type tests at exact, between, before, and after sample times.

### AOUSD-VR-004 — Value blocks lack schema-fallback resolution (P1)

**Normative area:** §§12.3 and 13 schema fallbacks.

Value blocks are parsed, written, and strength-composed reasonably well. However, `AttributeEval::EvalFromPrimSpec` can return a non-empty block value as the successful result, and there is no schema registry from which to obtain the required fallback. A block should suppress the authored weaker value, after which schema fallback or no-value semantics apply; it is not itself the consumer value.

**Fix needed:** give resolution an explicit “blocked authored value” state, then run schema fallback as a separate final step. Do not expose a block token as a typed attribute value except through authored-field inspection APIs.

### AOUSD-SCHEMA-001 — No general schema registry or prim-definition population (P0)

**Normative area:** §§12.3.5 and 13.3–13.5.

`next` has useful typed/convenience views for many schemas, but no general registry corresponding to OpenUSD's generated schema definitions and prim definitions. Consequently:

- built-in properties are not populated consistently on typed prims;
- schema fallback values are not available to the core resolver;
- applied API schema properties and multiple-apply namespaces are not generally expanded;
- validation cannot distinguish unknown applied API schemas from known ones;
- selected Tydra consumers hard-code defaults, producing API-dependent results.

This is a core conformance issue, not merely missing C++ wrapper classes.

**Fix needed:** generate or ingest a compact schema registry containing type inheritance, property definitions, variability, fallback values, allowed tokens, applied/multiple-apply API definitions, and field combination metadata. Make stage population, value resolution, validation, and typed views consume the same registry.

### AOUSD-REL-001 — Relationship forwarding is not exposed (P2)

**Normative area:** §12.4 Relationships.

The core can retrieve raw relationships, but this review found no general API that recursively resolves forwarded relationship targets with cycle detection and AOUSD ordering. Callers therefore cannot request both raw and forwarded targets with normative behavior.

**Fix needed:** add raw-target and forwarded-target APIs, with deterministic deduplication, cycle diagnostics, and prototype/instance-aware path handling.

### AOUSD-RES-001 — Resolver behavior is filesystem-centric (P2)

**Normative area:** §9 Asset Resolution.

The resolver supports relative/absolute filesystem paths, anchors, configured search paths, package paths, suffix fallback, and custom resolve/read callbacks. These are meaningful strengths. It does not provide a general URI/IRI scheme dispatcher, anonymous identifiers such as `usd-anon`, or normative variable/expression substitution as core behavior.

Callbacks can emulate some missing schemes, but conformance-sensitive callers need explicit identifier classification and stable resolver semantics, not implicit filesystem handling.

**Fix needed:** separate identifier parsing from resolution, add scheme registration and anonymous-layer handling, define substitution policy, and make suffix fallback opt-in for strict mode. Address the documented `lstat`-then-open TOCTOU window where security policy depends on the checked path.

### AOUSD-USDC-001 — Declared 0.12 support is incomplete (P0)

**Normative area:** Core File Formats, USDC version 0.12.

The Crate reader accepts versions from 0.4 through newer versions and the writer defaults to 0.8, upgrading for selected features. This is a strong structural foundation. AOUSD 1.0.1, however, requires reading the latest normative Crate version 0.12, including spline values. Merely accepting the version header is insufficient when a normative type cannot be decoded.

Other reviewed limitations include compressed boolean arrays being rejected and selected unexpected encodings being dropped. Those should produce actionable errors rather than partial-success layers.

**Fix needed:** maintain a version-feature matrix enforced by both reader and writer. A file using an unsupported normative feature must fail atomically. Add one fixture per normative type/encoding and differential field-table comparisons against OpenUSD.

### AOUSD-USDC-002 — Reference `customData` is decoded then ignored (P1)

**Normative area:** USDC References and the reference data model.

The Crate reader emits a warning that reference `customData` is ignored. AOUSD includes that dictionary as part of a reference item, so dropping it changes the authored field and can affect composition/tooling.

**Fix needed:** extend the reference value type, USDA and USDC codecs, equality, list-op composition, and writer to preserve per-reference custom data.

### AOUSD-META-001 — Unknown fields are not uniformly lossless (P1)

**Normative area:** §7 data model and format round-trip requirements.

Unknown prim metadata has a raw-preservation route and is a good forward-compatibility feature. Unknown layer metadata such as `framePrecision`, unknown property metadata, and some unsupported Crate fields do not have equivalent lossless storage. A fixed `PropMeta` representation necessarily drops fields it does not know.

**Fix needed:** add an extension-field map keyed by token with typed/raw value preservation at layer, prim, property, and variant/spec scopes. Known fields may be projected into optimized members, but the original authored representation must remain available for bijective rewrite.

### AOUSD-VALID-001 — The validator is useful lint, not a conformance runner (P2)

**Normative area:** all; this finding concerns evidence quality.

The validation implementation explicitly records several unavailable checks:

- the Crate validator group is registered but cannot run without table introspection;
- unknown API-schema validation lacks a schema registry;
- layer ownership is unavailable for some rules;
- authored-empty color configuration/management cannot always be distinguished;
- relationship variability is not represented;
- variant option bodies are not deeply validated;
- identifier validation approximates rather than implements Unicode XID rules.

Therefore, a clean `--validate-all` result does not establish AOUSD compliance.

**Fix needed:** label the tool as semantic validation, report skipped rule groups in machine-readable output, and build a separate AOUSD conformance harness that compares composed specs, authored fields, values, diagnostics, and format round trips.

## OpenUSD parity beyond AOUSD Core 1.0.1

These differences matter for interoperability with the reviewed OpenUSD revision but should not be mislabeled as AOUSD 1.0.1 violations.

### OUSD-USDC-001 — Crate `VtArrayEdit` 0.14 values are dropped (P2)

The reader recognizes newer Crate versions but does not retain OpenUSD's version-0.14 `VtArrayEdit` value. Because AOUSD Core 1.0.1 standardizes through Crate 0.12, this is an OpenUSD parity item. The same fail-closed rule should apply: accepting a newer version must not imply accepting fields that are then discarded.

### OpenUSD library and service surface

| OpenUSD area | TinyUSDZ `next`/Tydra status | Conformance significance |
|---|---|---|
| `Sdf` layer/spec/path/value model | Substantial custom equivalent | Core-relevant; exact gaps are documented above. |
| `Pcp` composition cache/index | Substantial custom PCP and legacy flattening paths | Core-relevant. Backend divergence must be reduced. |
| `Usd` stage/value/schema façade | Partial | Core-relevant where it changes population or resolution. |
| `Ar` resolver | Filesystem/package/callback subset | Core-relevant identifier semantics; plugin ecosystem is parity-only. |
| `Tf`, `Vt`, `Gf`, `Plug`, `Work`, `Trace` | Purpose-built smaller equivalents or absent | Generally API/performance parity, not automatically conformance. |
| Generated schema registry | Absent as a general shared registry | Core conformance blocker for fallbacks and applied schemas. |
| Hydra/Imaging/Storm | Tydra render-scene conversion, not a Hydra implementation | Outside AOUSD Core; product-scope difference. |
| Python bindings and `usdview` ecosystem | Separate, smaller bindings/tools | Product/API parity only. |

### Schema breadth snapshot

The reviewed OpenUSD source's `schema.usda` files declare approximately the following concrete and API schema inventory: core `usd` 7, `usdGeom` 31, `usdShade` 7, `usdLux` 21, `usdSkel` 5, `usdPhysics` 17, `usdMedia` 2, `usdMtlx` 1, `usdVol` 20, `usdRender` 5, `usdUI` 4, `usdProc` 1, `usdSemantics` 1, and `usdHydra` 1. Counts are a breadth indicator, not a compliance score.

`next`/Tydra provides meaningful native data handling for Xform, Mesh, PointInstancer, Camera, common geometry, common lights, Material/Shader and selected Preview Surface/MaterialX data, skeletal animation, physics scene/APIs/joints/collision/material, spatial audio, and project-specific AR extensions. It does not provide OpenUSD-equivalent generated wrappers or definitions for much of `usdVol`, `usdRender`, `usdUI`, `usdProc`, `usdSemantics`, `usdHydra`, or the long tail of geometry/light/API schemas.

The correct architectural target is not necessarily one handwritten C++ class per OpenUSD class. A compact generated schema registry plus generic prim/property access can close normative fallback/population gaps while typed wrappers remain demand-driven.

## Composition architecture observations

The newer PCP path has real depth: sublayers, references, payloads, variant selection, internal and external assets, deferred payload rules, inherits, specializes, relocates, implied arcs, cycles, instance keys, prototypes, and dependency invalidation are represented and tested. This is far beyond a simple overlay flattener.

There are nevertheless two observable semantics sources:

- the newer PCP cache/index path used by `next` tooling; and
- `Stage::Flatten` and legacy compositor paths, some of whose comments still declare external inherits unsupported.

Field-resolution fixes must be shared between these paths or one must become the sole normative engine. Otherwise a scene may produce different results depending on which public operation a caller uses. In particular, dictionary composition, generic list ops, specifier rules, custom flags, property/child order, and layer offset validation should live in reusable field-composition primitives rather than local copying code.

Specifier composition also needs a dedicated audit against §12.2. The current local-opinion logic includes useful behavior such as upgrading an `over` destination to `def`, but it does not visibly encode the complete class/def/over and direct-inherit/specialize rules as a testable table.

## Tydra-next feature and behavior review

Tydra is a render-ready scene conversion layer, not an AOUSD stage implementation. Its gaps should therefore be reported separately unless it overrides or masks core semantics.

### Supported conversion areas

- Meshes, points, basis curves, NURBS curves, and point instancers.
- Analytic cube, sphere, cone, cylinder, capsule, and plane conversion, including versioned cylinder/capsule handling.
- Cameras and common distant, dome, rectangle, disk, sphere, and cylinder lights.
- Material binding and selected USD Preview Surface, OpenPBR, UV texture, transform, and MaterialX-node behavior.
- Skeletons, joints, skinning, blend shapes, inherited skeleton bindings, and multiple-skeleton remapping.
- Physics annotations useful to downstream render/interaction consumers.
- Bounded diagnostics, unsupported-renderable recording, texture/colorspace handling, mesh cleanup, and animation sampling controls.

The dedicated `test_tydra_next` executable passed all reviewed tests, including render access/extraction, material parity and fallbacks, point-instancer diagnostics and visibility, MaterialX, physics annotations, curves, URDF, clip baking, skeletal parity, UV promotion, and legacy-schema parity.

### TYDRA-GEOM-001 — Unsupported renderable schemas (P2)

Hermite curves, volumes, tetrahedral meshes, and NURBS patches are explicitly unsupported renderables. Tydra records them and warns, which is preferable to an unexplained disappearance. NURBS orders and basis-curve bases outside supported subsets fall back with diagnostics.

**Fix needed:** preserve the current structured diagnostics; add converters in use-case order. Volume/field support requires a resource and shader design rather than only triangulation. For every fallback, expose whether topology or appearance changed so applications can enforce a strict conversion policy.

### TYDRA-SHADE-001 — Shader evaluation is a selected-node subset (P2)

Tydra is not an SDR/plugin shader registry. It evaluates selected Preview Surface, OpenPBR, texture, transform, and MaterialX patterns. Arbitrary `sourceAsset`, custom shader nodes, plugin lights, and filters do not have general execution semantics. Unsupported plugin lights and filters can degrade to inert or point-light behavior.

**Fix needed:** distinguish “unsupported but preserved,” “approximated,” and “failed” material/light results in the public render data. Add a strict mode that refuses approximations. If broader support is desired, use a registered node-definition/evaluator interface rather than expanding a monolithic name switch.

### TYDRA-ANIM-001 — Clip baking is renderer-local (P1)

Tydra's optional value-clip baking is valuable but has custom loading and maximum-sample constraints. It cannot provide AOUSD stage query semantics and risks diverging once core clip resolution is added.

**Fix needed:** move clip semantics to core and make Tydra request a bounded sampling plan from the core resolver. Retain conversion-level sample caps as resource policy, with an explicit truncation diagnostic.

### Product-scope differences

Tydra does not attempt to replace Hydra scene indices, imaging adapters, render delegates, render products/vars/settings, procedural plugins, or the complete OpenUSD UI/semantics/volume ecosystem. These are legitimate product-scope differences, not AOUSD violations. They should be documented as such so users do not infer Hydra parity from “render scene” terminology.

## Format-specific strengths and residual risks

### USDA

Strengths include a hand-written bounded parser, useful source diagnostics, lazy arrays, broad scalar/vector/array/dictionary syntax, composition-arc parsing, string escape handling, unknown prim-metadata preservation, and extensive parse/round-trip fixtures.

Residual risks are concentrated at forward-compatibility boundaries: unsupported grammar can be warned and skipped, declared-but-unsupported values can be dropped, property metadata has no generic preservation map, order statements are incomplete, path validation is permissive, and Unicode identifiers are overly restrictive. For an editor/converter, every warning that implies loss should become a hard error unless the caller explicitly chooses a lossy mode.

### USDC

Strengths include structural table decoding/encoding, lazy arrays, version gating, bounds and allocation checks, Crate dumps, malformed-input coverage, and writer upgrades for selected newer features. The reader's broad version acceptance is useful when paired with feature detection.

Residual risks include conflating “recognized version” with “fully decoded feature set,” ignored reference custom data, missing spline values, unsupported compressed boolean arrays, ignored unknown fields, and a validator group that currently cannot introspect Crate tables. Create a per-version capability table and reject a file at the first unsupported required value rather than returning a partial layer.

### USDZ

The writer closely follows the normative profile:

- entries are stored without compression or encryption;
- ZIP32 is used and ZIP64 is not emitted;
- the first entry is the root layer, normally `root.usdc`;
- file data is aligned to 64-byte boundaries;
- the end-of-central-directory comment is empty;
- asset paths and CRCs are checked/generated.

The reader scans local headers and is intentionally tolerant of archives whose central directory, CRCs, or alignment are imperfect. AOUSD permits implementations to read archives outside the writing profile, so permissiveness is not itself non-conformance. Security limits on archive size, entry count, allocation, and traversal must remain in effect.

## Security and robustness assessment

Security is a genuine comparative strength:

- no C++ exception dependency in normal error flow;
- explicit memory and archive budgets;
- Crate bounds, table, offset, and allocation validation;
- recursion/depth/cycle limits in multiple subsystems;
- lazy data paths for large arrays;
- malformed-file and fuzz-crash regression tests;
- package path validation and stored-entry USDZ policy.

Correctness gaps can still become security-relevant. Accepting invalid paths can bypass namespace assumptions; silently ignoring authored grammar can change scene intent; and resolver suffix fallback can intentionally re-home absolute/private paths. Strict applications need a mode that disables fallback search, fails on every lossy parse, validates canonical paths, and reports every external asset actually opened.

The resolver contains a documented check/use window around filesystem inspection and open. Where the threat model includes hostile filesystem mutation, open the file first with appropriate flags and validate the opened descriptor rather than relying on a prior `lstat`.

## Prioritized fix plan

### Phase 0 — Prevent silent loss

1. Add a `LossPolicy` with a strict default for conversion/rewrite tools. Any unsupported grammar, value type, field encoding, or metadata value must fail with layer/line/field context.
2. Audit every parser warning containing “unknown,” “unsupported,” “ignored,” or “dropped.” Classify it as safely preserved, deliberate lossy mode, or fatal.
3. Add a round-trip invariant test: after load/write/reload, compare every authored spec, field, value, target, and ordering token—not only the pretty-printed stage shape.

### Phase 1 — Close P0 normative gaps

1. Implement spline value types, USDA grammar/writer, USDC 0.12 codec, and resolver behavior.
2. Implement recursive dictionary composition and a generic list-op engine; route relationship targets, connections, API schemas, and metadata through it.
3. Add a generated schema/field registry and integrate prim definitions, applied APIs, property population, field-combination rules, and fallback values.
4. Implement general core value clips and make Tydra consume the core resolver.
5. Complete the AOUSD foundational type table, including lossless USDA/USDC handling.

### Phase 2 — Correct paths, time, and ordering

1. Implement UTF-8/XID identifiers and canonical path grammar validation.
2. Add explicit DefaultTime queries, schema fallback after blocks, and exhaustive held/linear interpolation dispatch.
3. Represent and compose prim/property order fields.
4. Reject non-positive/non-finite layer scales and implement remaining specifier/custom-field resolution rules.
5. Preserve reference custom data and generic extension fields at every spec scope.

### Phase 3 — Make compliance measurable

1. Build an adapter for the AOUSD Core supplemental cases, separated into stage population, value resolution, USDA, USDC, USD, and USDZ suites.
2. For each case, compare TinyUSDZ against the normative expected result and, when needed, the pinned OpenUSD oracle.
3. Produce a machine-readable manifest containing spec version, implementation revision, supported feature/version matrix, pass/fail/skip reason, and whether any lossy mode was enabled.
4. Make skipped validator groups visible and fail a “conformance” run on any skip.
5. Pin both the OpenUSD source revision and the actual `usdcat` binary version; rebuild the oracle when they differ.

### Phase 4 — Broaden OpenUSD/Tydra parity by demand

1. Add relationship forwarding and richer resolver schemes.
2. Expand schema wrappers from the generated registry rather than by hand duplication.
3. Add Tydra strict conversion and structured approximation reporting.
4. Prioritize volume/field, Hermite/TetMesh/NURBS-patch, render-settings, and shader-node work from application requirements, clearly labeling these as product parity.

## Verification performed

The existing `build-next` tree was rebuilt successfully. The following focused gate passed:

```sh
USDCAT_PATH=../OpenUSD/dist/bin/usdcat \
  ctest --test-dir build-next --output-on-failure \
  -L next -LE 'benchmark|corpus'
```

Result: **26/26 tests passed**.

The corpus-labelled gate also passed:

```text
files: 280
pass: 274
warn: 6
fail: 0
timeout: 0
crash: 0
```

All six corpus warnings concerned preserved unknown prim metadata named `hide_in_stage_window` and `no_delete`.

Direct `test_tydra_next` execution passed its complete registered suite. These results demonstrate a healthy regression baseline, but they do not contradict the conformance findings: the differential probes exercise normative cases not asserted by the current tests.

### Differential probe summary

| Probe | TinyUSDZ `next` | OpenUSD `usdcat` | Expected action |
|---|---|---|---|
| Unicode prim `München` | Rejects valid identifier | Accepts | Implement Unicode XID. |
| `spline_basic.usda` rewrite | Success with warnings; five splines gone | Five splines retained | Fail closed, then implement splines. |
| `frame4d = identity` rewrite | Declaration retained, value gone | Value retained | Complete foundational type codec. |
| Namespace reorder fixture | Order opinions lost | Order retained/applied | Store and compose order fields. |
| Relationship target `</Root//Child>` | Accepts | Parse error | Validate path grammar. |
| Sublayer scale `-2` | Applies negative mapping | Warns and uses default mapping | Reject `scale <= 0`. |
| Nested `customData` layers | Strong dictionary replaces weak | Recursively merged | Implement dictionary resolution. |
| Weaker target + stronger prepend | Weaker target lost | Both targets, correct order | Implement generic list ops. |
| Weak `custom`, strong non-custom | `custom` lost | `custom` remains true | OR the field across opinions. |

## Suggested regression tests

Each fixed finding should have four layers of coverage where applicable:

1. typed unit test of the data/field algorithm;
2. USDA parse/write/reparse authored-field equality;
3. USDC read/write/re-read authored-field equality at the normative version;
4. OpenUSD differential comparison of the composed/squashed result.

At minimum add named tests for:

- every Unicode identifier class and invalid UTF-8;
- every valid/invalid AOUSD path production;
- all foundational scalar and array types;
- spline interpolation, tangents, loops, extrapolation, and malformed input;
- nested dictionary strength and blocks;
- every list-op operation and multi-layer combination;
- `custom`, variability, specifier, and type-name field rules;
- child/property ordering with deletes, relocates, and variant contributions;
- positive, zero, negative, NaN, and infinite layer scales;
- DefaultTime versus numeric zero;
- interpolation dispatch for every registered type;
- value clips with manifests, multiple sets, offsets, gaps, and cycles;
- schema fallback, single-apply, and multiple-apply API schemas;
- USDC 0.8 through 0.12 feature matrices and unsupported-newer-feature failure;
- reference `customData` round trips;
- strict-loss mode for every currently ignored warning.

## Known review limitations

- The TinyUSDZ working tree contained pre-existing uncommitted changes, including in the reviewed `next`/Tydra areas. Findings describe the exact snapshot identified above and should be rechecked after those changes settle.
- The OpenUSD source checkout and installed comparison binary are not proven to be the same build: source describes `v26.03-278`, while installed headers report version 26.05. This was recorded rather than hidden; conformance tests should rebuild and pin one oracle.
- The review used targeted source inspection, the existing test suites, and focused differential probes. It is not an assertion that every OpenUSD API or every AOUSD sentence was exhaustively exercised.
- The AOUSD supplemental test suite was not run through a TinyUSDZ adapter, so no official pass percentage is claimed.
- OpenUSD behavior is treated as the expected oracle only when the normative text is ambiguous, consistent with the specification's guidance. A confirmed difference from newer OpenUSD alone is labeled separately.

## Final assessment

TinyUSDZ `next` is already credible as a bounded, portable USD ingestion and rendering foundation, especially for controlled content profiles. Its USDZ writer, broad Crate machinery, composition graph work, diagnostics, and Tydra conversion coverage are notable strengths.

For arbitrary AOUSD Core 1.0.1 content, the current implementation must be treated as **profile-based and potentially lossy**, not conformant. The critical path is clear: stop silent loss, centralize the type/schema/field rules, implement splines and clips, repair recursive field/list-op composition, and make conformance executable. Those changes will improve both AOUSD correctness and maintainability more than pursuing additional OpenUSD surface-area wrappers first.
