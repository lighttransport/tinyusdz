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
layout(location = 9) in vec3 aColor;  // per-vertex displayColor (default white)

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

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;
out vec3 vColor;

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
            pos = (skin * vec4(aPosition, 1.0)).xyz;
            nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
        }
    } else if (uSkinningEnabled && wsum > 0.0 && int(maxJoint) < uBoneMatrixCount) {
        mat4 skin =
            fetchBone(aJoint.x) * aWeight.x +
            fetchBone(aJoint.y) * aWeight.y +
            fetchBone(aJoint.z) * aWeight.z +
            fetchBone(aJoint.w) * aWeight.w;
        pos = (skin * vec4(aPosition, 1.0)).xyz;
        nrm = normalize((skin * vec4(aNormal, 0.0)).xyz);
    }
    vec4 worldPos = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * nrm);
    vUV = aUV.xy;
    vColor = aColor;
    gl_Position = uModelViewProj * vec4(pos, 1.0);
}
)glsl";
}

const char* getMaterialFragmentShaderGL330() {
    return R"glsl(#version 330 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;
in vec3 vColor;

// Material uniforms (one draw call per submesh)
uniform vec3 uBaseColor;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform float uAlpha;
// When set, shade with the geometric (screen-derivative) normal -- used for
// meshes without authored normals so hard surfaces aren't smeared by smooth
// (averaged) normals.
uniform bool uGeometricNormal;

uniform vec3 uCameraPos;

// Texture samplers
uniform sampler2D uBaseColorTex;
uniform bool uHasBaseColorTex;
uniform sampler2D uMetalRoughTex;
uniform bool uHasMetalRoughTex;
uniform sampler2D uNormalTex;
uniform bool uHasNormalTex;
uniform sampler2D uEmissiveTex;
uniform bool uHasEmissiveTex;

out vec4 fragColor;

uniform int uRenderMode;     // RenderMode (see renderer.hh)
uniform int uMatId;          // per-draw material id (material-id viz; -1 = none)
uniform float uDepthScale;   // depth AOV: camera-distance normalizer (scene extent)
uniform vec3 uSceneMin;      // position AOV: scene bbox min
uniform vec3 uSceneExtent;   // position AOV: scene bbox size (max-min)
uniform int uMeshId;         // mesh-id AOV (per-draw mesh index)
uniform bool uDoubleSided;   // double-sided AOV flag
uniform int uPurpose;        // purpose AOV: 0=default/1=render/2=proxy/3=guide

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

void main() {
    vec3 baseColor = uBaseColor * vColor;  // vColor defaults to white
    float metallic = uMetallic;
    float roughness = uRoughness;
    vec3 emissive = uEmissive;

    if (uHasBaseColorTex) {
        baseColor *= texture(uBaseColorTex, vUV).rgb;
    }
    if (uHasMetalRoughTex) {
        vec4 mr = texture(uMetalRoughTex, vUV);
        roughness *= mr.g;
        metallic *= mr.b;
    }
    if (uHasEmissiveTex) {
        emissive *= texture(uEmissiveTex, vUV).rgb;
    }

    vec3 N = uGeometricNormal
                 ? normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)))
                 : normalize(vNormal);
    if (uHasNormalTex) {
        vec3 tangentNormal = texture(uNormalTex, vUV).xyz * 2.0 - 1.0;
        N = normalize(N + tangentNormal * 0.1);
    }

    // Debug AOVs: override the shaded output with the requested channel.
    if (uRenderMode != 0) {
        vec3 Ngeo = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
        if (uRenderMode == 2) { fragColor = vec4(N * 0.5 + 0.5, 1.0); return; }       // shading normal
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
        if (uRenderMode == 12) { fragColor = vec4(vec3(uAlpha), 1.0); return; }        // opacity
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
    }

    vec3 V = normalize(uCameraPos - vWorldPos);

    // Simple directional light
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = max(dot(N, L), 0.0);

    // Simple PBR-ish shading
    vec3 diffuse = baseColor * (1.0 - metallic) * NdotL;

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specPower = mix(16.0, 256.0, 1.0 - roughness);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specColor * pow(NdotH, specPower);

    // Ambient
    vec3 ambient = baseColor * 0.05;

    vec3 color = ambient + diffuse + specular + emissive;
    fragColor = vec4(color, uAlpha);
}
)glsl";
}

// ==================== GL430 Bindless Shaders ====================

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

    // Simple directional light
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = max(dot(N, L), 0.0);

    // Simple PBR-ish shading
    vec3 diffuse = baseColor * (1.0 - metallic) * NdotL;

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specPower = mix(16.0, 256.0, 1.0 - roughness);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specColor * pow(NdotH, specPower);

    // Ambient
    vec3 ambient = baseColor * 0.05;

    vec3 color = ambient + diffuse + specular + emissive;
    fragColor = vec4(color, alpha);
}
)glsl";
}

// ==================== Vulkan 450 Shaders ====================

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

    // Simple directional light
    vec3 L = normalize(vec3(1.0, 1.0, 1.0));
    float NdotL = max(dot(N, L), 0.0);

    // Simple PBR-ish shading
    vec3 diffuse = baseColor * (1.0 - metallic) * NdotL;

    vec3 H = normalize(L + V);
    float NdotH = max(dot(N, H), 0.0);
    float specPower = mix(16.0, 256.0, 1.0 - roughness);
    vec3 specColor = mix(vec3(0.04), baseColor, metallic);
    vec3 specular = specColor * pow(NdotH, specPower);

    // Ambient
    vec3 ambient = baseColor * 0.05;

    vec3 color = ambient + diffuse + specular + emissive;
    fragColor = vec4(color, alpha);
}
)glsl";
}

} // namespace light3d
