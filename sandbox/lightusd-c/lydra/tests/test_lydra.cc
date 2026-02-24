// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// Lydra C++ API unit tests

#include "../lydra.hh"
#include "../lydra_mesh.hh"
#include "../lydra_transform.hh"
#include "../lydra_material.hh"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void test_##name()
#define RUN(name) do { \
    std::printf("  %-40s", #name); \
    try { test_##name(); g_pass++; std::printf("PASS\n"); } \
    catch (...) { g_fail++; std::printf("FAIL (exception)\n"); } \
} while(0)

#define ASSERT_TRUE(x) do { if (!(x)) { std::printf("FAIL (line %d)\n", __LINE__); g_fail++; return; } } while(0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(std::abs((a) - (b)) < (eps))

// ============================================================================
// Format tests
// ============================================================================

TEST(format_size) {
    ASSERT_EQ(lydra::format_size(lydra::Format::R32_SFLOAT), 4u);
    ASSERT_EQ(lydra::format_size(lydra::Format::R32G32_SFLOAT), 8u);
    ASSERT_EQ(lydra::format_size(lydra::Format::R32G32B32_SFLOAT), 12u);
    ASSERT_EQ(lydra::format_size(lydra::Format::R32G32B32A32_SFLOAT), 16u);
    ASSERT_EQ(lydra::format_size(lydra::Format::R16_UINT), 2u);
    ASSERT_EQ(lydra::format_size(lydra::Format::R32_UINT), 4u);
}

TEST(format_components) {
    ASSERT_EQ(lydra::format_components(lydra::Format::R32_SFLOAT), 1u);
    ASSERT_EQ(lydra::format_components(lydra::Format::R32G32_SFLOAT), 2u);
    ASSERT_EQ(lydra::format_components(lydra::Format::R32G32B32_SFLOAT), 3u);
    ASSERT_EQ(lydra::format_components(lydra::Format::R32G32B32A32_SFLOAT), 4u);
}

// ============================================================================
// Span tests
// ============================================================================

TEST(span_basics) {
    std::vector<float> v = {1.0f, 2.0f, 3.0f};
    lydra::Span<float> s(v);
    ASSERT_EQ(s.size(), 3u);
    ASSERT_EQ(s[0], 1.0f);
    ASSERT_EQ(s[1], 2.0f);
    ASSERT_EQ(s[2], 3.0f);
    ASSERT_TRUE(!s.empty());
    ASSERT_EQ(s.size_bytes(), 12u);
}

TEST(span_empty) {
    lydra::Span<float> s;
    ASSERT_TRUE(s.empty());
    ASSERT_EQ(s.size(), 0u);
}

// ============================================================================
// Triangulation tests
// ============================================================================

TEST(triangulate_quad) {
    // A single quad: 4 vertices, face_vertex_counts = [4]
    std::vector<uint32_t> indices = {0, 1, 2, 3};
    std::vector<uint32_t> counts = {4};
    auto result = lydra::triangulate(indices, counts);
    // Quad -> 2 triangles -> 6 indices
    ASSERT_EQ(result.size(), 6u);
    ASSERT_EQ(result[0], 0u); ASSERT_EQ(result[1], 1u); ASSERT_EQ(result[2], 2u);
    ASSERT_EQ(result[3], 0u); ASSERT_EQ(result[4], 2u); ASSERT_EQ(result[5], 3u);
}

TEST(triangulate_triangle) {
    std::vector<uint32_t> indices = {0, 1, 2};
    std::vector<uint32_t> counts = {3};
    auto result = lydra::triangulate(indices, counts);
    ASSERT_EQ(result.size(), 3u);
    ASSERT_EQ(result[0], 0u); ASSERT_EQ(result[1], 1u); ASSERT_EQ(result[2], 2u);
}

TEST(triangulate_mixed) {
    // Triangle + quad
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4, 5, 6};
    std::vector<uint32_t> counts = {3, 4};
    auto result = lydra::triangulate(indices, counts);
    // 1 + 2 = 3 triangles = 9 indices
    ASSERT_EQ(result.size(), 9u);
}

TEST(triangulate_degenerate) {
    // 2-vertex face (line) should be skipped
    std::vector<uint32_t> indices = {0, 1, 2, 3, 4};
    std::vector<uint32_t> counts = {2, 3};
    auto result = lydra::triangulate(indices, counts);
    ASSERT_EQ(result.size(), 3u);
}

// ============================================================================
// Normal computation tests
// ============================================================================

