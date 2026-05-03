# C API & Python binding — remaining tasks

Snapshot as of `2026-05-04` (post `287964e6` end-to-end authored
scene round-trip; coverage push 1057 → 1449 Python tests + one
binding bugfix).

Focused on outstanding work for the C API
(`src/c-tinyusd*.h`, `src/c-tinyusd-helpers.h`) and the Python binding
built on top of it (`src/python/module.c`, `python/tinyusdz/`).

## Recently shipped

The list below summarises items moved from "outstanding" to "done"
since the previous (`9032b730`) snapshot. Each entry names the commit
that landed it and the test file (or test name) that fences it.

### High-value additions

| Task | Commit | Tests |
|---|---|---|
| Composition arc authoring (Reference / Payload / Inherits / Specializes; per-qualifier listop) | `9ff86fb2` | `python/tests/test_composition_arcs.py` |
| Variant set + selection authoring (metadata level) | `7328b82f` | `python/tests/test_variant_authoring.py` |
| Variant content authoring (Prim subtrees + attributes inside variants) | `60200f7c` | `python/tests/test_variant_content_authoring.py` |
| Variant iteration helpers — `variant_sets()` / `variant_names()` / `variant_selection()` | (this commit) | `python/tests/test_variant_iteration.py` |
| `Prim.set_attribute_at_time` dtype propagation audit (color3f / point3f / matrix4d / quatf / texCoord2f / matrix4d) | `2c8b9e02` | `python/tests/test_timesample_dtype_propagation.py` |
| Stage typed-metadata setters: `set_up_axis`, `set_meters_per_unit`, `set_time_codes_per_second`, `set_frames_per_second`, `set_start_time_code`, `set_end_time_code` | `d7250cdf` | `python/tests/test_stage_metadata_typed_setters.py` |

### Medium-value additions

| Task | Commit | Tests |
|---|---|---|
| `texCoord2f/2d/3f/3d` scalar + array constructors and dispatch | `9f8c5475` | `python/tests/test_texcoord_authoring.py` |
| `frame4d` scalar/array writer + Python dispatch | `ad3a910d`, `0673f385` | `python/tests/test_authoring_extended_types.py` |
| `int64[]` / `uint64[]` / `bool[]` / `half[]` dtype dispatch | `a09a0a4a`, `ef1022f6` | `python/tests/test_array_dtype_dispatch.py` |
| `numpy.float16` / `float32` / `float64` ndarray accepted by `set_attribute(..., dtype="half[]")` (buffer-protocol fast path) | (this commit) | `python/tests/test_numpy_half_buffer.py` |
| No-dtype auto-detect: 1-D `numpy.float16` / `float32` / `float64` ndarray routes to `half[]` / `float[]` / `double[]` without an explicit `dtype=` hint | (this commit) | `python/tests/test_numpy_no_dtype_autodetect.py` |
| `kind` on Relationship/Attribute (USDA-only): pxr-style tolerance — parser accepts and pretty-printer round-trips via generic AttrMetas storage | (this commit) | `python/tests/test_kind_on_relationship.py` |
| Reference customData: USDC writer now emits arbitrary value types (doubles, int64 > 2^47, mixed) by reserving the dict frame, packing values (which may write out-of-line value data), and back-filling the frame — no more inline-only restriction | (this commit) | `python/tests/test_reference_customdata.py` (3 new cases) |
| `python/AUTHORING.md` — prose around when each `set_attribute` form is the right call (dtype-hint table, vec-array shapes, time samples, composition arcs, variants, stage metadata, USDZ packing) | (this commit) | _docs only_ |
| `tydra::GetPropertyNames` / `lookup_in_props` fall through to `Shader::value` (`ShaderNode::props`) when `Shader::props` is empty | `5a9e7055` | `python/tests/test_generic_shader_props.py` |
| `Attribute.value.to_string()` for asset paths emits `@…@` (was `@@@@…@@@@`) | (in earlier WIP, fenced by) | `python/tests/test_asset_path_normalization.py` |

### Parser / spec gaps closed

