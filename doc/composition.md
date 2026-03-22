# Composition in USD: OpenUSD PCP vs tinyusdz

## Overview

Composition is the core mechanism by which USD builds a resolved scene graph from
multiple layers, files, and composition arcs. Each prim in the final stage may
draw its opinions (property values, metadata, children) from many sources. The
AOUSD Core Spec (section 10) defines a strict strength ordering — **LIVRPS** —
that all conforming implementations must follow.

This document compares two implementations:

1. **OpenUSD PCP** (Prim Composition Protocol) — the reference implementation,
   a recursive DAG-based algorithm that preserves full opinion provenance.
2. **tinyusdz** — a progressive, iterative layer-flattening approach designed
   for minimal memory, zero dependencies, and security-focused parsing.

The goal is to analyze whether tinyusdz's approach correctly satisfies LIVRPS
ordering, document known limitations, and identify areas for future work.

---

## The LIVRPS Composition Ordering

LIVRPS defines the relative strength of composition arcs. Stronger arcs override
weaker ones when they provide opinions for the same property or metadata field.
From strongest to weakest:

| Position | Arc Type | Description |
|----------|----------|-------------|
| **L** | Local & SubLayers | Direct opinions in the root layer and its sublayer stack |
| **I** | Inherits | Class-based opinion sharing (like OOP inheritance) |
| **V** | Variants | Switchable opinion sets within a prim |
| **R** | References | Cross-file prim inclusion |
| **P** | Payloads | Deferred cross-file prim inclusion (same semantics as references) |
| **S** | Specializes | Globally weak shared opinions (weaker than all other arcs everywhere) |

**Relocates** are not part of the LIVRPS mnemonic. They modify the composed
namespace by renaming prims after composition arcs are resolved (AOUSD Core Spec
10.3.2.6).

**Key spec sections:**
- 10.4 — LIVRPS ordering
- 10.4.1 — Specializes are globally weaker than all other opinions
- 10.3.2.5 — Variant selection is deferred: selections are computed using
  opinions from all arcs (I, R, P, S), not just local

---

## OpenUSD's PCP (Prim Composition Protocol)

### DAG-Based Composition Graph

PCP builds a **PcpPrimIndex** for each prim — a directed acyclic graph (DAG) of
`PcpNodeRef` nodes. Each node represents an opinion source with its arc type,
layer stack site, and namespace mapping. The graph preserves the full provenance
of every opinion, enabling introspection, debugging, and incremental updates.

**Entry point:** `Pcp_BuildPrimIndex()` in `pxr/usd/pcp/primIndex.cpp`

The algorithm uses a **priority-ordered task queue** combined with recursive
construction via `PcpPrimIndex_StackFrame`. Tasks are enqueued for each arc type
discovered during a pre-scan (`_ScanArcs()`), and processed in strength order:

```
EvalNodeRelocations → EvalNodeReferences → EvalNodePayloads
  → EvalNodeInherits → EvalNodeSpecializes
```

Variant and dynamic payload evaluation is deferred to maintain correct strength
ordering within the task queue.

The arc types are defined in the `PcpArcType` enum (`pxr/usd/pcp/types.h`):

```cpp
enum PcpArcType {
    PcpArcTypeRoot,
    PcpArcTypeInherit,     // Strongest
    PcpArcTypeVariant,
    PcpArcTypeRelocate,    // Between Variant and Reference
    PcpArcTypeReference,
    PcpArcTypePayload,
    PcpArcTypeSpecialize,  // Weakest
};
```

Note that **Relocate sits between Variant and Reference** in the enum ordering,
giving it a defined strength position in the graph.

### Strength Ordering

`PcpCompareSiblingNodeStrength()` in `strengthOrdering.cpp` determines which of
two sibling nodes is stronger. The comparison uses multiple criteria in order:

1. **Arc type enum value** — lower values are stronger
2. **Namespace depth** — deeper opinions are stronger (for non-specializes arcs)
3. **Origin node strength** — recursively compares origin chains for propagated arcs
4. **Sibling arc number** — among same-type siblings, lower arc numbers are stronger

Specializes arcs receive special handling: propagated specializes nodes track
their "origin root distance" (chain length from the authored arc to the
propagated location), and the comparison considers the namespace depth of the
class hierarchy that the node belongs to.

For comparing arbitrary (non-sibling) nodes, `PcpCompareNodeStrength()` walks
up to the lowest common ancestor and compares at the divergence point.

### Key PCP Features

| Feature | Implementation |
|---------|---------------|
| Cycle detection | `_CheckForCycle()` — walks ancestor chain and parent stack frames |
| Specializes propagation | `_PropagateNodeToRoot()` — copies subtree to graph root for globally-weak semantics |
| Relocate handling | `_EvalNodeRelocations()` — adds relocation arcs, manages "spooky ancestors" (inherited opinions from relocation source) |
| Deferred evaluation | Variants and dynamic payloads evaluated lazily within the task queue |
| Node culling | Inert and culled nodes reduce traversal cost without losing dependency tracking |
| Namespace mapping | `PcpMapExpression` — composable, lazy-evaluated path mapping across arcs |
| Opinion retention | Full DAG preserves all opinion sources for later value resolution |

### Conceptual PcpPrimIndex DAG

```
Root (local layer stack)
├── Inherit → /__class__/Base
│   └── Reference → asset.usd</Base>
├── Variant → {modelingVariant=high}
├── Reference → model.usd</Model>
│   ├── Inherit → /__class__/ModelBase  (implied)
│   └── Payload → geo.usd</Geo>
└── Specialize → /__class__/Defaults  (propagated to root)
```

Each node carries: arc type, layer stack, prim path, map-to-parent, map-to-root,
flags (has specs, is inert, is culled), and origin tracking.

---

## tinyusdz's Progressive Layer-Flattening Approach

### Design Philosophy

tinyusdz takes a fundamentally different approach: instead of building a
composition graph, it **eagerly flattens** opinions from each arc type into a
single working `Layer` (a tree of `PrimSpec` objects). Each composition phase
processes the entire PrimSpec tree for one arc type, producing a new flattened
layer that becomes input to the next phase.

Design goals:
- **Minimal peak memory** — in-place variants free source data after copying
- **Zero external dependencies** — no Boost, TBB, or similar
- **Security-focused** — configurable depth limits, bounds checking, no exceptions
- **Simplicity** — ~2,700 lines of C++ vs OpenUSD PCP's ~10,000+ lines

Core files:
- `src/composition.hh` — public API (427 lines)
- `src/composition.cc` — implementation (2,035 lines)
- `src/composition-reconstruct.cc` — PrimSpec-to-Prim conversion (681 lines)
- `src/namespace-mapping.hh` — path remapping utilities
- `src/core/composition-types.hh` — Reference, Payload, ArcOrigin, LayerOffset

### CompositeAllArcs — The Main Pipeline

`CompositeAllArcs()` (`composition.cc` lines 1942–2033) orchestrates all
composition arcs in LIVRPS order. Sublayers (L) are composed before this
function is called.

```
Input: Layer with sublayers already composed (L done)
  │
  ├─ Collect variant selection opinions (local)
  │
  ├─ I: CompositeInherits()
  │    └─ Collect variant opinions from inherited content
  │
  ├─ [V deferred — skip for now]
  │
  ├─ R: CompositeReferences()
  │    └─ Collect variant opinions from referenced content
  │
  ├─ P: CompositePayload()
  │    └─ Collect variant opinions from payload content
  │
  ├─ V: Deferred variant resolution
  │    ├─ ComputeVariantSelections() — strongest opinion wins
  │    └─ ApplyDeferredVariantSelectionsRec()
  │
  ├─ S: CompositeSpecializes() — applied last (globally weaker)
  │
  └─ Relocates: CompositeRelocates() — final namespace rename

Output: Fully composed Layer
```

