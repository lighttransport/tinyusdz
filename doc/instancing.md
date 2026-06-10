# Instancing in TinyUSDZ

This document covers (1) how OpenUSD models instancing, (2) the instancing
structure of the Moana Island production scene, and (3) the state of instancing
in TinyUSDZ plus a plan for scaling to island-sized scenes.

OpenUSD distinguishes **two independent instancing mechanisms**:

| | Native / Scenegraph instancing | PointInstancer |
|---|---|---|
| Schema | `instanceable = true` prim metadata + composition arcs | `UsdGeomPointInstancer` (vectorized attrs) |
| Intent | Reuse an authored asset hierarchy many times | Sparse/large point-based instancing (crowds, particles, scatter) |
| Instance count | hundreds – thousands | thousands – billions |
| Per-instance data | properties on the instance prim only | full SRT arrays + velocities, ids, masking |
| Prototype | dynamically generated `/__Prototype_N` | prims named by the `prototypes` relationship |
| Memory | ~1 prim per instance + one shared prototype | ~tens of bytes per instance, no scenegraph bloat |

They are not mutually exclusive — a stage commonly uses scenegraph instancing
for hero assets and PointInstancer for scatter/crowds.

---

## 1. OpenUSD instancing model

### 1.1 Native / scenegraph instancing

Reference: `OpenUSD/pxr/usd/usd/docs/instancing.md`, `instanceKey.{h,cpp}`,
`instanceCache.{h,cpp}`, `prim.h` (lines ~2020–2123), `stage.h` (line ~1574).

A prim becomes an **instance** when it carries `instanceable = true` *and* has at
least one composition arc (reference, payload, inherit, specialize, or
variantSet):

```usda
def "Car_1" (
    instanceable = true
    references = @./Car.usd@</Car>
) { }
```

OpenUSD computes a `Usd_InstanceKey` for each instanceable prim from its
composition structure — arcs (in strength order), variant selections, value
clips, load rules, and population mask. **All instances with the same key share a
single prototype.** The key guarantees structural equivalence, so the shared
prototype is safe.

Prototypes are **synthesized dynamically** by `Usd_InstanceCache` and named
`/__Prototype_1`, `/__Prototype_2`, … (numbering is non-deterministic across runs
unless `USD_ASSIGN_PROTOTYPES_DETERMINISTICALLY=true`). One instance per key is
chosen as the prototype's composition source.

Traversal rules:
- An instance prim has **no children** in normal traversal; its subtree lives in
  the prototype.
- Prototypes are roots (`/__Prototype_N`) and are not returned by ordinary
  `Traverse()`/`GetChildren()`. Use `UsdStage::GetPrototypes()`.
- `GetPrimAtPath("/ParkingLot/Car_1/Body")` returns a read-only **instance
  proxy**; `Usd.TraverseInstanceProxies()` makes prototype descendants visible
  during traversal.

Query API: `UsdPrim::IsInstanceable / IsInstance / IsInstanceProxy / IsPrototype
/ IsInPrototype / GetPrototype / GetInstances`.

Editing restrictions: you may author properties/metadata on the **instance prim
itself**, but not override prototype descendants per-instance. To customize one
instance you must disable instancing on it (`SetInstanceable(false)`), which
breaks sharing. Nested instancing (an instanceable prim inside a prototype) is
supported.

### 1.2 PointInstancer

Reference: `OpenUSD/pxr/usd/usdGeom/pointInstancer.{h,cpp}`, `schema.usda`.

`UsdGeomPointInstancer` stores instancing as parallel arrays:

| Attribute | Type | Role |
|---|---|---|
| `prototypes` | `rel` | Ordered prototype roots; `protoIndices` indexes into this |
| `protoIndices` | `int[]` | **Required.** Prototype selector per instance; the instance-count authority |
| `positions` | `point3f[]` | Per-instance translation (instancer space) |
| `orientations` | `quath[]` (or `orientationsf : quatf[]`) | Per-instance rotation |
| `scales` | `float3[]` | Per-instance scale |
| `velocities` / `accelerations` | `vector3f[]` | Linear motion for sub-frame / motion-blur |
| `angularVelocities` | `vector3f[]` | Rotational motion (deg/sec) |
| `ids` | `int64[]` | Stable per-instance identity across time |
| `invisibleIds` | `int64[]` (animatable) | Per-time visibility masking |
| `inactiveIds` | `int64[]` (uniform metadata) | Time-invariant masking |

