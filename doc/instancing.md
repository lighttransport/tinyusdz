# USD Scene Graph Instancing

**Spec reference**: AOUSD Core Specification v1.0.1 (b7bc21a), 2025-12-12
**Date**: 2026-03-24

---

## 1. Overview

Instancing is a USD optimization that allows multiple prims sharing the same composition structure to reuse a single "prototype" representation. Instead of composing and storing each identical subtree independently, the stage composes it once and shares the result across all instances.

**Use case**: A scene with 1000 identical trees referenced from the same asset. Without instancing each tree is a full copy in memory. With instancing all 1000 share one prototype, reducing memory and composition time proportionally.

**Core concepts**:

| Term | Meaning |
|------|---------|
| **Instance prim** | A prim with `instanceable = true` and at least one composition arc |
| **Prototype** | The shared composed representation that all matching instances point to |
| **Instance key** | A structural signature (composition arcs + variant selections) that determines which instances share the same prototype |
| **Instance proxy** | (OpenUSD concept) A "virtual" prim at an instance path that transparently redirects to the prototype |

---

## 2. AOUSD Core Spec Requirements

### 2.1 The `instanceable` Metadata Field (Spec 7.6.2.4.3)

```
instanceable: bool
Fallback value: false
```

> "If true, composition and population behaviors at this location may be shared and ignore descendant opinions."

- This is a **population field** on prim specs -- it has no semantic meaning at the layer level
- The field must not affect a layer's available specs
- Resolution uses standard strongest-opinion rules

### 2.2 Instancing Conditions (Spec 11.3.3)

A prim becomes an **instance** when both conditions are met:

1. The **strongest opinion** of `instanceable` is `true`
2. The prim has **at least one composition arc** -- i.e., at least one value in: `references`, `payload`, `inheritPaths`, `variantSets`, or `specializes`

A prim with `instanceable = true` but no composition arcs is **not** an instance.

### 2.3 Scene Graph Instancing Rules (Spec 11.3.3)

When a prim is an instance:

1. **Single composition**: An optimization should be made such that all instances of the same prim are composed only once
2. **No overrides**: Overrides may not be specified on an instance prim
3. **Local opinions discarded**: Local opinions are discarded, which also includes local opinions contributing to `primChildren`. This ensures that only the opinions brought in by the composition arc are used to determine the set of children, which are then shared across all instances of the same prim
4. **Implementation freedom**: It is up to the implementation how to model the shared representation of instanced prims sharing the same arc

### 2.4 Stage Population Algorithm (Spec 11.3)

The spec provides pseudocode for how instancing integrates into stage population:

```python
populate_stage(root_layer, population_mask) -> stage:
  queue = []
  layer = create_scene_object('/')
  queue.enqueue(layer)

  while !queue.empty():
    scene_object = queue.dequeue()

    if is_absolute_root_path(scene_object.path) or
       scene_object.value_resolve('active'):

      scene_object.opinions = compose(root_layer, scene_object.path)

      # Instancing check
      if !is_absolute_root_path(scene_object.path) and
         scene_object.value_resolve('instanceable') and
         has_composition_arc(scene_object):

        # Instanceable -- compose once, reuse
        if not has_shared_representation(scene_object):
          create_shared_representation(scene_object)
          scene_object.children = populate_children(
              scene_object, population_mask, true)  # is_instanceable=true
        else:
          continue  # Already processed
      else:
        scene_object.children = populate_children(
            scene_object, population_mask, false)

      for child in scene_object.children:
        queue.enqueue(child)

  return create_stage(layer)
```

When `is_instanceable=true`, `populate_children` filters out local opinions before determining the child set:

```python
populate_children(scene_object, population_mask, is_instanceable):
  children = list_ordered_prim_children(scene_object)
  if is_instanceable:
    children = filter_out_local_opinions(scene_object, children)
  # ... (population mask filtering, child creation)
```

### 2.5 Stage Queries (Spec 11.5)

The spec defines an **Instance** predicate for stage queries:

> "**Instance** -- Is the prim marked as instanceable."

This allows querying which prims on a stage are instances.

### 2.6 Flatten and Squash (Spec 5.1.9, 5.1.34)

- **Flatten**: May preserve or introduce internal composition arcs to preserve instancing behaviors
- **Squash**: Removes all composition operators -- instancing and variant sets are dependent on the presence of arcs, so squashing may change behavior

---

## 3. OpenUSD Implementation Architecture

OpenUSD implements instancing as a two-layer system: **PCP** (Prim Cache Population) handles composition-level detection, and **USD** manages the stage-level cache.

