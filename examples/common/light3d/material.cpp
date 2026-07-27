#include "light3d/material.h"
#include "light3d/mesh_data.h"
#include <algorithm>
#include <cassert>

namespace light3d {

// --- MaterialLibrary ---

int MaterialLibrary::addMaterial(Material material) {
    int id = static_cast<int>(materials_.size());
    material.id = id;
    if (!material.name.empty()) {
        nameToIndex_[material.name] = id;
    }
    materials_.push_back(std::move(material));
    return id;
}

const Material* MaterialLibrary::getMaterial(int id) const {
    if (id < 0 || id >= static_cast<int>(materials_.size())) return nullptr;
    return &materials_[id];
}

Material* MaterialLibrary::getMaterial(int id) {
    if (id < 0 || id >= static_cast<int>(materials_.size())) return nullptr;
    return &materials_[id];
}

const Material* MaterialLibrary::findByName(const std::string& name) const {
    auto it = nameToIndex_.find(name);
    if (it == nameToIndex_.end()) return nullptr;
    return &materials_[it->second];
}

// --- Triangulation helper ---

// Fan-triangulate a single face, appending triangle vertex indices to `out`.
// For a face with vertices [v0, v1, v2, ..., vN-1], produces triangles:
//   (v0, v1, v2), (v0, v2, v3), ..., (v0, vN-2, vN-1)
static void triangulateFace(const int* faceIndices, int vertexCount,
                            std::vector<uint32_t>& out) {
    for (int i = 1; i + 1 < vertexCount; ++i) {
        out.push_back(static_cast<uint32_t>(faceIndices[0]));
        out.push_back(static_cast<uint32_t>(faceIndices[i]));
        out.push_back(static_cast<uint32_t>(faceIndices[i + 1]));
    }
}

// --- buildSubmeshes ---

SubmeshData buildSubmeshes(const MeshGeometry& geometry) {
    struct TriangleRecord {
        int materialId;
        uint32_t idx[3];
    };

    std::vector<TriangleRecord> triangles;
    bool hasMatIds = geometry.hasPerFaceMaterials();

    int offset = 0;
    for (size_t face = 0; face < geometry.faceVertexCounts.size(); ++face) {
        int count = geometry.faceVertexCounts[face];
        int matId = hasMatIds ? geometry.faceMaterialIds[face] : 0;
        const int* faceVerts = &geometry.faceVertexIndices[offset];

        for (int i = 1; i + 1 < count; ++i) {
            TriangleRecord rec;
            rec.materialId = matId;
            rec.idx[0] = static_cast<uint32_t>(faceVerts[0]);
            rec.idx[1] = static_cast<uint32_t>(faceVerts[i]);
            rec.idx[2] = static_cast<uint32_t>(faceVerts[i + 1]);
            triangles.push_back(rec);
        }

        offset += count;
    }

    // Stable sort by material ID (preserves face order within each material group)
    std::stable_sort(triangles.begin(), triangles.end(),
                     [](const TriangleRecord& a, const TriangleRecord& b) {
                         return a.materialId < b.materialId;
                     });

    // Build output
    SubmeshData result;
    result.triangleIndices.reserve(triangles.size() * 3);

    if (triangles.empty()) return result;

    int currentMatId = triangles[0].materialId;
    int groupStart = 0;

    for (size_t i = 0; i < triangles.size(); ++i) {
        result.triangleIndices.push_back(triangles[i].idx[0]);
        result.triangleIndices.push_back(triangles[i].idx[1]);
        result.triangleIndices.push_back(triangles[i].idx[2]);

        bool last = (i + 1 == triangles.size());
        bool matChange = !last && (triangles[i + 1].materialId != currentMatId);

        if (last || matChange) {
            Submesh sub;
            sub.materialId = currentMatId;
            sub.triangleOffset = groupStart;
            sub.triangleCount = static_cast<int>(i) - groupStart + 1;
            result.submeshes.push_back(sub);

            if (matChange) {
                currentMatId = triangles[i + 1].materialId;
                groupStart = static_cast<int>(i) + 1;
            }
        }
    }

    return result;
}

// --- packMaterialsToBuffer ---

std::vector<float> packMaterialsToBuffer(const MaterialLibrary& library) {
    std::vector<float> buf;
    buf.reserve(library.count() * kPackedMaterialFloats);

    for (const auto& mat : library.materials()) {
        // vec4(baseColor.rgb, metallic)
        buf.push_back(mat.baseColor.x);
        buf.push_back(mat.baseColor.y);
        buf.push_back(mat.baseColor.z);
        buf.push_back(mat.metallic);

        // vec4(emissive.rgb, roughness)
        buf.push_back(mat.emissive.x);
        buf.push_back(mat.emissive.y);
        buf.push_back(mat.emissive.z);
        buf.push_back(mat.roughness);

        // vec4(alpha, alphaCutoff, doubleSided, 0)
        buf.push_back(mat.alpha);
        buf.push_back(mat.alphaCutoff);
        buf.push_back(mat.doubleSided ? 1.0f : 0.0f);
        buf.push_back(0.0f);

        // vec4(baseColorTexIdx, metalRoughTexIdx, normalTexIdx, emissiveTexIdx)
        buf.push_back(static_cast<float>(mat.baseColorTexture));
        buf.push_back(static_cast<float>(mat.metallicRoughnessTexture));
        buf.push_back(static_cast<float>(mat.normalTexture));
        buf.push_back(static_cast<float>(mat.emissiveTexture));
    }

    return buf;
}

// --- buildTriangleMaterialIds ---

TriangleMaterialIdData buildTriangleMaterialIds(const MeshGeometry& geometry) {
    TriangleMaterialIdData result;
    bool hasMatIds = geometry.hasPerFaceMaterials();

    int offset = 0;
    for (size_t face = 0; face < geometry.faceVertexCounts.size(); ++face) {
        int count = geometry.faceVertexCounts[face];
        int matId = hasMatIds ? geometry.faceMaterialIds[face] : 0;
        const int* faceVerts = &geometry.faceVertexIndices[offset];

        for (int i = 1; i + 1 < count; ++i) {
            result.triangleIndices.push_back(static_cast<uint32_t>(faceVerts[0]));
            result.triangleIndices.push_back(static_cast<uint32_t>(faceVerts[i]));
            result.triangleIndices.push_back(static_cast<uint32_t>(faceVerts[i + 1]));
            result.materialIds.push_back(static_cast<uint32_t>(matId));
        }

        offset += count;
    }

    return result;
}

// --- Shader Source Strings ---

// ==================== GL330 Submesh Shaders ====================

const char* getMaterialVertexShaderGL330() {
    return R"glsl(#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUV;
layout(location = 3) in uvec4 aJoint;
layout(location = 4) in vec4 aWeight;
layout(location = 5) in uvec2 aInfluence;
layout(location = 6) in vec2 aUV1;        // 2nd texcoord set (multi-UV AOV; default 0)
layout(location = 7) in float aMorphInfl; // blendshape influence magnitude (default 0)
layout(location = 8) in uvec2 aMorphOffsetCount; // GPU morph (offset,count); default 0
layout(location = 9) in vec4 aColor;  // displayColor.rgb + displayOpacity (default 1)

uniform mat4 uModelViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;
uniform sampler2D uBoneTex;
uniform sampler2D uInfluenceTex;
uniform bool uSkinningEnabled;
uniform bool uExtendedSkinningEnabled;
uniform int uBoneTexWidth;
uniform int uBoneMatrixCount;
uniform int uInfluenceTexWidth;

// UsdPreviewSurface displacement (coarse per-vertex). The surface is offset along
// its normal by (height-map red, or uDisplacementConst) * uDisplacementScale. No
// derivatives in the vertex stage, so the map is sampled with textureLod(...,0).
uniform bool uHasDisplacement;
uniform bool uHasDisplacementTex;
uniform sampler2D uDisplacementTex;
uniform float uDisplacementConst;
uniform float uDisplacementScale;
uniform float uDisplacementTexScale;  // UsdUVTexture scale/bias (height = t*s + b)
uniform float uDisplacementTexBias;

// GPU blendshape morph: per-vertex (offset,count) into uMorphDeltaTex (RGBA16F:
// channelId, dx,dy,dz); uMorphCoeffTex (R32F) holds the per-frame coefficient per
// channel. pos += sum_i coeff[channel_i] * delta_i, applied before skinning.
// uMorphChanTex (R16UI) duplicates each entry's channelId so the loop can read the
// coefficient and skip the wide delta fetch when the channel is inactive.
uniform bool uHasMorph;
uniform samplerBuffer uMorphDeltaTex;
uniform samplerBuffer uMorphCoeffTex;
uniform usamplerBuffer uMorphChanTex;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec4 vColor;
flat out int vDomJoint;    // dominant skin joint (SkinWeights AOV); -1 = unskinned
out float vDomWeight;      // its weight
out vec2 vUV1;             // 2nd texcoord set (multi-UV AOV)
out float vMorphInfl;      // blendshape influence (world units)

mat4 fetchBone(uint idx) {
    int base = int(idx) * 4;
    return mat4(
        texelFetch(uBoneTex, ivec2((base + 0) % uBoneTexWidth, (base + 0) / uBoneTexWidth), 0),
        texelFetch(uBoneTex, ivec2((base + 1) % uBoneTexWidth, (base + 1) / uBoneTexWidth), 0),
        texelFetch(uBoneTex, ivec2((base + 2) % uBoneTexWidth, (base + 2) / uBoneTexWidth), 0),
        texelFetch(uBoneTex, ivec2((base + 3) % uBoneTexWidth, (base + 3) / uBoneTexWidth), 0));
}

void main() {
    vec3 pos = aPosition;
    vec3 nrm = aNormal;
    // GPU blendshape morph (before skinning): sum coeff*delta over this vertex's
    // sparse channel list. Loop cap mirrors the extended-skinning influence loop.
    if (uHasMorph && aMorphOffsetCount.y > 0u) {
        int mbase = int(aMorphOffsetCount.x);
        int mcount = min(int(aMorphOffsetCount.y), 256);
        for (int i = 0; i < 256; ++i) {
            if (i >= mcount) break;
            // Cheap channelId fetch first; skip the wide delta fetch when this
            // channel is inactive (facial animation: most coeffs are 0). The
            // channel order is shared across vertices, so the branch is coherent.
            int ch = int(texelFetch(uMorphChanTex, mbase + i).r);
            float c = texelFetch(uMorphCoeffTex, ch).r;
            if (abs(c) < 1e-6) continue;
            pos += c * texelFetch(uMorphDeltaTex, mbase + i).yzw;
        }
    }
    float wsum = aWeight.x + aWeight.y + aWeight.z + aWeight.w;
    uint maxJoint = max(max(aJoint.x, aJoint.y), max(aJoint.z, aJoint.w));
    if (uSkinningEnabled && uExtendedSkinningEnabled && aInfluence.y > 0u && uInfluenceTexWidth > 0) {
        mat4 skin = mat4(0.0);
        float fullWeightSum = 0.0;
        int boneRows = uBoneMatrixCount;
        int base = int(aInfluence.x);
        int count = min(int(aInfluence.y), 256);
        for (int i = 0; i < 256; ++i) {
            if (i >= count) break;
            int linear = base + i;
            vec4 iw = texelFetch(uInfluenceTex,
                                 ivec2(linear % uInfluenceTexWidth,
                                       linear / uInfluenceTexWidth),
                                 0);
            uint joint = uint(iw.x + 0.5);
            float weight = iw.y;
            if (weight > 0.0 && int(joint) < boneRows) {
                skin += fetchBone(joint) * weight;
                fullWeightSum += weight;
            }
        }
        if (fullWeightSum > 0.0) {
            skin *= 1.0 / fullWeightSum;
            pos = (skin * vec4(pos, 1.0)).xyz;  // skin the morphed position
            nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
        }
    } else if (uSkinningEnabled && wsum > 0.0 && int(maxJoint) < uBoneMatrixCount) {
        mat4 skin =
            fetchBone(aJoint.x) * aWeight.x +
            fetchBone(aJoint.y) * aWeight.y +
            fetchBone(aJoint.z) * aWeight.z +
            fetchBone(aJoint.w) * aWeight.w;
        pos = (skin * vec4(pos, 1.0)).xyz;  // skin the morphed position
        nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
    }
    // Coarse displacement: offset along the (object-space) normal before the world
    // transform, so the displacement scales with the model like the geometry does.
    if (uHasDisplacement) {
        float d = uHasDisplacementTex
                      ? textureLod(uDisplacementTex, aUV.xy, 0.0).r *
                                uDisplacementTexScale +
                            uDisplacementTexBias
                      : uDisplacementConst;
        pos += normalize(nrm) * (d * uDisplacementScale);
    }
    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * nrm);
    vUV = aUV.xy;
    vColor = aColor;
    vUV1 = aUV1;
    vMorphInfl = aMorphInfl;
    // Dominant skin joint (SkinWeights AOV) from the base 4-weight set.
    vDomJoint = -1;
    vDomWeight = 0.0;
    if (aWeight.x > vDomWeight) { vDomWeight = aWeight.x; vDomJoint = int(aJoint.x); }
    if (aWeight.y > vDomWeight) { vDomWeight = aWeight.y; vDomJoint = int(aJoint.y); }
    if (aWeight.z > vDomWeight) { vDomWeight = aWeight.z; vDomJoint = int(aJoint.z); }
    if (aWeight.w > vDomWeight) { vDomWeight = aWeight.w; vDomJoint = int(aJoint.w); }
    gl_Position = uModelViewProj * vec4(pos, 1.0);
}
)glsl";
}