**Instance transform** (`ComputeInstanceTransformsAtTime`,
`pointInstancer.cpp` ~989–1044). For each instance `i`, in row-vector order:

```
instanceXform = scale(scales[i]) · rotate(orientations[i]) · translate(positions[i])
finalXform    = prototypeXform · instanceXform        (when IncludeProtoXform)
```

With velocities authored, `positions += velocities · dt` and
`orientations` advance by `angularVelocities · dt`
(`dt = (time - baseTime) / timeCodesPerSecond`); with `accelerations`,
`velocities += 0.5 · accelerations · dt`. Without them, OpenUSD linearly
interpolates positions/scales and **slerps** orientations between samples.

**Masking** (`ComputeMaskAtTime`): an instance is culled if its id (from `ids`,
else its index) is in `invisibleIds` ∪ `inactiveIds`. An empty mask means "all
visible".

Best practice: place prototypes under an inactive/over/class scope so they are
not drawn as ordinary geometry; they are referenced solely through the
`prototypes` relationship.

---

## 2. Moana Island scene instancing structure

Data set: `/mnt/disk1/data/island` (Walt Disney Animation Studios). ~17 GB total
(`usd/` 3.1 GB, `textures/` 14 GB).

**Root** `usd/island.usda` (~22 KB) is a single `Xform "island"` (kind
`assembly`) with 20 element children, each pulled via `prepend references`:

```usda
def Xform "island" (kind = "assembly") {
    def Xform "isCoral" (
        kind = "group"
        prepend references = @./elements/isCoral/element.usda@</isCoral>
    ) { }
    # ... 19 more elements, plus cameras and ~30 lights ...
}
```

**Instancing is scenegraph-based, not PointInstancer** (at least in the text
layer):

| Construct | Count |
|---|---|
| `instanceable = true` | 60 |
| `PointInstancer` | 0 (top level) |
| `references` (all `prepend`) | 3,924 |
| `payload` | 214 |
| component instances | 101 |
| unique prototypes | 60 |

Each element uses a three-file pattern with embedded instancing and lazy loading:

```
element.usda          # multiple "component" prims; one marked instanceable=true
  -> instance.usda     # wraps references [materials.usda, geometry.usda]
       -> geometry.usda # Xforms with `prepend payload` to model.usd + xgenInstances/*.usd
```

Instances differ only by `matrix4d xformOp:transform` and a variant selection
(`over "geometry" (variants = {string model = "isCoral1"})`). Counts per element
range from 1 (e.g. isBeach, isMountainA) to 33 (isPalmRig).

**XGen procedural instancing** carries the bulk of the geometry but is **baked
inside binary `.usd`/`.usdc` files** (e.g. `xgGroundCover.usd` is 652 MB), not
visible in the USDA text layer. XGen wrappers expose only masking, e.g.:

```usda
def "bundle" (prepend references = @./xgHibiscus.usd@</bundle>) {
    over "instancer" { int64[] invisibleIds = [184, 147] }
}
```

**Takeaways for an efficient importer:**
1. Heavy use of `payload` for deferred loading is essential to memory.
2. Scenegraph instancing (shared prototype + per-instance matrix) dominates the
   text layer; PointInstancer-style data lives in binary archives.
3. The text layer is small (184 USDA files); the cost is in 752 binary files.

---

## 3. TinyUSDZ instancing support

### 3.1 What already existed (parsing/registry)

- `GeomPointInstancer` is fully parsed from USDA/USDC, with all attributes —
  `src/usdGeom.hh`, reconstruction in `src/prim-reconstruct-geom3.cc`, validation
  in `src/usd-validation.cc`.
- Scenegraph instancing: `instanceable` metadata (`src/core/metadata-base.hh`),
  128-bit `InstanceKey` dedup (`src/core/instance-key.hh`), and a Stage prototype
  registry (`src/stage.hh`): `BuildInstancePrototypes()`, `GetPrototypeIndex()`,
  `GetInstancesForPrototype()`, `num_prototypes()`, etc. (AOUSD Spec 11.3.3).
- Tydra render scene already had instance containers: `Node::is_instance /
  prototype_index / instance_id` and `RenderInstance` + `RenderScene::instances`
  (`src/tydra/render-data.hh`).

### 3.2 What this change adds (preliminary compute + expansion)

**Compute API** on `GeomPointInstancer` (`src/usdGeom.hh`, implemented in
`src/usdGeom-accessors.cc`):