TEST(smooth_normals_flat_triangle) {
    // XY-plane triangle: (0,0,0), (1,0,0), (0,1,0)
    std::vector<float> positions = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    std::vector<uint32_t> indices = {0, 1, 2};
    auto normals = lydra::compute_smooth_normals(positions, indices);
    ASSERT_EQ(normals.size(), 9u);
    // All normals should point in +Z
    for (int i = 0; i < 3; i++) {
        ASSERT_NEAR(normals[i * 3 + 0], 0.0f, 1e-5f);
        ASSERT_NEAR(normals[i * 3 + 1], 0.0f, 1e-5f);
        ASSERT_NEAR(normals[i * 3 + 2], 1.0f, 1e-5f);
    }
}

TEST(flat_normals) {
    std::vector<float> positions = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    std::vector<uint32_t> indices = {0, 1, 2};
    auto normals = lydra::compute_flat_normals(positions, indices);
    // Flat normals: one per index vertex = 3 normals = 9 floats
    ASSERT_EQ(normals.size(), 9u);
    for (int i = 0; i < 3; i++) {
        ASSERT_NEAR(normals[i * 3 + 2], 1.0f, 1e-5f);
    }
}

TEST(smooth_normals_unit_length) {
    // Two triangles sharing vertices
    std::vector<float> positions = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        1.0f, 1.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    std::vector<uint32_t> indices = {0, 1, 2, 0, 2, 3};
    auto normals = lydra::compute_smooth_normals(positions, indices);
    ASSERT_EQ(normals.size(), 12u);
    // All normals should be unit length
    for (size_t i = 0; i < 4; i++) {
        float len = std::sqrt(
            normals[i*3]*normals[i*3] +
            normals[i*3+1]*normals[i*3+1] +
            normals[i*3+2]*normals[i*3+2]);
        ASSERT_NEAR(len, 1.0f, 1e-5f);
    }
}

// ============================================================================
// Tangent computation tests
// ============================================================================

TEST(tangents_basic) {
    std::vector<float> positions = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f};
    std::vector<float> normals = {
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f};
    std::vector<float> texcoords = {
        0.0f, 0.0f,
        1.0f, 0.0f,
        0.0f, 1.0f};
    std::vector<uint32_t> indices = {0, 1, 2};

    auto tangents = lydra::compute_tangents(positions, normals, texcoords, indices);
    ASSERT_EQ(tangents.size(), 12u);  // 3 verts * 4 components

    // Tangent should point along +X (UV.u maps to position.x)
    for (int i = 0; i < 3; i++) {
        ASSERT_NEAR(tangents[i * 4 + 0], 1.0f, 1e-5f);
        ASSERT_NEAR(tangents[i * 4 + 1], 0.0f, 1e-5f);
        ASSERT_NEAR(tangents[i * 4 + 2], 0.0f, 1e-5f);
        // Handedness: +1 or -1
        ASSERT_TRUE(tangents[i * 4 + 3] == 1.0f || tangents[i * 4 + 3] == -1.0f);
    }
}

// ============================================================================
// Bounds tests
// ============================================================================

TEST(bounds_basic) {
    std::vector<float> positions = {
        -1.0f, -2.0f, -3.0f,
         4.0f,  5.0f,  6.0f,
         0.0f,  0.0f,  0.0f};
    auto bounds = lydra::compute_bounds(positions);
    ASSERT_NEAR(bounds.min[0], -1.0f, 1e-5f);
    ASSERT_NEAR(bounds.min[1], -2.0f, 1e-5f);
    ASSERT_NEAR(bounds.min[2], -3.0f, 1e-5f);
    ASSERT_NEAR(bounds.max[0], 4.0f, 1e-5f);
    ASSERT_NEAR(bounds.max[1], 5.0f, 1e-5f);
    ASSERT_NEAR(bounds.max[2], 6.0f, 1e-5f);
}

// ============================================================================
// Vertex deduplication tests
// ============================================================================

TEST(build_indexed_mesh_dedup) {
    // 6 vertices forming 2 triangles with shared vertices (a quad)
    // Positions: 4 unique, but we pass 6 (with repeats)
    float pos[] = {
        0.0f, 0.0f, 0.0f,   // tri0.v0
        1.0f, 0.0f, 0.0f,   // tri0.v1
        1.0f, 1.0f, 0.0f,   // tri0.v2
        0.0f, 0.0f, 0.0f,   // tri1.v0 (dup of tri0.v0)
        1.0f, 1.0f, 0.0f,   // tri1.v1 (dup of tri0.v2)
        0.0f, 1.0f, 0.0f,   // tri1.v2 (unique)
    };

    lydra::AttributeArray attr;
    attr.data = pos;
    attr.component_count = 3;
    attr.location = 0;

    auto result = lydra::build_indexed_mesh(
        lydra::Span<const lydra::AttributeArray>(&attr, 1), 6);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result->vertex_count, 4u);  // 4 unique vertices
    ASSERT_EQ(result->index_count, 6u);   // 6 indices
}

