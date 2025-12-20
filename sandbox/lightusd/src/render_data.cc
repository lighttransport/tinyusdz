// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Render Data Implementation
// Mesh triangulation, normal/tangent generation, scene conversion

#include "lightusd/render_data.hh"
#include "lightusd/stage.hh"
#include "lightusd/prim.hh"
#include "lightusd/value.hh"
#include "lightusd/relationship.hh"
#include "lightusd/lightexr.hh"
#include "lightusd/lighthdr.hh"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <unordered_map>

namespace lightusd {
namespace v1 {

// ============================================================================
// Mat4 Implementation
// ============================================================================

Mat4::Mat4() {
    for (int i = 0; i < 16; i++) m[i] = 0.0f;
}

Mat4 Mat4::identity() {
    Mat4 result;
    result.m[0] = 1.0f;
    result.m[5] = 1.0f;
    result.m[10] = 1.0f;
    result.m[15] = 1.0f;
    return result;
}

Mat4 Mat4::translate(float x, float y, float z) {
    Mat4 result = identity();
    result.m[12] = x;
    result.m[13] = y;
    result.m[14] = z;
    return result;
}

Mat4 Mat4::scale(float x, float y, float z) {
    Mat4 result = identity();
    result.m[0] = x;
    result.m[5] = y;
    result.m[10] = z;
    return result;
}

Mat4 Mat4::rotate_x(float radians) {
    Mat4 result = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    result.m[5] = c;
    result.m[6] = s;
    result.m[9] = -s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::rotate_y(float radians) {
    Mat4 result = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    result.m[0] = c;
    result.m[2] = -s;
    result.m[8] = s;
    result.m[10] = c;
    return result;
}

Mat4 Mat4::rotate_z(float radians) {
    Mat4 result = identity();
    float c = std::cos(radians);
    float s = std::sin(radians);
    result.m[0] = c;
    result.m[1] = s;
    result.m[4] = -s;
    result.m[5] = c;
    return result;
}

Mat4 Mat4::rotate_xyz(float rx, float ry, float rz) {
    // Apply rotations in XYZ order
    return rotate_x(rx) * rotate_y(ry) * rotate_z(rz);
}

Mat4 Mat4::from_quaternion(float x, float y, float z, float w) {
    Mat4 result = identity();

    float xx = x * x;
    float yy = y * y;
    float zz = z * z;
    float xy = x * y;
    float xz = x * z;
    float yz = y * z;
    float wx = w * x;
    float wy = w * y;
    float wz = w * z;

    result.m[0] = 1.0f - 2.0f * (yy + zz);
    result.m[1] = 2.0f * (xy + wz);
    result.m[2] = 2.0f * (xz - wy);

    result.m[4] = 2.0f * (xy - wz);
    result.m[5] = 1.0f - 2.0f * (xx + zz);
    result.m[6] = 2.0f * (yz + wx);

    result.m[8] = 2.0f * (xz + wy);
    result.m[9] = 2.0f * (yz - wx);
    result.m[10] = 1.0f - 2.0f * (xx + yy);

    return result;
}

Mat4 Mat4::from_double_matrix(const double* m16) {
    Mat4 result;
    for (int i = 0; i < 16; i++) {
        result.m[i] = static_cast<float>(m16[i]);
    }
    return result;
}

Mat4 Mat4::operator*(const Mat4& other) const {
    Mat4 result;
    for (int row = 0; row < 4; row++) {
        for (int col = 0; col < 4; col++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++) {
                sum += m[row * 4 + k] * other.m[k * 4 + col];
            }
            result.m[row * 4 + col] = sum;
        }
    }
    return result;
}

Vec3 Mat4::transform_point(const Vec3& p) const {
    float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (w != 0.0f && w != 1.0f) {
        x /= w;
        y /= w;
        z /= w;
    }
    return Vec3(x, y, z);
}

Vec3 Mat4::transform_normal(const Vec3& n) const {
    // Transform normal using upper-left 3x3 (no translation)
    // For non-uniform scale, should use inverse transpose
    float x = m[0] * n.x + m[4] * n.y + m[8] * n.z;
    float y = m[1] * n.x + m[5] * n.y + m[9] * n.z;
    float z = m[2] * n.x + m[6] * n.y + m[10] * n.z;
    // Normalize
    float len = std::sqrt(x * x + y * y + z * z);
    if (len > 0.0f) {
        x /= len;
        y /= len;
        z /= len;
    }
    return Vec3(x, y, z);
}

// ============================================================================
// AABB Implementation
// ============================================================================

AABB::AABB() {
    min.x = min.y = min.z = std::numeric_limits<float>::max();
    max.x = max.y = max.z = -std::numeric_limits<float>::max();
}

void AABB::expand(const Vec3& p) {
    min.x = std::min(min.x, p.x);
    min.y = std::min(min.y, p.y);
    min.z = std::min(min.z, p.z);
    max.x = std::max(max.x, p.x);
    max.y = std::max(max.y, p.y);
    max.z = std::max(max.z, p.z);
}

void AABB::expand(const AABB& other) {
    if (other.is_valid()) {
        expand(other.min);
        expand(other.max);
    }
}

Vec3 AABB::center() const {
    return Vec3(
        (min.x + max.x) * 0.5f,
        (min.y + max.y) * 0.5f,
        (min.z + max.z) * 0.5f
    );
}

Vec3 AABB::size() const {
    return Vec3(
        max.x - min.x,
        max.y - min.y,
        max.z - min.z
    );
}

bool AABB::is_valid() const {
    return min.x <= max.x && min.y <= max.y && min.z <= max.z;
}

// ============================================================================
// Triangulation
// ============================================================================

std::vector<uint32_t> triangulate_indices(
    const std::vector<uint32_t>& face_vertex_indices,
    const std::vector<uint32_t>& face_vertex_counts) {

    std::vector<uint32_t> triangles;

    // Estimate triangle count
    size_t estimated_tris = 0;
    for (uint32_t count : face_vertex_counts) {
        if (count >= 3) {
            estimated_tris += count - 2;
        }
    }
    triangles.reserve(estimated_tris * 3);

    size_t index_offset = 0;
    for (uint32_t count : face_vertex_counts) {
        if (count < 3) {
            // Skip degenerate faces
            index_offset += count;
            continue;
        }

        // Fan triangulation: vertex 0 connects to all other edges
        uint32_t v0 = face_vertex_indices[index_offset];
        for (uint32_t i = 1; i < count - 1; i++) {
            uint32_t v1 = face_vertex_indices[index_offset + i];
            uint32_t v2 = face_vertex_indices[index_offset + i + 1];
            triangles.push_back(v0);
            triangles.push_back(v1);
            triangles.push_back(v2);
        }

        index_offset += count;
    }

    return triangles;
}

// ============================================================================
// Normal Computation
// ============================================================================

namespace {

Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}

Vec3 normalize(const Vec3& v) {
    float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len > 0.0f) {
        return Vec3(v.x / len, v.y / len, v.z / len);
    }
    return Vec3(0.0f, 1.0f, 0.0f);  // Default up vector
}

Vec3 sub(const Vec3& a, const Vec3& b) {
    return Vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

Vec3 add(const Vec3& a, const Vec3& b) {
    return Vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

}  // namespace

std::vector<Vec3> compute_flat_normals(
    const std::vector<Vec3>& positions,
    const std::vector<uint32_t>& indices) {

    if (indices.size() % 3 != 0) {
        return {};
    }

    std::vector<Vec3> normals;
    normals.reserve(indices.size());

    for (size_t i = 0; i < indices.size(); i += 3) {
        const Vec3& p0 = positions[indices[i]];
        const Vec3& p1 = positions[indices[i + 1]];
        const Vec3& p2 = positions[indices[i + 2]];

        Vec3 edge1 = sub(p1, p0);
        Vec3 edge2 = sub(p2, p0);
        Vec3 normal = normalize(cross(edge1, edge2));

        // Same normal for all three vertices of the triangle
        normals.push_back(normal);
        normals.push_back(normal);
        normals.push_back(normal);
    }

    return normals;
}

std::vector<Vec3> compute_smooth_normals(
    const std::vector<Vec3>& positions,
    const std::vector<uint32_t>& indices) {

    if (indices.size() % 3 != 0 || positions.empty()) {
        return {};
    }

    // Accumulate face normals at each vertex
    std::vector<Vec3> normals(positions.size(), Vec3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vec3& p0 = positions[i0];
        const Vec3& p1 = positions[i1];
        const Vec3& p2 = positions[i2];

        Vec3 edge1 = sub(p1, p0);
        Vec3 edge2 = sub(p2, p0);
        Vec3 face_normal = cross(edge1, edge2);  // Not normalized (weighted by area)

        normals[i0] = add(normals[i0], face_normal);
        normals[i1] = add(normals[i1], face_normal);
        normals[i2] = add(normals[i2], face_normal);
    }

    // Normalize accumulated normals
    for (auto& n : normals) {
        n = normalize(n);
    }

    return normals;
}

// ============================================================================
// Tangent Computation (Simplified MikkTSpace-like algorithm)
// ============================================================================

std::vector<Vec4> compute_tangents(
    const std::vector<Vec3>& positions,
    const std::vector<Vec3>& normals,
    const std::vector<Vec2>& texcoords,
    const std::vector<uint32_t>& indices) {

    if (indices.size() % 3 != 0 || positions.empty() ||
        texcoords.size() != positions.size()) {
        return {};
    }

    // Accumulators for tangent and bitangent
    std::vector<Vec3> tan1(positions.size(), Vec3(0.0f, 0.0f, 0.0f));
    std::vector<Vec3> tan2(positions.size(), Vec3(0.0f, 0.0f, 0.0f));

    for (size_t i = 0; i < indices.size(); i += 3) {
        uint32_t i0 = indices[i];
        uint32_t i1 = indices[i + 1];
        uint32_t i2 = indices[i + 2];

        const Vec3& p0 = positions[i0];
        const Vec3& p1 = positions[i1];
        const Vec3& p2 = positions[i2];

        const Vec2& uv0 = texcoords[i0];
        const Vec2& uv1 = texcoords[i1];
        const Vec2& uv2 = texcoords[i2];

        Vec3 edge1 = sub(p1, p0);
        Vec3 edge2 = sub(p2, p0);

        float du1 = uv1.x - uv0.x;
        float dv1 = uv1.y - uv0.y;
        float du2 = uv2.x - uv0.x;
        float dv2 = uv2.y - uv0.y;

        float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-8f) {
            continue;  // Skip degenerate triangle
        }

        float inv_det = 1.0f / det;

        Vec3 tangent(
            (dv2 * edge1.x - dv1 * edge2.x) * inv_det,
            (dv2 * edge1.y - dv1 * edge2.y) * inv_det,
            (dv2 * edge1.z - dv1 * edge2.z) * inv_det
        );

        Vec3 bitangent(
            (-du2 * edge1.x + du1 * edge2.x) * inv_det,
            (-du2 * edge1.y + du1 * edge2.y) * inv_det,
            (-du2 * edge1.z + du1 * edge2.z) * inv_det
        );

        tan1[i0] = add(tan1[i0], tangent);
        tan1[i1] = add(tan1[i1], tangent);
        tan1[i2] = add(tan1[i2], tangent);

        tan2[i0] = add(tan2[i0], bitangent);
        tan2[i1] = add(tan2[i1], bitangent);
        tan2[i2] = add(tan2[i2], bitangent);
    }

    // Orthonormalize and compute handedness
    std::vector<Vec4> tangents;
    tangents.reserve(positions.size());

    for (size_t i = 0; i < positions.size(); i++) {
        const Vec3& n = normals[i];
        const Vec3& t = tan1[i];

        // Gram-Schmidt orthonormalize
        // tangent = normalize(t - n * dot(n, t))
        float dot_nt = n.x * t.x + n.y * t.y + n.z * t.z;
        Vec3 ortho(
            t.x - n.x * dot_nt,
            t.y - n.y * dot_nt,
            t.z - n.z * dot_nt
        );
        ortho = normalize(ortho);

        // Compute handedness
        Vec3 c = cross(n, t);
        float dot_cb = c.x * tan2[i].x + c.y * tan2[i].y + c.z * tan2[i].z;
        float w = (dot_cb < 0.0f) ? -1.0f : 1.0f;

        tangents.push_back(Vec4(ortho.x, ortho.y, ortho.z, w));
    }

    return tangents;
}

// ============================================================================
// Bounds Computation
// ============================================================================

AABB compute_bounds(const std::vector<Vec3>& positions) {
    AABB bounds;
    for (const auto& p : positions) {
        bounds.expand(p);
    }
    return bounds;
}

// ============================================================================
// Texture Loading Helpers
// ============================================================================

namespace {

// Detect image format from magic bytes
std::string detect_mime_type(const uint8_t* data, size_t size) {
    if (size < 4) return "";

    // PNG: 89 50 4E 47
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        return "image/png";
    }
    // JPEG: FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return "image/jpeg";
    }
    // WebP: RIFF....WEBP
    if (size >= 12 && data[0] == 'R' && data[1] == 'I' && data[2] == 'F' && data[3] == 'F' &&
        data[8] == 'W' && data[9] == 'E' && data[10] == 'B' && data[11] == 'P') {
        return "image/webp";
    }
    // BMP: BM
    if (data[0] == 'B' && data[1] == 'M') {
        return "image/bmp";
    }
    // GIF: GIF87a or GIF89a
    if (data[0] == 'G' && data[1] == 'I' && data[2] == 'F') {
        return "image/gif";
    }
    // EXR: 76 2F 31 01
    if (data[0] == 0x76 && data[1] == 0x2F && data[2] == 0x31 && data[3] == 0x01) {
        return "image/x-exr";
    }
    // HDR: #?RADIANCE or #?RGBE
    if (data[0] == '#' && data[1] == '?') {
        return "image/vnd.radiance";
    }