```cpp
bool ComputeInstanceTransformsAtTime(
    const GeomPointInstancer &pi, double time,
    value::TimeSampleInterpolationType interp,
    std::vector<value::matrix4d> *out_xforms, std::string *err,
    const std::vector<value::matrix4d> *proto_xforms = nullptr);

bool ComputeMaskAtTime(const GeomPointInstancer &pi, double time,
                       std::vector<bool> *out_mask, std::string *err);
```

Math (row-vector convention, matching TinyUSDZ `value::Mult`):
`localXform[i] = Mult(Mult(S, R), T)`, with `S` from `scales`, `R =
to_matrix(orientations[i])`, `T` from `positions`; optional `proto_xforms[i]` is
pre-multiplied. `protoIndices.size()` is the instance count; missing optional
arrays default to identity. Convenience getters were also added
(`get_protoIndices`, `get_positions`, `get_scales`, `get_orientations`,
`get_ids`, `get_invisibleIds`, `get_inactiveIds`).

**Tydra expansion** (`src/tydra/render-data-instancer.cc`,
`RenderSceneConverter::ExpandPointInstancer`, gated by
`RenderSceneConverterConfig::expand_point_instancers`, default on). For each
PointInstancer it emits one `RenderInstance` per *visible* instance × prototype
mesh, sharing geometry via `mesh_id` (no mesh duplication). Instance world
matrix = `Mult(local, instancer_world)`; the prototype-relative mesh transform is
folded in. Step "7b" (scenegraph `instanceable` instances) was also fixed to
populate per-instance `local_matrix`/`global_matrix` from the node hierarchy.

**Tests:** `tests/usda/pointinstancer-expand-001.usda` (deterministic fixture)
and `tests/feat/instancing/test-instancing.cc` (compute-API math + Tydra
expansion assertions), wired into CTest as `feat-instancing`.

### 3.3 Preliminary limitations

- `velocities` / `accelerations` / `angularVelocities` are ignored (no sub-frame
  motion blur).
- `orientations` are sampled with Held interpolation (no slerp across
  timeSamples).
- Prototype prims that are ordinary active children of a PointInstancer are also
  emitted as standalone meshes (drawn once at their authored location in addition
  to the instances). Author prototypes under an inactive/over scope to avoid
  this, or rely on a future prototype-pruning pass.
- Nested instancing (a prototype that itself contains a PointInstancer or an
  instanceable prim) is expanded only one level.
- Conversion is a single snapshot at `env.timecode`; cross-frame animation of
  instancer arrays is not expanded.

### 3.4 Plan for scaling to the Island scene

The island scene needs efficient **scenegraph instancing + payload deferral**
(its dominant pattern), plus eventual PointInstancer support for the XGen
archives. Roadmap:

1. **Payload deferral / lazy load.** Honor `payload` as deferred by default and
   expand on demand, so the 3.9k references / 214 payloads don't all resolve up
   front. Track which prototypes are actually reached.
2. **Prototype mesh dedup via InstanceKey.** Reuse the existing `InstanceKey`
   registry so structurally-identical instances (same reference + variant) share
   one `RenderMesh`, emitting only a `RenderInstance` (matrix) per instance —
   exactly as the PointInstancer path now does. Extend step "7b" to look up the
   prototype's mesh once and reference it from every instance.
3. **Instance-array everywhere (no flatten).** Keep representing instances as
   `RenderInstance` (shared `mesh_id` + transform), never duplicating geometry;
   this is the only way 100+ instances × large XGen meshes stay in memory.
4. **Prototype pruning.** Skip emitting prototype subtrees as standalone draw
   geometry when they are reachable only through instancing.
5. **PointInstancer inside binary archives.** Once a binary archive that uses
   PointInstancer is loaded, the new `ExpandPointInstancer` already handles it;
   add multi-mesh prototype support and instance-count caps with explicit
   `log()`-style reporting of any truncation.
6. **Nested instancing.** Generalize expansion to recurse through prototypes that
   contain PointInstancers or instanceable prims.
7. **Streaming / culling hooks.** Expose per-instance bounds
   (`ComputeExtentAtTime` equivalent) so a renderer can frustum/occlusion-cull
   instances without materializing all transforms.

### 3.5 Verification

```
cd build && cmake --build . -j16
cd build && ctest -R feat-instancing --output-on-failure
cd build && ctest --output-on-failure        # full regression
```

Manual sanity: convert `tests/usda/instancing-001.usda` and
`tests/usda/pointinstancer-full-001.usda` to a RenderScene and inspect
`RenderScene::instances` (count, shared `mesh_id`, masked instances dropped).