### 3.1 Key Source Files

#### PCP Layer (`pxr/usd/pcp/`)

| File | Purpose |
|------|---------|
| `instanceKey.h/cpp` | `PcpInstanceKey` -- composition-level instance identity |
| `instancing.h` | Helper utilities: `Pcp_PrimIndexIsInstanceable()`, traversal functions |

#### USD Layer (`pxr/usd/usd/`)

| File | Purpose |
|------|---------|
| `instanceKey.h/cpp` | `Usd_InstanceKey` -- stage-level key extending PCP key |
| `instanceCache.h/cpp` | `Usd_InstanceCache` -- maps keys to prototypes |
| `primData.h` | `IsInstance()`, `IsPrototype()`, `IsInPrototype()`, `GetPrototype()` |
| `prim.h` | Public API: `IsInstanceable()`, `IsInstance()`, `IsInstanceProxy()`, `GetPrototype()`, `GetInstances()` |

### 3.2 Instance Key

The instance key determines which instances can share a prototype. Two prims share a prototype if and only if their instance keys match.

**PcpInstanceKey** (composition level):
- Composition arcs contributing to the prim (arc type, source site, time offset)
- Variant selections
- Computed by traversing instanceable nodes strong-to-weak via `Pcp_TraverseInstanceableStrongToWeak()`

**Usd_InstanceKey** (stage level) extends PcpInstanceKey with:
- Value clip definitions (`_clipDefs`)
- Population mask (`_mask`)
- Load rules (`_loadRules`)

Both use hash-based equality for efficient lookup.

### 3.3 Instance Cache

`Usd_InstanceCache` is the central registry managing all instancing on a stage:

```
Registration:
  RegisterInstancePrimIndex(primIndex)
    -> creates Usd_InstanceKey from prim index
    -> checks if prototype with same key exists
    -> defers processing

ProcessChanges():
  Phase 1: Remove unregistered prim indexes
  Phase 2: Add newly-registered prim indexes
           Create new prototypes or assign to existing ones
  Cleanup: Remove prototypes with no instances
```

Key mappings maintained:
- `instanceKey -> prototypePath` -- which prototype serves a given key
- `prototype -> [primIndexPaths]` -- all instances of a prototype
- `primIndexPath -> prototype` -- reverse lookup

### 3.4 Prototype Naming

Prototypes are created as root-level prims with auto-incrementing names:

```
/__Prototype_1
/__Prototype_2
...
```

- Detection: path is a root prim path AND name starts with `__Prototype_`
- Ordering can be made deterministic via `USD_ASSIGN_PROTOTYPES_DETERMINISTICALLY` env var

### 3.5 Instance Proxies

When traversing an instance prim's children (e.g., `/World/Tree_1/Trunk`), OpenUSD returns **instance proxies** -- virtual prims that appear at the instance path but redirect data access to the corresponding prim in the prototype.

```
Stage tree:                    Prototype:
/World                         /__Prototype_1
  /Tree_1 (instance) -------->   /Trunk
  /Tree_2 (instance) -------->   /Branches
                                 /Leaves

Accessing /World/Tree_1/Trunk returns an instance proxy
that reads data from /__Prototype_1/Trunk
```

Properties:
- `IsInstance()` -- true for the instance root prim
- `IsInstanceProxy()` -- true for descendants of an instance prim
- `IsPrototype()` -- true for `/__Prototype_N` root prims
- `IsInPrototype()` -- true for any prim inside a prototype subtree

### 3.6 Edit Restrictions

Instance proxies are **read-only**. Attempting to:
- Define, override, or create class on an instance proxy
- Set metadata or properties on an instance proxy

...raises an error. Edits must go to the instance's source layers (which propagate to the prototype) or directly to the prototype.

### 3.7 Nested Instancing

Prims within prototypes can themselves be instances, creating a nested prototype hierarchy:

```
/__Prototype_1        (prototype for a "set" with repeated props)
  /Prop_A (instance)  -> /__Prototype_2
  /Prop_B (instance)  -> /__Prototype_2
/__Prototype_2        (prototype for individual prop)
  /Mesh
  /Material
```

The instance cache handles path translation through multiple nesting levels.

---

## 4. USDA Syntax Examples

### 4.1 Basic Instancing with References

