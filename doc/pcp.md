# tinyusdz DAG Composition Engine (PrimIndex / CompositionGraph) API

This is the detailed API reference for tinyusdz's **DAG-based** composition
engine — the OpenUSD-PCP-style code path that builds a per-prim composition
graph (`PrimIndex`) preserving opinion provenance, supports lazy payload
loading, and detects instances during composition.

> **Two composition code paths.** tinyusdz has two implementations:
> 1. The **iterative layer-flattening** pipeline (`CompositeAllArcs()` in
>    `src/composition.cc`) — the default, production path. See
>    [composition.md](composition.md) for the conceptual overview, LIVRPS
>    ordering, instancing, and variants.
> 2. The **DAG engine** documented here (`src/composition-graph.{hh,cc}`,
>    namespace `tinyusdz::composition_graph`) — a parallel implementation that
>    mirrors OpenUSD's PCP design. It builds `PrimIndex` graphs, then lowers
>    them back to `PrimSpec`/`Stage` via the same `composition-reconstruct.cc`
>    pipeline.
>
> All names below are verified against `src/composition-graph.hh`.

**Header**: `src/composition-graph.hh`
**Implementation**: `src/composition-graph.cc`
**Namespace**: `tinyusdz::composition_graph` (aliased at `tinyusdz::CompositionGraph`)

---

## Overview

The engine implements AOUSD Core Spec section 10 (Composition):

- Full **LIVRPS** ordering via a priority-ordered task queue
- Globally-weak specializes (propagated to root)
- Multi-level implied inherits/specializes
- Instancing-aware composition (`InstanceKey`-based deduplication)
- Incremental payload load/unload

Memory layout is deliberately compact: nodes are 40-byte `CompNode` structs
stored in a contiguous pool and linked by `uint16_t` indices; paths, layer
stacks, and namespace-mapping expressions are interned in shared tables owned
by the `CompositionGraph`.

---

## Arc types and ordering

```cpp
enum class ArcType : uint8_t {
  Root = 0,        // Root node (direct local opinions)
  SubLayer = 1,    // L: sublayer
  Inherit = 2,     // I: inherits (class-based)
  Variant = 3,     // V: variant selection
  Relocate = 4,    // Between V and R (matches PCP enum ordering)
  Reference = 5,   // R: references
  Payload = 6,     // P: payloads
  Specialize = 7,  // S: specializes (weakest, globally weak)
};

bool IsClassBasedArc(ArcType t);   // true for Inherit / Specialize
const char *ArcTypeName(ArcType t);
```

Lower enum value = stronger. `Relocate` sits between `Variant` and `Reference`,
matching OpenUSD's `PcpArcType` ordering. See
[composition.md](composition.md#the-livrps-composition-ordering) for the LIVRPS
semantics.

Tasks are processed in a separate priority order that drives graph construction:

```cpp
enum class TaskType : uint8_t {
  EvalRootNode = 0,
  EvalSubLayers = 1,
  EvalImpliedInherits = 2,
  EvalInherits = 3,
  EvalVariants = 4,
  EvalReferences = 5,
  EvalPayloads = 6,
  EvalImpliedSpecializes = 7,
  EvalSpecializes = 8,
  EvalRelocates = 9,
};
```

---

## CompNode

A single node in a prim's composition DAG (`<= 40` bytes, enforced by
`static_assert`). Children form a singly-linked sibling list in strength order
(strongest first); links are `uint16_t` indices into the node pool.

```cpp
struct CompNode {
  static constexpr uint16_t kInvalidIndex = 0xFFFF;

  uint16_t parent, first_child, next_sibling, origin;  // node indices
  ArcType  arc_type;
  uint8_t  depth;            // depth in the DAG
  NodeFlags flags;
  uint16_t layer_stack_idx;  // index into LayerStackEntry table
  uint16_t layer_idx;        // layer index within the stack
  uint16_t map_expr_idx;     // index into MapExpr pool
  uint32_t site_path_idx;    // index into interned path table
  int32_t  strength_order;   // pre-computed (lower = stronger)
  uint16_t sibling_num;      // order among same-type siblings at origin

  // queries: has_parent(), has_children(), has_next_sibling(),
  //          has_specs(), is_inert(), is_culled(),
  //          is_payload_deferred(), is_payload_loaded(), is_implied_arc()
};
```

### NodeFlags