const char* getMaterialFragmentShaderGL330() {
    return R"glsl(#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec4 vColor;
flat in int vDomJoint;   // dominant skin joint (SkinWeights AOV)
in float vDomWeight;
in vec2 vUV1;            // 2nd texcoord set (multi-UV AOV)
in float vMorphInfl;     // blendshape influence (world units)

// Material uniforms (one draw call per submesh)
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uAlpha;
uniform int uAlphaMode;       // 0=opaque, 1=mask, 2=blend
uniform float uAlphaCutoff;
// Specular F0 (T12): specular workflow -> specularColor directly; else the
// dielectric reflectance from ior. Unified with the Vulkan mesh.frag.
uniform int uUseSpecularWorkflow;
uniform int uOpenPbrSpecularModel;
uniform vec3 uSpecularColor;
uniform sampler2D uSpecularColorTex;
uniform bool uHasSpecularColorTex;
uniform vec3 uSpecularColorUv0;
uniform vec3 uSpecularColorUv1;
uniform int uSpecularColorUvSet;
uniform vec4 uSpecularColorScale;
uniform vec4 uSpecularColorBias;
uniform sampler2D uCoatNormalTex;
uniform bool uHasCoatNormalTex;
uniform vec3 uCoatNormalUv0;
uniform vec3 uCoatNormalUv1;
uniform int uCoatNormalUvSet;
uniform vec4 uCoatNormalScale;
uniform vec4 uCoatNormalBias;
// Advanced slots share otherwise-unused core UDIM array units per draw. The
// route indices select base/metallic/normal/opacity/emissive/roughness/
// occlusion respectively; -1 degrades to the neutral material constant when a
// material exhausts all seven routes.
uniform bvec4 uAdvancedTexIsUdim; // specular, coat weight/color/roughness
uniform ivec4 uAdvancedUdimRoutes;
uniform ivec4 uAdvancedUdimSlots;
uniform bool uCoatNormalTexIsUdim;
uniform int uCoatNormalUdimRoute;
uniform int uCoatNormalUdimSlot;
uniform float uIor;
uniform float uOcclusion;
uniform float uCoatWeight;
uniform vec3 uCoatColor;
uniform float uCoatRoughness;
uniform float uCoatIor;
// When set, shade with the geometric (screen-derivative) normal -- used for
// meshes without authored normals so hard surfaces aren't smeared by smooth
// (averaged) normals.
uniform bool uGeometricNormal;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
const int kMaxRasterLights = 16;
uniform int uLightCount;
uniform uint uLightMask;
uniform vec4 uLightPositionType[kMaxRasterLights];
uniform vec4 uLightDirectionAngle[kMaxRasterLights];
uniform vec4 uLightColorDiffuse[kMaxRasterLights];
uniform vec4 uLightSpecularShape[kMaxRasterLights];

// DomeLight split-sum IBL (precomputed at load; replaces the constant ambient
// floor when present).
uniform bool uHasIbl;
uniform vec3 uIblColor;             // dome effectiveColor (intensity baked in)
uniform float uExposure;            // authored Camera exposure, in stops
uniform mat3 uEnvRotation;          // world -> environment direction
uniform samplerCube uIrradianceMap; // cosine-convolved env, stored E/pi
uniform samplerCube uPrefilteredMap;// GGX chain, lod = roughness*(lods-1)
uniform int uPrefilteredLods;
uniform bool uHasShadowMap;
uniform int uShadowLightSlot;
uniform mat4 uShadowViewProj;
uniform sampler2D uShadowMap;
uniform bool uHasPointShadowMap;
uniform vec3 uPointShadowLightPos;
uniform mat4 uPointShadowViewProj[6];
uniform samplerCube uPointShadowMap;

// Texture samplers
uniform sampler2D uBaseColorTex;
uniform sampler2DArray uBaseColorUdimTex;
uniform bool uHasBaseColorTex;
uniform bool uBaseColorTexIsUdim;
uniform sampler2D uMetallicTex;
uniform sampler2DArray uMetallicUdimTex;
uniform bool uHasMetallicTex;
uniform bool uMetallicTexIsUdim;
uniform sampler2D uRoughnessTex;
uniform sampler2DArray uRoughnessUdimTex;
uniform bool uHasRoughnessTex;
uniform bool uRoughnessTexIsUdim;
uniform sampler2D uNormalTex;
uniform sampler2DArray uNormalUdimTex;
uniform bool uHasNormalTex;
uniform bool uNormalTexIsUdim;
uniform sampler2D uEmissiveTex;
uniform sampler2DArray uEmissiveUdimTex;
uniform bool uHasEmissiveTex;
uniform bool uEmissiveTexIsUdim;
uniform sampler2D uOpacityTex;
uniform sampler2DArray uOpacityUdimTex;
uniform bool uHasOpacityTex;
uniform bool uOpacityTexIsUdim;
uniform sampler2D uOcclusionTex;
uniform sampler2DArray uOcclusionUdimTex;
uniform bool uHasOcclusionTex;
uniform bool uOcclusionTexIsUdim;
// One scene-wide 100 x texture-count atlas. Each row maps UDIM 1001..1100 to
// an array layer; -1 means the tile is absent. Consolidating four independent
// LUT samplers avoids the GL 3.3 fragment-sampler ceiling.
uniform isampler2D uUdimLutAtlas;
uniform ivec4 uUdimSlots;  // base, metallic, normal, emissive texture rows
uniform int uOpacityUdimSlot;
uniform int uRoughnessUdimSlot;
uniform int uOcclusionUdimSlot;
// Per-slot UV set: 0 = vUV (texcoords_0), 1 = vUV1 (texcoords_1).
// x = base color, y = metal/rough, z = normal, w = emissive.
uniform ivec4 uUvSet;
uniform int uRoughnessUvSet;
uniform vec3 uBaseColorUv0;   // m00,m01,tx
uniform vec3 uBaseColorUv1;   // m10,m11,ty
uniform vec3 uMetallicUv0;
uniform vec3 uMetallicUv1;
uniform vec3 uRoughnessUv0;
uniform vec3 uRoughnessUv1;
uniform vec3 uNormalUv0;
uniform vec3 uNormalUv1;
uniform vec3 uEmissiveUv0;
uniform vec3 uEmissiveUv1;
uniform vec3 uOpacityUv0;
uniform vec3 uOpacityUv1;
uniform vec3 uOcclusionUv0;
uniform vec3 uOcclusionUv1;
uniform vec4 uBaseColorTexScale;
uniform vec4 uBaseColorTexBias;
uniform vec4 uNormalTexScale;
uniform vec4 uNormalTexBias;
uniform vec4 uEmissiveTexScale;
uniform vec4 uEmissiveTexBias;
uniform int uMetallicChannel;
uniform int uRoughnessChannel;
uniform float uMetallicTexScale;
uniform float uMetallicTexBias;
uniform float uRoughnessTexScale;
uniform float uRoughnessTexBias;
uniform int uOpacityUvSet;
uniform int uOpacityChannel;
uniform float uOpacityTexScale;
uniform float uOpacityTexBias;
uniform int uOcclusionUvSet;
uniform int uOcclusionChannel;
uniform float uOcclusionTexScale;
uniform float uOcclusionTexBias;

// Coat lobe maps. Ordinary images use their dedicated 2D units. UDIM images
// route through a free core sampler2DArray unit selected per draw, avoiding an
// increase beyond the GL 3.3 32-fragment-unit floor.
uniform sampler2D uCoatWeightTex;
uniform sampler2D uCoatColorTex;
uniform sampler2D uCoatRoughnessTex;
uniform bool uHasCoatWeightTex;
uniform bool uHasCoatColorTex;
uniform bool uHasCoatRoughnessTex;
uniform vec3 uCoatWeightUv0;
uniform vec3 uCoatWeightUv1;
uniform vec3 uCoatColorUv0;
uniform vec3 uCoatColorUv1;
uniform vec3 uCoatRoughnessUv0;
uniform vec3 uCoatRoughnessUv1;
uniform int uCoatWeightUvSet;
uniform int uCoatColorUvSet;
uniform int uCoatRoughnessUvSet;
uniform int uCoatWeightChannel;
uniform int uCoatRoughnessChannel;
uniform vec4 uCoatWeightScale;
uniform vec4 uCoatWeightBias;
uniform vec4 uCoatColorScale;
uniform vec4 uCoatColorBias;
uniform vec4 uCoatRoughnessScale;
uniform vec4 uCoatRoughnessBias;

out vec4 fragColor;

uniform int uRenderMode;     // RenderMode (see renderer.hh)
uniform int uMatId;          // per-draw material id (material-id viz; -1 = none)
uniform float uDepthScale;   // depth AOV: camera-distance normalizer (scene extent)
uniform vec3 uSceneMin;      // position AOV: scene bbox min
uniform vec3 uSceneExtent;   // position AOV: scene bbox size (max-min)
uniform int uMeshId;         // mesh-id AOV (per-draw mesh index)
uniform bool uDoubleSided;   // double-sided AOV flag
uniform int uPurpose;        // purpose AOV: 0=default/1=render/2=proxy/3=guide
uniform int uKind;           // kind AOV: 0=none/1=component/2=group/3=assembly/4=subcomponent
uniform usamplerBuffer uFaceIdTex;  // per-triangle source face id (source-face-id AOV)
uniform int uFaceBase;       // first triangle of this submesh (gl_PrimitiveID is submesh-local)
uniform bool uHasFaceId;
// Base-color Ptex uses an embedded face-rectangle table. The source-face buffer
// supplies the record for the current triangle; UVs are intrinsic face-local.
uniform bool uBasePtex;
uniform vec2 uBasePtexGrid; // rectangle texel offset, face count
uniform vec2 uMetallicPtexGrid;
uniform vec2 uRoughnessPtexGrid;
uniform vec2 uNormalPtexGrid;
uniform vec2 uEmissivePtexGrid;
uniform vec2 uOpacityPtexGrid;
uniform vec2 uOcclusionPtexGrid;
uniform vec2 uSpecularColorPtexGrid;
uniform vec2 uCoatWeightPtexGrid;
uniform vec2 uCoatColorPtexGrid;
uniform vec2 uCoatRoughnessPtexGrid;

// Stable distinct color per material id (-1 -> neutral gray).
vec3 idColor(int id) {
    if (id < 0) return vec3(0.45);
    uint h = (uint(id) + 1u) * 2654435761u;
    return vec3(float(h & 255u), float((h >> 8) & 255u), float((h >> 16) & 255u)) * (1.0 / 255.0);
}

// USD purpose -> distinct color (shared scheme across all backends).
vec3 purposeColor(int p) {
    if (p == 1) return vec3(0.2, 0.8, 0.3);    // render: green
    if (p == 2) return vec3(0.2, 0.45, 0.95);  // proxy: blue
    if (p == 3) return vec3(0.95, 0.75, 0.1);  // guide: amber
    return vec3(0.5);                          // default: gray
}

// USD model kind -> distinct color (shared scheme across all backends).
vec3 kindColor(int k) {
    if (k == 1) return vec3(0.2, 0.8, 0.8);    // component: cyan
    if (k == 2) return vec3(0.85, 0.3, 0.85);  // group: magenta
    if (k == 3) return vec3(0.95, 0.6, 0.15);  // assembly: orange
    if (k == 4) return vec3(0.5, 0.85, 0.4);   // subcomponent: green
    return vec3(0.35);                         // no kind: dark gray
}

vec4 sampleUdim(sampler2DArray tex, int slot, vec2 uv, vec4 missing) {
    ivec2 tile = ivec2(floor(uv));
    int idx = tile.x + tile.y * 10;
    if (idx < 0 || idx >= 100) {
        return missing;
    }
    int layer = texelFetch(uUdimLutAtlas, ivec2(idx, slot), 0).r;
    if (layer < 0) {
        return missing;
    }
    return texture(tex, vec3(fract(uv), float(layer)));
}

vec4 sampleRoutedUdim(int route, int slot, vec2 uv, vec4 missing) {
    if (route == 0) return sampleUdim(uBaseColorUdimTex, slot, uv, missing);
    if (route == 1) return sampleUdim(uMetallicUdimTex, slot, uv, missing);
    if (route == 2) return sampleUdim(uNormalUdimTex, slot, uv, missing);
    if (route == 3) return sampleUdim(uOpacityUdimTex, slot, uv, missing);
    if (route == 4) return sampleUdim(uEmissiveUdimTex, slot, uv, missing);
    if (route == 5) return sampleUdim(uRoughnessUdimTex, slot, uv, missing);
    if (route == 6) return sampleUdim(uOcclusionUdimTex, slot, uv, missing);
    return missing;
}

vec2 xformUv(vec2 uv, vec3 row0, vec3 row1) {
    return vec2(dot(vec3(uv, 1.0), row0), dot(vec3(uv, 1.0), row1));
}

vec2 ptexUv(sampler2D tex, vec2 uv, vec2 grid) {
    if (grid.y <= 0.5 || !uHasFaceId) return uv;
    int face = int(texelFetch(uFaceIdTex, uFaceBase + gl_PrimitiveID).r);
    int count = int(grid.y + 0.5);
    if (face < 0 || face >= count) return uv;
    ivec2 size = textureSize(tex, 0);
    int base = int(grid.x + 0.5) + face * 8;
    uint value[4];
    for (int component = 0; component < 4; ++component) {
        int lo = base + component * 2;
        ivec2 p0 = ivec2(lo % size.x, lo / size.x);
        ivec2 p1 = ivec2((lo + 1) % size.x, (lo + 1) / size.x);
        value[component] = uint(texelFetch(tex, p0, 0).a * 255.0 + 0.5) |
                           (uint(texelFetch(tex, p1, 0).a * 255.0 + 0.5) << 8u);
    }
    if (value[2] == 0u || value[3] == 0u) return uv;
    vec2 t = clamp(uv, 0.0, 1.0);
    vec2 px = vec2(value[0], value[1]) +
              vec2(t.x, 1.0 - t.y) * vec2(value[2] - 1u, value[3] - 1u);
    return (px + vec2(0.5)) / vec2(size);
}

// Linear -> sRGB OETF for the final shaded output. sRGB base-color textures are
// uploaded as GL_SRGB8_ALPHA8 (linearized on sample) and the scene is lit in
// linear space, so the encode happens here (the FBO is plain RGBA8). Lit path
// only -- AOVs stay raw. Matches the Vulkan mesh.frag.
vec3 linearToSrgb(vec3 c) {
    c = clamp(c, 0.0, 1.0);
    vec3 lo = c * 12.92;
    vec3 hi = 1.055 * pow(c, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, vec3(greaterThan(c, vec3(0.0031308))));
}

// Specular F0 (T12), matching the Vulkan mesh.frag: specular workflow ->
// specularColor; else the dielectric reflectance from ior lerped to base by
// metalness. ior 1.5 (the default) gives exactly 0.04.
vec3 computeF0(vec3 base, float metallic) {
    if (uUseSpecularWorkflow != 0) return uSpecularColor;
    float ior = max(1.0, uIor);
    float d = (ior - 1.0) / (ior + 1.0);
    vec3 dielectric = vec3(d * d);
    if (uOpenPbrSpecularModel != 0) dielectric *= uSpecularColor;
    return mix(dielectric, base, clamp(metallic, 0.0, 1.0));
}

const float kPi = 3.14159265358979323846;

float distributionGGX(float NoH, float roughness) {
    float a = max(roughness * roughness, 0.002);
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / max(kPi * d * d, 1e-6);
}

float geometrySchlickGGX(float NoX, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) * 0.125;
    return NoX / max(NoX * (1.0 - k) + k, 1e-6);
}

