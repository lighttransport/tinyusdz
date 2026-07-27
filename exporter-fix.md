# Fixing Fruit Stand USDZ Export

## Diagnosis

`fruit-stand.usdz` has an authoring problem in the USDZ, not primarily a
viewer problem.

The asset exports collision hulls as ordinary visible meshes. Those meshes are
active, material-bound, and do not have `purpose = "guide"`,
`purpose = "proxy"`, or `visibility = "invisible"`. A render viewer is therefore
allowed to draw them and include them in visual bounds.

Example problematic pattern:

```usda
def Xform "stand_hull" (
    prepend apiSchemas = [
        "PhysicsRigidBodyAPI",
        "PhysicsMassAPI",
        "PhysicsCollisionAPI",
        "PhysicsMaterialAPI"
    ]
)
{
    custom string physics:approximation = "meshSimplification"
    custom bool physics:collisionEnabled = 1

    def Mesh "stand_hull" (
        active = true
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        rel material:binding = </FruitStand/_materials/lightphys_palette_0>
        float3[] extent = [...]
    }
}
```

Because the collider is also a renderable, material-bound mesh, visual/collision
separation is ambiguous and visual bounding boxes can include physics-only
geometry.

Viewer-side name heuristics such as hiding `*_hull` meshes may work for this
file, but they are not reliable USD behavior. The exporter should author the
roles explicitly.

## Exporter Fix Procedure

### 1. Classify Visual and Collision Objects

The exporter should identify each object as either visual geometry or
physics-only collision geometry.

Recommended inputs:

- naming convention: `_hull`, `_collider`, `_COL`, `_collision`
- or explicit Blender custom property:

```text
lightphys_role = "collider"
```

Prefer explicit metadata over name matching when available.

### 2. Author Visual Meshes as Renderable Geometry

Visual meshes should keep material bindings and render data.

```usda
def Mesh "stand_render" (
    prepend apiSchemas = ["MaterialBindingAPI"]
)
{
    token purpose = "render"
    rel material:binding = </FruitStand/_materials/stand_legs>
    float3[] extent = [...]
}
```

Rules:

- keep `MaterialBindingAPI` on visual meshes
- keep texture and material bindings on visual meshes
- do not apply `PhysicsCollisionAPI` to visual meshes unless the same mesh is
  intentionally used for both rendering and collision

### 3. Author Collider Meshes as Physics-Only Geometry

Collider meshes should be marked as non-render geometry.

```usda
def Mesh "stand_collider" (
    prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsMeshCollisionAPI"]
)
{
    token purpose = "guide"
    token visibility = "invisible"
    uniform token physics:approximation = "convexHull"
    bool physics:collisionEnabled = 1
    float3[] extent = [...]
}
```

Rules:

- remove `MaterialBindingAPI` from collider meshes
- do not bind render materials to colliders
- set `purpose = "guide"` or `purpose = "proxy"`
- set `visibility = "invisible"` when the collider should never be drawn
- emit typed USD schema attributes, not `custom` properties in the `physics:`
  namespace

Use the appropriate collision approximation:

- `convexHull` for simple dynamic rigid bodies
- `convexDecomposition` for more accurate dynamic objects when supported
- `meshSimplification` or triangle mesh collision for static environment pieces
- `none` only when the raw mesh is intentionally used as-is

### 4. Put Rigid Body APIs on the Body Xform

A good structure is to put rigid body and mass APIs on the body prim, then put
the visual mesh and collider mesh below it as separate children.

```usda
def Xform "Apple_01" (
    prepend apiSchemas = ["PhysicsRigidBodyAPI", "PhysicsMassAPI"]
)
{
    bool physics:rigidBodyEnabled = 1
    double physics:mass = 0.12

    def Mesh "Apple_01_render" (
        prepend apiSchemas = ["MaterialBindingAPI"]
    )
    {
        token purpose = "render"
        rel material:binding = </FruitStand/_materials/apple>
        float3[] extent = [...]
    }

    def Mesh "Apple_01_collider" (
        prepend apiSchemas = ["PhysicsCollisionAPI", "PhysicsMeshCollisionAPI"]
    )
    {
        token purpose = "guide"
        token visibility = "invisible"
        uniform token physics:approximation = "convexHull"
        bool physics:collisionEnabled = 1
        float3[] extent = [...]
    }
}
```

This keeps simulation state on the body and collision shape data on the collider
shape, while leaving the visual mesh cleanly renderable.

### 5. Fix Bounding Box Export

For each mesh, `extent` must be the local-space bounds of that mesh's `points`.
It is not a world-space bounding box.

The exporter should:

- compute mesh `extent` from local mesh points after mesh-local baking
- preserve object transforms as `xformOp:*` instead of baking world bounds into
  `extent`
- exclude `purpose = "guide"`, `purpose = "proxy"`, and invisible collider
  geometry from visual preview bounds
- for animated simulation exports, compute preview bounds at the selected frame
  instead of unioning all time samples unless a time-range bound is intended
- author time-sampled extents when point positions are time-sampled

### 6. Validate the Export

Use `tusdcat` to inspect the final USDZ:

```bash
./build/tusdcat fruit-stand.usdz \
  | rg 'purpose|visibility|PhysicsCollisionAPI|PhysicsMeshCollisionAPI|extent|material:binding'
```

Expected checks:

- collider meshes have `PhysicsCollisionAPI`
- collider meshes have `purpose = "guide"` or `visibility = "invisible"`
- collider meshes do not have render material bindings
- visual meshes have material bindings
- visual meshes do not have collision APIs unless intentionally shared
- visual bounds are computed from visible render geometry only

After this fix, a normal viewer can render only visual geometry by default and
show collision meshes only in an explicit physics-debug mode.
