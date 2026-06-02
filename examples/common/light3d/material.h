#pragma once

#include "light3d.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace light3d {

struct MeshGeometry;  // forward declaration

// --- Material Definition ---

enum class AlphaMode { Opaque, Mask, Blend };

struct Material {
    std::string name;
    int id = 0;

    // PBR metallic-roughness
    Vec3 baseColor{0.8f, 0.8f, 0.8f};
    float metallic = 0.0f;
    float roughness = 0.5f;

    // Emission
    Vec3 emissive{0.0f, 0.0f, 0.0f};

    // Alpha
    float alpha = 1.0f;
    AlphaMode alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;

    bool doubleSided = false;

    // Texture indices into TextureLibrary (-1 = no texture)
    int baseColorTexture = -1;
    int metallicRoughnessTexture = -1;
    int normalTexture = -1;
    int emissiveTexture = -1;
};

// --- Material Library ---

class MaterialLibrary {
public:
    // Add a material; auto-assigns sequential ID. Returns the assigned ID.
    int addMaterial(Material material);

    // Retrieve material by ID. Returns nullptr if not found.
    const Material* getMaterial(int id) const;
    Material* getMaterial(int id);

    // Find material by name. Returns nullptr if not found.
    const Material* findByName(const std::string& name) const;

    // Number of materials in the library.
    int count() const { return static_cast<int>(materials_.size()); }

    // Access all materials (ordered by ID).
    const std::vector<Material>& materials() const { return materials_; }

private:
    std::vector<Material> materials_;
    std::unordered_map<std::string, int> nameToIndex_;
};

// --- Submesh Approach (WebGL / GL3) ---

struct Submesh {
    int materialId;
    int triangleOffset;  // start index in triangulated index buffer (in triangles)
    int triangleCount;   // number of triangles
};

struct SubmeshData {
    std::vector<Submesh> submeshes;          // sorted by materialId
    std::vector<uint32_t> triangleIndices;   // fully triangulated, grouped by material
};

SubmeshData buildSubmeshes(const MeshGeometry& geometry);

// --- Bindless Approach (GL4.3+ / Vulkan) ---

// GPU-packed material: 4 x vec4 = 16 floats per material (std430 aligned)
//   vec4(baseColor.rgb, metallic)
//   vec4(emissive.rgb, roughness)
//   vec4(alpha, alphaCutoff, doubleSided, 0)
//   vec4(baseColorTexIdx, metalRoughTexIdx, normalTexIdx, emissiveTexIdx)
constexpr int kPackedMaterialFloats = 16;

std::vector<float> packMaterialsToBuffer(const MaterialLibrary& library);

// Per-triangle material ID buffer (for SSBO, indexed by gl_PrimitiveID)
struct TriangleMaterialIdData {
    std::vector<uint32_t> materialIds;     // one per triangle
    std::vector<uint32_t> triangleIndices; // triangulated index buffer
};

TriangleMaterialIdData buildTriangleMaterialIds(const MeshGeometry& geometry);

// --- Shader Source Strings ---

// Submesh shaders (GL330): material via uniforms, one draw call per submesh
const char* getMaterialVertexShaderGL330();
const char* getMaterialFragmentShaderGL330();

// Bindless shaders (GL430): materials SSBO + face material ID SSBO + gl_PrimitiveID
const char* getMaterialVertexShaderGL430();
const char* getMaterialFragmentShaderGL430();

// Vulkan 450: same SSBO approach with push constants
const char* getMaterialVertexShaderVK450();
const char* getMaterialFragmentShaderVK450();

} // namespace light3d