Each phase checks whether the arc type is present before processing, avoiding
unnecessary work for simple scenes.

### Two PrimSpec Merging Primitives

All composition arcs ultimately merge PrimSpec trees using one of two operations:

**InheritPrimSpec** (`composition.cc` lines 1593–1678)

Creates a copy of the source (weaker) PrimSpec, then overlays the destination
(stronger) PrimSpec's opinions on top. Properties present in `dst` override
those from `src`; properties only in `src` are preserved as defaults.

```
Result = copy(src)          // Start from weaker opinion
Result.metas = src.metas    // Copy metadata
Result.metas.update_from(dst.metas)  // Stronger overrides
Result.props[p] = dst.props[p] if exists, else keep src.props[p]
```

Used for: prepend references, prepend payloads, inherits, specializes.

**OverridePrimSpec** (`composition.cc` lines 1520–1588)

Directly overlays the source (stronger) PrimSpec onto the destination (weaker).
Source properties replace destination properties.

```
dst.metas.update_from(src.metas)  // Stronger overrides
dst.props[p] = src.props[p] for each p in src
```

Used for: append references, append payloads.

Both operations implement AOUSD Core Spec section 12.2 metadata resolution:

| Spec | Rule | Implementation |
|------|------|---------------|
| 12.2.1 | Specifier: `over` + `def`/`class` = `def` | Strongest specifier wins; `over` defers to defining specs |
| 12.2.2 | typeName from defining specs only | Only `def`/`class` specs contribute typeName |
| 12.2.3 | Preserve uniform variability | If weaker opinion is `uniform`, keep it even if stronger is `varying` |
| 12.2.4 | Custom flag OR semantics | `custom = true` if ANY opinion says true |

### SubLayer Composition

`CompositeSublayersRec()` (lines 566–650):

- Processes the root layer first (strongest), then sublayers in declaration order
- Uses `CombinePrimSpecRec()` for merging (weaker fills in gaps, does not override)
- **Cycle detection** via `IsVisited()` — tracks visited layer names in a stack of
  sets (lines 47–55, 597–602)
- Applies layer offsets to timeSamples per Spec 10.3.1 via `ApplyLayerOffsetRec()`
- Tags PrimSpecs with arc origins via `TagPrimSpecArcOriginRec()` for implied
  inherit propagation

### Reference and Payload Composition

`CompositeReferencesRec()` (lines 698–895) and `CompositePayloadRec()` (lines
897–1075) share the same structure:

1. **Depth-first traversal** — process children before the current prim
2. **Asset resolution** — `LoadAsset()` resolves paths using per-PrimSpec
   resolver state (current working path, search paths)
3. **Internal references** — empty asset path with absolute prim path treated as
   inherit-like operations within the same layer
4. **ListOp handling:**
   - `prepend` / `resetToExplicit` → `InheritPrimSpec()` (weaker fills in)
   - `append` / `add` → `OverridePrimSpec()` (stronger replaces)
   - `delete`, `order` → not yet supported (returns error)
5. **Layer offset** — applied to timeSamples per Spec 10.3.2.2
6. **Arc origin tracking** — recorded for implied inherit propagation (Spec 10.3.2.3)
7. **Path prefix replacement** — `ReplaceRootPrimPathRec()` remaps referenced
   prim paths to the referencing prim's namespace

### Inherits Composition

`CompositeInheritsRec()` (lines 1103–1220):

- Finds the target PrimSpec by absolute path within the same layer
- Applies `InheritPrimSpec()` — inherited content provides defaults
- **Implied inherits/specializes** (Spec 10.3.2.3/10.3.2.4): single-level
  propagation from referenced/payload layers via `inheritPaths`/`specializePaths`
  (lines 1170–1217). Checks `arc_origins` and `inheritPaths` metadata propagated
  from referenced layers. If matching class prims exist in the current layer,
  applies them via `InheritPrimSpec()`.
