/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2024 Light Transport Entertainment Inc. */
/*
 * Lydra C API unit tests
 */

#include "../lydra_c.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_pass = 0;
static int g_fail = 0;

#define ASSERT_TRUE(x) do { if (!(x)) { printf("  FAIL (line %d)\n", __LINE__); g_fail++; return; } } while(0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabs((double)(a) - (double)(b)) < (eps))

#define TEST(name) static void test_##name(void)
#define RUN(name) do { \
    printf("  %-40s", #name); \
    test_##name(); \
    if (g_fail == prev_fail) { g_pass++; printf("PASS\n"); } \
    prev_fail = g_fail; \
} while(0)

/* ========================================================================== */
/* Context                                                                    */
/* ========================================================================== */

TEST(context_create_destroy) {
    LydraContext ctx = NULL;
    LydraResult r = lydraCreateContext(&ctx);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_TRUE(ctx != NULL);
    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Triangulation                                                              */
/* ========================================================================== */

TEST(c_triangulate_quad) {
    LydraContext ctx = NULL;
    lydraCreateContext(&ctx);

    uint32_t indices[] = {0, 1, 2, 3};
    uint32_t counts[] = {4};
    uint32_t out_count = 0;

    /* First call: query size */
    LydraResult r = lydraTriangulate(ctx, 4, indices, 1, counts, &out_count, NULL);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_EQ(out_count, 6u);

    /* Second call: get data */
    uint32_t out_indices[6];
    r = lydraTriangulate(ctx, 4, indices, 1, counts, &out_count, out_indices);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_EQ(out_indices[0], 0u);
    ASSERT_EQ(out_indices[1], 1u);
    ASSERT_EQ(out_indices[2], 2u);
    ASSERT_EQ(out_indices[3], 0u);
    ASSERT_EQ(out_indices[4], 2u);
    ASSERT_EQ(out_indices[5], 3u);

    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Smooth normals                                                             */
/* ========================================================================== */

TEST(c_smooth_normals) {
    LydraContext ctx = NULL;
    lydraCreateContext(&ctx);

    float positions[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    uint32_t indices[] = {0, 1, 2};
    float normals[9];

    LydraResult r = lydraComputeSmoothNormals(ctx, 3, positions, 3, indices, normals);
    ASSERT_EQ(r, LYDRA_SUCCESS);

    /* All normals should point +Z */
    int i;
    for (i = 0; i < 3; i++) {
        ASSERT_NEAR(normals[i * 3 + 0], 0.0f, 1e-5);
        ASSERT_NEAR(normals[i * 3 + 1], 0.0f, 1e-5);
        ASSERT_NEAR(normals[i * 3 + 2], 1.0f, 1e-5);
    }

    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Flat normals                                                               */
/* ========================================================================== */

TEST(c_flat_normals) {
    LydraContext ctx = NULL;
    lydraCreateContext(&ctx);

    float positions[] = {
        0.0f, 0.0f, 0.0f,
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f
    };
    uint32_t indices[] = {0, 1, 2};
    uint32_t out_vert_count = 0;

    /* First call: query count */
    LydraResult r = lydraComputeFlatNormals(ctx, 3, positions, 3, indices, &out_vert_count, NULL);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_EQ(out_vert_count, 3u);  /* one per index */

    /* Second call: get data */
    float normals[9];
    r = lydraComputeFlatNormals(ctx, 3, positions, 3, indices, &out_vert_count, normals);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_NEAR(normals[2], 1.0f, 1e-5);

    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Tangents                                                                   */
/* ========================================================================== */

TEST(c_tangents) {
    LydraContext ctx = NULL;
    lydraCreateContext(&ctx);

    float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    float normals[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    float texcoords[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};
    uint32_t indices[] = {0, 1, 2};
    float tangents[12];

    LydraResult r = lydraComputeTangents(ctx, 3, positions, normals, texcoords, 3, indices, tangents);
    ASSERT_EQ(r, LYDRA_SUCCESS);

    /* Tangent should point along +X */
    ASSERT_NEAR(tangents[0], 1.0f, 1e-5);
    ASSERT_NEAR(tangents[1], 0.0f, 1e-5);
    ASSERT_NEAR(tangents[2], 0.0f, 1e-5);

    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Bounds                                                                     */
/* ========================================================================== */

TEST(c_bounds) {
    float positions[] = {
        -1.0f, -2.0f, -3.0f,
         4.0f,  5.0f,  6.0f,
         0.0f,  0.0f,  0.0f
    };
    float out_min[3], out_max[3];

    LydraResult r = lydraComputeBounds(3, positions, out_min, out_max);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_NEAR(out_min[0], -1.0f, 1e-5);
    ASSERT_NEAR(out_min[1], -2.0f, 1e-5);
    ASSERT_NEAR(out_min[2], -3.0f, 1e-5);
    ASSERT_NEAR(out_max[0], 4.0f, 1e-5);
    ASSERT_NEAR(out_max[1], 5.0f, 1e-5);
    ASSERT_NEAR(out_max[2], 6.0f, 1e-5);
}

/* ========================================================================== */
/* Interleaved packing                                                        */
/* ========================================================================== */

TEST(c_pack_interleaved) {
    LydraContext ctx = NULL;
    lydraCreateContext(&ctx);

    float positions[] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    float texcoords[] = {0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f};

    LydraAttributeDesc attrs[2];
    attrs[0].pData = positions;
    attrs[0].componentCount = 3;
    attrs[0].location = 0;
    attrs[1].pData = texcoords;
    attrs[1].componentCount = 2;
    attrs[1].location = 1;

    uint32_t stride = 0, size = 0;

    /* First call: query */
    LydraResult r = lydraPackInterleaved(ctx, 2, attrs, 3, &stride, &size, NULL);
    ASSERT_EQ(r, LYDRA_SUCCESS);
    ASSERT_EQ(stride, 20u);
    ASSERT_EQ(size, 60u);

    /* Second call: get data */
    void* buf = malloc(size);
    ASSERT_TRUE(buf != NULL);
    r = lydraPackInterleaved(ctx, 2, attrs, 3, &stride, &size, buf);
    ASSERT_EQ(r, LYDRA_SUCCESS);

    /* Verify first vertex */
    float* fdata = (float*)buf;
    ASSERT_NEAR(fdata[0], 0.0f, 1e-5);
    ASSERT_NEAR(fdata[1], 0.0f, 1e-5);
    ASSERT_NEAR(fdata[2], 0.0f, 1e-5);
    ASSERT_NEAR(fdata[3], 0.0f, 1e-5);
    ASSERT_NEAR(fdata[4], 0.0f, 1e-5);

    free(buf);
    lydraDestroyContext(ctx);
}

/* ========================================================================== */
/* Index format selection                                                     */
/* ========================================================================== */

TEST(c_index_format) {
    ASSERT_EQ(lydraSelectIndexFormat(100), LYDRA_FORMAT_R16_UINT);
    ASSERT_EQ(lydraSelectIndexFormat(65535), LYDRA_FORMAT_R16_UINT);
    ASSERT_EQ(lydraSelectIndexFormat(65536), LYDRA_FORMAT_R32_UINT);
}

/* ========================================================================== */
/* Transform utilities                                                        */
/* ========================================================================== */

TEST(c_mat4_identity) {
    float m[16];
    lydraMat4Identity(m);
    ASSERT_NEAR(m[0], 1.0f, 1e-5);
    ASSERT_NEAR(m[5], 1.0f, 1e-5);
    ASSERT_NEAR(m[10], 1.0f, 1e-5);
    ASSERT_NEAR(m[15], 1.0f, 1e-5);
    ASSERT_NEAR(m[1], 0.0f, 1e-5);
    ASSERT_NEAR(m[4], 0.0f, 1e-5);
}

TEST(c_mat4_transform_points) {
    /* Build a translation matrix manually (column-major) */
    float m[16];
    lydraMat4Identity(m);
    m[12] = 10.0f;
    m[13] = 20.0f;
    m[14] = 30.0f;

    float pin[] = {1.0f, 2.0f, 3.0f};
    float pout[3];
    lydraMat4TransformPoints(m, 1, pin, pout);
    ASSERT_NEAR(pout[0], 11.0f, 1e-5);
    ASSERT_NEAR(pout[1], 22.0f, 1e-5);
    ASSERT_NEAR(pout[2], 33.0f, 1e-5);
}

TEST(c_mat4_multiply) {
    float a[16], b[16], out[16];
    lydraMat4Identity(a);
    lydraMat4Identity(b);
    a[12] = 5.0f;  /* translate X by 5 */
    b[0] = 2.0f;   /* scale X by 2 */

    /* a * b = translate(5) * scale(2) */
    lydraMat4Multiply(a, b, out);

    float pin[] = {1.0f, 0.0f, 0.0f};
    float pout[3];
    lydraMat4TransformPoints(out, 1, pin, pout);
    /* scale(1,0,0) -> (2,0,0), then translate -> (7,0,0) */
    ASSERT_NEAR(pout[0], 7.0f, 1e-5);
}

/* ========================================================================== */
/* Main                                                                       */
/* ========================================================================== */

int main(void) {
    int prev_fail = 0;

    printf("=== Lydra C API Unit Tests ===\n\n");

    printf("[Context]\n");
    RUN(context_create_destroy);

    printf("\n[Triangulation]\n");
    RUN(c_triangulate_quad);

    printf("\n[Normals]\n");
    RUN(c_smooth_normals);
    RUN(c_flat_normals);

    printf("\n[Tangents]\n");
    RUN(c_tangents);

    printf("\n[Bounds]\n");
    RUN(c_bounds);

    printf("\n[Interleave]\n");
    RUN(c_pack_interleaved);

    printf("\n[Index Format]\n");
    RUN(c_index_format);

    printf("\n[Transforms]\n");
    RUN(c_mat4_identity);
    RUN(c_mat4_transform_points);
    RUN(c_mat4_multiply);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
