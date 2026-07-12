# tusdview + tusdrender audit (2026-07-12)

Deep review of the two renderers that consume tydra / tydra-next, across four
lenses: correctness, robustness on malformed USD, backend/tool parity, and
performance/memory. Method: seven parallel subsystem reviewers, each finding
carried a concrete failure scenario and was checked against the code (many were
reproduced against the live Vulkan-headless / hardware-GL binaries). Findings
reported by two independent reviewers are marked **[×2]** — highest confidence.

This started as a findings report for triage. After review, the high-severity
findings were fixed where the change was contained; see **Fix status** below.
The regression-suite work is in the last section.

## Fix status (2026-07-12)

Fixed, each with a pinning regression test that fails against the old behavior:

- **R5** — `-frames` FRAMESPEC bounded (cap + sub-ULP stride reject).
  `tool-tusdrender-frames-framespec`.
- **R2** — tusdrender next path honors the bound texture's `uv_primvar` (targeted
  fix: the mesh's `st` selection follows the base-color texture's UV set; the full
  multi-set-per-mesh rework is not needed for real assets).
  `tool-tusdrender-uv-set-routing`.
- **R4** — textureless DomeLight synthesizes a constant environment and
  illuminates. `tool-tusdrender-textureless-dome`.
- **R3** — IBL specular uses the bounded split-sum in bsdf mode (no low-roughness
  blow-up). `tool-tusdrender-ibl-specular-bound`.
- **T4** — normal maps never compress to BC1/BC3 (go to BC7); usage classification
  now runs before compression on every load path.
  `tusdview_texture_pipeline_test` (extended).
- **T2** — per-frame skin/morph GPU uploads route through `postGpu()` so they run
  on the render thread on the threaded path. Verified single-threaded (headless
  skinning + the hardware skinning tests still pass); the threaded path is correct
  by construction and not reproducible headless.
- **T1** — non-instanced blendshapes now animate on the `--next` loader: a morphed
  mesh is emitted standalone (object-space vertices + world in `dm.world`, morph
  built, `absPath` preserved), like `EmitInstancedProto` de-instances a morphed
  prototype, instead of being world-baked into an absPath-less shared batch.
  `tusdview-noninstanced-blendshape`.

- **R1** — per-face GeomSubset materials render on the tusdrender next path.
  `ExpandGeomSubsetJobsNext` splits a subset-bound mesh into one mesh-job per
  bound subset (an authored-face mask honored by `AddRTPreviewMeshNext`, and the
  GeomSubset prim as the binding source — `GetInheritedBoundMaterialPath` on it
  gives exact UsdShade semantics) plus a remainder job, so the existing
  one-material-per-job streaming concat needs no change.
  `tool-tusdrender-geomsubset-materials`.
- **T3** — per-vertex `displayColor` renders on the Vulkan raster backend.
  Rather than a new vertex-input binding (which would need a white fallback
  buffer per colorless mesh), `mesh.vert` fetches the color by `gl_VertexIndex`
  from a set-24 SSBO — the same dummy-descriptor pattern as the morph sets 7–9,
  gated by push-constant flag `ids.w & 32` — and `mesh.frag` multiplies it into
  the base color (and the Albedo AOV), matching GL's attrib-9 path. The SSBO is
  the pre-existing `vtxColorBuf`, now created regardless of RT support; the tess
  path outputs white. SPIR-V regenerated for `mesh.vert`/`mesh.frag`/
  `mesh_tess.tese` only. `tusdview-vertex-color`.

Severity key: **critical** (crash/DoS or silent data loss on common assets) ·
**high** (wrong render on a common asset, or a crash on hostile input) ·
**medium** (wrong on a less-common asset, or a parity gap) · **low** (edge case).

---

## Confirmed findings — tusdview

