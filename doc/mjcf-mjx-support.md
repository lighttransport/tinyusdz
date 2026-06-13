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
| `<custom><numeric|text|tuple>` | — | Missing (MJX reads some, e.g. `max_contact_points`) |
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
| `<freejoint>` / `<joint type="free">` | root body / `PhysicsArticulationRootAPI` (non-root → fixed + warn) | Partial |
| **multiple `<joint>` per body** | chain of single-DOF joints via massless intermediate link Xforms | Full |
| `<joint>` limit/range/ref/damping/armature/stiffness/frictionloss | typed limits + `MjcJointAPI` + `physxLimit:*`/`state:*` mirror | Full |
| `<frame>` (MuJoCo 3 grouping) | flattened into child transforms | Partial |

## Geoms / shapes

| MJCF geom type | USD | Status |
|---|---|---|
| box / sphere / capsule / cylinder / ellipsoid / plane | native `GeomCube/Sphere/Capsule/Cylinder/Plane` (ellipsoid→scaled sphere) | Full |
| mesh (`<asset><mesh>` .stl/.obj/.msh) | `GeomMesh` | Full |
| `<geom>` contype/conaffinity/group/condim/priority/solref/solimp/margin/gap/friction | `PhysicsCollisionAPI` + `MjcCollisionAPI` (`mjc:*`) | Full |
| visual vs collision classification (group 0–2 vs 3–5) | `purpose` + `physics:collisionEnabled` + `MjcImageableAPI` | Full |
| hfield (heightfield) | — | Missing |
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
| `<actuator><general/cylinder/adhesion/damper/intvelocity>` (joint/tendon/site) | `MjcActuator` (params preserved) | Partial (gain/bias/range preserved; type-specific dynamics not interpreted) |

## Constraints & contacts

| MJCF | USD | Status |
|---|---|---|
| `<equality><connect/weld/joint>` | `MjcEquality{Connect,Weld,Joint}API` host Xform + `mjc:target` rel | Full |
| `<equality><tendon/distance/flex>` | — | Missing |
| `<contact><exclude>` | `PhysicsFilteredPairsAPI` (`physics:filteredPairs`) | Full |
| `<contact><pair>` (explicit pair + friction/solref) | — | Missing |

## Sensors, deformables, misc (not yet converted)

| MJCF | Status | Notes |
|---|---|---|
| `<sensor>` (touch/accel/gyro/jointpos/framepos/…) | Missing | Large taxonomy; candidates for a `MjcSensor` prim + `MjcSensorAPI` |
| `<light>` (directional→`DistantLight`, point/spot→`SphereLight`+cone) | **Full** | color from `diffuse`, castshadow, emission oriented by `dir`, baked world xform |
| `<camera>` (fovy→aperture, orthographic) | **Full** | `UsdGeomCamera` with baked world xform |
| `<asset><texture/material>` | Missing (MJCF side) | Geom rgba only; PBR material binding TBD |
| `<asset><skin>` | Missing | Map to `UsdSkel` |
| `<deformable>/<flexcomp>/<flex>` (MuJoCo 3 FEM soft bodies) | Missing | No USD physics soft-body schema; needs design |

> **Soft bodies — note.** Not every "soft" MuJoCo model uses `<flex>`. Tendon-driven
> compliant mechanisms — e.g. **`iit_softfoot`** (a chain of rigid phalanx bodies
> coupled by elastic `<tendon><spatial>` cables through ~200 sites + `<geom
> sidesite>` wraps) — are **fully supported** today via the spatial-tendon + site
> path above (the spring behavior is preserved as tendon stiffness/springlength).
> True FEM/`<flex>` deformables remain Missing.
| `<plugin>` / `<extension>` (engine plugins) | Missing | Preserve as `mjc:plugin:*` custom data |

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
