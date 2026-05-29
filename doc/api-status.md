# USD API Implementation Status

Coverage of OpenUSD schema domains in tinyusdz.

**Legend**: done = full pipeline (parse + pprint + USDC write), stub = struct exists but not wired into pipeline, -- = not implemented

---

## UsdGeom -- Geometry

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| Scope | concrete | done | In `core/model-scope.hh` |
| Xform | concrete | done | |
| Mesh | concrete | done | |
| BasisCurves | concrete | done | |
| NurbsCurves | concrete | done | |
| Points | concrete | done | |
| PointInstancer | concrete | done | |
| GeomSubset | concrete | done | |
| Sphere | concrete | done | |
| Cube | concrete | done | |
| Cone | concrete | done | |
| Cylinder | concrete | done | |
| Capsule | concrete | done | |
| Camera | concrete | done | |
| Plane | concrete | done | |
| NurbsPatch | concrete | done | |
| HermiteCurves | concrete | done | |
| TetMesh | concrete | done | |
| Cylinder_1 | concrete | done | Updated cylinder with separate top/bottom radii |
| Capsule_1 | concrete | done | Updated capsule with separate top/bottom radii |
| VisibilityAPI | API (single) | done | |
| GeomModelAPI | API (single) | done | |
| MotionAPI | API (single) | done | |
| PrimvarsAPI | API (non-applied) | done | |
| XformCommonAPI | API (non-applied) | done | |

**Coverage: 20/20 concrete, 5/5 API -- 100%**

---

## UsdLux -- Lighting

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| SphereLight | concrete | done | |
| DiskLight | concrete | done | |
| RectLight | concrete | done | |
| CylinderLight | concrete | done | |
| DistantLight | concrete | done | |
| DomeLight | concrete | done | |
| DomeLight_1 | concrete | done | Adds poleAxis for flexible orientation |
| GeometryLight | concrete | done | |
| PortalLight | concrete | done | |
| PluginLight | concrete | done | |
| LightFilter | concrete | done | |
| PluginLightFilter | concrete | done | |
| LightAPI | API (single) | done | |
| MeshLightAPI | API (single) | done | |
| VolumeLightAPI | API (single) | done | |
| LightListAPI | API (single) | done | |
| ListAPI | API (single) | done | |
| ShapingAPI | API (single) | done | |
| ShadowAPI | API (single) | done | |

**Coverage: 12/12 concrete, 7/7 API -- 100%**

---

## UsdShade -- Materials & Shaders

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| Material | concrete | done | |
| Shader | concrete | done | |
| NodeGraph | concrete | done | |
| MaterialBindingAPI | API (single) | done | |
| NodeDefAPI | API (single) | done | |
| ConnectableAPI | API (non-applied) | done | |
| CoordSysAPI | API (multi) | done | |

**Shader node implementations:**

| Shader | Status |
|--------|--------|
| UsdPreviewSurface | done |
| UsdUVTexture | done |
| UsdTransform2d | done |
| UsdPrimvarReader_float | done |
| UsdPrimvarReader_float2 | done |
| UsdPrimvarReader_float3 | done |
| UsdPrimvarReader_float4 | done |
| UsdPrimvarReader_int | done |
| UsdPrimvarReader_string | done |
| UsdPrimvarReader_normal | done |
| UsdPrimvarReader_point | done |
| UsdPrimvarReader_vector | done |
| UsdPrimvarReader_matrix | done |
| OpenPBRSurface | done |
| MtlxUsdPreviewSurface | done |
| MtlxAutodeskStandardSurface | done |
| MtlxOpenPBRSurface | done |
| MtlxUniformEdf | done |
| MtlxConicalEdf | done |
| MtlxMeasuredEdf | done |
| MtlxLight | done |

**Coverage: 3/3 concrete, 4/4 API, 21/21 shader nodes -- 100%**

---

## UsdSkel -- Skeletal Animation

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| SkelRoot | concrete | done | |
| Skeleton | concrete | done | |
| SkelAnimation | concrete | done | |
| BlendShape | concrete | done | |
| SkelBindingAPI | API (single) | done | |

**Coverage: 4/4 concrete, 1/1 API -- 100%**

---

## UsdPhysics -- Physics Simulation

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| PhysicsScene | concrete | done | |
| PhysicsCollisionGroup | concrete | done | |
| PhysicsJoint | concrete | done | Generic D6 joint (all DOFs free) |
| PhysicsRevoluteJoint | concrete | done | |
| PhysicsPrismaticJoint | concrete | done | |
| PhysicsSphericalJoint | concrete | done | |
| PhysicsDistanceJoint | concrete | done | |
| PhysicsFixedJoint | concrete | done | |
| PhysicsRigidBodyAPI | API (single) | done | |
| PhysicsCollisionAPI | API (single) | done | |
| PhysicsMaterialAPI | API (single) | done | |
| PhysicsMeshCollisionAPI | API (single) | done | |
| PhysicsMassAPI | API (single) | done | Struct + enum registered |
| PhysicsFilteredPairsAPI | API (single) | done | Struct + enum registered |
| PhysicsArticulationRootAPI | API (single) | done | Marker schema, struct + enum registered |
| PhysicsDriveAPI | API (multi) | done | |
| PhysicsLimitAPI | API (multi) | done | |

**Also: MuJoCo extensions** (tinyusdz-specific, not in OpenUSD):
MjcActuator, MjcTendon, MjcKeyframe (concrete); MjcSceneAPI, MjcJointAPI, MjcCollisionAPI, MjcMeshCollisionAPI, MjcMaterialAPI, MjcSiteAPI, MjcImageableAPI, MjcEqualityAPI and variants (API schemas).