// ============================================================================
// Interleaved packing tests
// ============================================================================

TEST(pack_interleaved_basic) {
    float positions[] = {0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f};
    float texcoords[] = {0.0f, 0.0f,  1.0f, 0.0f,  0.0f, 1.0f};

    lydra::AttributeArray attrs[2];
    attrs[0] = {positions, 3, 0};
    attrs[1] = {texcoords, 2, 1};

    auto result = lydra::pack_interleaved(
        lydra::Span<const lydra::AttributeArray>(attrs, 2), 3);
    ASSERT_TRUE(result.ok());
    ASSERT_EQ(result->layout.stride, 20u);  // (3+2)*4 = 20 bytes
    ASSERT_EQ(result->layout.attributes.size(), 2u);
    ASSERT_EQ(result->data.size(), 60u);  // 3 verts * 20 bytes

    // Verify first vertex: pos(0,0,0), uv(0,0)
    const float* v0 = reinterpret_cast<const float*>(result->data.data());
    ASSERT_NEAR(v0[0], 0.0f, 1e-5f);
    ASSERT_NEAR(v0[1], 0.0f, 1e-5f);
    ASSERT_NEAR(v0[2], 0.0f, 1e-5f);
    ASSERT_NEAR(v0[3], 0.0f, 1e-5f);
    ASSERT_NEAR(v0[4], 0.0f, 1e-5f);
}

// ============================================================================
// Index format tests
// ============================================================================

TEST(select_index_format) {
    ASSERT_EQ(lydra::select_index_format(100), lydra::Format::R16_UINT);
    ASSERT_EQ(lydra::select_index_format(65535), lydra::Format::R16_UINT);
    ASSERT_EQ(lydra::select_index_format(65536), lydra::Format::R32_UINT);
}

TEST(indices_to_u16) {
    std::vector<uint32_t> indices = {0, 100, 65535};
    auto u16 = lydra::indices_to_u16(indices);
    ASSERT_EQ(u16.size(), 3u);
    ASSERT_EQ(u16[0], 0);
    ASSERT_EQ(u16[1], 100);
    ASSERT_EQ(u16[2], 65535);
}

// ============================================================================
// Transform tests
// ============================================================================

TEST(mat4_identity) {
    auto m = lydra::Mat4::identity();
    ASSERT_NEAR(m.m[0], 1.0f, 1e-5f);
    ASSERT_NEAR(m.m[5], 1.0f, 1e-5f);
    ASSERT_NEAR(m.m[10], 1.0f, 1e-5f);
    ASSERT_NEAR(m.m[15], 1.0f, 1e-5f);
    ASSERT_NEAR(m.m[1], 0.0f, 1e-5f);
    ASSERT_NEAR(m.m[4], 0.0f, 1e-5f);
}

TEST(mat4_translate) {
    auto m = lydra::Mat4::translate(1.0f, 2.0f, 3.0f);
    float point[] = {0.0f, 0.0f, 0.0f};
    float out[3];
    m.transform_points(lydra::Span<const float>(point, 3), out, 1);
    ASSERT_NEAR(out[0], 1.0f, 1e-5f);
    ASSERT_NEAR(out[1], 2.0f, 1e-5f);
    ASSERT_NEAR(out[2], 3.0f, 1e-5f);
}

TEST(mat4_scale) {
    auto m = lydra::Mat4::scale(2.0f, 3.0f, 4.0f);
    float point[] = {1.0f, 1.0f, 1.0f};
    float out[3];
    m.transform_points(lydra::Span<const float>(point, 3), out, 1);
    ASSERT_NEAR(out[0], 2.0f, 1e-5f);
    ASSERT_NEAR(out[1], 3.0f, 1e-5f);
    ASSERT_NEAR(out[2], 4.0f, 1e-5f);
}

TEST(mat4_rotate_z_90) {
    float pi = 3.14159265358979f;
    auto m = lydra::Mat4::rotate_z(pi / 2.0f);
    float point[] = {1.0f, 0.0f, 0.0f};
    float out[3];
    m.transform_points(lydra::Span<const float>(point, 3), out, 1);
    // (1,0,0) rotated 90 deg around Z -> (0,1,0)
    ASSERT_NEAR(out[0], 0.0f, 1e-5f);
    ASSERT_NEAR(out[1], 1.0f, 1e-5f);
    ASSERT_NEAR(out[2], 0.0f, 1e-5f);
}

