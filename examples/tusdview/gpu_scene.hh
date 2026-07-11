// SPDX-License-Identifier: Apache-2.0
// tusdview - backend-neutral GPU scene representation.
//
// `DrawScene` is the hand-off between the USD/Tydra side (mesh_build.cc) and the
// graphics backends (gl_renderer / vk_renderer). It contains nothing
// backend-specific: interleaved vertices, a triangulated index buffer grouped
// into per-material submeshes, materials (mapped from UsdPreviewSurface), and
// decoded RGBA8 textures. Both the OpenGL and Vulkan backends consume this
// identical structure.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "light3d/texture.h"  // light3d::Image (CPU texel container)
#include "tydra/openpbr-params.hh"

namespace tusdview {

// Halton(2,3) sub-pixel jitter offset in [-0.5, 0.5) for supersampled ray-traced
// screenshots (the CUDA/HIP trace path). Sample 0 is the pixel center (0,0), so
// spp==1 reproduces the un-jittered image exactly. Shared by both GPU tracers.
inline void RtPixelJitter(int s, int spp, float* jx, float* jy) {
  if (spp <= 1 || s == 0) { *jx = 0.0f; *jy = 0.0f; return; }
  auto radical = [](int i, int base) {
    float f = 1.0f, r = 0.0f;
    while (i > 0) { f /= float(base); r += f * float(i % base); i /= base; }
    return r;
  };
  *jx = radical(s, 2) - 0.5f;
  *jy = radical(s, 3) - 0.5f;
}

// Interleaved vertex: matches the GL330 / VK450 shader attribute layout
//   location 0: vec3 aPosition  (offset 0)
//   location 1: vec3 aNormal    (offset 12)
//   location 2: vec2 aUV        (offset 24)  -> shader declares vec3 aUV, reads .xy
struct DrawVertex {
  float px, py, pz;
  float nx, ny, nz;
  float u, v;
};

// One draw call: a contiguous run of indices sharing a single material.
struct DrawSubmesh {
  uint32_t indexOffset{0};  // offset into DrawMeshCPU::indices (in indices)
  uint32_t indexCount{0};
  int materialId{-1};  // index into DrawScene::materials (-1 = default material)
};

// One blendshape target in DrawVertex order (sparse), for per-frame GPU morph.
// `vtx[i]` is the affected DrawVertex; `dpos` holds its position offset (3
// floats, parallel to `vtx`). Normals are regenerated from the morphed
// positions (RegenNormalsOriented), so authored normal offsets are not stored.
// One in-between shape sample of a blendshape: its position-offset deltas at a
// weight in (0,1). Parallel to MorphTargetCPU::vtx (same affected DrawVertices).
struct MorphInbetweenCPU {
  float weight{0.5f};        // USD inbetween weight (0 < w < 1, ascending order)
  std::vector<float> dpos;   // 3 floats per MorphTargetCPU::vtx entry
};

struct MorphTargetCPU {
  std::string name;  // BlendShape prim name == SkelAnimation weight key
  std::vector<uint32_t> vtx;
  std::vector<float> dpos;   // primary (weight == 1.0) offsets, 3 per vtx entry
  // In-between shapes, sorted ascending by weight. Empty = simple linear morph
  // (rest at w=0 -> primary at w=1). With inbetweens the morph piecewise-lerps
  // through (0, rest), each (weight, sample), and (1, primary).
  std::vector<MorphInbetweenCPU> inbetweens;
};

// GPU-morph "channels" for one blendshape target: the primary and each in-between
// sample become a channel with a mesh-local id and its own sparse per-vertex delta
// (in DrawMeshCPU::morphDeltaHalf). Per frame the CPU computes one coefficient
// per channel (reproducing the piecewise-lerp), and the vertex shader sums
// coeff[channel] * delta. `usdWeights`/`channelIds` are parallel and ascending:
// the in-between weights then 1.0 (primary). The implicit rest sample (weight 0)
// has no channel.
struct MorphTargetChannelsCPU {
  std::string name;               // BlendShape name == weight key
  std::vector<float> usdWeights;  // ascending: inbetween weights..., then 1.0
  std::vector<int> channelIds;    // mesh-local channel id per usdWeights entry
};

struct DrawMeshCPU {
  std::string name;
  std::string absPath;
  std::string purpose{"default"};  // USD purpose token: default/render/proxy/guide