vec3 fresnelSchlick(float VoH, vec3 f0) {
    float f = pow(1.0 - clamp(VoH, 0.0, 1.0), 5.0);
    return f0 + (vec3(1.0) - f0) * f;
}

float channelOf(vec4 c, int ch) {
    if (ch == 1) return c.g;
    if (ch == 2) return c.b;
    if (ch == 3) return c.a;
    return c.r;
}

float sampleOcclusion() {
    if (!uHasOcclusionTex) return 1.0;
    vec2 uv = (uOcclusionUvSet == 1) ? vUV1 : vUV;
    vec2 tuv = xformUv(uv, uOcclusionUv0, uOcclusionUv1);
    tuv = ptexUv(uOcclusionTex, tuv, uOcclusionPtexGrid);
    vec4 c = uOcclusionTexIsUdim
        ? sampleUdim(uOcclusionUdimTex, uOcclusionUdimSlot, tuv, vec4(1.0))
        : texture(uOcclusionTex, tuv);
    return clamp(channelOf(c, uOcclusionChannel) * uOcclusionTexScale +
                 uOcclusionTexBias, 0.0, 1.0);
}

// Scalar/color fetches for the coat and specular-workflow slots. The material
// constant is neutralized to 1 by the loaders whenever a texture is bound, so
// the caller multiplies the two unconditionally.
float sampleCoatScalar(sampler2D tex, bool has, bool isUdim, int udimRoute,
                       int udimSlot, vec3 uv0, vec3 uv1, int uvSet, int ch,
                       vec4 scale, vec4 bias, vec2 ptexGrid) {
    if (!has) return 1.0;
    vec2 uv = (uvSet == 1) ? vUV1 : vUV;
    uv = xformUv(uv, uv0, uv1);
    uv = ptexUv(tex, uv, ptexGrid);
    vec4 texel = isUdim ? sampleRoutedUdim(udimRoute, udimSlot, uv, vec4(1.0))
                        : texture(tex, uv);
    vec4 c = texel * scale + bias;
    return clamp(channelOf(c, ch), 0.0, 1.0);
}

