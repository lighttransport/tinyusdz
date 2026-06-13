# MJCF / MuJoCo-MJX → USD support matrix

Status of MuJoCo MJCF (and MJX-compatible) element/primitive conversion to USD
in tinyusdz, plus what is still required for *full* coverage. This is the
authoritative checklist for the `urdf-to-usd` CLI (`examples/urdf-to-usd`),
the shared converter (`src/tydra/urdf-to-usd.cc`), and the web demo
(`web/js/urdf.js`, `web/js/cli/urdf-to-usd.js`).

Legend:
- **Full** — parsed and emitted as the canonical USD/UsdPhysics/MjcPhysics representation, round-trips through USDA+USDC.
- **Preserved** — round-trips losslessly as generic/namespaced props (no typed schema), survives USDC.
- **Viz** — visualized in the web demo source view (not necessarily in the converted USD).
- **Partial** — converted with documented approximations/limitations.
- **Missing** — not yet converted (dropped, usually with a warning).

## Scene-level

| MJCF | USD representation | Status |
|---|---|---|
| `<mujoco model>` | stage `defaultPrim`, `/World` Xform | Full |
| `<compiler angle/eulerseq/autolimits/.../inertiafromgeom>` | `MjcSceneAPI` `mjc:compiler:*` on `PhysicsScene` | Full |
| `<option timestep/integrator/solver/iterations/cone/jacobian/.../wind/gravity>` | `MjcSceneAPI` `mjc:option:*` | Full |
| `<option><flag .../>` (constraint, contact, gravity, …24 flags) | `MjcSceneAPI` `mjc:flag:*` | Full |
| `<size njmax/nconmax/nkey/nuser_*>` | — | Missing (rarely needed; allocator hints) |
| `<statistic>` | — | Missing (viewer hints: extent/center/meansize) |
| `<visual>` (rgba/quality/headlight/map) | — | Missing (render hints) |
| `<default>` class tree (geom/joint/site/tendon/muscle/general) | resolved/inlined at parse time | Full (geom/joint/site/tendon); muscle/general via class merge |
| `<custom><numeric|text>` | `/World/MjcCustom` Xform: `mjc:custom:<name>` (double[]) / `mjc:customtext:<name>` (token) | Preserved (MJX knobs e.g. `max_contact_points` round-trip; `<tuple>` still dropped) |
| multiple `<worldbody>` / `<include>` merge | all `<worldbody>` blocks (local + included) merged into one world | Full |
| `<keyframe><key qpos/qvel/act/ctrl/mpos/mquat>` | `MjcKeyframe` prim | Full |
| up-axis (Z-up source → Y-up stage) | single corrective root `RotateX(-90)` | Full |

## Bodies / kinematics