  std::vector<DrawVertex> vertices;  // rest pose (GPU morph re-derives from this)
  // Optional per-vertex displayColor (rgb, parallel to `vertices`); empty = none.
  // Used by the flat --next preview to tint geometry; the material shader
  // multiplies baseColor by it (default white when absent).
  std::vector<float> vertexColors;
  // Optional per-vertex alpha (parallel to `vertices`); empty = fully opaque.
  // Carries USD `primvars:displayOpacity` when it is authored per-point or
  // per-face-vertex; constant/uniform opacity folds into DrawMaterialCPU::alpha
  // instead and leaves this empty.
  std::vector<float> vertexAlpha;
  // Optional tangent-space basis, parallel to `vertices`; each entry is xyz.
  // Populated from USD primvars or Tydra-computed tangents/binormals. Current
  // renderers ignore these until full normal-map/anisotropy evaluation lands.
  std::vector<float> tangents;
  std::vector<float> binormals;
  // True when the mesh has no authored normals: the shader shades it with the
  // geometric (screen-derivative) normal instead of the per-vertex normal, so
  // hard-surface geometry isn't smeared by averaged smooth normals.
  bool geometricNormal{false};
  // Winding sign used when regenerating smooth normals from triangle topology.
  // Tydra preserves source handedness; CPU repacks apply this sign after
  // skinning/blendshape deformation.
  float normalSign{1.0f};
  // Blendshape targets remapped to DrawVertex order; empty = no blendshapes.
  // The CPU/RT path morphs `vertices` (ApplyMorphTarget) for baked geometry.
  std::vector<MorphTargetCPU> morphs;
  // GPU-morph buffers (raster path): the morph is applied in the vertex shader
  // from a static sparse per-vertex delta list + a tiny per-frame coefficient
  // array, so weight changes upload only the coefficients (no VBO re-upload).
  // Mirrors the extended-skinning influence layout.
  //   morphOffsetCount: 2 uints/vertex (offset, count) into morphDeltaHalf.
  //   morphDeltaHalf: 4 halfs/entry (channelId, dx, dy, dz), half-precision to
  //     halve both the buffer (facial rigs: 64+ targets x 64k+ points) and the
  //     per-vertex shader fetch bandwidth. GL binds it as an RGBA16F texture
  //     buffer (hardware f16->f32 on texelFetch); VK reads uvec2 + unpackHalf2x16.
  //     The packed channelId (slot 0) is a legacy fallback -- the active-channel
  //     skip reads channelId from the exact uint16 `morphChannelId` side buffer,
  //     so only dx,dy,dz are live and the channel count is bounded by uint16, not
  //     by half-exactness (<= 2048).
  //   morphChannelCount: number of channels (size of the per-frame coeff array).
  //   morphTargetChannels: per-target channel metadata for the per-frame eval.
  std::vector<uint32_t> morphOffsetCount;
  std::vector<uint16_t> morphDeltaHalf;
  // Parallel to the morphDeltaHalf entries (one uint16 channelId each). A small
  // side buffer the vertex shader fetches FIRST (GL: R16UI texture buffer; VK:
  // widened to a uint SSBO, set 9), so it can skip the wide delta fetch when that
  // channel's coefficient is ~0 (facial animation: only a handful of 64+
  // expressions are active per frame). Channels are assigned per target in a fixed
  // order shared by every vertex, so the skip branch stays warp-coherent.
  std::vector<uint16_t> morphChannelId;
  int morphChannelCount{0};
  std::vector<MorphTargetChannelsCPU> morphTargetChannels;
  // Optional GPU skinning attributes, parallel to `vertices`.
  // `jointIdx` stores four absolute bone-matrix texture row indices per vertex;
  // `jointWt` stores the corresponding normalized weights. Empty = unskinned.
  std::vector<uint32_t> jointIdx;
  std::vector<float> jointWt;
  // Optional GL3-compatible full influence stream. `influenceOffsetCount` stores
  // two uints per vertex: texel offset and influence count. `influenceTexels`
  // stores one influence per RGBA32F texel: (absoluteJointRow, weight, 0, 0).
  std::vector<uint32_t> influenceOffsetCount;
  std::vector<float> influenceTexels;
  int influenceTexWidth{0};
  int influenceTexHeight{0};
  int maxInfluencesPerVertex{0};
  std::vector<uint32_t> indices;  // triangulated, grouped by submesh/material
  std::vector<DrawSubmesh> submeshes;