| Task | Commit | Tests |
|---|---|---|
| `allowedTokens = [...]` attribute meta — USDA parser, USDC writer field-emit, and USDC reader handler all wired (full USDA→USDC→USDA round-trip) | `6412505d`, (this commit) | `python/tests/test_allowed_tokens.py` |
| `customData = {…}` clause on `Reference` (USDA parser + USDC writer/reader; rejects on Payload) | `e1b56880`, `31516d6a`, `a1971db3` | `python/tests/test_reference_customdata.py` |
| `sceneName` (USDZ scene-library extension) typed as `string` end-to-end (was `token` in writer + Python C-API, causing silent drop and reload errors) | `1f4a7edb`, follow-up | `python/tests/test_scene_name_metadata.py`, `tests/usda/sceneLibrary-001.usda` (now passes `usdc-roundtrip-test`) |
| `customData` value packer covers `bool[]`, `uint32[]`, `int64[]`, `uint64[]`, `half[]` (was rejecting any prim authoring `bool[] zUp = [1]`) | `1f4a7edb` | `python/tests/test_customdata_array_types.py`, `tests/usda/customData-prim-003.usda` |

### Polish

| Task | Commit | Tests |
|---|---|---|
| `Stage.__repr__` surfaces `defaultPrim` / `upAxis` / `metersPerUnit` when authored; `Prim.__repr__` includes `children=N` | `cb23b1e6` | `python/tests/test_repr_polish.py` |
| USDZ packing with extra assets via `Stage.save(path, assets={archive_name: bytes})`; `tinyusdz.rewrite_asset_paths(stage, mapping)` helper to retarget asset paths before packing. Output is uncompressed Store-only ZIP, 64-byte aligned, root layer first. | (earlier) | `python/tests/test_usdz_packing.py` |

### Bug fixes (2026-05-03 / 2026-05-04)

| Task | Commit | Tests |
|---|---|---|
| `Prim.children()` segfault on orphan prims — `make_prim()` called `Py_INCREF(owner)` unconditionally; for a Python-authored Prim that hasn't been added to a Stage, `owner` is NULL. Switched to `Py_XINCREF` (dealloc was already `Py_XDECREF`). | `71019566` | `python/tests/test_prim_children_api.py` |

### Test coverage expansion (2026-05-03 / 2026-05-04)

Net new Python tests: **1057 → 1449** (+392 across ~80 new test
files, all green; no C++/binding source changes besides the
`Prim.children()` fix above). Areas now fenced:

- Schema-typed primitives (Sphere/Cube/Cone/Cylinder/Capsule/Plane)
  with xformOps; Mesh topology incl. creases/corners/holes/
  orientation; BasisCurves variants (bezier/linear/catmullRom,
  periodic wrap); NurbsCurves order/knots/ranges
- usdGeom: indexed primvars, primvar interpolation modes
  (constant/uniform/vertex/faceVarying), extent on Boundables,
  primvars:displayColor / displayOpacity, custom-namespace primvars,
  GeomSubset face/familyName/familyType
- usdLux variants: DomeLight + texture, DistantLight angle,
  RectLight w/h, CylinderLight, DiskLight, ShapingAPI cone/focus
- usdShade: multi-node graph (UsdPreviewSurface +
  UsdUVTexture + UsdPrimvarReader_float2 chains), normal map,
  emission, wrap modes, sourceColorSpace,
  fallback/scale/bias (USDA fences for the inputs USDC drops),
  material binding full/preview/strength
- usdSkel: SkelRoot + Skeleton with joints/bind+rest transforms,
  SkelAnimation translations/rotations/scales, blendShape weights,
  SkelBindingAPI primvars; multi-joint animations
- usdPhysics: PhysicsScene, RigidBodyAPI/MassAPI, CollisionAPI,
  RevoluteJoint, PhysicsMaterialAPI, FilteredPairsAPI
- usdRender: RenderSettings resolution/aspect, RenderProduct +
  RenderVar wiring, includedPurposes, camera rel
- PointInstancer: protoIndices, velocities, orientations, scales,
  invisibleIds
- Composition: `over`/`class` specifiers (def/over/class incl.
  nested), references/payload with explicit prim_path, internal
  refs (no asset), multi-element ref lists, prepend payload, layer
  offset/scale (USDA fence), listOp qualifiers (prepend/append
  inherits, prepend specializes, apiSchemas combos, delete
  references USDA fence); Python authoring via `add_reference`/
  `add_payload`/`add_inherit`/`add_specialize`/`clear_*`
- Variants: variantSet with attr overrides, multi-variantSet,
  variant containing child prim, `define_variant` +
  `variant_set_attribute` + `variant_add_child` Python authoring,
  `variant_names`/`variant_sets` iteration,
  `clear_variant_selection`