    return "";
}

// Check if format is HDR
bool is_hdr_format(const std::string& mime_type) {
    return mime_type == "image/x-exr" ||
           mime_type == "image/vnd.radiance" ||
           mime_type == "image/x-hdr";
}

// Get file extension from path
std::string get_file_extension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    std::string ext = path.substr(dot + 1);
    // Convert to lowercase
    for (char& c : ext) {
        if (c >= 'A' && c <= 'Z') c += 32;
    }
    return ext;
}

// HDR loading is now handled by lighthdr library

}  // anonymous namespace

// ============================================================================
// RenderConverter Implementation
// ============================================================================

class RenderConverter::Impl {
public:
    std::string last_error_;

    Result<RenderMesh> convert_mesh_prim(const Prim& prim, const RenderConverterConfig& config);
    void traverse_prims(const Prim& prim, const Mat4& parent_transform,
                        RenderScene& scene, const RenderConverterConfig& config,
                        std::map<std::string, int32_t>& material_index_map);

    // Extract transform from xformOps
    Mat4 extract_transform(const Prim& prim, double time);

    // Texture loading
    Result<RenderTexture> load_texture(const std::string& uri,
                                       const std::vector<uint8_t>& file_data,
                                       TextureDecodeMode decode_mode);

    // Material extraction
    Result<RenderMaterial> extract_material(const Prim& prim, const RenderConverterConfig& config);