  // GPU instancing: per-instance 3x4 object-to-world matrices, 12 floats each
  // (3 rows of (x,y,z,translate); the constant bottom row is implicit). When
  // non-empty, the mesh is drawn with glDrawElementsInstanced and `world` is
  // ignored (each instance carries its own placement). Empty = a single
  // non-instanced draw.
  std::vector<float> instanceXforms;
  size_t instanceCount() const { return instanceXforms.size() / 12; }
  // Instance/prototype displayColor/displayOpacity for the flat instanced path.
  // When instanceColors is non-empty (3 floats/instance) each instance is tinted
  // individually; otherwise the whole instanced draw uses flatColor (e.g. the
  // prototype's average displayColor). instanceOpacities follows the same rule
  // with one float/instance, falling back to flatOpacity. Ignored for
  // non-instanced draws.
  std::vector<float> instanceColors;
  std::vector<float> instanceOpacities;
  float flatColor[3]{0.8f, 0.8f, 0.8f};
  float flatOpacity{1.0f};

  float world[16];  // column-major (light3d::Mat4 layout), world transform
  // USD row-vector matrix copied with the same convention as `world`.
  float skinGeomBind[16];
  int skelId{-1};
  int skinMatrixBase{-1};  // first matrix row in DrawScene's bone texture layout
  // Optional local-space skinned point samples for dense point-joint helper
  // display. Stored as xyz triples and updated by GPU skinning frame builds.
  std::vector<float> skinnedHelperPoints;
  float aabbMin[3]{0, 0, 0};
  float aabbMax[3]{0, 0, 0};
  // Prototype-LOCAL bbox over `vertices` (object space, before any instance
  // transform). For instanced meshes aabbMin/Max is the union over all instance
  // placements (scene-spanning); this local box is what per-instance frustum
  // culling (raster) and the CUDA instance world-AABBs transform per instance.
  float protoAabbMin[3]{0, 0, 0};
  float protoAabbMax[3]{0, 0, 0};
  // Max per-axis morph displacement (object space), used to pad protoAabb so a
  // GPU-morphed instanced prototype is not wrongly frustum-culled when the morph
  // pushes geometry past the rest box. 0 = no morph. See BuildMorphChannelsNext.
  float morphExtent[3]{0, 0, 0};
  bool doubleSided{false};
  int kindId{0};  // USD model kind AOV (resolved up ancestors); see KindId()
  // Optional 2nd texcoord set (2 floats/vertex, parallel to `vertices`); empty =
  // none. Drives the UV-set-1 (multi-UV) debug AOV. V is flipped like uv set 0.
  std::vector<float> uv1;
  // Optional per-vertex blendshape "influence": the largest displacement (world
  // units) any single morph target applies to that vertex. Computed at load from
  // `morphs`; empty when the mesh has no blendshapes. Drives the influence AOV.
  std::vector<float> morphInfluence;
  // Optional per-TRIANGLE original USD face id (the polygon each triangle was
  // triangulated from), parallel to the triangles of `indices` (grouped order).
  // Empty when triangulation face counts were unavailable. Drives SourceFaceId.
  std::vector<uint32_t> sourceFaceId;
  // Original-polygon wireframe edges as GL_LINES vertex-index pairs (perimeter of
  // each USD face, deduped). Built from the pre-triangulation topology so the
  // wireframe shows quads/ngons -- correct even for double-sided (doubled-tri)
  // meshes where a per-triangle approach fails. Empty => fall back to tri edges.
  std::vector<uint32_t> wireframeIndices;
  // Coarse displacement baked into geometry for the ray-tracing backends (which
  // intersect real triangles, so displacement can't be a vertex/tess-shader effect
  // like the raster path). Parallel to `vertices`; empty when the mesh has no
  // displaced material. The raster path keeps using `vertices` (shader displacement
  // with live sliders); the VK ray-query BLAS/hit reads these instead.
  std::vector<DrawVertex> rtDisplacedVertices;
};

// USD purpose token -> compact id used by the Purpose debug AOV (consistent across
// all backends): 0=default, 1=render, 2=proxy, 3=guide.
inline int PurposeId(const std::string& p) {
  if (p == "render") return 1;
  if (p == "proxy") return 2;
  if (p == "guide") return 3;
  return 0;  // default / unknown
}

// USD model kind token -> compact id for the Kind debug AOV (consistent across all
// backends): 0=none/unknown, 1=component, 2=group, 3=assembly, 4=subcomponent.
inline int KindId(const std::string& k) {
  if (k == "component") return 1;
  if (k == "group") return 2;
  if (k == "assembly") return 3;
  if (k == "subcomponent") return 4;
  return 0;  // no authored kind on this prim or any ancestor
}

enum class AlphaMode : int { Opaque = 0, Mask = 1, Blend = 2 };

enum class UdimMode : int { Sparse = 0, Atlas = 1 };
enum class TextureCompressionMode : int { Off = 0, BCn = 1, BC7 = 2 };

struct TextureRuntimeOptions {
  int maxTextureSize{0};       // longest edge cap in texels; 0 = no cap
  int textureBudgetMB{0};      // decoded/upload budget; 0 = no budget
  UdimMode udimMode{UdimMode::Sparse};
  TextureCompressionMode compression{TextureCompressionMode::Off};
  // Build content-aware CPU mip chains (sRGB/alpha-coverage/normal-map aware;
  // needs the vendored textools; no-op otherwise). Backends upload the
  // precomputed levels instead of glGenerateMipmap (GL) / no mips (VK).
  bool generateMips{false};
  // DomeLight image-based lighting bake at load (vendored envmap lib):
  // 0 = off, 1 = low (32px faces), 2 = high (64px faces). Needs textools.
  int domeIbl{2};
};

struct DrawUvXformCPU {
  // Affine UV transform:
  //   u' = m00*u + m01*v + tx
  //   v' = m10*u + m11*v + ty
  float m00{1.0f};
  float m01{0.0f};
  float m10{0.0f};
  float m11{1.0f};
  float tx{0.0f};
  float ty{0.0f};
};

struct DrawTexSampleCPU {
  DrawUvXformCPU uv;
  float scale[4]{1.0f, 1.0f, 1.0f, 1.0f};
  float bias[4]{0.0f, 0.0f, 0.0f, 0.0f};
};

enum class DrawMaterialParamType : int { Float = 0, Vec2 = 1, Vec3 = 2, Vec4 = 3 };

struct DrawMaterialParamCPU {
  // Neutral USD/MaterialX shader input record for future evaluators. Existing
  // preview shaders intentionally ignore this and keep using the legacy fields.
  std::string shader;  // "UsdPreviewSurface" or "OpenPBRSurface"
  std::string name;    // input name without the "inputs:" namespace
  DrawMaterialParamType type{DrawMaterialParamType::Float};
  float value[4]{0.0f, 0.0f, 0.0f, 1.0f};
  int texture{-1};        // DrawScene::textures index, -1 when unconnected/unloaded
  int renderTexture{-1};  // Tydra RenderScene::textures index, preserves connection
  int channel{-1};        // selected packed channel, -1 when whole value is used
  DrawTexSampleCPU sample;
};

// Baked constant fallback for raster/RT backends. Texture-connected inputs keep
// their texture ids in DrawMaterialParamCPU and the legacy material slots.
using DrawLightRtOpenPBRCPU = tinyusdz::tydra::LightRtOpenPBRParams;

struct DrawMaterialCPU {
  std::string name;
  std::string absPath;
  std::string displayName;
  bool hasUsdPreviewSurface{false};
  bool hasOpenPBRSurface{false};
  bool hasDisplacementOutput{false};
  bool hasVolumeOutput{false};
  std::string displacementShaderPath;
  std::string volumeShaderPath;
  std::string materialXNodeGraphJson;
  std::vector<DrawMaterialParamCPU> params;
  bool hasLightRtOpenPBR{false};
  DrawLightRtOpenPBRCPU lightRtOpenPBR;
  float baseColor[3]{0.8f, 0.8f, 0.8f};
  float metallic{0.0f};
  float roughness{0.5f};
  float emissive[3]{0.0f, 0.0f, 0.0f};
  float alpha{1.0f};
  int alphaMode{static_cast<int>(AlphaMode::Opaque)};
  float alphaCutoff{0.5f};
  // Indices into DrawScene::textures (-1 = no texture)
  int baseColorTex{-1};
  int metalRoughTex{-1};
  int normalTex{-1};
  int coatNormalTex{-1};
  int emissiveTex{-1};
  DrawTexSampleCPU baseColorSample;
  DrawTexSampleCPU metalRoughSample;
  DrawTexSampleCPU normalSample;
  DrawTexSampleCPU coatNormalSample;
  DrawTexSampleCPU emissiveSample;
  int metallicChannel{2};  // glTF ORM default: B
  int roughnessChannel{1}; // glTF ORM default: G
  float metallicTexScale{1.0f};
  float metallicTexBias{0.0f};
  float roughnessTexScale{1.0f};
  float roughnessTexBias{0.0f};
  // UsdPreviewSurface inputs:displacement. The surface is offset along its normal
  // by (displacementConst, or the displacement texture's red channel when present)
  // times the global displacement scale. The displacement map is linear (raw).
  int displacementTex{-1};
  DrawUvXformCPU displacementUv;
  float displacementConst{0.0f};
  // UsdUVTexture inputs:scale/inputs:bias for the displacement map's sampled
  // channel: effective height = texel*scale + bias (bias commonly centers a [0,1]
  // map). Identity (1, 0) when unauthored or for constant displacement.
  float displacementTexScale{1.0f};
  float displacementTexBias{0.0f};
  bool hasDisplacement() const { return displacementTex >= 0 || displacementConst != 0.0f; }
};

// Wrap modes (match light3d / GL semantics).
enum class WrapMode : int { ClampToEdge = 0, Repeat = 1, Mirror = 2, ClampToBorder = 3 };
enum class DrawCompressedFormat : int { None = 0, BC1 = 1, BC3 = 3, BC7 = 7 };

struct DrawCompressedMipCPU {
  int width{0};
  int height{0};
  std::vector<uint8_t> data;
};

struct DrawCompressedImageCPU {
  DrawCompressedFormat format{DrawCompressedFormat::None};
  int width{0};
  int height{0};
  std::vector<uint8_t> data;
  // Optional precomputed mip payloads for levels 1..N (level 0 is
  // width/height/data above), same block format. Empty = single level.
  std::vector<DrawCompressedMipCPU> mips;
};

struct DrawUdimTileCPU {
  uint32_t udim{1001};
  uint32_t u{0};
  uint32_t v{0};
  std::string assetIdentifier;
  int renderImageId{-1};
  light3d::Image image;  // RGBA8 tile layer
  DrawCompressedImageCPU compressed;
  // Optional precomputed RGBA8 mips for levels 1..N (level 0 = `image`).
  // NormalizeUdimTiles equalizes tile dims, so all tiles of a texture carry
  // the same level count/dims when mips are generated.
  std::vector<light3d::Image> mipImages;
};

struct DrawTextureCPU {
  light3d::Image image;  // always normalized to RGBA8 (channels == 4) on the CPU side
  std::string assetIdentifier;  // Tydra TextureImage::asset_identifier, if known
  int renderImageId{-1};        // source RenderScene::images index, or -1
  int renderUdimId{-1};         // source RenderScene::udim_textures index, or -1
  bool srgb{false};      // sRGB color data (baseColor/emissive) vs linear (normal/metalRough)
  int wrapS{static_cast<int>(WrapMode::Repeat)};
  int wrapT{static_cast<int>(WrapMode::Repeat)};
  bool requestedCompressed{false};
  DrawCompressedImageCPU compressed;
  bool isUdim{false};
  std::vector<DrawUdimTileCPU> udimTiles;
  std::array<int, 100> udimLayer{};
  int udimTileWidth{0};
  int udimTileHeight{0};
  // Texture usage, classified from the materials after BuildDrawMaterials
  // (FinalizeDrawTextures). Drives content-aware mip generation.
  bool isNormalMap{false};
  bool isAlphaTested{false};
  float alphaCutoff{0.5f};
  // Per-channel downsample rule for packed maps (0=linear, 1=majority,
  // 2=roughness variance-aware); indices match RGBA.
  int channelOp[4]{0, 0, 0, 0};
  // Optional precomputed RGBA8 mip images for levels 1..N (level 0 is
  // `image`). Empty = backends fall back to GPU mip generation (GL) or a
  // single level (VK).
  std::vector<light3d::Image> mipImages;
};

// A UsdVol volume (OpenVDB) as a dense scalar (density) grid, for GPU 3D-texture
// raymarching. `data` is the dense float grid (x-contiguous), `world` maps the
// object-space box [aabbMin, aabbMax] into world space.
struct DrawVolumeCPU {
  std::string name;
  int dim[3]{0, 0, 0};
  std::vector<float> density;  // dense, length dim[0]*dim[1]*dim[2]
  float world[16];             // column-major 4x4 (light3d::Mat4 layout)
  float aabbMin[3]{0, 0, 0};   // object-space grid bounds
  float aabbMax[3]{0, 0, 0};
  float densityScale{1.0f};
  float albedo[3]{0.6f, 0.6f, 0.65f};
  float emission[3]{0.0f, 0.0f, 0.0f};
  float background{0.0f};
};

// Precomputed split-sum IBL for a DomeLight (baked at load from the HDR float
// envmap by the vendored envmap library). Cube data is face-major float RGB in
// the KTX/D3D/GL face order +X,-X,+Y,-Y,+Z,-Z (matches GL cube upload).
struct DomeIblCPU {
  bool valid{false};
  int specFaceSize{0};  // level 0 face dim; level l is specFaceSize >> l
  // specLevels[l]: 6 * (specFaceSize>>l)^2 * 3 floats, GGX-prefiltered at
  // roughness l/(N-1); level 0 = mirror.
  std::vector<std::vector<float>> specLevels;
  int irrFaceSize{0};
  std::vector<float> irradiance;  // 6 * irrFaceSize^2 * 3, stored as E/pi
  int lutSize{0};
  std::vector<float> brdfLut;  // lutSize^2 * 2 (scale, bias)
  // Full-resolution (pre-firefly-clamp) source cube for env backgrounds (RT
  // miss); 6 * envCubeSize^2 * 3 floats, same face order as specLevels.
  int envCubeSize{0};
  std::vector<float> envCube;
  // Order-2 real-SH projection of the irradiance (E/pi), 9 coefficients x RGB
  // (coefficient-major: coeff k channel c at [k*3+c]). Evaluated by the RT
  // shaders as the surface ambient term; the basis polynomial set matches
  // ShIrradianceBasis in texture_tools.cc / the RT kernels.
  std::vector<float> shIrradiance;
};

struct DrawLightCPU {
  enum class Type : int {
    Point = 0,
    Sphere = 1,
    Disk = 2,
    Rect = 3,
    Cylinder = 4,
    Distant = 5,
    Dome = 6,
    Geometry = 7,
    Portal = 8,
  };
  enum class DomeTextureFormat : int {
    Automatic = 0,
    Latlong = 1,
    MirroredBall = 2,
    Angular = 3,
  };