- Metadata: visibility/purpose/kind/hidden/active/instanceable
  imageable meta, assetInfo (identifier, name, version, payload
  deps), xformOpOrder edge cases (`!resetXformStack!`,
  `!invert!xformOp`, suffixed names, rotateXYZ, transform matrix,
  scale/rotateX/Y/Z, orient quat); subLayers; layer-level meta
  (defaultPrim, doc, customLayerData); customData nested dicts /
  string[] / multi-type / on attributes
- Time samples: negative/fractional/very-large/single-key
  timecodes, stage timecode meta (start/end/timeCodesPerSecond,
  negative startTimeCode), default+timeSamples coexist on same
  attribute, time-sampled vec3/quat/color3f/int via Python
  `set_attribute_at_time` + `get_attribute_timesamples`
  round-trip
- Strings & tokens: unicode (Japanese), emoji, escaped quotes,
  backslashes, triple-quoted multi-line, empty string, string[]
  arrays; token/token[] with namespaced colon values, slash-bearing
  values, empty arrays
- Numbers: small floats (1e-6), negative zero, double precision
  (>7 sig figs), int32 min/max, uint32 large, scientific notation,
  int64 full range; bool words/numeric/array
- Role-typed scalars and arrays: color3f/3d/4f, normal3f[],
  point3f vs vector3f, texCoord2f[]; matrix2d/3d/4d scalar+array
  buffer protocol (with USDC matrix2d-inflate USDA fence noted)
- Relationships: multi-target lists, prepend/append qualifiers,
  property-path targets, single attribute `.connect`,
  `get_relationship_targets` API, `add_relationship` round-trip
- Stage / module API: typed setters
  (set_up_axis/meters_per_unit/timeCodesPerSecond/frames_per_second
  /start/end_time_code/default_prim), generic `get_metadata`/
  `set_metadata`, `tinyusdz.loads`/`load_bytes`/`is_usd`/
  `detect_format`, `tinyusdz.traverse`, `Stage.visit_prims` with
  callback exception propagation
- tydra: `convert_to_render_scene` with mesh/camera/light
  extraction (RenderMesh.points dtype/face_vertex_counts via
  numpy buffer, RenderCamera.focal_length/aperture, RenderLight.
  intensity/color), `list_prims_by_type`, `tydra.visit_prims`
- Buffer protocol: scalar Value `as_scalar()`, array
  `np.asarray(value)` for float[]/double[]/int[]/float3[]/
  double3[]/int2[]/color3f[] with shape and dtype assertions;
  matrix4d scalar to_string round-trip
- Save/load: format dispatch by extension (.usda/.usdc/.usd/.usdz),
  PXR-USDC magic, ZIP_STORED enforcement, repeated save
  idempotency, USDA→USDC→USDA stable, multi-stage isolation
- Stress: 1k int[] / 10k float[] / 100-element string[], 200 root
  prims, 30-deep hierarchy
- Errors / negative paths: unsupported value type to set_attribute,
  invalid dtype fallback, missing-file load, invalid-USDA loads,
  invalid save path, get_attribute on nonexistent name returns
  None
- API surface: Prim children() (after fix), set_element_name +
  rename persistence, to_string/repr, Attribute.value/name/repr/
  type_name (after load), property_names, api_schemas (single/
  multi/repeats/CollectionAPI multi-instance), apply_api_schema
  USDC round-trip
- Empty / minimal stages, in-memory string round-trip
- USDZ packing with embedded PNG via assets dict, ZIP_STORED
  enforcement (no DEFLATE per USDZ spec)
- End-to-end: full Python-authored scene (Mesh+Camera+Light under
  World Xform) saved as USDC, reloaded, render scene extracted

### Known gaps closed

(Followups since the 2026-05-04 coverage push.)

