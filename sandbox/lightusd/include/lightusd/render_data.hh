// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Render Data for WebGPU/graphics APIs
// Provides triangulated, indexed mesh data ready for GPU upload

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "lightusd/result.hh"

namespace lightusd {
namespace v1 {

// Forward declarations
class Stage;
class Prim;

// ============================================================================
// Basic types
// ============================================================================

struct Vec2 {
    float x, y;
    Vec2() : x(0), y(0) {}
    Vec2(float x_, float y_) : x(x_), y(y_) {}
};

struct Vec3 {
    float x, y, z;
    Vec3() : x(0), y(0), z(0) {}
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
};

struct Vec4 {
    float x, y, z, w;
    Vec4() : x(0), y(0), z(0), w(0) {}
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

struct Mat4 {
    float m[16];
    Mat4();
    static Mat4 identity();
    static Mat4 translate(float x, float y, float z);
    static Mat4 scale(float x, float y, float z);
    Mat4 operator*(const Mat4& other) const;
    Vec3 transform_point(const Vec3& p) const;
    Vec3 transform_normal(const Vec3& n) const;
};

struct AABB {
    Vec3 min;
    Vec3 max;
    AABB();
    void expand(const Vec3& p);
    void expand(const AABB& other);
    Vec3 center() const;
    Vec3 size() const;
    bool is_valid() const;
};

// ============================================================================
// Texture data
// ============================================================================

/// Texture decode mode
enum class TextureDecodeMode {
    Browser,    // Use browser's native decode (PNG, JPEG, etc.)
    WebGPU,     // Decode in WebGPU shader (limited HDR/EXR support)
    Native      // Decode in C++ (full HDR/EXR support)
};

/// Texture format
enum class TextureFormat {
    Unknown,
    R8,
    RG8,
    RGB8,
    RGBA8,
    R16F,
    RG16F,
    RGB16F,
    RGBA16F,
    R32F,
    RG32F,
    RGB32F,
    RGBA32F
};

/// Texture data (ready for GPU upload)
struct RenderTexture {
    std::string name;
    std::string uri;            // Original URI/path

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t channels = 0;      // 1-4
    TextureFormat format = TextureFormat::Unknown;

    // Decoded pixel data (when decode_mode is Native)
    // For browser decode, this is empty and uri is used directly
    std::vector<uint8_t> data_u8;    // For 8-bit formats
    std::vector<float> data_f32;     // For float formats (HDR)

    // For browser decode - raw file bytes
    std::vector<uint8_t> file_data;
    std::string mime_type;           // "image/png", "image/jpeg", etc.

    bool is_hdr = false;
    bool is_valid() const { return width > 0 && height > 0; }
};

// ============================================================================
// Material data
// ============================================================================

struct RenderMaterial {
    std::string name;
    std::string path;           // USD path

    // Base color
    Vec4 base_color = {0.8f, 0.8f, 0.8f, 1.0f};
    int32_t base_color_texture = -1;    // Index to RenderScene::textures

    // Metallic-Roughness
    float metallic = 0.0f;
    float roughness = 0.5f;
    int32_t metallic_roughness_texture = -1;

    // Normal map
    int32_t normal_texture = -1;
    float normal_scale = 1.0f;

    // Emission
    Vec3 emissive = {0.0f, 0.0f, 0.0f};
    int32_t emissive_texture = -1;

    // Occlusion
    int32_t occlusion_texture = -1;
    float occlusion_strength = 1.0f;

    // Other
    bool double_sided = false;
    float alpha_cutoff = 0.5f;

    bool is_valid() const { return !name.empty(); }
};

// ============================================================================
// Mesh data (triangulated, ready for GPU)
// ============================================================================

/// Vertex attribute
struct VertexAttribute {
    std::vector<float> data;    // Interleaved or flat
    uint32_t component_count;   // 1, 2, 3, or 4
    uint32_t vertex_count;

    VertexAttribute() : component_count(0), vertex_count(0) {}
    bool empty() const { return data.empty(); }
    size_t byte_size() const { return data.size() * sizeof(float); }
};

/// Submesh (for multi-material meshes)
struct SubMesh {
    uint32_t index_offset = 0;
    uint32_t index_count = 0;
    int32_t material_index = -1;    // Index to RenderScene::materials
};

/// Triangulated mesh ready for GPU
struct RenderMesh {
    std::string name;
    std::string path;           // USD path

    // Vertex data (all triangulated, same vertex count)
    VertexAttribute positions;  // vec3
    VertexAttribute normals;    // vec3
    VertexAttribute texcoords0; // vec2 (primary UV)
    VertexAttribute texcoords1; // vec2 (secondary UV, optional)
    VertexAttribute tangents;   // vec4 (xyz = tangent, w = bitangent sign)
    VertexAttribute colors;     // vec4 (vertex colors, optional)