    // Find bound material for a mesh
    std::string find_bound_material(const Prim& prim);
};

RenderConverter::RenderConverter() : impl_(std::make_unique<Impl>()) {}
RenderConverter::~RenderConverter() = default;

const std::string& RenderConverter::error() const {
    return impl_->last_error_;
}

Result<RenderMesh> RenderConverter::Impl::convert_mesh_prim(
    const Prim& prim, const RenderConverterConfig& config) {

    RenderMesh mesh;
    mesh.name = prim.name();
    mesh.path = prim.path().prim_part();

    // Get points
    const Attribute* points_attr = prim.get_attribute("points");
    if (!points_attr) {
        return Error("Mesh has no points attribute");
    }

    auto points_result = points_attr->get(config.time);
    if (!points_result) {
        return Error("Failed to get points value");
    }

    Value points_val = std::move(points_result).value();

    // Extract points data
    std::vector<Vec3> positions;
    if (points_val.is_array()) {
        // Handle float3[] or point3f[]
        auto arr = points_val.as_float3_array();
        if (!arr.empty()) {
            const float* float_data = static_cast<const float*>(arr.data);
            size_t float_count = arr.count * 3;
            positions.reserve(arr.count);
            for (size_t i = 0; i + 2 < float_count; i += 3) {
                positions.emplace_back(float_data[i], float_data[i + 1], float_data[i + 2]);
            }
        }
    }

    if (positions.empty()) {
        return Error("Failed to extract points data");
    }

    // Get face vertex counts
    const Attribute* fvc_attr = prim.get_attribute("faceVertexCounts");
    std::vector<uint32_t> face_vertex_counts;
    if (fvc_attr) {
        auto fvc_result = fvc_attr->get(config.time);
        if (fvc_result && fvc_result.value().is_array()) {
            auto arr = fvc_result.value().as_int32_array();
            if (!arr.empty()) {
                const int32_t* int_data = static_cast<const int32_t*>(arr.data);
                face_vertex_counts.reserve(arr.count);
                for (size_t i = 0; i < arr.count; i++) {
                    face_vertex_counts.push_back(static_cast<uint32_t>(int_data[i]));
                }
            }
        }
    }

    // Get face vertex indices
    const Attribute* fvi_attr = prim.get_attribute("faceVertexIndices");
    std::vector<uint32_t> face_vertex_indices;
    if (fvi_attr) {
        auto fvi_result = fvi_attr->get(config.time);
        if (fvi_result && fvi_result.value().is_array()) {
            auto arr = fvi_result.value().as_int32_array();
            if (!arr.empty()) {
                const int32_t* int_data = static_cast<const int32_t*>(arr.data);
                face_vertex_indices.reserve(arr.count);
                for (size_t i = 0; i < arr.count; i++) {
                    face_vertex_indices.push_back(static_cast<uint32_t>(int_data[i]));
                }
            }
        }
    }

    // Triangulate if needed
    if (config.triangulate && !face_vertex_counts.empty()) {
        mesh.indices = triangulate_indices(face_vertex_indices, face_vertex_counts);
    } else if (!face_vertex_indices.empty()) {
        mesh.indices = std::vector<uint32_t>(face_vertex_indices.begin(), face_vertex_indices.end());
    } else {
        // No indices - create sequential indices
        mesh.indices.reserve(positions.size());
        for (size_t i = 0; i < positions.size(); i++) {
            mesh.indices.push_back(static_cast<uint32_t>(i));
        }
    }

    // Store positions
    mesh.positions.component_count = 3;
    mesh.positions.vertex_count = static_cast<uint32_t>(positions.size());
    mesh.positions.data.reserve(positions.size() * 3);
    for (const auto& p : positions) {
        mesh.positions.data.push_back(p.x);
        mesh.positions.data.push_back(p.y);
        mesh.positions.data.push_back(p.z);
    }

    // Try to get normals
    const Attribute* normals_attr = prim.get_attribute("normals");
    std::vector<Vec3> normals;
    if (normals_attr) {
        auto normals_result = normals_attr->get(config.time);
        if (normals_result && normals_result.value().is_array()) {
            auto arr = normals_result.value().as_float3_array();
            if (!arr.empty()) {
                const float* float_data = static_cast<const float*>(arr.data);
                size_t float_count = arr.count * 3;
                normals.reserve(arr.count);
                for (size_t i = 0; i + 2 < float_count; i += 3) {
                    normals.emplace_back(float_data[i], float_data[i + 1], float_data[i + 2]);
                }
            }
        }
    }

    // Compute normals if missing
    if (normals.empty() && config.compute_normals) {
        normals = compute_smooth_normals(positions, mesh.indices);
    }

    if (!normals.empty()) {
        mesh.normals.component_count = 3;
        mesh.normals.vertex_count = static_cast<uint32_t>(normals.size());
        mesh.normals.data.reserve(normals.size() * 3);
        for (const auto& n : normals) {
            mesh.normals.data.push_back(n.x);
            mesh.normals.data.push_back(n.y);
            mesh.normals.data.push_back(n.z);
        }
    }

    // Try to get UVs (primvars:st or texCoords)
    std::vector<Vec2> texcoords;
    const Attribute* st_attr = prim.get_attribute("primvars:st");
    if (!st_attr) {
        st_attr = prim.get_attribute("texCoords");
    }
    if (st_attr) {
        auto st_result = st_attr->get(config.time);
        if (st_result && st_result.value().is_array()) {
            auto arr = st_result.value().as_float2_array();
            if (!arr.empty()) {
                const float* float_data = static_cast<const float*>(arr.data);
                size_t float_count = arr.count * 2;
                texcoords.reserve(arr.count);
                for (size_t i = 0; i + 1 < float_count; i += 2) {
                    texcoords.emplace_back(float_data[i], float_data[i + 1]);
                }
            }
        }
    }

    if (!texcoords.empty()) {
        mesh.texcoords0.component_count = 2;
        mesh.texcoords0.vertex_count = static_cast<uint32_t>(texcoords.size());
        mesh.texcoords0.data.reserve(texcoords.size() * 2);
        for (const auto& uv : texcoords) {
            mesh.texcoords0.data.push_back(uv.x);
            mesh.texcoords0.data.push_back(uv.y);
        }
    }

    // Compute tangents if we have UVs, normals, and config says to
    if (config.compute_tangents && !normals.empty() && !texcoords.empty() &&
        normals.size() == positions.size() && texcoords.size() == positions.size()) {
        auto tangents = compute_tangents(positions, normals, texcoords, mesh.indices);
        if (!tangents.empty()) {
            mesh.tangents.component_count = 4;
            mesh.tangents.vertex_count = static_cast<uint32_t>(tangents.size());
            mesh.tangents.data.reserve(tangents.size() * 4);
            for (const auto& t : tangents) {
                mesh.tangents.data.push_back(t.x);
                mesh.tangents.data.push_back(t.y);
                mesh.tangents.data.push_back(t.z);
                mesh.tangents.data.push_back(t.w);
            }
        }
    }

    // Compute bounds
    mesh.bounds = compute_bounds(positions);
    mesh.transform = Mat4::identity();

    // Single submesh for now (no multi-material support yet)
    SubMesh submesh;
    submesh.index_offset = 0;
    submesh.index_count = static_cast<uint32_t>(mesh.indices.size());
    submesh.material_index = -1;
    mesh.submeshes.push_back(submesh);

    return mesh;
}