TEST(mat4_multiply) {
    auto t = lydra::Mat4::translate(1.0f, 0.0f, 0.0f);
    auto s = lydra::Mat4::scale(2.0f, 2.0f, 2.0f);
    // Apply scale first, then translate: T * S
    auto m = t * s;
    float point[] = {1.0f, 0.0f, 0.0f};
    float out[3];
    m.transform_points(lydra::Span<const float>(point, 3), out, 1);
    // scale(1,0,0) -> (2,0,0), then translate -> (3,0,0)
    ASSERT_NEAR(out[0], 3.0f, 1e-5f);
    ASSERT_NEAR(out[1], 0.0f, 1e-5f);
    ASSERT_NEAR(out[2], 0.0f, 1e-5f);
}

TEST(mat4_quaternion_identity) {
    auto m = lydra::Mat4::from_quaternion(0.0f, 0.0f, 0.0f, 1.0f);
    float point[] = {1.0f, 2.0f, 3.0f};
    float out[3];
    m.transform_points(lydra::Span<const float>(point, 3), out, 1);
    ASSERT_NEAR(out[0], 1.0f, 1e-5f);
    ASSERT_NEAR(out[1], 2.0f, 1e-5f);
    ASSERT_NEAR(out[2], 3.0f, 1e-5f);
}

TEST(transform_normals) {
    auto m = lydra::Mat4::scale(2.0f, 1.0f, 1.0f);
    float normal[] = {1.0f, 0.0f, 0.0f};
    float out[3];
    m.transform_normals(lydra::Span<const float>(normal, 3), out, 1);
    // Normal should still be unit length after transform
    float len = std::sqrt(out[0]*out[0] + out[1]*out[1] + out[2]*out[2]);
    ASSERT_NEAR(len, 1.0f, 1e-5f);
}

TEST(transform_aabb) {
    lydra::AABB box;
    box.min[0] = -1.0f; box.min[1] = -1.0f; box.min[2] = -1.0f;
    box.max[0] =  1.0f; box.max[1] =  1.0f; box.max[2] =  1.0f;

    auto m = lydra::Mat4::translate(5.0f, 0.0f, 0.0f);
    auto result = lydra::transform_aabb(box, m);
    ASSERT_NEAR(result.min[0], 4.0f, 1e-5f);
    ASSERT_NEAR(result.max[0], 6.0f, 1e-5f);
    ASSERT_NEAR(result.min[1], -1.0f, 1e-5f);
    ASSERT_NEAR(result.max[1], 1.0f, 1e-5f);
}

// ============================================================================
// Material tests
// ============================================================================

TEST(flat_material_defaults) {
    lydra::FlatMaterial mat;
    ASSERT_NEAR(mat.base_color[0], 0.8f, 1e-5f);
    ASSERT_NEAR(mat.metallic, 0.0f, 1e-5f);
    ASSERT_NEAR(mat.roughness, 0.5f, 1e-5f);
    ASSERT_TRUE(!mat.double_sided);
    ASSERT_EQ(mat.base_color_tex.index, -1);
    ASSERT_EQ(mat.normal_tex.index, -1);
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::printf("=== Lydra C++ Unit Tests ===\n\n");

    std::printf("[Core Types]\n");
    RUN(format_size);
    RUN(format_components);
    RUN(span_basics);
    RUN(span_empty);

    std::printf("\n[Triangulation]\n");
    RUN(triangulate_quad);
    RUN(triangulate_triangle);
    RUN(triangulate_mixed);
    RUN(triangulate_degenerate);

    std::printf("\n[Normals]\n");
    RUN(smooth_normals_flat_triangle);
    RUN(flat_normals);
    RUN(smooth_normals_unit_length);

    std::printf("\n[Tangents]\n");
    RUN(tangents_basic);

    std::printf("\n[Bounds]\n");
    RUN(bounds_basic);

    std::printf("\n[Vertex Dedup]\n");
    RUN(build_indexed_mesh_dedup);

    std::printf("\n[Interleave]\n");
    RUN(pack_interleaved_basic);

    std::printf("\n[Index Format]\n");
    RUN(select_index_format);
    RUN(indices_to_u16);

    std::printf("\n[Transforms]\n");
    RUN(mat4_identity);
    RUN(mat4_translate);
    RUN(mat4_scale);
    RUN(mat4_rotate_z_90);
    RUN(mat4_multiply);
    RUN(mat4_quaternion_identity);
    RUN(transform_normals);
    RUN(transform_aabb);

    std::printf("\n[Materials]\n");
    RUN(flat_material_defaults);

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