  std::string name;
  std::string absPath;
  std::string displayName;
  Type type{Type::Point};
  float color[3]{1.0f, 1.0f, 1.0f};
  float intensity{1.0f};
  float exposure{0.0f};
  // color * colorTemperature(if enabled) * intensity * 2^exposure.
  float effectiveColor[3]{1.0f, 1.0f, 1.0f};
  // effectiveColor divided by shape area when normalize=true and area > 0.
  float normalizedColor[3]{1.0f, 1.0f, 1.0f};
  float effectiveIntensity{1.0f};
  float diffuse{1.0f};
  float specular{1.0f};
  bool normalize{false};
  bool enableColorTemperature{false};
  float colorTemperature{6500.0f};
  float transform[16]{};
  float position[3]{0.0f, 0.0f, 0.0f};
  float direction[3]{0.0f, -1.0f, 0.0f};
  float radius{0.5f};
  float width{1.0f};
  float height{1.0f};
  float length{1.0f};
  float area{0.0f};
  float invArea{0.0f};
  float angle{0.53f};
  std::string textureFile;
  int envmapTexture{-1};  // DrawScene::textures index when decoded/uploadable
  int renderEnvmapImage{-1};  // Tydra RenderScene::images index for DomeLight
  DomeTextureFormat domeTextureFormat{DomeTextureFormat::Automatic};
  DomeIblCPU ibl;  // split-sum IBL bake (Dome only; empty when disabled)
  float guideRadius{1.0e5f};
  float shapingConeAngle{90.0f};
  float shapingConeSoftness{0.0f};
  float shapingFocus{0.0f};
  float shapingFocusTint[3]{0.0f, 0.0f, 0.0f};
  std::string shapingIesFile;
  float shapingIesAngleScale{0.0f};
  bool shapingIesNormalize{false};
  bool hasShaping{false};
  bool shadowEnable{true};
  float shadowColor[3]{0.0f, 0.0f, 0.0f};
  float shadowDistance{-1.0f};
  float shadowFalloff{-1.0f};
  float shadowFalloffGamma{1.0f};
  int geometryMesh{-1};
  std::string materialSyncMode;
  bool lightLinksAll{true};
  std::vector<int> lightLinkMeshIndices;
  bool shadowLinksAll{true};
  std::vector<int> shadowLinkMeshIndices;
  bool hasSpectralEmission{false};
};

struct DrawScene {
  std::vector<DrawMeshCPU> meshes;
  std::vector<DrawMaterialCPU> materials;
  std::vector<DrawTextureCPU> textures;
  std::vector<DrawVolumeCPU> volumes;  // UsdVol volumes (OpenVDB)
  std::vector<DrawLightCPU> lights;    // USD light parameters for later shading
  bool hasPreviewLight{false};
  // A single derived key light used by today's simple preview shaders. Full
  // multi-light evaluation will consume DrawLightCPU directly later.
  float previewLightDir[3]{0.40160966f, 0.64257544f, 0.48193160f};
  float previewLightColor[3]{1.0f, 1.0f, 1.0f};
  int boneMatrixCount{0};  // height of the per-frame 4xN RGBA32F bone texture