**Coverage: 8/8 concrete, 9/9 API -- 100%**

---

## UsdMedia -- Media

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| SpatialAudio | concrete | done | |
| AssetPreviewsAPI | API (single) | done | Marker schema, struct + enum registered |

**Coverage: 1/1 concrete, 1/1 API -- 100%**

---

## Apple Preliminary USDZ AR Schemas

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| Preliminary_PhysicsGravitationalForce | concrete | done | |
| Preliminary_InfiniteColliderPlane | concrete | done | |
| Preliminary_ReferenceImage | concrete | done | |
| Preliminary_Behavior | concrete | done | |
| Preliminary_Trigger | concrete | done | |
| Preliminary_Action | concrete | done | |
| Preliminary_Text | concrete | done | |
| Preliminary_AnchoringAPI | API (single) | done | Via host prim props |
| Preliminary_PhysicsMaterialAPI | API (single) | done | Via host prim props |
| Preliminary_PhysicsRigidBodyAPI | API (single) | done | Via host prim props |
| Preliminary_PhysicsColliderAPI | API (single) | done | Via host prim props |

**Coverage: 7/7 concrete, 4/4 API -- 100%**

---

## UsdVol -- Volumes

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| Volume | concrete | stub | Placeholder struct in `core/model-scope.hh` (holds `OpenVDBAsset`/`VoxAsset`), no type trait, not in pipeline |
| OpenVDBAsset | concrete | stub | Placeholder struct in `core/model-scope.hh`, no type trait, not in pipeline |
| Field3DAsset | concrete | -- | |
| VolumeFieldBase | abstract | -- | |
| VolumeFieldAsset | abstract | -- | |
| FieldBase | abstract | -- | |
| FieldAsset | abstract | -- | |
| ParticleField | concrete | -- | |
| ParticleField3DGaussianSplat | concrete | -- | Gaussian splatting |
| 11 ParticleField API schemas | API (single) | -- | Position, orientation, scale, opacity, kernel, radiance |

**Coverage: 0/9 concrete (2 stub), 0/11 API -- 0%**

---

## UsdRender -- Render Settings

| Schema | Type | Status | Notes |
|--------|------|--------|-------|
| RenderSettings | concrete | -- | |
| RenderProduct | concrete | -- | |
| RenderVar | concrete | -- | |
| RenderPass | concrete | -- | |
| RenderSettingsBase | abstract | -- | |

**Coverage: 0/5 -- 0%**

---

## Built-in Mesh Import Plugins (non-USD formats)

These are tinyusdz-specific built-in importers (not OpenUSD schemas). A file
referenced via `references = @model.obj@` is decoded into a `GPrim`/mesh.
Import only -- no writer.

| Format | Source | Status | Notes |
|--------|--------|--------|-------|
| Wavefront OBJ (`usdObj`) | `src/usdObj.{hh,cc}` | partial (import) | Group/object hierarchy flattened to a single mesh; no materials; texcoords/normals expanded to face-varying. Built-in, gated by `TINYUSDZ_USE_USDOBJ`. |
| MagicaVoxel VOX (`usdVox`) | `src/usdVox.{hh,cc}` | partial (import) | Voxel asset import only. |

OBJ TODO (durable): indexed primvars for texcoords/normals, preserve shape
hierarchy, per-face material (GeomSubset), tinyobjloader skin weights, optional
load-time triangulation.

---

## Unsupported Domains (no plans to implement)

The following OpenUSD domains are renderer/DCC-specific and are out of scope for tinyusdz:

| Domain | Schemas | Reason |
|--------|---------|--------|
| **UsdRi** | StatementsAPI, RiMaterialAPI, RiSplineAPI | RenderMan-specific |
| **UsdHydra** | HydraGenerativeProceduralAPI | Hydra renderer-specific |
| **UsdUI** | Backdrop, NodeGraphNodeAPI, SceneGraphPrimAPI, AccessibilityAPI | DCC UI metadata |
| **UsdProc** | GenerativeProcedural | Requires runtime procedural engine |

These prims are preserved as generic `Model` prims when encountered, so data is not lost during roundtrip.

---

## Summary

| Domain | Concrete | API | Overall |
|--------|----------|-----|---------|
| UsdGeom | 20/20 (100%) | 5/5 (100%) | 25/25 (100%) |
| UsdLux | 12/12 (100%) | 7/7 (100%) | 19/19 (100%) |
| UsdShade | 3/3 (100%) | 4/4 (100%) | 7/7 (100%) |
| UsdSkel | 4/4 (100%) | 1/1 (100%) | 5/5 (100%) |
| UsdPhysics | 8/8 (100%) | 9/9 (100%) | 17/17 (100%) |
| UsdMedia | 1/1 (100%) | 1/1 (100%) | 2/2 (100%) |
| Preliminary AR | 7/7 (100%) | 4/4 (100%) | 11/11 (100%) |
| UsdVol | 0/9 (0%) | 0/11 (0%) | 0/20 (0%) |
| UsdRender | 0/5 (0%) | -- | 0/5 (0%) |
| **Total (supported)** | **55/69 (80%)** | **31/42 (74%)** | **86/111 (77%)** |
| Unsupported | -- | -- | 12 schemas (UsdRi, UsdHydra, UsdUI, UsdProc) |

### Remaining gaps

1. **UsdVol** -- Volume and Gaussian splat support (20 schemas)
2. **UsdRender** -- Render settings for offline rendering (5 schemas)