vec3 sampleCoatColor(sampler2D tex, bool has, bool isUdim, int udimRoute,
                     int udimSlot, vec3 uv0, vec3 uv1, int uvSet, vec4 scale,
                     vec4 bias, vec2 ptexGrid) {
    if (!has) return vec3(1.0);
    vec2 uv = (uvSet == 1) ? vUV1 : vUV;
    uv = xformUv(uv, uv0, uv1);
    uv = ptexUv(tex, uv, ptexGrid);
    vec4 texel = isUdim ? sampleRoutedUdim(udimRoute, udimSlot, uv, vec4(1.0))
                        : texture(tex, uv);
    return (texel * scale + bias).rgb;
}

float sampleShadow(vec3 worldPos, vec3 normal, vec3 lightDir) {
    if (uHasPointShadowMap) {
    vec3 d = worldPos - uPointShadowLightPos;
    vec3 a = abs(d);
    int face;
    if (a.x >= a.y && a.x >= a.z) face = d.x >= 0.0 ? 0 : 1;
    else if (a.y >= a.z) face = d.y >= 0.0 ? 2 : 3;
    else face = d.z >= 0.0 ? 4 : 5;
    vec4 clip = uPointShadowViewProj[face] * vec4(worldPos, 1.0);
    vec3 p = clip.xyz / clip.w;
    p = p * 0.5 + 0.5;
    if (p.z <= 0.0 || p.z >= 1.0) return 1.0;
    float bias = max(0.00035, 0.0015 * (1.0 - max(dot(normal, lightDir), 0.0)));
    return (p.z - bias <= texture(uPointShadowMap, normalize(d)).r) ? 1.0 : 0.0;
    }
    if (!uHasShadowMap) return 1.0;
    vec4 clip = uShadowViewProj * vec4(worldPos, 1.0);
    vec3 p = clip.xyz / clip.w;
    p = p * 0.5 + 0.5;
    if (p.z <= 0.0 || p.z >= 1.0 || any(lessThan(p.xy, vec2(0.0))) ||
        any(greaterThan(p.xy, vec2(1.0)))) return 1.0;
    float bias = max(0.00035, 0.0015 * (1.0 - max(dot(normal, lightDir), 0.0)));
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    float visible = 0.0;
    for (int y = -1; y <= 1; ++y)
      for (int x = -1; x <= 1; ++x)
        visible += p.z - bias <= texture(uShadowMap, p.xy + vec2(x, y) * texel).r
                       ? 1.0 : 0.0;
    return visible / 9.0;
}