    // Index buffer (triangles, 3 indices per face)
    std::vector<uint32_t> indices;

    // Submeshes for multi-material
    std::vector<SubMesh> submeshes;

    // Bounding box
    AABB bounds;

    // Transform (local to world)
    Mat4 transform;

    // Rendering flags
    bool double_sided = false;
    bool is_visible = true;

    // Helper methods
    uint32_t vertex_count() const { return positions.vertex_count; }
    uint32_t triangle_count() const { return static_cast<uint32_t>(indices.size() / 3); }
    bool is_valid() const { return !positions.empty() && !indices.empty(); }
};

// ============================================================================
// Scene data
// ============================================================================

/// Camera
struct RenderCamera {
    std::string name;
    std::string path;

    Mat4 transform;

    // Perspective projection
    float fov_y = 45.0f;        // Degrees
    float aspect = 1.0f;
    float near_z = 0.1f;
    float far_z = 1000.0f;

    // Orthographic (if fov_y <= 0)
    float ortho_width = 10.0f;

    bool is_perspective() const { return fov_y > 0.0f; }
};

/// Light
struct RenderLight {
    enum class Type { Directional, Point, Spot, Area };

    std::string name;
    std::string path;
    Type type = Type::Point;

    Mat4 transform;
    Vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;

    // Spot light
    float inner_cone_angle = 30.0f;  // Degrees
    float outer_cone_angle = 45.0f;

    // Point/Spot light
    float range = 100.0f;
};

/// Complete render scene
struct RenderScene {
    std::string name;
    std::string up_axis = "Y";
    float meters_per_unit = 0.01f;

    std::vector<RenderMesh> meshes;
    std::vector<RenderMaterial> materials;
    std::vector<RenderTexture> textures;
    std::vector<RenderCamera> cameras;
    std::vector<RenderLight> lights;

    // Scene bounds (union of all mesh bounds)
    AABB bounds;

    // Default camera index (-1 if none)
    int32_t default_camera = -1;

    bool is_valid() const { return !meshes.empty(); }
};

// ============================================================================
// Converter configuration
// ============================================================================

struct RenderConverterConfig {
    // Mesh conversion
    bool triangulate = true;            // Convert quads/ngons to triangles
    bool compute_normals = true;        // Generate normals if missing
    bool compute_tangents = true;       // Generate tangents for normal mapping
    bool flip_winding = false;          // Reverse triangle winding

    // Texture handling
    TextureDecodeMode texture_decode = TextureDecodeMode::Browser;
    bool load_textures = true;          // Load texture files
    uint32_t max_texture_size = 4096;   // Resize if larger

    // Transform
    bool apply_transforms = true;       // Bake transforms into vertices
    bool y_up_to_z_up = false;          // Convert Y-up to Z-up

    // LOD/Quality
    float time = 0.0;                   // Time code for animated data
};

// ============================================================================
// Converter
// ============================================================================

class RenderConverter {
public:
    RenderConverter();
    ~RenderConverter();

    /// Convert Stage to RenderScene
    Result<RenderScene> convert(const Stage& stage, const RenderConverterConfig& config = {});

    /// Convert single Mesh prim
    Result<RenderMesh> convert_mesh(const Prim& prim, const RenderConverterConfig& config = {});

    /// Get last error message
    const std::string& error() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Utility functions
// ============================================================================

/// Triangulate polygon indices
/// faceVertexIndices: USD-style indices
/// faceVertexCounts: vertices per face (3 for tris, 4 for quads, etc.)
/// Returns: triangle indices (3 per triangle)
std::vector<uint32_t> triangulate_indices(
    const std::vector<uint32_t>& face_vertex_indices,
    const std::vector<uint32_t>& face_vertex_counts);

/// Compute flat normals from triangulated mesh
std::vector<Vec3> compute_flat_normals(
    const std::vector<Vec3>& positions,
    const std::vector<uint32_t>& indices);

/// Compute smooth normals (averaged at vertices)
std::vector<Vec3> compute_smooth_normals(
    const std::vector<Vec3>& positions,
    const std::vector<uint32_t>& indices);

/// Compute tangents for normal mapping (MikkTSpace algorithm)
std::vector<Vec4> compute_tangents(
    const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals,
    const std::vector<Vec2>& texcoords,
    const std::vector<uint32_t>& indices);

/// Compute bounding box
AABB compute_bounds(const std::vector<Vec3>& positions);

} // namespace v1
} // namespace lightusd
