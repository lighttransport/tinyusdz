# Wavefront OBJ support (`usdObj`)

LightUSD imports Wavefront OBJ geometry, MTL materials and the `vw` skin-weight
extension into standard USD schemas. OBJ and MTL parsing use the vendored C11
tinyobjloader and its tessellator, shared by standalone imports, Python and
URDF/MuJoCo mesh loading. The pinned revision, MIT license and local patches are
recorded in [`UPSTREAM.txt`](../src/external/tinyobjloader-c/UPSTREAM.txt).

`usdObj` describes an import facility, **not an Obj prim schema**. Output uses
Mesh, BasisCurves, Points, Material, Shader and SkelBindingAPI. OpenUSD also has
an [example read-only OBJ file-format plugin](https://github.com/PixarAnimationStudios/OpenUSD/tree/2095fafafd033fa23386d7ec6d58c7cc33974518/extras/usd/examples/usdObj).
That inspected implementation imports grouped polygon meshes and indexed
face-varying `primvars:uv`, but does not translate normals or MTL materials.
LightUSD's hierarchy, vertex splitting and `primvars:st` mapping differ from
that example.

## Loading and converting

```python
import lightusd

stage = lightusd.import_obj("mesh.obj")
print(stage.warnings())
stage.save("mesh.usda")
stage.save("mesh.usdc")
stage.save_usdz("mesh.usdz")  # Includes successfully loaded texture assets.

# Equivalent filename-based dispatch:
stage = lightusd.load("mesh.obj")

# Bytes require an explicit format. The source anchors relative MTL paths.
with open("mesh.obj", "rb") as source:
    stage = lightusd.load_bytes(source.read(), format="obj", source="mesh.obj")
```

`import_obj` accepts `up_axis="Z"`, `asset_dirs=()`, `max_memory=0` and
`resolver=None`. `up_axis` may be `"Y"` or `"Z"`; it declares the stage axis and
does not rotate or rescale OBJ coordinates. A positive `max_memory` is an import
budget in bytes; zero keeps finite library defaults. `loads()` parses USDA text,
and `is_usd()` remains a USD-only check. See the [Python API](../python/README.md).

The native `urdf-to-usd` CLI also accepts OBJ:

```sh
urdf-to-usd mesh.obj --format all -o mesh.usda
```

This writes `mesh.usda`, `mesh.usdc` and `mesh.usdz`. Use `--input-format obj` to
override filename detection, `--asset-dir DIR` for additional asset roots, and
`--max-memory BYTES` for a custom budget.

## Geometry mapping

| OBJ input | USD output |
|---|---|
| `o`, `g` partitions | Separate geometry prims with sanitized, unique names and the original label as `displayName` |
| `v`, `f` | Mesh points, `faceVertexCounts` and `faceVertexIndices`; polygon arity is preserved |
| Positive and negative face indices | Resolved OBJ indices, then zero-based USD indices |
| `vn` | Vertex-interpolated normals; explicit source normals are retained |
| Missing normals, `s` smoothing groups | Computed geometric normals, smoothed within matching source position/group pairs |
| `vt` | Vertex-interpolated `texCoord2f[] primvars:st` using the first two components |
| RGB on `v` records | Vertex-interpolated `color3f[] primvars:displayColor` when the color array is complete |
| `l` | Linear, nonperiodic BasisCurves with `curveVertexCounts` |
| `p` | Points |
| `usemtl` | MaterialBindingAPI bindings on face GeomSubsets in the `materialBind` family |
| `vw` | Vertex joint-index/weight primvars with SkelBindingAPI; see below |

Meshes have `subdivisionScheme = "none"`; point-based prims receive a computed
extent. UV/normal seams and flat-shaded corners can split one source position
into several USD points. Point count and index order therefore need not match
the source OBJ position array. A mesh with incomplete corner UVs omits
`primvars:st` and warns instead of inventing UVs for the missing corners.

Standalone imports use `/World` as the default prim, with geometry under
`/World/Links/Geometry` and materials under `/World/Materials`. Names are made
valid and collision-safe. Reference composition can replace the `/World` prefix;
select geometry by type or `displayName` when paths are not known in advance.

OBJ meshes used by URDF/MJCF are triangulated. Their line and point primitives
are omitted with a warning. Standalone OBJ import preserves polygons and emits
line/point prims. Bare `v` records without faces, lines or points do not create a
Points prim. Free-form curves/surfaces (`cstype`, `curv`, `surf`) are unsupported
and produce a warning; there is no NURBS evaluation.

OBJ supplies no physical units or rigid-body description. Standalone import
does not invent `metersPerUnit`, colliders, masses or physics joints. Physics
for an OBJ mesh used in a robot asset comes from the URDF/MJCF description.

## MTL materials and textures

Materials become UsdPreviewSurface networks. Supported translations include:

| MTL input | PreviewSurface input or behavior |
|---|---|
| `Kd`, `Ks`, `Ke` | `diffuseColor`, `specularColor`, `emissiveColor` |
| `d` / `Tr` | `opacity` |
| `Ni` | `ior` |
| `Pr`, `Pm` | `roughness`, `metallic` |
| `Ns` without explicit `Pr` | Roughness approximation: `sqrt(2 / (Ns + 2))` |
| Explicit `Pm` | Metallic workflow; otherwise the specular workflow is selected |
| `map_Kd`, `map_Ks`, `map_Ke` | Diffuse, specular and emissive textures |
| `map_d`, `map_Pr`, `map_Pm` | Opacity, roughness and metallic textures |
| `norm` | Normal texture with raw sampling and the `[0, 1]` to `[-1, 1]` remap |
| Texture `-s`, `-o`, `-clamp` | Two-dimensional UV scale/translation and clamp/repeat wrapping |

Maps use UsdUVTexture, a `st` primvar reader and UV transforms. Color maps use
`sRGB`; scalar and normal maps use `raw`. Missing MTL files or textures warn and
leave the geometry usable. Bump maps require height-to-normal conversion and
are not connected. Displacement and unsupported shading features such as sheen
or anisotropy are not reconstructed; this is not a lossless MTL shader archive.

Relative MTL paths resolve from the OBJ file. Each material's texture paths
resolve from the MTL file that defines it. Additional filesystem roots can be
provided through `asset_dirs`; dependencies outside the source/allowed roots
are rejected. An injected resolver is authoritative: a failed callback is not
followed by a direct filesystem read. Memory inputs with external dependencies
need a source anchor or a resolver capable of resolving those dependencies.

Successfully loaded texture bytes are retained for later USDZ packaging,
including after USD composition. Loose USDA/USDC exports keep source texture
identifiers; they do not copy the textures alongside the output.

## Skin weights: the `vw` extension

`vw` is a tinyobjloader extension, not a standard Wavefront OBJ statement. It
provides joint indices and weights while the containing USD supplies the rig:

```text
vw <vertex_id> [<joint_id> <weight>]...
```

Both `vertex_id` and `joint_id` are **zero-based**. The vertex ID addresses the
original `v` position array, not a face corner, UV, normal or emitted USD point.
This differs from ordinary OBJ face indices, which are one-based or negative
relative indices. `vw` does not support negative relative indexing.

For example, save the following as `skin.obj`:

```obj
v 0 0 0
v 1 0 0
v 0 1 0
vw 0 0 0.25 1 0.75
vw 1 1 1.0
vw 2 0 1.0
f 1 2 3
```

The first point has two influences, the second uses joint 1, and the third uses
joint 0. These are joint-order indices; OBJ contains no joint names.

### USD representation and preservation rules

For this triangle the Mesh receives SkelBindingAPI and the following arrays:

```usda
int[] primvars:skel:jointIndices = [0, 1, 1, 0, 0, 0] (
    interpolation = "vertex"
    elementSize = 2
)
float[] primvars:skel:jointWeights = [0.25, 0.75, 1, 0, 1, 0] (
    interpolation = "vertex"
    elementSize = 2
)
```

`elementSize` is the largest influence count in any source `vw` row. For each
emitted point the arrays contain that many consecutive indices and weights.
The same mapping applies to imported Mesh, BasisCurves and Points prims.

- Every influence is retained in source order, including repeated joint IDs.
  There is no four-influence limit, sorting, merging, pruning or normalization.
- Finite signed weights, zero weights, weights above one and non-unit sums are
  accepted. This preserves source data; the consuming skinning implementation
  determines whether those weights are appropriate for deformation.
- Short rows are padded with `(joint 0, weight 0)`. A missing or empty row for an
  emitted point receives all-zero weights and a warning; no root-joint weight
  is guessed.
- With no `vw` records, or with only empty rows, no skin binding is authored.
  All-empty rows produce a warning.
- Original position IDs are tracked through seam splitting, object/group
  partitioning, triangulation and line/point expansion. Every copy of a source
  position receives the same influences.

There may be at most one row per source vertex. Negative, fractional or
out-of-range vertex IDs, negative/fractional joint IDs, duplicate vertex rows,
unpaired joint/weight tokens and non-finite weights fail the import. Joint IDs
must fit a nonnegative signed 32-bit integer. USD import uses float weights and
rejects values outside finite float range. Expanded arrays are checked against
the import memory budget.

The C11 loader populates its arena-owned skin arrays in both the float (`_f`)
and double (`_d`) APIs. The USD frontend uses the float API and authors
`float[]` weights. It cannot check an index against a skeleton's joint count
during OBJ parsing because the skeleton is supplied separately.

### Binding to a skeleton supplied in USD

Importing `vw` does not create a Skeleton, SkelRoot, joint-name order, bind/rest
transforms, animation or `skel:skeleton` relationship. Supply these in the
containing USD. The following complete `character.usda` binds `skin.obj` to a
two-joint skeleton; identity transforms make this a minimal bind-space example:

```usda
#usda 1.0
def SkelRoot "Rig" (
    prepend references = @skin.obj@
    prepend apiSchemas = ["SkelBindingAPI"]
) {
    rel skel:skeleton = </Rig/Skeleton>

    def Skeleton "Skeleton" {
        uniform token[] joints = ["root", "root/child"]
        uniform matrix4d[] bindTransforms = [
            ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1)),
            ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1))
        ]
        uniform matrix4d[] restTransforms = [
            ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1)),
            ((1,0,0,0), (0,1,0,0), (0,0,1,0), (0,0,0,1))
        ]
    }
}
```

The referenced geometry inherits the skeleton relationship from `/Rig`.
Joint IDs index the effective `skel:joints` order when one is supplied through
SkelBindingAPI, otherwise the bound Skeleton's `joints` order. Match this order
to the IDs used by the OBJ; an index must be valid for that order. For an
existing rig, use its actual joint order and bind/rest transforms.

OBJ coordinates must agree with the rig's bind space. The importer does not
author `primvars:skel:geomBindTransform`, so USD's identity fallback applies
unless the containing asset authors a suitable transform.

Compose with LightUSD and export standard USD for downstream tools:

```python
import lightusd

stage = lightusd.load("character.usda")
stage.save("character.usdc")
```

The exported USD contains geometry and skin primvars without requiring an OBJ
reader in the consuming application. Loading skin data does not itself evaluate
skeletal animation or run a deformable-body simulation.

## Composition and native APIs

The next-core layer loader used by Python supports OBJ references, payloads and
sublayers. An OBJ layer has default prim `/World`, so a reference without an
explicit prim path imports that subtree, as in the binding example above. An
OBJ sublayer contributes its `/World` hierarchy directly. OBJ, MTL and textures
can also be resolved within the same USDZ package; package dependencies cannot
escape to arbitrary filesystem files.

Unflattened USD assets that refer to OBJ require OBJ loading support in the
consumer. OpenUSD's example plugin does not implement LightUSD's material and
`vw` mapping. Export the composed stage as USDA, USDC or USDZ when those features
must be portable to other USD readers.

| API | Scope |
|---|---|
| Python `import_obj`, `load`, `load_bytes` | Full stage import, including materials, curves, points and skin primvars |
| C `lightusd_import_file`, `lightusd_import_memory` | Full import with `LIGHTUSD_IMPORT_OBJ`; initialize options with `lightusd_import_options_init` |
| C++ `usdObj::ReadObjStageFromFile`, `ReadObjStageFromMemory`, `ReadObjLayerFromMemory` | Legacy full-stage/layer adapters; memory APIs accept source and import options |
| C++ `usdObj::ReadObjFromFile`, `ReadObjFromString` | Legacy geometry-only adapter: combines polygon meshes, requires faces and does not provide the full material/curve/point scene |
| Next-core `pcp::LoadLayerFromFile`, `LoadLayerFromMemory` | Layer loading and composition; use a `.obj` source identifier for format dispatch |

See [`lightusd-import-c.h`](../src/c-api/lightusd-import-c.h),
[`usdObj.hh`](../src/usdObj.hh) and the
[next-core layer loader](../src/next/pcp/layer-registry.hh) for signatures and
ownership. The legacy composition loader also accepts loose OBJ layers; this
does not make every USD-only memory loader an OBJ reader.

OBJ export is a separate Tydra mesh exporter (`tydra::export_to_obj`), not a
general USD-stage-to-OBJ file-format writer. It can write the same zero-based
`vw` extension from render-mesh skin data.

## Build options, diagnostics and verification

`LIGHTUSD_WITH_USDOBJ=ON` enables the OBJ layer adapter. In the standalone next
build it works with `LIGHTUSD_WITH_TYDRA=OFF`, without the XML robot frontend.
The legacy full-stage adapter requires Tydra as well. Disabling USDOBJ removes
the layer adapter; robot import frontends have their own shared OBJ dependency.

Import budgets default to 1 GiB of tracked memory estimates and 512 MiB per
input. They cover parser allocations, dependency reads and expanded data, not
the process's total resident memory. Invalid geometry, malformed skin data,
exceeded parser/expanded-array limits and inputs with no supported primitives
fail the import. Optional missing MTL/maps,
incomplete UVs, missing weights and unsupported features are reported through
`stage.warnings()` in Python or the native warning output.

Regression coverage includes:

- [C11/next OBJ reader tests](../tests/next/test_obj_reader.cc): float/double
  skin arrays, invalid records, mixed primitive types and influence metadata.
- [Python OBJ layer/skin tests](../python/tests/test_obj_layers_skin.py): seam
  copies, signed weights, more than four influences, composition, packaged
  dependencies, retained textures and an optional OpenUSD UsdSkel deformation
  oracle.
- [Native import tests](../python/tests/test_native_import.py): normals, UVs,
  MTL maps, concave tessellation, texture packaging and robot mesh imports.

See [regression instructions](testing-cpp.md#native-objrobot-imports-and-python-physics)
for isolated builds, the OpenUSD oracle and the full MuJoCo Menagerie sweep.