```usda
#usda 1.0

# A reusable tree asset defined elsewhere (tree.usda)
# def Xform "Tree" { def Mesh "Trunk" { ... } def Mesh "Leaves" { ... } }

def Xform "World"
{
    def Xform "Tree_1" (
        instanceable = true
        references = @./tree.usda@
    )
    {
        # Local opinions here are DISCARDED because this is an instance
    }

    def Xform "Tree_2" (
        instanceable = true
        references = @./tree.usda@
    )
    {
    }

    def Xform "Tree_3" (
        instanceable = true
        references = @./tree.usda@
    )
    {
    }
}
```

All three trees share the same prototype because they have identical composition arcs (same reference).

### 4.2 Instancing with Payloads

```usda
#usda 1.0
(
    defaultPrim = "World"
)

def Xform "World"
{
    def Xform "Building_1" (
        instanceable = true
        payload = @./building.usdc@
    )
    {
    }

    def Xform "Building_2" (
        instanceable = true
        payload = @./building.usdc@
    )
    {
    }
}
```

### 4.3 Different Variants Break Sharing

```usda
#usda 1.0

def Xform "World"
{
    # These two share a prototype (same reference, same variant)
    def Xform "Chair_1" (
        instanceable = true
        references = @./chair.usda@
        variants = { string style = "modern" }
    )
    {
    }

    def Xform "Chair_2" (
        instanceable = true
        references = @./chair.usda@
        variants = { string style = "modern" }
    )
    {
    }

    # This one gets a DIFFERENT prototype (different variant selection)
    def Xform "Chair_3" (
        instanceable = true
        references = @./chair.usda@
        variants = { string style = "classic" }
    )
    {
    }
}
```

### 4.4 Not an Instance (No Composition Arc)

```usda
#usda 1.0

def Xform "World"
{
    # instanceable=true but NO composition arc -> NOT an instance
    def Mesh "DirectMesh" (
        instanceable = true
    )
    {
        float3[] points = [(0,0,0), (1,0,0), (0,1,0)]
    }
}
```

---

## 5. TinyUSDZ Implementation Status

### 5.1 Core Layer

#### Metadata Support
- **`instanceable` field**: Fully implemented in `MetadataBase` (`src/core/metadata-base.hh`)
  - `has_instanceable()`, `get_instanceable()`, `set_instanceable()`, `remove_instanceable()`
- **Prim-level queries** (`src/core/prim.hh`):
  - `Prim::IsInstance()` -- checks `instanceable=true` metadata
  - `Prim::HasCompositionArcs()` -- checks for references, payload, inherits, specializes, or variantSets
- **USDA parsing/printing**: `instanceable` round-trips through usda read/write

#### Instance Key Computation

Three computation functions exist:

1. **PrimSpec-based** (`src/core/instance-key.cc`):
   - `ComputeInstanceKeyFromPrimSpec(const PrimSpec &ps, InstanceKey *key)`
2. **PrimMetas-based** (`src/core/instance-key.cc`):
   - `ComputeInstanceKeyFromPrimMetas(const PrimMeta &metas, const std::string &type_name, InstanceKey *key)`
   - Used by `Stage::BuildInstancePrototypes()`
3. **DAG-based** (`src/composition-graph.cc`):
   - `ComputeInstanceKey(const PrimIndex &index, ...)`
   - Used by the CompositionGraph engine

All hash: type name, references, payloads, inherits, specializes, variant selections using SpookyHash (128-bit).

#### Stage-Level API (`src/stage.hh`)

```cpp
class Stage {
  size_t BuildInstancePrototypes();           // InstanceKey-based grouping
  int GetPrototypeIndex(const Path &path);    // -1 if not instance
  int GetPrototypeIndexForInstance(const Path &path);  // alias
  bool IsInstancePrim(const Path &path);
  std::vector<Path> GetInstancesForPrototype(int prototype_index);
  std::string GetPrototypeSourcePath(int prototype_index);
  size_t num_prototypes() const;
  void ImportInstanceData(...);               // from CompositionGraph
};
```

`BuildInstancePrototypes()` groups instances by InstanceKey (128-bit hash of composition arc signature). Per AOUSD Spec 11.3.3, only prims with `instanceable=true` AND at least one composition arc are registered.

### 5.2 Tydra Layer

#### RenderInstance struct (`src/tydra/render-data.hh`)

```cpp
struct RenderInstance {
  std::string prim_name, abs_path, display_name;
  int32_t prototype_index{-1};  // prototype group
  int32_t mesh_id{-1};          // index to RenderScene::meshes (shared)
  int32_t material_id{-1};
  value::matrix4d local_matrix, global_matrix;
  bool visible{true};
};
```

#### Node instance fields