```cpp
enum class NodeFlags : uint16_t {
  None             = 0,
  HasSpecs         = 1 << 0,  // authored specs at this path
  Inert            = 1 << 1,  // cannot contribute opinions
  Culled           = 1 << 2,  // pruned during culling
  PermissionDenied = 1 << 3,
  PayloadLoaded    = 1 << 4,
  PayloadDeferred  = 1 << 5,  // payload exists but not yet loaded
  IsImpliedArc     = 1 << 6,  // propagated implied inherit/specialize
  IsDueToAncestor  = 1 << 7,
};
// operators | & ~ and HasFlag(flags, flag) are provided.
```

---

## PrimIndex

The composition graph for a single prim: a DAG of `CompNode` in a contiguous
vector (node 0 is always the root), plus a pre-computed strength order for fast
value resolution.

```cpp
class PrimIndex {
 public:
  const Path &GetPath() const;
  const CompNode &GetRootNode() const;             // node 0
  const CompNode &GetNode(uint16_t idx) const;
  uint16_t GetNodeCount() const;
  const std::vector<uint16_t> &GetStrengthOrder() const;  // strongest first
  bool HasAnySpecs() const;
  bool IsPayloadLoaded() const;                    // true if no deferred payloads
  bool IsInstanceable() const;                     // instanceable=true metadata
  std::string DumpToString() const;
};
```

`PrimIndex` borrows the shared path/layer-stack/map-expression tables from its
owning `CompositionGraph`, so it must not outlive that graph.

---

## Shared tables

Owned by `CompositionGraph`, referenced by every `PrimIndex`:

```cpp
struct LayerStackEntry {
  const Layer *layer;       // borrowed (owned by CompositionGraph)
  std::string identifier;   // asset path or layer name
  LayerOffset offset;       // cumulative time offset to root
};

// Namespace mapping with lazy composition up a parent chain.
struct MapExpr {
  NamespaceMapping mapping;  // direct mapping for this arc
  int32_t parent_expr;       // index of parent expr (-1 = identity)
  const NamespaceMapping &GetComposed(const std::vector<MapExpr> &pool) const;
  Path Apply(const Path &path, const std::vector<MapExpr> &pool) const;
};

// Provenance of a resolved opinion.
struct OpinionSource {
  uint16_t node_idx, layer_stack_idx, layer_idx;
  std::string site_path;
};
```