| MJCF | USD representation | Status |
|---|---|---|
| `<body name/pos/quat/...>` | `Xform` under `/World/Links` (geometry world-baked) | Full |
| orientation specifiers (quat/axisangle/euler/xyaxes/zaxis) | resolved to a matrix; `eulerseq` honored | Full |
| `<inertial mass/pos/diaginertia>` | `PhysicsMassAPI`/`PhysicsRigidBodyAPI` | Full |
| `<inertial fullinertia>` (off-diagonal) | Jacobi-diagonalized → `diagonalInertia` + `principalAxes` | Full |
| `<joint hinge/slide>` | `PhysicsRevolute/PrismaticJoint` | Full |
| `<joint ball>` | `PhysicsSphericalJoint` | Partial (cone-limit only; 3-DOF can't be slider-driven) |
| `<freejoint>` / `<joint type="free">` | 6-DOF floating base: the body is a free `PhysicsArticulationRootAPI` (not joined to its parent) carrying `mjc:freeJoint=1`, distinguishing it from a fixed/anchored base (a parentless link without the flag) | Full |
| **multiple `<joint>` per body** | chain of single-DOF joints via massless intermediate link Xforms | Full |
| `<joint>` limit/range/ref/damping/armature/stiffness/frictionloss | typed limits + `MjcJointAPI` + `physxLimit:*`/`state:*` mirror | Full |
| `<frame>` (MuJoCo 3 grouping transform) | dissolved at parse time: the frame transform is composed into each child's pose (pos+quat, fromto, childclass) and the children lifted to the frame's parent | Full |
| `<body mocap="true">` | `mjc:mocap=1` on the link `Xform` (mocap target body) | Full |

## Geoms / shapes

| MJCF geom type | USD | Status |
|---|---|---|
| box / sphere / capsule / cylinder / ellipsoid / plane | native `GeomCube/Sphere/Capsule/Cylinder/Plane` (ellipsoid→scaled sphere) | Full |
| mesh (`<asset><mesh>` .stl/.obj/.msh) | `GeomMesh` | Full |
| `<geom>` contype/conaffinity/group/condim/priority/solref/solimp/margin/gap/friction | `PhysicsCollisionAPI` + `MjcCollisionAPI` (`mjc:*`) | Full |
| visual vs collision classification (group 0–2 vs 3–5) | `purpose` + `physics:collisionEnabled` + `MjcImageableAPI` | Full |
| `<geom type="hfield">` + `<asset><hfield file=.png \| nrow/ncol/elevation>` | top surface tessellated to a `GeomMesh` (normalized elevation × `size.z`, smooth normals) | Full (CLI/native + JS CLI; browser demo views via USD load). PNG decode + inline `elevation` |
| world-fixed geoms directly under `<worldbody>` (floor/ground/hfield) | collected onto a static root link `world` (`rigidBodyEnabled=false`, no articulation root); a visible collider is emitted as BOTH a render mesh and an exact triangle-mesh collider (`approximation=none`) so the ground actually collides | Full |
| sdf (signed-distance geom) | — | Missing |

## Sites, tendons, actuators (muscle)

| MJCF | USD | Status |
|---|---|---|
| `<site>` (in body) | `GeomSphere` marker under `/World/Sites` + `MjcSiteAPI` (baked world xform) | Full |
| `<tendon><fixed>` (joint coupling) | `MjcTendon` type=fixed, `mjc:path`→joints, `mjc:path:coef` | Full |
| **`<tendon><spatial>` (muscle path)** | `MjcTendon` type=spatial, `mjc:path`→routing sites, rgba/width | Full + **Viz** (red polylines through the sites, drawn on **both** the MJCF-source and converted-USD views) |
| `<spatial><geom sidesite=..>` wrap | routed via the sidesite via-point | Partial (wrap-surface geometry not modeled) |
| `<spatial><pulley divisor>` | — | Missing |
| **`<actuator><muscle>` / `<general class="muscle">`** | `MjcActuator` (`mjc:target`→tendon, `gainPrm`/`biasPrm`/`lengthRange`/`ctrlRange`) | Full |
| `<actuator><motor/position/velocity>` (joint) | `NewtonActuator` (PD: kp/kv, force/ctrl range) | Full |
| `<actuator><general/cylinder/adhesion/damper/intvelocity/plugin>` (joint/tendon/site/body) | `MjcActuator` (all gain/bias/transmission/range params + `mjc:plugin`/`mjc:instance`) | Partial (params preserved + USDC-round-trip; type-specific dynamics not interpreted) |

## Constraints & contacts

| MJCF | USD | Status |
|---|---|---|
| `<equality><connect/weld/joint>` | `MjcEquality{Connect,Weld,Joint}API` host Xform + `mjc:target` rel | Full |
| `<equality><tendon/distance/flex>` | — | Missing |
| `<contact><exclude>` | `PhysicsFilteredPairsAPI` (`physics:filteredPairs`) | Full |
| `<contact><pair>` (explicit pair + friction/solref/solimp/condim) | `/World/Contacts` `MjcContactPair` prim (`mjc:geom1`/`mjc:geom2` + friction/solref/solimp/condim/gap/margin) | Full |

## Sensors, deformables, misc (not yet converted)

| MJCF | Status | Notes |
|---|---|---|
| `<sensor>` (all kinds: touch/accel/gyro/velocimeter/jointpos/jointvel/actuatorpos/frc/framepos/framequat/subtreecom/…) | **Full** | one typed `MjcSensor` prim per sensor under `/World/Sensors`; kind in `mjc:type`, measured object in `mjc:objtype`/`mjc:objname` (+ `mjc:reftype`/`mjc:refname`), `mjc:cutoff`/`mjc:noise`/`mjc:group`/`mjc:user`. Round-trips through USDA+USDC |
| `<light>` (directional→`DistantLight`, point/spot→`SphereLight`+cone) | **Full** | color from `diffuse`, castshadow, emission oriented by `dir`, baked world xform |
| `<camera>` (fovy→aperture, orthographic) | **Full** | `UsdGeomCamera` with baked world xform |
| `<asset><material>` (rgba/metallic/roughness/emission) | **Full** | `UsdShade` Material + `UsdPreviewSurface` under `/World/Materials`, bound to visual geoms via `material:binding`. A material with a file `texture` also emits a `UsdUVTexture` + `UsdPrimvarReader_float2` graph (`diffuseColor` ← texture `outputs:rgb`) |
| `<asset><texture>` (builtin checker/gradient + file) + `<material texture texrepeat>` | **Partial** | **file textures** become a `UsdUVTexture` (`inputs:file`, `wrapS/T=repeat`, `sourceColorSpace=sRGB`) in the converted USD, with `primvars:st` carried on the meshes (OBJ `vt` loaded). The reference is a portable, **source-relative** path (e.g. `assets/foo.png`) so usda/usdc open beside their assets; **usdz output embeds the texture bytes** into the package so it is self-contained. The web demo additionally renders builtin checker/gradient via canvas. Remaining follow-ons: builtin checker/gradient as generated PNG assets in the USD, `texrepeat` tiling (`UsdTransform2d`), skybox-as-background |
| `<asset><model>` + `<attach model body prefix>` (sub-model composition) | **Full** | child model loaded + its named body subtree grafted at the attach point, every name/reference prefixed, child `<default>`/`<asset>`/`<tendon>`/`<actuator>`/`<equality>` merged; child radian euler/axisangle converted to the parent's angle units; mesh paths rebased. Used by `iit_softfoot/scene.xml`. Joint range/ref keep child units (limits only); a root body's joint-to-world is still not emitted |
| `<asset><skin>` | Missing | Map to `UsdSkel` |
| `<deformable>/<flexcomp>/<flex>` (MuJoCo 3 FEM soft bodies) | Missing | No USD physics soft-body schema; needs design |

> **Soft bodies — note.** Not every "soft" MuJoCo model uses `<flex>`. Tendon-driven
> compliant mechanisms — e.g. **`iit_softfoot`** (a chain of rigid phalanx bodies
> coupled by elastic `<tendon><spatial>` cables through ~200 sites + `<geom
> sidesite>` wraps) — are **fully supported** today via the spatial-tendon + site
> path above (the spring behavior is preserved as tendon stiffness/springlength).
> True FEM/`<flex>` deformables remain Missing.
| `<plugin>` / `<extension>` (engine plugins, e.g. `mujoco.pid`) | **Preserved** | `<actuator><plugin>` -> `MjcActuator` with `mjc:plugin`/`mjc:instance`; `<extension><plugin><instance><config>` -> `/World/MjcPlugins` `mjc:plugin:<instance>:plugin` + `:config:<key>`. Round-trips through USDA+USDC (recovers shadow_dexee's PID hand actuators) |

## MuJoCo MJX notes

MJX (MuJoCo on XLA/JAX, `mujoco.mjx`) consumes **standard MJCF** — there is no
separate "MJX file format" — so the matrix above applies directly. MJX does,
however, support only a **subset** of MuJoCo features and reads a few extra
`<custom>` knobs. For an MJX-targeted export the relevant points are:

- **Supported and round-tripped**: bodies, hinge/slide/ball/free joints,
  geoms (box/sphere/capsule/cylinder/plane/mesh), `<option>`/`<flag>`,
  fixed & spatial tendons, `<general>`/muscle actuators, equality
  connect/weld/joint, `<contact><exclude>`, keyframes — all **Full** above.
- **MJX-relevant `<custom><numeric>`** (e.g. `max_contact_points`,
  `max_geom_pairs`) — currently **Missing**; worth preserving as
  `mjc:custom:*` since MJX reads them at compile time.
- **MJX-unsupported MuJoCo features** (so absence in the converter is benign
  for MJX targets): `<deformable>`/flex, some sensor types, `<hfield>`/`<sdf>`
  collisions, muscle *dynamics* (geometry/params convert, the solver model does
  not). These are flagged **Missing** above.
- **Solver/option flags** MJX cares about (`integrator`, `cone`, `solver`,
  `iterations`, `ls_iterations`, contact flags) are all captured in
  `MjcSceneAPI` (**Full**).

## Validating the export with the OpenUSD toolset

The converter is cross-checked against **pixar OpenUSD** (the reference
implementation) as an independent oracle — tinyusdz wrote the file, OpenUSD
reads/validates it. Get the tools with `pip install usd-core` (provides
`usdchecker`, `usdcat`, and the `pxr` Python module); a full OpenUSD build adds
`usdview`/`usdrecord`. Without OpenUSD, tinyusdz's own `tusdcat`
(`examples/tusdcat`) mirrors steps 2–3.

First export a model in all three formats:

```sh
build/examples/urdf-to-usd/urdf-to-usd \
  /path/to/mujoco_menagerie/agility_cassie/cassie.xml \
  --input-format mjcf --format all -o /tmp/cassie
# -> /tmp/cassie.usda, /tmp/cassie.usdc, /tmp/cassie.usdz
```

**1. Conformance — `usdchecker`** (must print `Success!`):

```sh
usdchecker /tmp/cassie.usdc
usdchecker /tmp/cassie.usdz        # also runs the USDZ/ARKit package checks
```

Catches missing stage metadata (e.g. `metersPerUnit`), unresolved asset paths,
and compressed-ZIP-in-USDZ violations.

**2. Composition — `usdcat`** (opens + flattens; confirms OpenUSD reads
tinyusdz's crate and resolves the shading network):

```sh
usdcat --flatten /tmp/cassie.usdz | less
# inside a usdz the texture resolves to a package-internal path:
#   asset inputs:file = @/tmp/cassie.usdz[assets/cassie-texture.png]@
# in usda/usdc it is the source-relative reference:
#   asset inputs:file = @assets/cassie-texture.png@
```

**3. Texture / USDZ packaging — `pxr` Python** (confirms the texture bytes are
packaged, resolve, and are bound to the meshes — exactly what a renderer feeds
on):

```python
from pxr import Usd, UsdShade, UsdGeom
stage = Usd.Stage.Open("/tmp/cassie.usdz")
assert stage.GetMetadata("metersPerUnit") == 1.0        # SI scale authored
zf = Usd.ZipFile.Open("/tmp/cassie.usdz")
assert "assets/cassie-texture.png" in zf.GetFileNames() # texture embedded
tex = UsdShade.Shader(stage.GetPrimAtPath("/World/Materials/cassie/DiffuseTexture"))
print(tex.GetInput("file").Get().resolvedPath)          # .../cassie.usdz[assets/cassie-texture.png]
bound = sum(
    1 for p in stage.Traverse() if p.IsA(UsdGeom.Mesh)
    and UsdShade.MaterialBindingAPI(p).ComputeBoundMaterial()[0]
    and UsdGeom.PrimvarsAPI(p).GetPrimvar("st").IsDefined())
print("textured meshes:", bound)                        # 25 for cassie
```

**4. Visual render — `usdview` / `usdrecord`** (optional; needs a GL 4.5 context):

```sh
usdview   /tmp/cassie.usdz
usdrecord --imageWidth 640 /tmp/cassie.usdz /tmp/cassie.png
```

Both drive Hydra/Storm and require a GPU or a GL-4.5-capable software stack; they
will not initialize on a headless box without one. Use steps 1–3 for headless/CI
validation — they exercise the same data Hydra renders, no GL context required.

Known-good baseline (`agility_cassie/cassie.xml`): `usdchecker` is clean on
usda/usdc/usdz, the texture is embedded + package-resolved in usdz and
source-relative in usda/usdc, and all 25 visual meshes bind the material with
`primvars:st`.

## Required for "full" MJCF/MJX coverage (prioritized backlog)

1. **`<contact><pair>`** explicit contact pairs → a `MjcContactPairAPI`/prim
   (friction/solref/solimp/gap). Companion to the existing `<exclude>` support.
2. **`<sensor>`** → `MjcSensor` prim + per-type `MjcSensorAPI` (start with
   jointpos/jointvel/framepos/framequat/touch/accelerometer/gyro).
3. **`<camera>`/`<light>`** → `UsdGeomCamera` / `UsdLux` (straightforward).
4. **`<asset><texture/material>`** → `UsdShade` material binding (geom rgba →
   UsdPreviewSurface) so converted models keep their appearance.
5. **`<custom>`** numeric/text/tuple → `mjc:custom:*` customData (MJX knobs).
6. **`<spatial>` wrap geoms + `<pulley>`** → faithful muscle wrap geometry
   (sphere/cylinder wrap surfaces) for accurate moment arms.
7. **`<hfield>` / `<sdf>` geoms** → `GeomMesh` (tessellated) or a dedicated prim.
8. **`<deformable>`/`<flex>`** (MuJoCo 3 soft bodies) → needs a USD physics
   soft-body/FEM schema (design work; no UsdPhysics analog yet).
9. **`<equality><tendon/distance/flex>`** → extend the `MjcEquality*API` family.
10. **`<plugin>`/`<extension>`** → preserve plugin config as customData.
