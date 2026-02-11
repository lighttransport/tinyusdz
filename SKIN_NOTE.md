# Skinning Investigation Notes

## Dragon Stretched Vertices (AnimFinal_LowRes.usdz)

### Problem
Visible vertex stretching/spikes on the Dragon model at certain animation frames,
particularly on the skull, wing membranes, and tail areas.

### Model Characteristics
- 3001 joints named `point0`-`point2999` (point-based deformation rig, not traditional skeletal bones)
- 15 meshes, 1 skeleton, 1 animation (500s, 9003 tracks)
- Skull: 375 vertices, 33 influences per vertex -> TEXTURE_48 skinning mode
- Body: 12973 vertices, 56 influences per vertex -> TEXTURE_48 skinning mode
- geomBindTransform: identity

### Investigation Summary

#### Texture-Based Skinning vs 4-Bone Fallback
Added a runtime `useTextureSkinning` uniform to toggle between the full texture-based
skinning path (all 33-56 influences) and standard Three.js 4-bone skinning at runtime.

**Result: Both paths produce identical visual output.** The texture-based shader is
NOT the cause of the stretching.

#### Standard Three.js Materials Test
Replaced all mesh materials with plain Three.js materials (no `onBeforeCompile`
shader modifications at all).

**Result: Identical visual output.** The custom shader code is not at fault.

#### Numerical Analysis (Frame 210, time=8.75s)
- Skull (375 verts, 33 inf): max position diff between 4-bone and full-influence = 0.079 units
- Body (12973 verts, 56 inf): max diff = 0.107 units, avg = 0.010 units
- All vertex skin weights sum to exactly 1.0000
- Max vertex displacement from rest pose: 13.81 units (body), avg 6.75 units
- No extreme vertex outliers found across any mesh

#### Triangle Edge Analysis
- 174 triangles with >3x edge stretch ratio found across all meshes
- All involve sub-unit edge lengths (max skinned edge ~0.7 units)
- Not visually significant at normal zoom levels

#### Shadow Contribution
Disabling shadows reduces the perceived severity of the artifacts. Shadow depth
material uses the same extended skinning shader modifications (`customDepthMaterial`).

### Conclusion
**The skinning system is working correctly.** The visible stretching at certain
animation frames is inherent to the point-based deformation animation data, not a
rendering or skinning bug. The 4-bone approximation is excellent for this rig because
the point-based deformers have highly localized influence - each vertex's top 4
influences account for nearly all the deformation weight.

### Key Insight: Point-Based Deformation Rigs
With 3001 control points acting as surface deformers (not skeletal bones), the
per-vertex influences are highly localized. The top 4 weights dominate, making
standard 4-bone skinning nearly equivalent to full N-bone evaluation.

---

## Extended Skinning Architecture

### Skinning Modes (ExtendedSkinning.js)
- `STANDARD` (4): Standard Three.js skinIndex/skinWeight
- `EXTENDED_8` (8): Extra vertex attributes skinIndex2/skinWeight2
- `TEXTURE_16`..`TEXTURE_128`: Bone data packed into DataTexture (Float32 RGBA)

### Texture Packing (TEXTURE_16+)
Each texel packs 2 influences: R=boneIdx0, G=weight0, B=boneIdx1, A=weight1.
`texelsPerVertex = ceil(maxInfluences / 2)`.

### Runtime Toggle (useTextureSkinning uniform)
- Shared uniform object (`_useTexSkinUniform`) between render and shadow passes
- 1.0 = texture-based path, 0.0 = 4-bone fallback
- Only affects TEXTURE_16+ modes; EXTENDED_8 always uses all 8 bones
- GUI checkbox "Extended Skinning" in Visualization folder

### Shadow Support (customDepthMaterial)
Meshes with >4 bone influences need `customDepthMaterial` with the same shader
modifications. Without this, bones 5+ are ignored in shadow pass, causing vertices
to stick at bind pose and producing stretched shadows during animation.

---

## Verified Models

### StandingRunForward.usdz
- 67 joints (mixamo rig), 7 skinned meshes
- Body: 40614 vertices, 8 influences -> EXTENDED_8 mode
- Tops/Bottoms: 6-8 influences -> EXTENDED_8 mode
- Animation plays correctly, no artifacts

### AnimFinal_LowRes.usdz
- 3001 joints (point deformation), 15 skinned meshes
- Up to 56 influences/vertex -> TEXTURE_48 mode
- Skinning correct; visible stretching is animation data, not a bug

### CesiumMan.usdz
- Standard skeletal rig, 4 influences/vertex -> STANDARD mode
- upAxis="Y" with Z_UP rotation node in hierarchy