`MapExpr` is the engine's equivalent of OpenUSD's `PcpMapExpression`: composed
mappings are computed lazily and cached. The underlying prefix-remapping type is
`NamespaceMapping` from `src/namespace-mapping.hh` (see
[composition.md](composition.md#namespace-mapping)).

---

## InstanceKey

A 128-bit structural signature of a prim's composition graph. Two prims with
equal keys have structurally identical arcs and produce identical child
namespaces, so they can share a prototype (AOUSD Core Spec 11.3.3).

```cpp
struct InstanceKey {
  uint64_t hash_lo{0}, hash_hi{0};
  bool operator==(const InstanceKey &) const;
  bool operator!=(const InstanceKey &) const;
  bool is_valid() const;   // non-zero
};

struct InstanceKeyHasher { size_t operator()(const InstanceKey &) const; };

// Returns an invalid key if the prim is not instanceable.
InstanceKey ComputeInstanceKey(const PrimIndex &index,
                               const std::vector<LayerStackEntry> &stacks,
                               const std::vector<std::string> &path_table);
```

> The DAG engine's `InstanceKey` is distinct from the `Stage`/`PrimSpec`-level
> `tinyusdz::InstanceKey` in `src/core/instance-key.hh` used by
> `Stage::BuildInstancePrototypes()`. Both are 128-bit hashes of the same
> structural inputs (type name + composition arcs + variant selections); the
> core one uses SpookyHash. See [composition.md](composition.md#instancing).

---

## DeferredPayloadInfo

Context retained for a payload skipped during initial composition so it can be
loaded later:

```cpp
struct DeferredPayloadInfo {
  Path prim_path;       // prim that owns the payload arc
  Payload payload;      // the payload descriptor
  uint16_t node_idx;    // node index in the PrimIndex
  std::string current_working_path;            // resolver context at discovery
  std::vector<std::string> asset_search_paths;
};
```

---

## CompositionGraphOptions

```cpp
struct CompositionGraphOptions {
  // Return true to load a payload, false to defer. nullptr => load eagerly.
  std::function<bool(const Path &prim_path, const Payload &payload)> payload_policy;

  uint32_t max_depth{256};        // recursion limit
  bool detect_instances{true};    // instancing detection during composition
  std::unordered_map<std::string, FileFormatHandler> fileformats;
  size_t max_memory_mb{16384};
  bool error_when_asset_not_found{false};
  bool error_when_unsupported_fileformat{false};
};
```

---

## CompositionGraph

The top-level engine. Move-only (it owns the layers loaded during composition).

```cpp
class CompositionGraph {
 public:
  // Compose a scene from a root layer (sublayers should already be composed).
  static nonstd::expected<CompositionGraph, std::string> Compose(
      AssetResolutionResolver &resolver, const Layer &root_layer,
      const CompositionGraphOptions &options = {});

  // -- Query --
  const PrimIndex *GetPrimIndex(const Path &prim_path) const;   // nullptr if absent
  std::vector<Path> GetAllPrimPaths() const;
  size_t GetPrototypeCount() const;
  std::vector<Path> GetInstancesForPrototype(size_t prototype_idx) const;
  const PrimIndex *GetPrototypePrimIndex(size_t prototype_idx) const;

  // -- Lazy payloads --
  nonstd::expected<bool, std::string> LoadPayload(
      const Path &prim_path, AssetResolutionResolver &resolver);
  nonstd::expected<bool, std::string> UnloadPayload(const Path &prim_path);
  std::vector<Path> GetDeferredPayloadPaths() const;
  bool HasDeferredPayload(const Path &prim_path) const;

  // -- Stage construction --
  // Lowers the DAG back to a Stage via composition-reconstruct.cc; result is
  // structurally identical to the CompositeAllArcs + LayerToStage pipeline.
  bool BuildStage(Stage *stage, std::string *warn, std::string *err) const;

  // -- Diagnostics --
  size_t EstimateMemoryUsage() const;
  std::string DumpToString() const;
};
```

`Compose()` expects the **L** phase (sublayers) to already be flattened into
`root_layer`; use `CompositeSublayers()` from `src/composition.hh` first.

---

## PrimIndexBuilder (internal)

`CompositionGraph::Compose()` builds each `PrimIndex` with an internal
`PrimIndexBuilder` driven by a `std::priority_queue<CompositionTask>`:

```cpp
struct CompositionTask {
  TaskType type{TaskType::EvalRootNode};
  uint16_t node_idx;     // node that spawned this task
  uint8_t  list_op_idx;  // ListOp index
  uint8_t  item_idx;     // item within ListOp
  bool operator<(const CompositionTask &) const;  // lower TaskType = higher prio
};
```

The builder scans a `PrimSpec`'s metadata for arcs (`ScanArcsAndEnqueueTasks`),
enqueues `Eval*` tasks, and processes them in `TaskType` order. It tracks two
cycle-detection sets — `(layer_id, prim_path)` pairs for references/payloads and
per-path sets for inherits/specializes — collects variant opinions across
phases, then computes the strength order and culls inert nodes. This is the
DAG-engine analogue of the phase-by-phase flow in `CompositeAllArcs()`.

---

## Usage

```cpp
#include "composition-graph.hh"
#include "composition.hh"   // for CompositeSublayers

using namespace tinyusdz;

// 1) Flatten sublayers (L) into the root layer first.
Layer root = /* loaded + CompositeSublayers(...) */;

// 2) Build the DAG.
CompositionGraphOptions opts;
opts.detect_instances = true;
auto result = CompositionGraph::Compose(resolver, root, opts);
if (!result) { /* result.error() */ }

CompositionGraph graph = std::move(*result);

// 3) Inspect a prim's composition graph.
if (const PrimIndex *idx = graph.GetPrimIndex(Path("/Model/Geom"))) {
  for (uint16_t n : idx->GetStrengthOrder()) {
    const composition_graph::CompNode &node = idx->GetNode(n);
    // node.arc_type, node.has_specs(), ...
  }
}

// 4) Lower to a Stage (reuses composition-reconstruct.cc).
Stage stage;
std::string warn, err;
graph.BuildStage(&stage, &warn, &err);
```

Lazy payloads:

```cpp
for (const Path &p : graph.GetDeferredPayloadPaths()) {
  graph.LoadPayload(p, resolver);   // recomposes the affected prim
}
```

---

## Status

The DAG engine is a parallel implementation alongside the default
layer-flattening pipeline (see the header comment in `composition-graph.hh`).
`BuildStage()` is designed to produce a `Stage` structurally identical to the
`CompositeAllArcs()` + `LayerToStage()` path. For the conceptual model, LIVRPS
correctness analysis, instancing, and variants, see
[composition.md](composition.md).

---

## References

- `src/composition-graph.hh` / `.cc` — this engine
- `src/composition.hh` / `.cc` — default flattening pipeline
- `src/composition-reconstruct.cc` — PrimSpec → Prim/Stage lowering
- `src/core/instance-key.hh` — `Stage`-level `InstanceKey` (SpookyHash)
- `src/namespace-mapping.hh` — `NamespaceMapping` prefix remapping
- AOUSD Core Spec v1.0.1, Section 10 (Composition), Section 11.3.3 (Instancing)