// ============================================================================
// Texture Loading Implementation
// ============================================================================

Result<RenderTexture> RenderConverter::Impl::load_texture(
    const std::string& uri,
    const std::vector<uint8_t>& file_data,
    TextureDecodeMode decode_mode) {

    RenderTexture tex;
    tex.uri = uri;
    tex.name = uri;

    // Extract name from path
    size_t slash = uri.rfind('/');
    if (slash != std::string::npos) {
        tex.name = uri.substr(slash + 1);
    }

    if (file_data.empty()) {
        return Error("Empty texture data");
    }

    // Detect format
    tex.mime_type = detect_mime_type(file_data.data(), file_data.size());
    if (tex.mime_type.empty()) {
        // Try to determine from extension
        std::string ext = get_file_extension(uri);
        if (ext == "png") tex.mime_type = "image/png";
        else if (ext == "jpg" || ext == "jpeg") tex.mime_type = "image/jpeg";
        else if (ext == "webp") tex.mime_type = "image/webp";
        else if (ext == "bmp") tex.mime_type = "image/bmp";
        else if (ext == "gif") tex.mime_type = "image/gif";
        else if (ext == "exr") tex.mime_type = "image/x-exr";
        else if (ext == "hdr") tex.mime_type = "image/vnd.radiance";
    }

    tex.is_hdr = is_hdr_format(tex.mime_type);

    switch (decode_mode) {
        case TextureDecodeMode::Browser:
            // Store raw file data for browser to decode
            tex.file_data = file_data;
            // Set placeholder dimensions (browser will provide actual size)
            tex.width = 1;
            tex.height = 1;
            tex.channels = 4;
            tex.format = tex.is_hdr ? TextureFormat::RGBA16F : TextureFormat::RGBA8;
            break;

        case TextureDecodeMode::WebGPU:
            // For WebGPU shader decode, pass through with format hints
            tex.file_data = file_data;
            tex.width = 1;
            tex.height = 1;
            tex.channels = 4;
            tex.format = tex.is_hdr ? TextureFormat::RGBA32F : TextureFormat::RGBA8;
            break;

        case TextureDecodeMode::Native:
            // Decode in C++
            if (tex.is_hdr) {
                // HDR decode
                if (tex.mime_type == "image/vnd.radiance") {
                    // Use LightHDR for Radiance HDR decoding
                    lighthdr::HDRImage hdr_image;
                    lighthdr::LoadOptions hdr_opts;
                    hdr_opts.output_rgba = true;

                    auto result = lighthdr::LoadHDRFromMemory(
                        file_data.data(), file_data.size(), &hdr_image, hdr_opts);

                    if (!result) {
                        return Error("Failed to decode HDR: " + result.error);
                    }

                    tex.width = hdr_image.header.width;
                    tex.height = hdr_image.header.height;
                    tex.channels = hdr_image.channels;
                    tex.data_f32 = std::move(hdr_image.pixels);
                    tex.format = TextureFormat::RGBA32F;
                } else if (tex.mime_type == "image/x-exr") {
                    // Use LightEXR for EXR decoding
                    lightexr::EXRImage exr_image;
                    lightexr::LoadOptions exr_opts;
                    exr_opts.convert_to_rgba = true;

                    auto result = lightexr::LoadEXRFromMemory(
                        file_data.data(), file_data.size(), &exr_image, exr_opts);

                    if (!result) {
                        return Error("Failed to decode EXR: " + result.error);
                    }

                    tex.width = static_cast<uint32_t>(exr_image.header.width());
                    tex.height = static_cast<uint32_t>(exr_image.header.height());
                    tex.channels = static_cast<uint32_t>(exr_image.num_channels());
                    tex.data_f32 = std::move(exr_image.pixels);
                    tex.format = TextureFormat::RGBA32F;
                } else {
                    return Error("Unknown HDR format");
                }
            } else {
                // LDR decode would need stb_image
                // For now, just pass through for browser decode
                tex.file_data = file_data;
                tex.width = 1;
                tex.height = 1;
                tex.channels = 4;
                tex.format = TextureFormat::RGBA8;
            }
            break;
    }

    return tex;
}