  // --next per-frame GPU skinning. The Tydra path re-poses from
  // RenderScene::skeletons; the next path has no RenderScene, so each skinned
  // source mesh records here how to re-pose itself straight from the retained
  // next Stage. One entry per skinned source mesh (not per DrawMesh: the next
  // loader merges meshes into material batches, so one batch may draw vertices
  // from several of these).
  //
  // The mesh's vertices are already world-baked into its batch, so the bone rows
  // this entry owns ([matrixBase, matrixBase + numJoints)) are pre-composed with
  // both `geomBind` and the mesh's world transform -- see BuildNextSkinningFrame.
  // Vertices reference them by ABSOLUTE row index through DrawMeshCPU::jointIdx.
  struct NextSkelBinding {
    std::string skelPath;  // Skeleton prim
    std::string animPath;  // bound SkelAnimation ("" = rest pose)
    std::string meshPath;  // the skinned mesh (diagnostics)
    int numJoints{0};
    int matrixBase{0};
    double geomBind[16];  // primvars:skel:geomBindTransform (row-vector)
    double world[16];     // mesh world transform baked into the vertices
  };
  std::vector<NextSkelBinding> nextSkels;

  // World-space bounds over all meshes.
  float aabbMin[3]{-1, -1, -1};
  float aabbMax[3]{1, 1, 1};
  bool hasBounds{false};

  // Stage up axis ("Y" or "Z"); drives camera orbit + grid orientation. Set by
  // the loader (the Tydra path uses RenderScene.meta.upAxis directly instead).
  std::string upAxis{"Y"};

  // Diagnostics surfaced in the GUI (skipped meshes/textures, UDIM, etc.)
  std::vector<std::string> skipped;
  size_t triangleCount{0};
  // Total vertex count, captured at load time. Stored separately because the
  // --next path frees per-mesh CPU geometry after GPU upload (so summing
  // meshes[].vertices later would report 0).
  size_t vertexCount{0};

  // True when a render budget (triangles / VRAM) was hit and the scene was only
  // partially built to avoid freezing / VRAM thrashing.
  bool truncated{false};

  bool empty() const { return meshes.empty() && volumes.empty() && lights.empty(); }
};

struct SkinningFrameCPU {
  int matrixCount{0};  // texture height; width is always 4 RGBA texels
  std::vector<float> rgba32f;  // matrixCount * 4 texels * 4 floats
  bool enabled{false};
};

}  // namespace tusdview