void main() {
    vec3 baseColor = uBaseColor * vColor.rgb;  // vColor defaults to white
    float metallic = uMetallic;
    float roughness = uRoughness;
    vec3 emissive = uEmissive;
    float opacity = clamp(uAlpha * vColor.a, 0.0, 1.0);

    if (uHasBaseColorTex) {
        vec2 uv = xformUv(uUvSet.x == 1 ? vUV1 : vUV, uBaseColorUv0, uBaseColorUv1);
        if (uBasePtex && uHasFaceId) {
            int face = int(texelFetch(uFaceIdTex, uFaceBase + gl_PrimitiveID).r);
            int count = int(uBasePtexGrid.y + 0.5);
            if (face >= 0 && face < count) {
                ivec2 size = textureSize(uBaseColorTex, 0);
                int base = int(uBasePtexGrid.x + 0.5) + face * 8;
                uint value[4];
                for (int component = 0; component < 4; ++component) {
                    int lo = base + component * 2;
                    float a = texelFetch(uBaseColorTex,
                                        ivec2(lo % size.x, lo / size.x), 0).a;
                    float b = texelFetch(uBaseColorTex,
                                        ivec2((lo + 1) % size.x,
                                              (lo + 1) / size.x), 0).a;
                    value[component] = uint(a * 255.0 + 0.5) |
                                       (uint(b * 255.0 + 0.5) << 8u);
                }
                vec2 px = vec2(float(value[0]), float(value[1])) +
                          vec2(clamp(uv.x, 0.0, 1.0),
                               1.0 - clamp(uv.y, 0.0, 1.0)) *
                          vec2(float(max(value[2], 1u) - 1u),
                               float(max(value[3], 1u) - 1u));
                uv = (px + vec2(0.5)) / vec2(size);
            }
        }
        vec4 texel = uBaseColorTexIsUdim
                         ? sampleUdim(uBaseColorUdimTex, uUdimSlots.x, uv,
                                      vec4(1.0, 0.0, 1.0, 1.0))
                         : texture(uBaseColorTex, uv);
        vec4 sample = texel * uBaseColorTexScale + uBaseColorTexBias;
        baseColor *= sample.rgb;
        opacity *= clamp(sample.a, 0.0, 1.0);
    }
    if (uHasMetallicTex) {
        vec2 uv = xformUv(uUvSet.y == 1 ? vUV1 : vUV, uMetallicUv0, uMetallicUv1);
        uv = ptexUv(uMetallicTex, uv, uMetallicPtexGrid);
        vec4 texel = uMetallicTexIsUdim
                      ? sampleUdim(uMetallicUdimTex, uUdimSlots.y, uv, vec4(1.0))
                      : texture(uMetallicTex, uv);
        metallic *= channelOf(texel, uMetallicChannel) * uMetallicTexScale + uMetallicTexBias;
    }
    if (uHasRoughnessTex) {
        vec2 uv = xformUv(uRoughnessUvSet == 1 ? vUV1 : vUV, uRoughnessUv0, uRoughnessUv1);
        uv = ptexUv(uRoughnessTex, uv, uRoughnessPtexGrid);
        vec4 texel = uRoughnessTexIsUdim
                      ? sampleUdim(uRoughnessUdimTex, uRoughnessUdimSlot, uv, vec4(1.0))
                      : texture(uRoughnessTex, uv);
        roughness *= channelOf(texel, uRoughnessChannel) * uRoughnessTexScale + uRoughnessTexBias;
    }
    if (uHasEmissiveTex) {
        vec2 uv = xformUv(uUvSet.w == 1 ? vUV1 : vUV, uEmissiveUv0, uEmissiveUv1);
        uv = ptexUv(uEmissiveTex, uv, uEmissivePtexGrid);
        vec4 texel = uEmissiveTexIsUdim
                         ? sampleUdim(uEmissiveUdimTex, uUdimSlots.w, uv,
                                      vec4(1.0, 0.0, 1.0, 1.0))
                         : texture(uEmissiveTex, uv);
        emissive *= (texel * uEmissiveTexScale + uEmissiveTexBias).rgb;
    }

    vec3 N = uGeometricNormal
                 ? normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)))
                 : normalize(vNormal);
    if (uHasNormalTex) {
        vec2 uv = xformUv(uUvSet.z == 1 ? vUV1 : vUV, uNormalUv0, uNormalUv1);
        uv = ptexUv(uNormalTex, uv, uNormalPtexGrid);
        vec3 tangentNormal = ((uNormalTexIsUdim
                                  ? sampleUdim(uNormalUdimTex, uUdimSlots.z, uv,
                                               vec4(0.5, 0.5, 1.0, 1.0))
                                  : texture(uNormalTex, uv)) * uNormalTexScale +
                              uNormalTexBias).xyz;
        // Full derivative TBN (unified with the Vulkan backend). The old
        // `normalize(N + tangentNormal*0.1)` was a weak non-TBN perturbation
        // that made relief nearly flat on GL and pronounced on VK; build the
        // tangent frame from the screen-space position/UV gradients so the
        // tangent-space sample perturbs N at full strength in the right basis.
        vec3 dp1 = dFdx(vWorldPos);
        vec3 dp2 = dFdy(vWorldPos);
        vec2 du1 = dFdx(uv);
        vec2 du2 = dFdy(uv);
        float r = du1.x * du2.y - du2.x * du1.y;
        vec3 t = dp1 * du2.y - dp2 * du1.y;
        t = (abs(r) > 1e-8) ? t / r : dp1;
        t = normalize(t - N * dot(N, t));
        vec3 b = normalize(cross(N, t)) * (r < 0.0 ? -1.0 : 1.0);
        N = normalize(mat3(t, b, N) * tangentNormal);
    }
    vec3 coatN = N;
    if (uHasCoatNormalTex) {
        vec2 uv = xformUv(uCoatNormalUvSet == 1 ? vUV1 : vUV,
                          uCoatNormalUv0, uCoatNormalUv1);
        vec4 coatTexel = uCoatNormalTexIsUdim
                             ? sampleRoutedUdim(uCoatNormalUdimRoute,
                                                uCoatNormalUdimSlot, uv,
                                                vec4(0.5, 0.5, 1.0, 1.0))
                             : texture(uCoatNormalTex, uv);
        vec3 tn = (coatTexel * uCoatNormalScale +
                   uCoatNormalBias).xyz;
        vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
        vec2 du1 = dFdx(uv), du2 = dFdy(uv);
        float rr = du1.x * du2.y - du2.x * du1.y;
        vec3 t = dp1 * du2.y - dp2 * du1.y;
        t = normalize((abs(rr) > 1e-8 ? t / rr : dp1) - N * dot(N, t));
        vec3 b = normalize(cross(N, t)) * (rr < 0.0 ? -1.0 : 1.0);
        coatN = normalize(mat3(t, b, N) * tn);
    }
    if (uHasOpacityTex) {
        vec2 uv = xformUv(uOpacityUvSet == 1 ? vUV1 : vUV,
                          uOpacityUv0, uOpacityUv1);
        uv = ptexUv(uOpacityTex, uv, uOpacityPtexGrid);
        // Missing opacity UDIM tiles are opaque, not magenta/channel-dependent.
        vec4 ot = uOpacityTexIsUdim
                      ? sampleUdim(uOpacityUdimTex, uOpacityUdimSlot, uv, vec4(1.0))
                      : texture(uOpacityTex, uv);
        opacity *= clamp(channelOf(ot, uOpacityChannel) * uOpacityTexScale +
                         uOpacityTexBias, 0.0, 1.0);
    }
    opacity = clamp(opacity, 0.0, 1.0);
    if (uAlphaMode == 1) {
        opacity = (opacity >= uAlphaCutoff) ? 1.0 : 0.0;
    }

    // Debug AOVs: override the shaded output with the requested channel.
    if (uRenderMode != 0 && uRenderMode != 36 && uRenderMode != 37 &&
        uRenderMode != 38 && uRenderMode != 39 && uRenderMode != 40) {
        vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
        if (uRenderMode == 2) { fragColor = vec4(N * 0.5 + 0.5, 1.0); return; }       // shading normal
        if (uRenderMode == 35) { fragColor = vec4(coatN * 0.5 + 0.5, 1.0); return; } // coat normal
        if (uRenderMode == 3) { fragColor = vec4(idColor(uMatId), 1.0); return; }     // material id
        if (uRenderMode == 4) { fragColor = vec4(Ngeo * 0.5 + 0.5, 1.0); return; }    // geometric normal
        if (uRenderMode == 6) {                                                       // depth
            float d = clamp(length(uCameraPos - vWorldPos) / uDepthScale, 0.0, 1.0);
            fragColor = vec4(vec3(1.0 - d), 1.0);
            return;
        }
        if (uRenderMode == 5) { fragColor = vec4(fract(vUV), 0.0, 1.0); return; }      // uv set 0
        if (uRenderMode == 7) { fragColor = vec4(baseColor, 1.0); return; }            // albedo (unlit)
        if (uRenderMode == 8) {                                                        // facing
            fragColor = gl_FrontFacing ? vec4(0.1, 0.7, 0.1, 1.0) : vec4(0.7, 0.1, 0.1, 1.0);
            return;
        }
        if (uRenderMode == 9)  { fragColor = vec4(vec3(roughness), 1.0); return; }     // roughness
        if (uRenderMode == 10) { fragColor = vec4(vec3(metallic), 1.0); return; }      // metallic
        if (uRenderMode == 11) { fragColor = vec4(emissive, 1.0); return; }            // emissive
        if (uRenderMode == 12) { fragColor = vec4(vec3(opacity), 1.0); return; }       // opacity
        if (uRenderMode == 13) {                                                       // world position
            fragColor = vec4(clamp((vWorldPos - uSceneMin) / uSceneExtent, 0.0, 1.0), 1.0);
            return;
        }
        if (uRenderMode == 23) {                                                       // uv checker
            vec2 c = floor(fract(vUV) * 16.0);
            float chk = mod(c.x + c.y, 2.0);
            fragColor = vec4(vec3(mix(0.25, 0.85, chk)), 1.0);
            return;
        }
        if (uRenderMode == 15) { fragColor = vec4(idColor(gl_PrimitiveID), 1.0); return; }  // prim id
        if (uRenderMode == 16) { fragColor = vec4(idColor(uMeshId), 1.0); return; }         // mesh id
        if (uRenderMode == 19) {                                                            // missing normals
            fragColor = uGeometricNormal ? vec4(0.95, 0.1, 0.85, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
            return;
        }
        if (uRenderMode == 20) {                                                            // double-sided
            fragColor = uDoubleSided ? vec4(0.95, 0.55, 0.1, 1.0) : vec4(0.2, 0.2, 0.2, 1.0);
            return;
        }
        if (uRenderMode == 18) { fragColor = vec4(purposeColor(uPurpose), 1.0); return; }    // purpose
        if (uRenderMode == 29) { fragColor = vec4(kindColor(uKind), 1.0); return; }          // kind
        if (uRenderMode == 30) {                                                             // udim tile
            int tile = int(floor(vUV.x)) + 10 * int(floor(vUV.y));
            fragColor = vec4(idColor(tile), 1.0); return;
        }
        if (uRenderMode == 34) {                                                             // source USD face id
            int fid = uHasFaceId ? int(texelFetch(uFaceIdTex, uFaceBase + gl_PrimitiveID).r) : -1;
            fragColor = vec4(idColor(fid), 1.0); return;
        }
        if (uRenderMode == 33) {                                                             // texel density (UV/world area ratio)
            vec2 du = dFdx(vUV), dv = dFdy(vUV);
            float uvArea = abs(du.x * dv.y - dv.x * du.y);
            float worldArea = length(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
            float td = sqrt(uvArea / max(worldArea, 1e-12));
            float c = clamp(td * uDepthScale * 0.5, 0.0, 1.0);  // scene-relative
            fragColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0); return;
        }
        if (uRenderMode == 31) { fragColor = vec4(fract(vUV1), 0.0, 1.0); return; }          // uv set 1
        if (uRenderMode == 32) {                                                             // blendshape influence
            // Normalize by ~10% of the scene extent into a blue->red heatmap.
            float c = clamp(vMorphInfl / max(uDepthScale * 0.1, 1e-4), 0.0, 1.0);
            fragColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0); return;
        }
        if (uRenderMode == 21) {                                                             // skin weights
            vec3 jc = idColor(vDomJoint);                  // dominant joint -> hashed color
            fragColor = vec4(jc * (0.3 + 0.7 * clamp(vDomWeight, 0.0, 1.0)), 1.0);
            return;
        }
        if (uRenderMode == 22) {                                                             // tangent (from UV gradient)
            vec3 dp1 = dFdx(vWorldPos), dp2 = dFdy(vWorldPos);
            vec2 du1 = dFdx(vUV), du2 = dFdy(vUV);
            float r = du1.x * du2.y - du2.x * du1.y;
            vec3 T = dp1 * du2.y - dp2 * du1.y;
            T = (abs(r) > 1e-8) ? T / r : dp1;
            fragColor = vec4(normalize(T) * 0.5 + 0.5, 1.0);
            return;
        }
        if (uRenderMode == 25) {                                                             // curvature
            vec3 n = normalize(vNormal);
            float c = clamp((length(dFdx(n)) + length(dFdy(n))) * 8.0, 0.0, 1.0);
            // blue (flat) -> red (high curvature) heatmap
            fragColor = vec4(c, 1.0 - abs(c - 0.5) * 2.0, 1.0 - c, 1.0);
            return;
        }
        if (uRenderMode == 26) { fragColor = vec4(idColor(-1), 1.0); return; }  // instance id: non-instanced -> gray
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Real-time Cook-Torrance preview. Face two-sided shading normals toward the
    // camera, then evaluate an energy-conserving GGX direct lobe. The constant
    // ambient fallback below keeps scenes without authored lighting readable.
    vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
    vec3 coatNf = (dot(coatN, V) < 0.0) ? -coatN : coatN;
    float NoV = max(dot(Nf, V), 1e-4);
    float rgh = clamp(roughness, 0.02, 1.0);
    float met = clamp(metallic, 0.0, 1.0);
    vec3 F0 = computeF0(baseColor, met);
    if (uUseSpecularWorkflow != 0 || uOpenPbrSpecularModel != 0) {
        F0 *= sampleCoatColor(uSpecularColorTex, uHasSpecularColorTex,
                              uAdvancedTexIsUdim.x, uAdvancedUdimRoutes.x,
                              uAdvancedUdimSlots.x,
                              uSpecularColorUv0, uSpecularColorUv1,
                              uSpecularColorUvSet, uSpecularColorScale,
                              uSpecularColorBias, uSpecularColorPtexGrid);
    }
    if (uRenderMode == 39) { fragColor = vec4(F0, 1.0); return; }
    if (uRenderMode == 40) {
        float d = (max(uIor, 1.0) - 1.0) / (max(uIor, 1.0) + 1.0);
        fragColor = vec4(vec3(d * d), 1.0); return;
    }
    float cw = clamp(uCoatWeight *
                     sampleCoatScalar(uCoatWeightTex, uHasCoatWeightTex,
                                      uAdvancedTexIsUdim.y,
                                      uAdvancedUdimRoutes.y,
                                      uAdvancedUdimSlots.y,
                                      uCoatWeightUv0, uCoatWeightUv1,
                                      uCoatWeightUvSet, uCoatWeightChannel,
                                      uCoatWeightScale, uCoatWeightBias,
                                      uCoatWeightPtexGrid),
                     0.0, 1.0);
    float cr = clamp(uCoatRoughness *
                     sampleCoatScalar(uCoatRoughnessTex, uHasCoatRoughnessTex,
                                      uAdvancedTexIsUdim.w,
                                      uAdvancedUdimRoutes.w,
                                      uAdvancedUdimSlots.w,
                                      uCoatRoughnessUv0, uCoatRoughnessUv1,
                                      uCoatRoughnessUvSet,
                                      uCoatRoughnessChannel,
                                      uCoatRoughnessScale, uCoatRoughnessBias,
                                      uCoatRoughnessPtexGrid),
                     0.02, 1.0);
    vec3 coatTint = uCoatColor * sampleCoatColor(uCoatColorTex, uHasCoatColorTex,
                                                 uAdvancedTexIsUdim.z,
                                                 uAdvancedUdimRoutes.z,
                                                 uAdvancedUdimSlots.z,
                                                 uCoatColorUv0, uCoatColorUv1,
                                                 uCoatColorUvSet,
                                                 uCoatColorScale,
                                                 uCoatColorBias,
                                                 uCoatColorPtexGrid);
    if (uRenderMode == 36) { fragColor = vec4(vec3(cw), 1.0); return; }
    if (uRenderMode == 37) { fragColor = vec4(coatTint, 1.0); return; }
    if (uRenderMode == 38) { fragColor = vec4(vec3(cr), 1.0); return; }
    float ci = max(uCoatIor, 1.0);
    float cd = (ci - 1.0) / (ci + 1.0);
    vec3 direct = vec3(0.0);
    for (int li = 0; li < kMaxRasterLights; ++li) {
        if (li >= uLightCount) break;
        if ((uLightMask & (1u << uint(li))) == 0u) continue;
        vec4 pt = uLightPositionType[li];
        vec4 da = uLightDirectionAngle[li];
        vec4 lc = uLightColorDiffuse[li];
        vec4 ss = uLightSpecularShape[li];
        int lightType = int(pt.w + 0.5);
        vec3 L;
        float attenuation = 1.0;
        if (lightType == 5) {
            L = normalize(da.xyz);
        } else {
            vec3 toLight = pt.xyz - vWorldPos;
            float dist2 = max(dot(toLight, toLight), 1e-6);
            L = toLight * inversesqrt(dist2);
            attenuation = 1.0 / dist2;
        }
        float shape = 1.0;
        if (ss.w > 0.5 && lightType != 5) {
            float coneCos = dot(normalize(da.xyz), -L);
            float outer = cos(radians(clamp(da.w, 0.0, 180.0)));
            float innerAngle = da.w * (1.0 - clamp(ss.y, 0.0, 1.0));
            float inner = cos(radians(clamp(innerAngle, 0.0, 180.0)));
            shape = smoothstep(outer, max(inner, outer + 1e-5), coneCos);
            shape *= pow(max(coneCos, 0.0), max(ss.z, 0.0));
        }
        float NoL = max(dot(Nf, L), 0.0);
        if (NoL <= 0.0 || shape <= 0.0) continue;
        vec3 H = normalize(L + V);
        float NoH = max(dot(Nf, H), 0.0);
        float VoH = max(dot(V, H), 0.0);
        vec3 F = fresnelSchlick(VoH, F0);
        float D = distributionGGX(NoH, rgh);
        float G = geometrySchlickGGX(NoV, rgh) *
                  geometrySchlickGGX(NoL, rgh);
        vec3 specular = D * G * F / max(4.0 * NoV * NoL, 1e-5);
        vec3 diffuse = (vec3(1.0) - F) * (1.0 - met) * baseColor / kPi;
        float coatNoL = max(dot(coatNf, L), 0.0);
        float coatNoV = max(dot(coatNf, V), 1e-4);
        float coatNoH = max(dot(coatNf, H), 0.0);
        vec3 coatF = fresnelSchlick(VoH, vec3(cd * cd));
        float coatD = distributionGGX(coatNoH, cr);
        float coatG = geometrySchlickGGX(coatNoV, cr) *
                      geometrySchlickGGX(coatNoL, cr);
        vec3 coatSpec = coatD * coatG * coatF /
                        max(4.0 * coatNoV * coatNoL, 1e-5);
        vec3 baseBrdf = diffuse * lc.w +
                        specular * (vec3(1.0) - coatF * cw) * ss.x;
        vec3 coatBrdf = coatSpec * coatTint * cw * ss.x;
        float visibility = (li == uShadowLightSlot)
                               ? sampleShadow(vWorldPos, Nf, L) : 1.0;
        direct += (baseBrdf * NoL + coatBrdf * coatNoL) * lc.rgb *
                  (attenuation * shape * visibility);
    }
    // Scenes without authored direct lights retain the readable preview key.
    if (uLightCount == 0) {
        vec3 L = (dot(uLightDir, uLightDir) > 1e-8)
                     ? normalize(uLightDir)
                     : normalize(vec3(0.3, 0.5, 0.8));
        vec3 lightColor = (dot(uLightColor, uLightColor) > 1e-8)
                              ? uLightColor : vec3(1.0);
        float NoL = max(dot(Nf, L), 0.0);
        vec3 H = normalize(L + V);
        float NoH = max(dot(Nf, H), 0.0), VoH = max(dot(V, H), 0.0);
        vec3 F = fresnelSchlick(VoH, F0);
        vec3 specular = distributionGGX(NoH, rgh) *
                        geometrySchlickGGX(NoV, rgh) *
                        geometrySchlickGGX(NoL, rgh) * F /
                        max(4.0 * NoV * NoL, 1e-5);
        vec3 diffuse = (vec3(1.0) - F) * (1.0 - met) * baseColor / kPi;
        direct = (diffuse + specular) * lightColor * NoL;
    }

    // Ambient: DomeLight split-sum IBL when baked, else a constant floor
    // (keeps unlit faces lifted off black).
    vec3 ambient;
    if (uHasIbl) {
        vec3 Ne = normalize(uEnvRotation * Nf);
        vec3 Re = normalize(uEnvRotation * reflect(-V, Nf));
        vec3 irr = texture(uIrradianceMap, Ne).rgb;
        float lod = clamp(roughness, 0.0, 1.0) * float(uPrefilteredLods - 1);
        vec3 pref = textureLod(uPrefilteredMap, Re, lod).rgb;
        // Analytic split-sum approximation keeps the GL 3.3 fragment sampler
        // count within the guaranteed 16 after adding independent roughness.
        float rr = clamp(roughness, 0.0, 1.0);
        vec4 r = rr * vec4(-1.0, -0.0275, -0.572, 0.022) +
                 vec4(1.0, 0.0425, 1.04, -0.04);
        float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
        vec2 dfg = vec2(-1.04, 1.04) * a004 + r.zw;
        ambient = (baseColor * (1.0 - met) * irr +
                   pref * (F0 * dfg.x + dfg.y)) * uIblColor;
        vec3 coatPref = textureLod(uPrefilteredMap, Re,
                                   cr * float(uPrefilteredLods - 1)).rgb;
        vec4 rc = cr * vec4(-1.0, -0.0275, -0.572, 0.022) +
                  vec4(1.0, 0.0425, 1.04, -0.04);
        float ca004 = min(rc.x * rc.x, exp2(-9.28 * NoV)) * rc.x + rc.y;
        vec2 coatDfg = vec2(-1.04, 1.04) * ca004 + rc.zw;
        vec3 coatF0 = vec3(cd * cd);
        vec3 coatIbl = coatPref * (coatF0 * coatDfg.x + coatDfg.y) *
                       coatTint * cw * uIblColor;
        ambient = ambient * (vec3(1.0) - coatF0 * cw) + coatIbl;
    } else {
        ambient = baseColor * 0.12;
    }
    if (uAlphaMode == 1 && opacity <= 0.0) {
        discard;
    }
    ambient *= clamp(uOcclusion, 0.0, 1.0) * sampleOcclusion();
    vec3 color = linearToSrgb((ambient + direct + emissive) * exp2(uExposure));
    fragColor = vec4(color, opacity);
}
)glsl";
}