// ============================================================================
// Material Extraction Implementation
// ============================================================================

Result<RenderMaterial> RenderConverter::Impl::extract_material(
    const Prim& prim, const RenderConverterConfig& config) {

    RenderMaterial mat;
    mat.name = prim.name();
    mat.path = prim.path().prim_part();

    // Look for UsdPreviewSurface shader
    for (const auto& child_name : prim.child_names()) {
        const Prim* child = prim.child(child_name);
        if (!child) continue;

        if (child->type_name() == "Shader") {
            // Check if it's a UsdPreviewSurface
            const Attribute* id_attr = child->get_attribute("info:id");
            if (!id_attr) continue;

            auto id_result = id_attr->get(config.time);
            if (!id_result) continue;

            // Get diffuseColor
            const Attribute* diffuse_attr = child->get_attribute("inputs:diffuseColor");
            if (diffuse_attr) {
                auto diffuse_result = diffuse_attr->get(config.time);
                if (diffuse_result) {
                    const float* rgb = diffuse_result.value().as_float3();
                    if (rgb) {
                        mat.base_color = Vec4(rgb[0], rgb[1], rgb[2], 1.0f);
                    }
                }
            }

            // Get metallic
            const Attribute* metallic_attr = child->get_attribute("inputs:metallic");
            if (metallic_attr) {
                auto metallic_result = metallic_attr->get(config.time);
                if (metallic_result) {
                    const float* val = metallic_result.value().as_float();
                    if (val) mat.metallic = *val;
                }
            }

            // Get roughness
            const Attribute* roughness_attr = child->get_attribute("inputs:roughness");
            if (roughness_attr) {
                auto roughness_result = roughness_attr->get(config.time);
                if (roughness_result) {
                    const float* val = roughness_result.value().as_float();
                    if (val) mat.roughness = *val;
                }
            }

            // Get emissiveColor
            const Attribute* emissive_attr = child->get_attribute("inputs:emissiveColor");
            if (emissive_attr) {
                auto emissive_result = emissive_attr->get(config.time);
                if (emissive_result) {
                    const float* rgb = emissive_result.value().as_float3();
                    if (rgb) {
                        mat.emissive = Vec3(rgb[0], rgb[1], rgb[2]);
                    }
                }
            }

            // Get opacity
            const Attribute* opacity_attr = child->get_attribute("inputs:opacity");
            if (opacity_attr) {
                auto opacity_result = opacity_attr->get(config.time);
                if (opacity_result) {
                    const float* val = opacity_result.value().as_float();
                    if (val) mat.base_color.w = *val;
                }
            }

            break; // Found the shader
        }
    }

    return mat;
}

