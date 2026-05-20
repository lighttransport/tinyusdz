# How `upAxis` is considered in the USD Physics API

## Question

In UsdPhysics (e.g. joint `physics:axis`), how is the stage `upAxis` taken into
account? When converting a scene between up-axis conventions (e.g. Z-up USD →
Y-up engine, as in a physics importer), must the conversion be applied to **each
physics property independently**, or is it sufficient to apply it **once at the
scene root**?

## Short answer

**Apply the up-axis conversion once at the scene root — do *not* convert physics
properties individually.** Per-property conversion would double-rotate
local-frame data such as `physics:axis`.

Two facts drive this:

1. **`upAxis` is declarative metadata, not a baked transform.** USD never rotates
   coordinates because of `upAxis`; it only *declares* the intended orientation
   so consumers can interpret/correct it
   (`pxr/usd/usdGeom/metrics.h:25-37`).

2. **Most physics quantities are local or hierarchy-relative** (relative to a
   body, a joint frame, the prim's own xform, or the node-xform space). A single
   root rotation propagates through the transform hierarchy to all of them
   automatically; converting them per-property on top of that would
   double-rotate them.

The **only** place the Physics API itself consults `upAxis` is the *default*
gravity direction of `UsdPhysicsScene`. Nothing else — not `physics:axis`, not
joint local poses — references `upAxis`.

## Evidence

### 1. The single upAxis consumer in physics: default gravity

`pxr/usd/usdPhysics/parseUtils.cpp:1556-1562` — when `physics:gravityDirection`
is the sentinel `(0,0,0)`, the parser substitutes the negative stage up axis:

```cpp
TfToken upAxis = UsdGeomGetStageUpAxis(stage);
if (upAxis == ...->x)      gravityDirection = GfVec3f(-1, 0, 0);
else if (upAxis == ...->y) gravityDirection = GfVec3f(0, -1, 0);
else                       gravityDirection = GfVec3f(0, 0, -1);
```

The `X` branch is defensive parser code, not a normal authored stage mode:
`UsdGeom` stage `upAxis` is legally `Y` or `Z` (`metrics.h:52-58`,
`metrics.cpp:54-66`).

Schema docs confirm the semantics:

- `schema.usda:96-104` / `scene.h:138-141`: *"Gravity direction vector in
  simulation **world space**. … A zero vector is a request to use the negative
  upAxis."*
- `parseDesc.h:162-163`: *"Gravity direction, if default 0,0,0 was used negative
  upAxis direction will be returned."*

So `gravityDirection` is a **world-space** vector; if left at default it tracks
`upAxis`; if explicitly authored it is an absolute world vector independent of
any prim transform.

### 2. `physics:axis` is a LOCAL-frame token, independent of upAxis

- `revoluteJoint.h:142-154`, `prismaticJoint.h:142-154`, `sphericalJoint.h`:
  `physics:axis` is `uniform token` ∈ {X, Y, Z}, default `"X"`. Docs say only
  "Joint axis" / "Cone limit axis" — no world-space claim.
- The frame is the **joint local0 frame**, confirmed by the spherical-joint cone
  limit docs: *"Cone limit from the primary joint axis **in the local0 frame**"*
  (`schema.usda:686,696`).
- The joint local frame is defined by `localPos0/localRot0` (relative to body0)
  and `localPos1/localRot1` (relative to body1) — `joint.h:140-225`,
  `schema.usda:503-533`: *"Relative position/orientation of the joint frame to
  body0's/body1's frame."*
- Parser confirmation: `parseUtils.cpp:1020-1044` reads localPos/localRot
  straight into the descriptor; `:1332-1339` / `:1397-…` map the axis token to an
  enum and pass it through. **The axis is never multiplied by upAxis or by a
  world transform** — the physics engine applies it inside the local joint frame.

### 3. Reference-frame classification of physics quantities

| Quantity | Frame | Needs per-property conversion? |
|---|---|---|
| `physics:axis` (revolute/prismatic/spherical) | joint local0 | **No** — rides with body frame |
| `physics:localPos0/1`, `physics:localRot0/1` | relative to body0/body1 | **No** — rides with body frame |
| `physics:centerOfMass`, `physics:principalAxes` | prim local space (`massAPI.h:217,264`) | **No** |
| `physics:diagonalInertia` | scalar magnitudes along principal axes | **No** |
| `physics:velocity`, `physics:angularVelocity` | *"same space as the node's xform"* (`schema.usda:183,192`; `rigidBodyAPI.h:231,254`) | **No independent remap** — keep in the node-xform space; rotate only if flattening/handing off as world-space engine vectors |
| collider `axis` (capsule/cylinder/cone) | prim local | **No** |
| Prim `xformOp:*` (body/collider poses) | parent space | **No** — root rotation propagates |
| **`physics:gravityDirection`** (if explicitly authored) | **world space** (`scene.h:138-141`) | **YES** |

### 4. How USD itself says to do up-axis conversion

`docs/user_guides/render_user_guide.rst:43-72`: the up axis is stage-root
metadata that *"applies to all Xformable objects in the stage, including any
referenced geometry"*; mixed-up-axis content is fixed by *"apply[ing] a
corrective transform"*. Test assets
(`usdImagingGL/.../testUsdImagingGLDomeLight/domeLight{Yup,Zup}.usda`) implement
that corrective transform as a single **90° rotation about X**
(`xformOp:rotateX`). Cameras are a special case — always internally Y-up
regardless of stage upAxis (`usdGeom/camera.h:57-62`).

## Authoring USD Physics from Blender (Z-up)

Blender is Z-up. The simplest correct path is to **keep Z-up** and author
natively:

- Set `upAxis = "Z"` on the stage (matches Blender).
- Author body `xformOp:*`, `physics:localPos0/1`, `physics:localRot0/1` directly
  in Blender's Z-up coordinates — no rotation, no baking.
- Leave `physics:gravityDirection` unauthored; it auto-derives to `(0,0,-1)` from
  `upAxis="Z"`.

**The `physics:axis` token is a *local-frame* selector, not a stage/world axis**
— so it is never "in Z-up coordinates" to be converted. Set it to the joint's DOF
axis within the joint local frame defined by `localRot0/localRot1`:

- If Blender provides the axis as a frame-relative selector → map straight to the
  `X/Y/Z` token.
- If you have a world-space (Z-up) direction → bake that direction into
  `localRot0/localRot1` so a chosen local axis points along it, then set the token
  to that local axis.

Concrete example — a door hinge rotating about world Z in Blender:

```usda
(
    upAxis = "Z"
)
def PhysicsRevoluteJoint "hinge"
{
    rel physics:body0 = </frame>
    rel physics:body1 = </door>
    quatf physics:localRot0 = (1, 0, 0, 0)   # identity → joint frame == body frame
    quatf physics:localRot1 = (1, 0, 0, 0)
    uniform token physics:axis = "Z"         # local-Z; with identity localRot and
                                             # axis-aligned bodies this == world Z
}
```

So a Z-up hinge → `physics:axis = "Z"`, and that `"Z"` is correct as-is in a Z-up
stage. (The schema default is `"X"`, so author it explicitly.)

## Reading & parsing Z-up (Blender) physics into a Y-up system

Use a **single conversion rotation applied only to world-space quantities**. Do
not touch local-frame data.

Conversion (Z-up → Y-up, right-handed):

- Rotation `R = Rx(-90°)`
- Point/vector map: `(x, y, z) → (x, z, -y)`
- Quaternion: `R = (w, x, y, z) = (√2/2, -√2/2, 0, 0)`
- Sanity check: up `+Z (0,0,1) → +Y (0,1,0)`.

Apply `R` on the **world** side only. Be explicit about math convention:
USD/Gf uses row vectors that pre-multiply matrices (`UsdGeom_LinAlgBasics`), so
the Gf matrix form is right-multiplication by the corrective rotation. Many
engines document the equivalent operation with column-vector math, where the
same conversion is written as left-multiplication.

- USD/Gf row-vector matrix: `W_gf' = W_gf · R_gf`.
- USD/Gf row-vector direction: `v_gf' = v_gf · R_gf`
  (equivalently, `R_gf.TransformDir(v_gf)`).
- Column-vector engine matrix: `W_col' = R_col · W_col`.
- Column-vector engine direction/orientation: `v_col' = R_col · v_col`,
  `q_col' = q_R · q_col`.

Per-quantity table when reading into the Y-up engine:

| What you read from USD | Frame | Action |
|---|---|---|
| Rigid body **world** transform (from `UsdGeomXformCache`) | Z-up world | Convert the world transform once (`W_gf' = W_gf · R_gf` in Gf, or `W_col' = R_col · W_col` in column-vector engine math) |
| `physics:localPos0/1`, `physics:localRot0/1` | body-local | **pass through unchanged** |
| `physics:axis` (revolute/prismatic/spherical) | joint-local token | **pass through unchanged** |
| collider `axis` (capsule/cylinder/cone) | prim-local token | **pass through unchanged** |
| `physics:centerOfMass`, `physics:principalAxes`, `physics:diagonalInertia` | prim-local | **pass through unchanged** |
| `physics:velocity`, `physics:angularVelocity` | node xform space | Keep unchanged if preserving the converted hierarchy; rotate with the same world-side `R` only if your engine consumes them as flattened world vectors |
| Resolved `gravityDirection` (from parser) | Z-up world | Convert as a world direction (`g_gf' = g_gf · R_gf`, or `g_col' = R_col · g_col`) |
| Collision mesh points | prim-local | unchanged (world position comes via the converted world transform) |

Notes:

- **Let OpenUSD resolve gravity first.** `LoadUsdPhysicsFromRange`
  (`pxr/usd/usdPhysics/parseUtils.h:94`) reports a `UsdPhysicsSceneDesc` whose
  `gravityDirection` already has the sentinel `(0,0,0)` substituted with
  `-upAxis` (so Z-up → `(0,0,-1)`); then just apply the same world-side `R`,
  giving `(0,-1,0)`. Don't special-case the sentinel yourself.
- **Never remap the `physics:axis` token (e.g. Z→Y).** It is a local-frame token;
  the converted body world transform already reorients the joint frame.
  Rewriting the token double-applies the rotation — the classic bug.
- If your engine is up-axis agnostic, the alternative is to feed `upAxis` through
  and apply no `R` at all; only feed gravity from the resolved direction.

## Conclusion

`upAxis` enters the USD Physics API in exactly one spot — the default gravity
direction of `UsdPhysicsScene`. It does not bake any transform and does not touch
joints. Because `physics:axis`, joint local poses, mass frame, and node-xform
space velocities all ride with the transformed hierarchy, the correct and
minimal conversion is **a single root/world-side rotation**, not per-property
conversion. The lone per-property exception is a world-space direction such as
resolved or explicitly-authored `physics:gravityDirection`.