### T1. [high] Non-instanced blendshapes are frozen on the default (`--next`) loader  **[×2]**
`examples/tusdview/next_scene_loader.cc` — `BuildMorphChannelsNext` /
`BakeBlendShapes` are called **only** from `BuildProtoMesh` (the instanced-
prototype path, the single call site is line 1350–1352). The non-instanced
batched-mesh loop (~3037–3275) builds skinning but never any morph, so
`morphChannelCount` stays 0 and the blendshape is silently discarded.
- **Scenario (empirically confirmed, both reviewers):** a plain SkelRoot → one
  Mesh with `skel:blendShapes` + animated `blendShapeWeights` renders at rest
  pose at every time on the default loader. A/B at weight 0 vs 1 (a blendshape
  translating the mesh +12u): `--legacy-load` → screenshot maxdiff 222/255;
  default `--next` → maxdiff **0**. Skinning itself animates on `--next`, so the
  loss is morph-specific. Even if channels were built, batching never sets
  `dm.absPath`, so `BuildNextMorphWeights`'s `ResolveBlendWeights(GetPrimAtPath(
  dm.absPath))` (line 2159) could not resolve weights anyway.
- **Fix:** treat morphed meshes like the instanced path — keep them out of the
  multi-prim batch (own `DrawMeshCPU`, preserve `absPath`) and call
  `BuildMorphChannelsNext`, or `BakeBlendShapes` before the world-bake.
- **Test:** headless golden rendering a non-instanced animated blendshape at two
  times, asserting the frames differ (mirrors `run-inst-morph-cull.sh`, which
  only covers the instanced path).

### T2. [high] Per-frame skinning/morph GPU uploads bypass the render-thread queue
`examples/tusdview/app.cc` — `updateGpuSkinningFrameIfNeeded` (1264) and
`updateNextDeformFrameIfNeeded` (1334) call the renderer directly
(`uploadSkinningFrame`, `updateMeshVertices`, `updateMorphWeights`,
`updateMeshWorld`) on the **main thread**, not through `postGpu()` (which the
scene upload at 676 correctly uses). Under `--threaded` (windowed GL/VK) the
render thread owns the GL context and the main thread has released it
(`glfwMakeContextCurrent(nullptr)`, 1982).
- **Scenario:** `tusdview character.usda --threaded --skinning gpu` (windowed
  GL) → the bone-texture / morph-coeff GL calls run on a thread with no current
  context, so the mesh stays in rest pose or the app GL-errors. Threaded Vulkan:
  buffer writes race the render thread's submits (validation error / hang).
- **Fix:** route these uploads through `postGpu([...]{...})`, or force CPU
  skinning while `renderThreadActive_`.
- **Test:** windowed `--threaded` on a skinned model on a real display; not
  reproducible headless (`renderThreadActive_` requires `!headless_`).

### T3. [high] Vulkan raster ignores per-vertex `displayColor`
GL multiplies base color by the per-vertex color (`examples/common/light3d/
material.cpp:479`, attrib 9 bound in `gl_renderer.cc:1669`), but the Vulkan
non-instanced pipeline never binds/consumes `DrawMeshCPU::vertexColors`
(`vk/shaders/mesh.frag:309` uses `pc.baseColor.rgb` only; `vtxColorBuf` is fed
only to the RT MeshDesc, `vk_renderer.cc:5374`).
- **Scenario:** a vertex-painted mesh (`primvars:displayColor`, vertex interp) with
  no base-color texture: GL raster and VK-RT show the painted colors, VK raster
  shows a flat material color. Albedo AOV (mode 7) diverges identically.
- **Fix:** add a per-vertex color vertex binding to `mesh.vert`/`mesh.frag` and
  bind `gm.vtxColorBuf` in the non-instanced draw loop.

### T4. [high] Normal maps / packed ORM maps are compressed as generic color (BC1)
`examples/tusdview/mesh_build.cc` — `ChooseCompressedFormat`/`EncodeBCn`
(864–968) pick the block format from opacity alone and never return BC5, and the
usage tagging (`isNormalMap`/`channelOp`) is done in `FinalizeDrawTextures`,
which runs *after* `ApplyTextureCompression` on both load paths — so the
classification can't even inform the choice.
- **Scenario:** a tangent-space normal map with `--texture-compress bc` →
  `EncodeBCn` sees `alpha==255` → BC1. BC1's 2-endpoint RGB line can't hold
  independent X/Y, so normals cross-contaminate and lighting/specular is visibly
  wrong. Same for an ORM map (AO/rough/metal collapse onto one BC1 color line).
  BC5 exists in `caps.bc5` but is reachable only through the kept-compressed KTX2
  passthrough, never this CPU re-encode.
- **Fix:** route `isNormalMap` textures to BC5 (or force BC7 under BCn), and
  classify usage before `ApplyTextureCompression`.

### T5. [medium] Instanced-prototype meshes carry material id 0 (default gray) in the RT scene
`examples/tusdview/next_scene_loader.cc` — `BuildProtoMesh` emits its submesh
with a hardcoded `DrawSubmesh{0, N, 0}` and never resolves the prototype's bound
material.
- **Scenario:** a PointInstancer whose prototype binds a red `UsdPreviewSurface`:
  `rt_scene_build.cc:218` reads `submeshMatId()` = 0 for the prototype and reuses
  it for all instances, so they all shade default gray with no texture. Raster is
  unaffected (flat instanced shader uses per-vertex displayColor); impact is the
  RT path and material-driven AOVs.
- **Fix:** resolve the prototype's bound material (`GetInheritedBoundMaterialPath`
  → `BuildNextMaterial`) and set the submesh `materialId` + GeomSubsets.

### T6. [medium] GeomSubset face bindings not remapped through mesh sanitization (`--next`)
`examples/tusdview/next_scene_loader.cc` — `buildTriMaterials` (2888–2932)
applies authored GeomSubset `indices` directly against the post-sanitize face
array without routing them through `RenderMesh::sanitize_face_remap` (the core
converter does, `render-converter.cc:3025`).
- **Scenario:** a Mesh with one malformed face that `SanitizeMeshTopology` drops,
  plus a face GeomSubset binding material M to authored faces {5,6,7}: the
  surviving faces get M shifted by the number of dropped faces → wrong triangles
  shaded. Clean meshes are unaffected; malformed input mis-shades silently.
- **Fix:** when `sanitize_dropped_faces > 0`, remap each authored subset index
  through `sanitize_face_remap` (skip −1) before indexing, as the core does.

### T7. [medium] Facing AOV (mode 8) is inverted on Vulkan
`vk/shaders/mesh.frag:198` and `mesh_inst.frag:88` read `gl_FrontFacing` for the
Facing AOV, but VK uses a Y-flipped (negative-height) viewport
(`vk_renderer.cc:6748`) that reverses winding vs GL. The shaded path avoids this
(uses N·V), the AOV does not.
- **Scenario:** RenderMode::Facing on a closed single-sided sphere: GL paints the
  visible surface green (front), VK paints it red (back).
- **Fix:** derive front/back from `dot(Ngeo, V)` in the AOV, as the shaded path does.

### T8. [medium] Vulkan never back-face culls; GL culls single-sided meshes  **[×2]**
All VK raster pipelines set `cullMode = VK_CULL_MODE_NONE`
(`vk_renderer.cc:1327/1475/1621`); GL culls back faces of non-double-sided
meshes (`gl_renderer.cc:2235`). (Also: authored `doubleSided` never reaches the
next RenderScene — the converter has no writer for `RenderMaterial::double_sided`
— so all three of GL-cull / VK-none / tusdrender-faceforward disagree.)
- **Scenario:** a single-sided translucent (Blend) plane from behind: GL culls it
  (invisible), VK draws it (visible, blended). tusdrender lights the backface.
- **Fix:** give the mesh pipelines a `VK_DYNAMIC_STATE_CULL_MODE` gated on
  double-sidedness; plumb `doubleSided` through the converter.

### T9. [medium] Unchecked `vkAllocateMemory` / `vkBindImageMemory` in every texture upload
`vk_renderer.cc:2735, 2888, 3039, 3139, 3224` (+ IBL creators) ignore the return
values and proceed to record copies / create views on a possibly-unbacked image.
- **Scenario:** a texture set exceeding the DEVICE_LOCAL budget: one alloc returns
  `VK_ERROR_OUT_OF_DEVICE_MEMORY`, bind fails, the subsequent
  `vkCmdCopyBufferToImage`/sample hits an unbound image → validation error /
  device loss (viewport black / abort) instead of the intended white-texture
  fallback.
- **Fix:** check both results; on failure destroy the image + staging and
  `return false` (the caller already treats false as "keep white slot").

### T10. [medium] Vulkan silently drops empty meshes, desyncing per-mesh indices
`vk_renderer.cc:4344` returns early on `vertices.empty() || indices.empty()`,
while GL keeps a placeholder slot (`gl_renderer.cc:1543`). So `meshes_` no longer
maps 1:1 to appendMesh order, and every per-mesh index (visibility mask,
highlight, `updateMeshWorld/Vertices`) is off.
- **Scenario:** an empty Mesh at index 0 ahead of real geometry: toggling
  visibility of mesh 1 in the GUI or selecting it for highlight affects the wrong
  mesh on Vulkan; GL is correct.
- **Fix:** push a zero-draw placeholder `VkMeshGPU` and guard the draw with
  `indexCount == 0`.

### T11. [medium] sRGB base-color textures not linearized in tusdview raster
tusdview uploads uncompressed textures as `_UNORM` with no shader `pow(2.2)` and
a non-sRGB framebuffer (`vk_renderer.cc:2716`; the `srgb` flag only picks resize
filter / block format). tusdrender applies `SrgbToLinear` on color slots.
- **Scenario:** a `sourceColorSpace="sRGB"` base-color map is lit in gamma space
  by tusdview but linear space by tusdrender; under non-unity lighting the
  mid-tones differ. (Normal/scalar maps agree — both treat them raw.)
- **Fix:** upload sRGB slots as `_SRGB` formats (or linearize in shader).

### T12. [medium] tusdview `--next` misses `inputs:opacity` texture, `displayOpacity`, specular workflow, IOR
`next_scene_loader.cc:2043–2076` reads only baseColor/metal-rough/emissive/normal
— no `opacity.texture_id`, no `use_specular_workflow`/`specular_color`/`ior`;
varying `displayOpacity` is stored but never sampled (3322).
- **Scenario:** an RGB diffuse + separate grayscale `inputs:opacity` mask with
  `opacityThreshold` → tusdrender cuts out, tusdview renders opaque. A
  `useSpecularWorkflow=1, ior=1.33` material → tusdrender honors it, tusdview
  falls back to dielectric IOR 1.5.
- **Fix:** read the opacity texture, sample `displayOpacity`, honor the specular
  workflow + ior.

### Lower-severity / other (tusdview)
- **[low-med] Legacy UDIM keeps tiles that fail to resize** — `mesh_build.cc:745`
  retains a wrong-sized tile (only a diagnostic); the `--next` path was just fixed
  to drop them (`LoadNextUdimTexture`). GL 2D-array leaves that layer black where
  `--next` shows the magenta sentinel → the two loaders differ.
- **[low-med] UDIM mixed-opacity under BCn falls back to uncompressed** —
  `mesh_build.cc:909` picks BC1/BC3 per tile from that tile's alpha, so a mixed
  set produces mixed block sizes and the GL array uploader bails to uncompressed
  (`gl_renderer.cc:1049`), silently negating `--texture-compress bc` and wasting
  the encode. Fix: one format for the whole tile set.
- **[low-med] Kept RGBA mip chain wasted for compressed textures** —
  `FinalizeDrawTextures` retains the full uncompressed `mipImages` alongside the
  compressed `compressed.mips` that the uploaders actually use; ~21 MB/4K texture
  of dead CPU RAM, regenerated on reload. Fix: clear `mipImages` after emitting
  the compressed mips.
- **[low-med] `--next` metal/rough drops texture scale/bias and can't hold
  separate metallic + roughness images** — `next_scene_loader.cc:1990`; tusdrender
  applies both. Common packed-ORM case still matches.
- **[low] RT+`--next` skinning is baked once at load, never re-posed per frame** —
  `app.cc:1188` disables per-frame next deform under RT; the legacy RT path
  re-poses via `BuildRtSkinnedMeshVertices`. `--rt` (next) freezes a skinned mesh
  at the load-time pose; `--rt --legacy-load` animates.
- **[low] Normal mapping math differs GL vs VK** — GL adds `tangentNormal*0.1`
  (weak, no TBN), VK uses a full derivative TBN; relief is pronounced on VK,
  nearly flat on GL. Normals AOV diverges too.
- **[low] Default Shaded diffuse differs GL vs VK** — VK is hard Lambert on the
  key light (shadow side → dark ambient floor), GL is a soft headlight +
  half-Lambert. Only the ambient term was matched.
- **[low] Wireframe render mode renders solid on Vulkan** — VK `mesh.frag` has no
  mode==1 case (may be by design; VK has no wireframe pass).
- **[low] UDIM sRGB flag / NaN texel** — `mesh_build.cc:1454` lets the last decoded
  UDIM tile decide the whole array's sRGB flag; `clamp8` (515) leaves a NaN float
  texel as an arbitrary byte.

---

## Confirmed findings — tusdrender

Dispatch fact that shapes severity: `tusdrender.cc:837` takes the tydra-next
(`RunRTPreviewNext`) path only for `.usdc` inputs **or** `-rtPreview`/`-mmapRt`;
a plain `.usda`/`.usdz` with no flags, and any `-subdivisionLevel` render, takes
the **legacy** tydra `RenderScene` path (`tusdr_legacy.cc`).

### R1. [high] GeomSubset per-face material bindings dropped by the next path  **[×2]**
`tusdr_next.cc:1654` (`ResolveMeshMaterialCached` resolves one whole-mesh
material) + `:499` (stamped on every triangle). No `GeomSubset`/
`familyName=="materialBind"` traversal exists in the next path; the legacy path
handles it (`tusdr_legacy.cc:400`). The converter computes correct
triangle-space subsets (`render-converter.cc:3050`) that **neither** next path
reads (tusdview re-derives, tusdrender ignores).
- **Scenario (empirically confirmed, both reviewers):** one 18-tri mesh, three
  `materialBind` GeomSubsets → Red/Green/Blue, no mesh-level binding. `-rtPreview`
  (next) → uniform gray `(191,191,191)` (default fallback), red/blue = 0.
  `-legacyLoad` → red 5490 / blue 5490 px (correct). This is the default path for
  every `.usdc`, and it disagrees with tusdview (which does resolve subsets).
- **Fix:** consume `RenderMesh::material_subsets` (already triangle-space) when
  assigning per-triangle `TriMat`.

### R2. [high] Per-texture UV-set selection (`uv_primvar`) ignored by the next path  **[×2]**
`tusdr_next.cc:281` (`AddRTPreviewMeshNext`) reads `primvars:<name>` from a fixed
preference list and takes the first (always `st`); `RenderTexture::uv_primvar` is
never consulted. tusdview honors it (`next_scene_loader.cc:1911`).
- **Scenario (empirically confirmed):** `models/multi-uv-quad.usda` — base-color
  bound through `UsdPrimvarReader varname="uvSet1"`. tusdrender's `st` and
  `uvSet1` renders are byte-identical (maxdiff 0 — it samples `st` regardless);
  tusdview's differ. This is the exact regression `multi-uv-quad.usda` was
  authored to catch, uncaught for tusdrender.
- **Fix:** select the UV primvar by the bound texture's `uv_primvar` (or consume
  the converter's promoted `texcoords_0/1` + `_name`).

### R3. [high] IBL specular blows up at low roughness in `-materialShading lightrt-bsdf`
`tusdr_integrator.cc:393` (`EvalMaterialIblSpecular`) evaluates the analytic
microfacet BRDF at the exact mirror direction × prefiltered radiance × NdotL,
instead of the bounded split-sum `spec_env·(F0·A + B)`. At `wi = reflect(view)`,
`NdotH = 1` so `D = 1/(π·alpha)` → unbounded as roughness → 0.
- **Scenario:** a sphere, specularRoughness 0.05, any envmap DomeLight, bsdf mode:
  `alpha ≈ 4e-4` → `D ≈ 796` → the surface reflects the environment at ~280× its
  radiance (blown-out white). The legacy split-sum path renders ~env radiance.
- **Fix:** use the split-sum form (prefiltered radiance × BRDF-LUT) in bsdf mode.

### R4. [high] Textureless DomeLight renders black on the tydra-next path
`tusdr_next.cc:4376` — `BuildIblFromEnvNext` returns false when the dome has no
`inputs:texture:file`, and `CollectLightsNext` never adds it as a light nor sets
`has_dome`/`env_color`. The textureless-dome handling exists only on the legacy
collector.
- **Scenario (empirically confirmed):** a white quad + a textureless colored
  DomeLight (intensity 1), `-materialResolver tydra-next` → the quad renders
  `(1,1,1)/255` (≈black) instead of uniformly lit. A constant-color environment
  fill — a very common setup — renders black.
- **Fix:** synthesize a 1×1 constant EnvImage from `color·intensity`, or push the
  dome into `LightCache` (`has_dome`, `env_color`).

### R5. [high] `-frames` FRAMESPEC is unbounded → OOM / non-terminating
`tusdr_next.cc:4608` (`ParseFrameSpec`): `for (t=start; t<=end+1e-9; t+=stride)
times->push_back(t)` with no cap on `times->size()` and `stride` guarded only
against exact 0.
- **Scenario (empirically confirmed):** `-frames 0:100000000` → killed at the 10s
  timeout still enumerating; `-frames 0:1000x1e-320` never terminates (`t += tiny`
  with large `t` doesn't progress).
- **Fix:** cap `times->size()` and reject `stride < DBL_EPSILON·|range|`.

### R6. [medium] `-frames` silently ignored on the default (`.usda`/`.usdz`, non-`-rtPreview`) path
Consumed only in `RunRTPreviewNext` (`tusdr_next.cc:4664`); the legacy render
path never reads `opt.frames`, and the `#`/`####` token is never substituted.
- **Scenario (empirically confirmed):** `tusdrender scene.usda anim.####.png
  -frames 1:24` → one file named literally `anim.####.png`, no animation. The help
  text advertises `-frames` with no caveat.