std::string RenderConverter::Impl::find_bound_material(const Prim& prim) {
    // Look for material:binding relationship
    const Relationship* binding = prim.get_relationship("material:binding");
    if (binding) {
        const auto& targets = binding->targets();
        if (!targets.empty()) {
            return targets[0].prim_part();
        }
    }
    return "";
}

// Degrees to radians conversion
static constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

Mat4 RenderConverter::Impl::extract_transform(const Prim& prim, double time) {
    Mat4 result = Mat4::identity();

    // Get property names and look for xformOpOrder
    auto prop_names = prim.property_names();

    // Collect xformOp names in order
    std::vector<std::string> xform_ops;

    // First check for explicit xformOpOrder
    const Attribute* order_attr = prim.get_attribute("xformOpOrder");
    if (order_attr) {
        // xformOpOrder is a token[] - get the value and parse
        auto order_result = order_attr->get(time);
        if (order_result && order_result.value().is_array()) {
            // Parse the array - check type
            std::string type_name = order_result.value().type_name();
            if (type_name.find("token") != std::string::npos) {
                // For token arrays, iterate by checking property names that match
                for (const auto& prop : prop_names) {
                    if (prop.find("xformOp:") == 0 && prop != "xformOpOrder") {
                        xform_ops.push_back(prop);
                    }
                }
            }
        }
    }

    // If no xformOpOrder found, collect all xformOp:* attributes
    if (xform_ops.empty()) {
        for (const auto& prop : prop_names) {
            if (prop.find("xformOp:") == 0) {
                xform_ops.push_back(prop);
            }
        }
    }

    // Process each xformOp in order
    for (const auto& op_name : xform_ops) {
        const Attribute* attr = prim.get_attribute(op_name);
        if (!attr) continue;

        auto val_result = attr->get(time);
        if (!val_result) continue;

        const Value& val = val_result.value();

        // Parse operation type from name
        // Format: xformOp:type or xformOp:type:suffix
        std::string op_type;
        size_t colon1 = op_name.find(':');
        if (colon1 != std::string::npos) {
            size_t colon2 = op_name.find(':', colon1 + 1);
            if (colon2 != std::string::npos) {
                op_type = op_name.substr(colon1 + 1, colon2 - colon1 - 1);
            } else {
                op_type = op_name.substr(colon1 + 1);
            }
        }

        Mat4 op_mat = Mat4::identity();

        if (op_type == "translate") {
            // double3 or float3
            if (const double* d = val.as_double3()) {
                op_mat = Mat4::translate(static_cast<float>(d[0]),
                                         static_cast<float>(d[1]),
                                         static_cast<float>(d[2]));
            } else if (const float* f = val.as_float3()) {
                op_mat = Mat4::translate(f[0], f[1], f[2]);
            }
        }
        else if (op_type == "scale") {
            // float3 or double3
            if (const float* f = val.as_float3()) {
                op_mat = Mat4::scale(f[0], f[1], f[2]);
            } else if (const double* d = val.as_double3()) {
                op_mat = Mat4::scale(static_cast<float>(d[0]),
                                     static_cast<float>(d[1]),
                                     static_cast<float>(d[2]));
            }
        }
        else if (op_type == "rotateX") {
            // float or double (degrees)
            if (const float* f = val.as_float()) {
                op_mat = Mat4::rotate_x(*f * kDegToRad);
            } else if (const double* d = val.as_double()) {
                op_mat = Mat4::rotate_x(static_cast<float>(*d) * kDegToRad);
            }
        }
        else if (op_type == "rotateY") {
            if (const float* f = val.as_float()) {
                op_mat = Mat4::rotate_y(*f * kDegToRad);
            } else if (const double* d = val.as_double()) {
                op_mat = Mat4::rotate_y(static_cast<float>(*d) * kDegToRad);
            }
        }
        else if (op_type == "rotateZ") {
            if (const float* f = val.as_float()) {
                op_mat = Mat4::rotate_z(*f * kDegToRad);
            } else if (const double* d = val.as_double()) {
                op_mat = Mat4::rotate_z(static_cast<float>(*d) * kDegToRad);
            }
        }
        else if (op_type == "rotateXYZ" || op_type == "rotateZYX" ||
                 op_type == "rotateXZY" || op_type == "rotateYXZ" ||
                 op_type == "rotateYZX" || op_type == "rotateZXY") {
            // float3 or double3 (degrees)
            float rx = 0, ry = 0, rz = 0;
            if (const float* f = val.as_float3()) {
                rx = f[0] * kDegToRad;
                ry = f[1] * kDegToRad;
                rz = f[2] * kDegToRad;
            } else if (const double* d = val.as_double3()) {
                rx = static_cast<float>(d[0]) * kDegToRad;
                ry = static_cast<float>(d[1]) * kDegToRad;
                rz = static_cast<float>(d[2]) * kDegToRad;
            }

            // Apply in specified order
            if (op_type == "rotateXYZ") {
                op_mat = Mat4::rotate_x(rx) * Mat4::rotate_y(ry) * Mat4::rotate_z(rz);
            } else if (op_type == "rotateXZY") {
                op_mat = Mat4::rotate_x(rx) * Mat4::rotate_z(rz) * Mat4::rotate_y(ry);
            } else if (op_type == "rotateYXZ") {
                op_mat = Mat4::rotate_y(ry) * Mat4::rotate_x(rx) * Mat4::rotate_z(rz);
            } else if (op_type == "rotateYZX") {
                op_mat = Mat4::rotate_y(ry) * Mat4::rotate_z(rz) * Mat4::rotate_x(rx);
            } else if (op_type == "rotateZXY") {
                op_mat = Mat4::rotate_z(rz) * Mat4::rotate_x(rx) * Mat4::rotate_y(ry);
            } else if (op_type == "rotateZYX") {
                op_mat = Mat4::rotate_z(rz) * Mat4::rotate_y(ry) * Mat4::rotate_x(rx);
            }
        }
        else if (op_type == "orient") {
            // quatf or quatd (x, y, z, w)
            if (const float* q = val.as_quatf()) {
                op_mat = Mat4::from_quaternion(q[0], q[1], q[2], q[3]);
            } else if (const double* q = val.as_quatd()) {
                op_mat = Mat4::from_quaternion(static_cast<float>(q[0]),
                                               static_cast<float>(q[1]),
                                               static_cast<float>(q[2]),
                                               static_cast<float>(q[3]));
            }
        }
        else if (op_type == "transform") {
            // matrix4d
            if (const double* m = val.as_matrix4d()) {
                op_mat = Mat4::from_double_matrix(m);
            } else if (const float* m = val.as_matrix4f()) {
                for (int i = 0; i < 16; i++) {
                    op_mat.m[i] = m[i];
                }
            }
        }

        // Concatenate this operation
        result = result * op_mat;
    }

    return result;
}