- Limitation: only single inherit target per prim (line 1128)
- Limitation: ListEditQual is ignored — all inherits treated as prepend (line 1148)

### Specializes Composition

`CompositeSpecializesRec()` (lines 1432–1496):

- Same structure as inherits composition
- Uses `InheritPrimSpecImpl()` — identical property-override semantics
- Applied last in `CompositeAllArcs()` to ensure globally weaker behavior
- Limitation: only single specialize target per prim (line 1455)
- Limitation: ListEditQual is ignored (line 1473)

### Deferred Variant Evaluation

Implements AOUSD Core Spec 10.3.2.5 in three phases:

1. **Collect** — `CollectVariantSelectionOpinionsRec()` (lines 1734–1747) gathers
   per-prim `VariantSelectionMap` values from each composition phase (local, I, R, P)
2. **Resolve** — `ComputeVariantSelections()` (lines 1759–1771) takes the
   strongest opinion per variant set name (first in the collected vector wins)
3. **Apply** — `ApplyDeferredVariantSelectionsRec()` (lines 1774–1809) applies
   resolved selections via `VariantSelectPrimSpec()`

This correctly ensures that variant selection can be influenced by opinions from
references and payloads, not just local opinions.

### Relocates

`CompositeRelocates()` implements AOUSD Core Spec 10.3.2.6:

- Validates entries via `ValidateRelocates()` from `namespace-mapping.hh`
- **Phase 1: Prim relocation** — detaches prims from source locations and
  reattaches at target locations. Handles same-parent renames, root-level
  renames (updates primspecs map key), and cross-parent moves (creates
  intermediate `over` prims as needed). Processes deepest paths first to
  avoid invalidating parent paths.
- **Phase 2: Path remapping** — remaps all path references throughout the tree
  using `NamespaceMapping::Apply()`: relationship targets, attribute connections,
  and composition arc target paths (inherits, specializes, internal references,
  internal payloads)
- Applied after all composition arcs as a final pass

### Namespace Mapping

`namespace-mapping.hh` provides path remapping utilities per Spec 10.5:

```cpp
struct NamespaceMapping {
  std::vector<std::pair<Path, Path>> entries;
  Path Apply(const Path &path) const;  // Prefix-match remapping
};
```

Factory functions:
- `MakeReferenceMapping()` — per Spec 10.3.2.1.1
- `MakeInheritMapping()` — per Spec 10.3.2.3.1
- `MakeRelocatesMapping()` — per Spec 10.3.2.6.1
- `ComposeNamespaceMappings()` — for nested arcs per Spec 10.5

Currently used primarily in relocates composition. Reference and payload
composition use `ReplaceRootPrimPathRec()` for path prefix replacement instead.

### In-Place Memory Optimization

The `*InPlace()` function variants take ownership of the source layer via
`std::unique_ptr<Layer>` and progressively free memory as PrimSpecs are
converted:

- `CompositeSublayersInPlace()`
- `CompositeReferencesInPlace()`
- `CompositePayloadInPlace()`
- `LayerToStageInPlace()`
- `PrimSpecToPrimInPlace()`

This reduces peak memory during composition of large scenes by releasing source
data as soon as it has been merged into the target.

---

## Comparison Table