// ==================== GL430 Bindless Shaders ====================
//
// UNUSED scaffold for a future GL 4.3 path (no callers; the renderer uses the
// GL330 shaders). STALE: these predate GPU blendshape morph, GPU skinning, and
// displacement, so activating them as-is would render none of those. Re-derive
// from the GL330 shaders (which carry the current morph/skin/displacement) before
// wiring up a 4.3 path.

const char* getMaterialVertexShaderGL430() {
    return R"glsl(#version 430 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUV;

uniform mat4 uModelViewProj;
uniform mat4 uModel;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vUV = aUV.xy;
    gl_Position = uModelViewProj * vec4(aPosition, 1.0);
}
)glsl";
}

const char* getMaterialFragmentShaderGL430() {
    return R"glsl(#version 430 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

// Materials SSBO: each material is 4 x vec4 (16 floats)
layout(std430, binding = 1) readonly buffer Materials {
    vec4 materials[];
};

// Per-triangle material ID
layout(std430, binding = 2) readonly buffer FaceMaterialIds {
    uint faceMaterialId[];
};

uniform vec3 uCameraPos;
uniform sampler2DArray uTextureArray;

out vec4 fragColor;

void main() {
    int matId = int(faceMaterialId[gl_PrimitiveID]);
    int base = matId * 4;

    vec3 baseColor = materials[base + 0].xyz;
    float metallic = materials[base + 0].w;
    vec3 emissive  = materials[base + 1].xyz;
    float roughness = materials[base + 1].w;
    float alpha     = materials[base + 2].x;

    // Texture indices from 4th vec4
    float baseColorTexIdx  = materials[base + 3].x;
    float metalRoughTexIdx = materials[base + 3].y;
    float normalTexIdx     = materials[base + 3].z;
    float emissiveTexIdx   = materials[base + 3].w;

    if (baseColorTexIdx >= 0.0) {
        baseColor *= texture(uTextureArray, vec3(vUV, baseColorTexIdx)).rgb;
    }
    if (metalRoughTexIdx >= 0.0) {
        vec4 mr = texture(uTextureArray, vec3(vUV, metalRoughTexIdx));
        roughness *= mr.g;
        metallic *= mr.b;
    }
    if (emissiveTexIdx >= 0.0) {
        emissive *= texture(uTextureArray, vec3(vUV, emissiveTexIdx)).rgb;
    }

    vec3 N = normalize(vNormal);
    if (normalTexIdx >= 0.0) {
        vec3 tangentNormal = texture(uTextureArray, vec3(vUV, normalTexIdx)).xyz * 2.0 - 1.0;
        N = normalize(N + tangentNormal * 0.1);
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Soft camera-headlight shading (USD-viewer look). Face the shading normal
    // toward the camera so back / grazing faces never read as pure black, then
    // combine a view-aligned headlight (N.V) with a gentle half-Lambert key and an
    // ambient floor -- soft, no hard terminator, no unlit black facets.
    vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
    float facing = max(dot(Nf, V), 0.0);                 // N.V headlight
    vec3 L = normalize(vec3(0.3, 0.5, 0.8));
    float key = dot(Nf, L) * 0.5 + 0.5;                  // half-Lambert, never 0
    float shade = 0.6 * facing + 0.4 * key;              // [0,1]

    // Ambient floor + view-driven diffuse (keeps unlit faces lifted off black).
    vec3 ambient = baseColor * 0.25;
    vec3 diffuse = baseColor * (1.0 - metallic) * (0.75 * shade);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(Nf, H), 0.0);
    float specPower = mix(16.0, 256.0, 1.0 - roughness);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specColor * pow(NdotH, specPower) * facing;

    vec3 color = ambient + diffuse + specular + emissive;
    fragColor = vec4(color, alpha);
}
)glsl";
}