- **Fix:** honor `-frames` on the legacy path, or error when it can't take effect.

### R7. [medium] Legacy shaded path ignores `purpose` (the default path for `.usda`)
`tusdr_legacy.cc:496` never sets `TriInfo::purpose_bit`, so `-purpose`,
`-hideProxy`, `-hideRender`, `-showGuide` are no-ops and guide/proxy geometry is
always drawn. The next path filters correctly.
- **Scenario:** a `purpose="guide"` Mesh in a `.usda` with no flags → legacy draws
  it (spec: guide isn't rendered by default); the same as `.usdc` hides it.
- **Fix:** resolve inherited purpose in the legacy collector and stamp
  `purpose_bit`.

### R8. [medium] Legacy path drops constant `inputs:opacity` / `opacityThreshold` / `displayOpacity`
`tusdr_legacy.cc:528` binds only the opacity *texture*; `tri.opacity` /
`opacity_threshold` keep defaults. The next path resolves them.
- **Scenario:** a UsdPreviewSurface with `inputs:opacity=0.15` (no texture) →
  legacy fully opaque, next 85% transparent. Cutout via `opacityThreshold` →
  legacy solid, next masked.

### R9. [medium] tydra-next resolver hard-codes sRGB per slot, ignoring `source_color_space`
`tusdr_next.cc:1315` (`LoadRenderTexture` takes a literal `bool srgb`; callers
pass `true` for color slots) never consults `RenderTexture::source_color_space`;
the legacy-fallback resolver does read `sourceColorSpace`.
- **Scenario:** a base-color map authored linear with `sourceColorSpace="raw"` →
  the next resolver sRGB-decodes it a second time (0.5 → ~0.21); `-materialResolver
  legacy` renders it correctly → the two resolvers disagree silently.

### R10. [medium] Analytic-light `normalize` and dome `texture:format` ignored (vs tusdview)
`tusdr_lighting.cc:448` — `normalize` is honored only for mesh/area lights; a
SphereLight is always `÷ πr²` (`integrator.cc:312`), rect/disk always `× area`.
DistantLight `angle` is unused; dome `texture:format` always sampled as latlong.
- **Scenario:** a SphereLight radius 2, `normalize=false`: tusdview emits
  `color·intensity`, tusdrender `÷ π·4` → ~12.6× brightness gap. A `mirroredBall`
  dome uses the wrong angular mapping in tusdrender only.
- **Fix:** honor `RenderLight::normalize` per shape; add MirroredBall/Angular dome
  mapping.

### R11. [medium] No upper bound on width/height/samples; `-fitScale abc` aborts
`tusdr_args.cc:240/246/309` accept width/height/samples up to `INT_MAX` (only
`>0`); the framebuffer `resize(w*h*4)` and `for (s<spp)` are uncapped, and a
hostile camera aperture can overflow the auto-derived `int` height
(`tusdr_next.cc:2360`). Separately, `-fitScale` uses throwing `std::stof`
(`tusdr_args.cc:258`) with no try/catch in `main`.
- **Scenario (empirically confirmed):** `-fitScale abc` → `std::invalid_argument`
  → SIGABRT (exit 134). `-width 2000000000` or a camera
  `horizontalAperture=1e-6, verticalAperture=1e30` → OOM. No `kMaxDecodedImageBytes`-
  style cap like the core.
- **Fix:** clamp width/height/samples to sane maxima; validate the derived height;
  use `ParseFloatStrict` for `-fitScale`.

### R12. [low] `visibility="invisible"` and `doubleSided=false` culling honored by neither path
Next checks only `IsActive()` (`tusdr_next.cc:1865`); legacy ignores visibility
(`tusdr_legacy.cc:579`); the integrator faceforwards every hit
(`integrator.cc:1062`), so `doubleSided=false` never culls.
- **Scenario:** a `visibility="invisible"` prim still renders on both paths.

### Lower-severity / other (tusdrender)
- **[low] Default gray + roughness floor differ between paths** — legacy 0.18 gray
  + roughness floor 0.02; next 0.55 gray + floor 0.0 → different preview
  brightness for unmateraled meshes.
- **[low] `-materialShading lightrt-bsdf` drops the base texture** —
  `openpbr.baseColor` keeps the constant `diffuseColor` and is never reset to
  white when a base texture loads (`tusdr_next.cc:1463`), so a textured surface
  renders flat in bsdf mode.
- **[low] MIS under-counts sphere light when the BSDF partner is skipped** —
  `nee_sphere` runs with a power-heuristic weight <1 even when
  `sample_bsdf_bounce` early-returns (`depth>=2` or `opacity<0.999`), so no term
  supplies the missing weight → up to ~2× too dark on `opacity=0.5` / 2nd-bounce
  surfaces.
- **[low] Beer-Lambert transmission color dropped for `transmissionDepth>0`** —
  `bsdf_sample` defers the tint to the tracer, but `transmission_medium()` is
  called nowhere → colored glass renders clear.
- **[low] Diffuse IBL includes the specular lobe** — `EvalMaterialIblDiffuse`
  evaluates the full BSDF at `wi=N` × π, so diffuse IBL is slightly too bright /
  F0-tinted.
- **[low] SphereLight radius clamp mismatch** — radiance clamps `r` to 1e-4 but
  sampling/pdf use raw `r`; a `5e-5` sphere renders 4× too dark.
- **[low] Texture cache key collides on per-channel scale/bias** —
  `LoadTextureCached` keys only `scale.x`/`bias.x` (`tusdr_next.cc:736`).

---

## Cross-tool parity — the missing oracle

Neither tool has a test that renders the **same** scene through **both** binaries
and asserts agreement, which is exactly why R1, R2, R4 (all tusdrender-next-only,
all silent) went unnoticed. The single highest-value addition is a cross-tool
parity fixture + harness — see the proposal in the regression section. The
converging parity gaps: GeomSubset materials (R1/T-none), UV-set routing (R2),
double-sidedness (T8), sRGB color (T11/R9), light `normalize` (R10).

---

## Coverage gaps (behaviors no registered test exercises)

- **GeomSubset per-face materials** — neither tool.
- **The entire tusdrender legacy shaded path** — no test passes `-legacyLoad`.
- **`-frames`** — no test at all (parse bounds, default-path no-op, numbered output).
- **Purpose *filtering* / visibility** — `run-purpose-filtering.sh` enables all
  purposes and only counts stats.
- **Colorspace (`sourceColorSpace="raw"`)** — neither tool.
- **Multi-UV per-texture selection** — tusdview covered (`tusdview-uv-set-routing`),
  tusdrender not.
- **Double-sided / winding / backface** — neither tool.
- **CLI robustness** (negative/huge/non-numeric args, hostile camera) — none.
- **Lights** — tusdview none registered (rich `lighting_test.cc` was orphaned,
  now registered); tusdrender only `sphere-light-nee`.
- **White furnace / energy conservation, PDF-eval consistency, MIS weight sum** —
  none (would catch R3, R4, and OpenPBR energy gain).

---

## Regression suite — landed alongside this audit

- **Registered 12 built-but-unregistered test binaries** (they ran in no CI, all
  pass): the 6 `tusdview_*_test` C++ tests (`examples/tusdview/CMakeLists.txt` had
  *zero* `add_test`) and the 6 `textools_*` tests (lost from CMake in a merge). To
  make the subdirectory's `add_test` take effect, `enable_testing()` is now called
  at the top level before the example subdirectories are added. Full `ctest`:
  79/79 pass (was 67), and still 100% on the real GPU (`DISPLAY=:0`).

### Proposed new tests (to land with the fixes they pin)

Because the confirmed findings are unfixed, their regression tests would fail in
CI today, so they land with the fix, not before. Priorities:

1. **Cross-tool parity fixture** `models/parity-material-uv-subset.usda` + a
   registered harness: one mesh with two UV sets (`st` full, `uvSet1` a `[0,0.25]`
   sub-tile) and a checkerboard bound through `varname="uvSet1"`, three
   `materialBind` GeomSubsets → Red/Green/Blue, one DistantLight. Render through
   `tusdview --headless` (Vulkan) and `tusdrender -rtPreview`; assert three
   distinct colored regions (catches R1) and the zoomed `uvSet1` crop (catches
   R2), plus region-mean agreement (catches R9/R10/T11 as they arise).
2. **Non-instanced blendshape golden** (T1) — two times, assert frames differ.
3. **Malformed-USD robustness** — OOB `faceVertexIndices`, primvar element count
   contradicting interpolation, hostile `-width`/camera aperture, `-fitScale abc`
   (R5/R11) — drive both tools, assert clean exit + diagnostic, not a picture.
4. **tusdview purpose / visibility / double-sided** headless goldens (T8).