| Aspect | OpenUSD PCP | tinyusdz |
|--------|------------|----------|
| **Data structure** | PcpPrimIndex DAG per prim | Flat PrimSpec tree (single working Layer) |
| **Algorithm** | Recursive, task-queue based | Iterative, phase-by-phase across whole tree |
| **Opinion retention** | All opinions preserved in DAG | Eagerly merged (destructive) |
| **Code size** | ~10,000+ lines (PCP module) | ~2,700 lines (composition.cc + .hh + reconstruct) |
| **LIVRPS encoding** | PcpArcType enum values + strength comparator | Sequential phase execution in CompositeAllArcs() |
| **Relocate position** | Between V and R in arc type enum | Applied after all arcs as final pass |
| **Specializes** | Propagated to graph root nodes | Applied as last composition phase |
| **Cycle detection** | All arc types (full graph walk) | All arc types (visited sets per arc type; depth limits as fallback) |
| **Multiple inherit targets** | Fully supported | Fully supported (processed in list order) |
| **Multiple specialize targets** | Fully supported | Fully supported (processed in list order) |
| **ListEditQual (inherits/specializes)** | Full support (prepend, append, delete, order) | Full support via ResolveListOpsT (prepend, append, delete, order) |
| **Implied inherits/specializes** | Multi-level propagation | Single-level propagation via inheritPaths/specializePaths |
| **Lazy payload evaluation** | Yes (can defer loading) | Yes (via `load_policy` callback in PayloadCompositionOptions) |
| **Deferred variant selection** | Yes | Yes (Spec 10.3.2.5 compliant) |
| **Layer offset application** | Yes | Yes (Spec 10.3.1, 10.3.2.2) |
| **Namespace mapping** | PcpMapExpression (composable, lazy) | NamespaceMapping (simple prefix matching) |
| **Memory model** | High (preserves full graph) | Low peak (in-place variants available) |
| **Dependencies** | Boost, TBB, etc. | Zero external dependencies |
| **Error handling** | C++ exceptions + error objects | nonstd::expected, error strings (no exceptions) |

---

## LIVRPS Correctness Analysis for tinyusdz

### L (Local/SubLayers): Correct

SubLayers are composed before `CompositeAllArcs()` is called.
`CompositeSublayersRec()` correctly:
- Processes root layer first (strongest), then sublayers in declaration order
- Uses `CombinePrimSpecRec()` with fill-in semantics (weaker opinions do not
  override stronger ones)
- Applies layer offsets to timeSamples
- Detects circular sublayer references via `IsVisited()`

### I (Inherits): Correct

`CompositeInherits()` correctly applies inherit arcs after local opinions. The
`InheritPrimSpec()` semantics are correct: inherited content provides defaults
that local opinions override.

- Multiple inherit targets per prim are supported (processed in list order)
- ListEditQual is handled: prepend/append use `InheritPrimSpec()`, delete/order
  warn and skip (cannot undo flattened opinions)
- Cycle detection via visited path set prevents infinite recursion
- Implied inherits: single-level propagation from referenced/payload layers is
  implemented. When a referenced prim has `inherits`, those paths are propagated
  via `inheritPaths` metadata and applied if matching class prims exist in the
  referencing layer stack. Multi-level propagation (chains of references each
  carrying inherits) works through the progressive composition model.

### V (Variants): Correct

Deferred variant evaluation correctly implements Spec 10.3.2.5:
- Opinions collected from L, I, R, and P phases
- Strongest opinion wins per variant set name
- Applied after R and P, ensuring referenced/payload content contributes selections

### R (References): Correct

References correctly use:
- `InheritPrimSpec()` for `prepend`/`resetToExplicit` — local (stronger) opinions
  override referenced (weaker) content
- `OverridePrimSpec()` for `append` — appended content overrides existing values
- Layer offset application per Spec 10.3.2.2
- Arc origin tracking per Spec 10.3.2.3

- `delete` and `order` list edit qualifiers warn and skip (cannot undo
  flattened opinions in our model)
- Cycle detection via visited `(asset_path, prim_path)` set

### P (Payloads): Correct

Identical semantics to references. Same prepend/append handling, same layer
offset behavior. The only difference is that payloads are conceptually
deferrable, but tinyusdz resolves them eagerly.

### S (Specializes): Correct for Simple Cases

Specializes are applied last in `CompositeAllArcs()`, which correctly makes them
globally weaker than all other opinions per Spec 10.4.1.

- Multiple specialize targets supported, ListEditQual handled
- Implied specializes: single-level propagation from referenced/payload layers
  via `specializePaths` metadata (Spec 10.3.2.4), applied if matching prims
  exist in the referencing layer stack