| Gap | Fix | Tests |
|---|---|---|
| `matrix2d` USDC inflated to `matrix4d` | reader read sizeof(matrix2d) bytes into a `value::matrix4d`; switched to `value::matrix2d` | `test_matrix_roundtrip.py::test_matrix2d_usdc_round_trip` |
| `skel:skeleton` / `skel:blendShapeTargets` dropped on Mesh through USDC | `sconv-geom.cc::ExtractMeshProperties` had stub `if (mesh->skeleton) {}`; now calls `ConvertRelationshipToFields` | `test_skel_topology.py::test_skel_root_with_skeleton_rel_usdc` |
| `collection:<inst>:{includes,excludes}` dropped on GPrim through USDC | `stage-converter.cc` writer iterates `Collection::instances()` (GPrim mixin) and re-emits as relationships before the props-map fallback. Added `GetPrimCollection()` helper. | `test_collection_api.py::test_collection_api_applied_with_instance_name_usdc` |
| `Prim.variant_selection()` had no zero-arg form | Made `vset` arg optional; with no args returns `dict[str, str]` of all `{variant_set: selection}` pairs by enumerating the metas `variants` map. New C API `c_tinyusd_prim_get_variant_selection_keys`. | `test_variant_selection_dict.py` |
| `Prim.children()` segfault on orphan prims | `Py_INCREF(owner)` → `Py_XINCREF(owner)` (NULL owner = orphan) | `test_prim_children_api.py` |
| `Camera.focalLength.timeSamples` (and other animated camera scalars) dropped on USDC | `sconv-geom.cc::add_float_attr/add_double_attr` now emit `<name>.timeSamples` from the Animatable<T> | `test_camera_attributes.py::test_camera_animated_focal_length_usdc` |
| `Mesh.normals.timeSamples` dropped on USDC | `sconv-geom.cc::ExtractMeshProperties` mirrors the `points.timeSamples` block for `normals` | `test_timesamples_array_comprehensive.py::test_normal3f_array_timesamples` |
| `Cylinder.axis` (uniform token) dropped on USDC | `sconv-geom.cc::ExtractCylinderProperties` was missing the axis emit Cone/Capsule had | `test_primitive_shapes.py::test_cylinder_radius_height_axis` |
| Schema-typed AttrMeta routing asymmetry (elementSize, customData, displayName, displayGroup, documentation, hidden on Mesh.points/normals) | Generic `<base>.<meta_key>` suffix router in `stage-converter.cc` (kAttrMetaSuffixes); `sconv-geom.cc` emits the keys for points/normals AttrMeta | covered by `test_attribute_metadata_extras.py` plus inline checks |
| `CollectionAPI:<inst>:expansionRule` / `includeRoot` typed attrs dropped on USDC | `stage-converter.cc` Collection re-emit pass now constructs `Attribute::Uniform(token)` / `Attribute::Uniform(bool)` and calls `ConvertAttributeToFields` | `test_collection_api.py::test_collection_includeRoot_usdc`, `::test_collection_expansionRule_usdc` |
| Multi-target attribute `.connect = [</a>, </b>, ...]` rejected by parser | `ascii-parser-props.cc` connection branch now handles `[`-led path arrays and routes them to `Attribute::set_connections(vec)`. Writer side (`ConvertConnectionToFields`) already had the multi-target branch. | `test_multi_target_connect.py` (4 cases incl. USDC round-trip) |
| UsdUVTexture inputs `fallback`/`scale`/`bias` dropped on USDC | reconstruct path didn't have `PARSE_TYPED_ATTRIBUTE` calls for these typed `color4f`/`float4` inputs — they fell into `texture->props` instead of the typed `texture->fallback/scale/bias` fields the writer reads. Added the three missing `PARSE_TYPED_ATTRIBUTE` lines to `prim-reconstruct-shader.cc`. | `test_uv_textures_extra.py::test_uvtexture_fallback_and_scale_usdc`, `::test_uvtexture_bias_usdc` |

### Still outstanding gaps

No major gaps remain. Everything from the original "Still
outstanding" list has been closed. Future work that may surface
during further coverage expansion would go here.

## Still outstanding

### Medium value

### Low value / nice to have

#### Half-array reader buffer protocol

Already works (`Value.__buffer__` returns half-precision arrays as
numpy `float16`). Listed here because the previous snapshot flagged
it; closing it out as resolved.

## Out of scope (or larger projects)

These are noted to keep the discussion focused but should be tackled
as their own design exercises rather than rolled into this list.

- USDZ packaging integrity for external textures (writer side).
- MaterialX validation completeness.
- Full pxr-USD `defaultPrim` / `startTimeCode` validation semantics.
- Layer-offset support on `subLayers` as opposed to references /
  payloads (the parser currently consumes the syntax for refs /
  payloads only).