// ==================== Vulkan 450 Shaders ====================
//
// UNUSED: the Vulkan backend compiles vk/shaders/*.vert|frag to SPIR-V (which
// carry the current morph/skin/displacement) rather than these strings. STALE
// (predate that work); kept only as a GLSL reference. Don't wire these up without
// re-deriving from vk/shaders/.

const char* getMaterialVertexShaderVK450() {
    return R"glsl(#version 450

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec3 aUV;

layout(push_constant) uniform PushConstants {
    mat4 modelViewProj;
    mat4 model;
} pc;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;

void main() {
    vec4 worldPos = pc.model * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    // Approximate normal matrix (works for uniform scale)
    vNormal = normalize(mat3(pc.model) * aNormal);
    vUV = aUV.xy;
    gl_Position = pc.modelViewProj * vec4(aPosition, 1.0);
}
)glsl";
}

const char* getMaterialFragmentShaderVK450() {
    return R"glsl(#version 450

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;

// Materials SSBO: each material is 4 x vec4 (16 floats)
layout(set = 0, binding = 1) readonly buffer Materials {
    vec4 materials[];
};

// Per-triangle material ID
layout(set = 0, binding = 2) readonly buffer FaceMaterialIds {
    uint faceMaterialId[];
};

layout(set = 0, binding = 0) uniform SceneUBO {
    vec3 cameraPos;
};

layout(set = 0, binding = 3) uniform sampler2DArray uTextureArray;

layout(location = 0) out vec4 fragColor;

void main() {
    int matId = int(faceMaterialId[gl_PrimitiveID]);
    int base = matId * 4;

    vec3 baseColor = materials[base + 0].xyz;
    float metallic = materials[base + 0].w;
    vec3 emissive  = materials[base + 1].xyz;
    float roughness = materials[base + 1].w;
    float alpha     = materials[base + 2].x;

    // Texture indices from 4th vec4
    float baseColorTexIdx  = materials[base + 3].x;
    float metalRoughTexIdx = materials[base + 3].y;
    float normalTexIdx     = materials[base + 3].z;
    float emissiveTexIdx   = materials[base + 3].w;

    if (baseColorTexIdx >= 0.0) {
        baseColor *= texture(uTextureArray, vec3(vUV, baseColorTexIdx)).rgb;
    }
    if (metalRoughTexIdx >= 0.0) {
        vec4 mr = texture(uTextureArray, vec3(vUV, metalRoughTexIdx));
        roughness *= mr.g;
        metallic *= mr.b;
    }
    if (emissiveTexIdx >= 0.0) {
        emissive *= texture(uTextureArray, vec3(vUV, emissiveTexIdx)).rgb;
    }

    vec3 N = normalize(vNormal);
    if (normalTexIdx >= 0.0) {
        vec3 tangentNormal = texture(uTextureArray, vec3(vUV, normalTexIdx)).xyz * 2.0 - 1.0;
        N = normalize(N + tangentNormal * 0.1);
    }

    vec3 V = normalize(cameraPos - vWorldPos);

    // Soft camera-headlight shading (USD-viewer look). Face the shading normal
    // toward the camera so back / grazing faces never read as pure black, then
    // combine a view-aligned headlight (N.V) with a gentle half-Lambert key and an
    // ambient floor -- soft, no hard terminator, no unlit black facets.
    vec3 Nf = (dot(N, V) < 0.0) ? -N : N;
    float facing = max(dot(Nf, V), 0.0);                 // N.V headlight
    vec3 L = normalize(vec3(0.3, 0.5, 0.8));
    float key = dot(Nf, L) * 0.5 + 0.5;                  // half-Lambert, never 0
    float shade = 0.6 * facing + 0.4 * key;              // [0,1]

    // Ambient floor + view-driven diffuse (keeps unlit faces lifted off black).
    vec3 ambient = baseColor * 0.25;
    vec3 diffuse = baseColor * (1.0 - metallic) * (0.75 * shade);

    vec3 H = normalize(L + V);
    float NdotH = max(dot(Nf, H), 0.0);
    float specPower = mix(16.0, 256.0, 1.0 - roughness);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specColor * pow(NdotH, specPower) * facing;

    vec3 color = ambient + diffuse + specular + emissive;
    fragColor = vec4(color, alpha);
}
)glsl";
}

} // namespace light3d