```cpp
struct Node {
  // ... existing fields ...
  bool is_instance{false};
  int32_t prototype_index{-1};
  int32_t instance_id{-1};
};
```

#### RenderScene

`RenderScene::instances` vector populated during `ConvertToRenderScene()`. Instance detection happens after mesh conversion: the Stage's prototype registry is queried and `RenderInstance` entries are created for each instance prim, with `mesh_id` pointing to shared mesh data.

### 5.3 WASM/JS Binding (`web/binding.cc`)

- `numInstances()` -- count of RenderInstances
- `getInstance(id)` -- returns JS object with primName, absPath, prototypeIndex, meshId, transforms, etc.
- `getInstancesForMesh(mesh_id)` -- array of instance IDs sharing a mesh
- `buildNodeRec()` exposes `isInstance`, `prototypeIndex`, `instanceId` on every node

### 5.4 Remaining Gaps

| Gap | Description | Spec Reference |
|-----|-------------|----------------|
| **Prototype sharing** | Each instance has its own full Prim subtree copy; no memory deduplication | 11.3.3 |
| **Local opinion filtering** | Instance prims don't filter out local opinions during population | 11.3.3 |
| **Instance proxies** | No concept of instance proxy prims (virtual prims redirecting to prototype) | OpenUSD extension |
| **Nested instancing** | No support for instances within prototypes | OpenUSD extension |
| **Edit restrictions** | No enforcement that instance prims cannot be overridden | 11.3.3 |
| **THREE.js InstancedMesh** | JS loader uses shared geometry/material but not THREE.InstancedMesh | N/A |

### 5.5 Key Source Files

| File | Role |
|------|------|
| `src/core/metadata-base.hh` | `instanceable` field storage |
| `src/core/prim.hh` | `Prim::IsInstance()`, `Prim::HasCompositionArcs()` |
| `src/core/instance-key.hh` | `InstanceKey` struct |
| `src/core/instance-key.cc` | PrimSpec/PrimMetas-based key computation |
| `src/composition-graph.hh` | DAG-based instance detection, registry |
| `src/composition-graph.cc` | `ComputeInstanceKey()` from DAG |
| `src/stage.hh` | `Stage::BuildInstancePrototypes()`, query APIs |
| `src/stage.cc` | Stage-level InstanceKey-based prototype registry |
| `src/tydra/render-data.hh` | `RenderInstance` struct, `Node` instance flags, `RenderScene::instances` |
| `src/tydra/render-data.cc` | Instance-aware node building and RenderInstance population |
| `src/tydra/render-data-converter.hh` | `RenderSceneConverter::instances` vector |
| `web/binding.cc` | WASM: `numInstances()`, `getInstance()`, `getInstancesForMesh()`, node flags |

### 5.6 Architecture Comparison

```
OpenUSD                          TinyUSDZ
--------                         --------
PcpInstanceKey                   InstanceKey (in instance-key.hh)
  arcs + variants                  hash_lo + hash_hi (128-bit SpookyHash)
  |                                |
Usd_InstanceKey                  (no equivalent -- no clips/mask extension)
  + clips, mask, load rules
  |                                |
Usd_InstanceCache                Stage::BuildInstancePrototypes()
  key -> /__Prototype_N            InstanceKey -> prototype_index
  manages lifecycle                ImportInstanceData() from CompositionGraph
  |                                |
Instance proxies                 Node.is_instance / RenderInstance
  transparent child access         query-based (not transparent proxies)
  read-only enforcement
  |                                |
Nested instancing                (not implemented)
  multi-level path translation
```

---

## 6. Implementation Roadmap (Remaining Work)

### Phase 1: Prototype Sharing
- Create internal prototype prims (e.g., `/__Prototype_N` naming convention)
- Share prim subtrees via pointer/index indirection instead of copying
- Map instance paths to prototype paths for property access

### Phase 2: Local Opinion Filtering
- Filter out local opinions on instanceable prims during stage population (Spec 11.3.3)
- Ensure `primChildren` comes from composition arcs only

### Phase 3: THREE.js InstancedMesh
- Post-traversal assembly of `THREE.InstancedMesh` for prototypes with many instances
- Collect instance transforms per prototype, create InstancedMesh with count

---

## References

- AOUSD Core Specification v1.0.1, Sections 7.6.2.4.3, 11.3, 11.3.3, 11.5
- OpenUSD source: `pxr/usd/pcp/instancing.h`, `pxr/usd/usd/instanceCache.h`
- OpenUSD glossary: https://openusd.org/release/glossary.html