- Cycle detection via visited path set

**Caveat:** For complex scenarios with nested arcs, the flattening approach
cannot fully capture the "globally weaker" rule that OpenUSD PCP preserves
in its DAG-based strength comparator.

### Relocates: Different Positioning, Functionally Equivalent for Most Cases

tinyusdz applies relocates after all composition arcs as a final namespace
renaming pass. OpenUSD places Relocate between Variant and Reference in the arc
type strength enum, giving it a defined position in the DAG.

For most practical cases, the result is the same because relocates primarily
rename prims rather than contribute property opinions. The difference could
matter in edge cases where relocate-induced namespace changes affect which prims
are found by later-evaluated arcs — but since tinyusdz evaluates all arcs before
applying relocates, the relocation is purely a post-composition rename.

### Cycle Detection: All Arc Types Covered

Cycle detection is implemented for all arc types:
- **SubLayers**: `IsVisited()` with a stack of visited layer name sets
- **References/Payloads**: `ArcVisitedSet` tracking `(asset_path, prim_path)` pairs,
  shared across both references and payloads phases in `CompositeAllArcs()` for
  cross-arc cycle detection
- **Inherits/Specializes**: `PathVisitedSet` tracking visited prim paths within
  the same layer

The shared `ArcVisitedSet` catches cycles where an asset is loaded in the
references phase and then again in the payloads phase. Depth limits remain as
an additional safety net.

### Active Prim Metadatum

Prims with `active = false` (a composed metadatum) are removed from the
composed layer in a post-composition filtering pass. Both root-level and child
prims are handled. This ensures inactive prims and their descendants are excluded
from the final stage.

### Summary Verdict

tinyusdz's `CompositeAllArcs()` **correctly implements the overall LIVRPS
ordering** with full support for: multi-target inherits/specializes, ListEditQual
handling, cycle detection for all arc types, deferred variant evaluation
(Spec 10.3.2.5), and `active` prim metadatum filtering.

The remaining correctness risks are in edge cases: complex implied inherit
propagation across multiple layer stacks, and nested-arc specializes "globally
weaker" semantics. These affect spec compliance for advanced USD patterns but
do not impact the majority of real-world USD scenes.

---

## Known Limitations and Gaps

| # | Limitation | Severity | Impact |
|---|-----------|----------|--------|
| 1 | Relocates applied as post-pass | Low | Post-composition rename (architecturally correct for flattening model) |
| 2 | `order` ListEditQual for references/payloads | Low | Warns and skips (implemented for inherits/specializes) |

---

## Future Work

Remaining items (all Low severity):

1. **Consider relocate positioning** — Evaluate whether applying relocates
   between V and R (matching OpenUSD) improves compatibility. In our flattening
   model, the post-pass approach is architecturally correct since relocates
   rename prims that must already exist.

2. **`order` ListEditQual for references/payloads** — Implemented for
   inherits/specializes via `ResolveListOpsT`. References/payloads warn+skip
   because they need to preserve the prepend/append merge semantic distinction.
   This qualifier is deprecated in USD and extremely rare in practice.

---

## References

- AOUSD Core Spec v1.0.1, Sections 10 (Composition), 12 (Value Resolution)
- OpenUSD source:
  - `pxr/usd/pcp/primIndex.cpp` — core composition algorithm
  - `pxr/usd/pcp/strengthOrdering.cpp` — node strength comparison
  - `pxr/usd/pcp/types.h` — arc type enum and strength ordering
  - `pxr/usd/pcp/composeSite.cpp` — single-site field composition
- tinyusdz source:
  - `src/composition.hh` — public composition API
  - `src/composition.cc` — composition implementation
  - `src/composition-reconstruct.cc` — PrimSpec-to-Prim conversion
  - `src/namespace-mapping.hh` — namespace mapping utilities
  - `src/core/composition-types.hh` — Reference, Payload, ArcOrigin, LayerOffset types