void RenderConverter::Impl::traverse_prims(
    const Prim& prim, const Mat4& parent_transform,
    RenderScene& scene, const RenderConverterConfig& config,
    std::map<std::string, int32_t>& material_index_map) {

    // Extract local transform from xformOps and combine with parent
    Mat4 prim_transform = extract_transform(prim, config.time);
    Mat4 local_transform = parent_transform * prim_transform;

    // Check if this is a mesh
    if (prim.type_name() == "Mesh") {
        auto result = convert_mesh_prim(prim, config);
        if (result) {
            RenderMesh mesh = std::move(result).value();
            if (config.apply_transforms) {
                mesh.transform = local_transform;
            }

            // Find and bind material
            std::string mat_path = find_bound_material(prim);
            if (!mat_path.empty()) {
                auto it = material_index_map.find(mat_path);
                if (it != material_index_map.end()) {
                    // Update submesh material index
                    for (auto& submesh : mesh.submeshes) {
                        submesh.material_index = it->second;
                    }
                }
            }

            scene.bounds.expand(mesh.bounds);
            scene.meshes.push_back(std::move(mesh));
        }
    }
    // Check if this is a Material
    else if (prim.type_name() == "Material") {
        auto result = extract_material(prim, config);
        if (result) {
            RenderMaterial mat = std::move(result).value();
            int32_t index = static_cast<int32_t>(scene.materials.size());
            material_index_map[mat.path] = index;
            scene.materials.push_back(std::move(mat));
        }
    }

    // Recurse into children
    for (const auto& child_name : prim.child_names()) {
        auto child = prim.child(child_name);
        if (child) {
            traverse_prims(*child, local_transform, scene, config, material_index_map);
        }
    }
}

Result<RenderScene> RenderConverter::convert(
    const Stage& stage, const RenderConverterConfig& config) {

    RenderScene scene;
    scene.name = "converted_scene";
    scene.up_axis = stage.up_axis();
    scene.meters_per_unit = static_cast<float>(stage.meters_per_unit());

    Mat4 root_transform = Mat4::identity();

    // Material path -> index map for binding resolution
    std::map<std::string, int32_t> material_index_map;

    // Traverse all root prims (collects both materials and meshes)
    for (const auto& root_name : stage.root_prim_names()) {
        auto root_prim = stage.root_prim(root_name);
        if (root_prim) {
            impl_->traverse_prims(*root_prim, root_transform, scene, config, material_index_map);
        }
    }

    if (scene.meshes.empty()) {
        impl_->last_error_ = "No meshes found in stage";
        return Error("No meshes found in stage");
    }

    return scene;
}

Result<RenderMesh> RenderConverter::convert_mesh(
    const Prim& prim, const RenderConverterConfig& config) {
    return impl_->convert_mesh_prim(prim, config);
}

}  // namespace v1
}  // namespace lightusd
