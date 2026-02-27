/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c mesh utilities — pure C11 implementation.
 */
#include "lydra_c_mesh.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int lydra_c_triangulate(const int32_t* face_vertex_indices, uint32_t fvi_count,
                        const int32_t* face_vertex_counts, uint32_t fvc_count,
                        uint32_t** out_indices, uint32_t* out_count) {
    *out_indices = NULL;
    *out_count = 0;

    if (!face_vertex_indices || !face_vertex_counts || fvc_count == 0)
        return -1;

    /* Count triangles */
    uint32_t tri_count = 0;
    for (uint32_t i = 0; i < fvc_count; i++) {
        int32_t c = face_vertex_counts[i];
        if (c >= 3) tri_count += (uint32_t)(c - 2);
    }

    uint32_t* buf = (uint32_t*)malloc((size_t)tri_count * 3 * sizeof(uint32_t));
    if (!buf) return -1;

    uint32_t idx_offset = 0;
    uint32_t out_idx = 0;
    for (uint32_t f = 0; f < fvc_count; f++) {
        int32_t count = face_vertex_counts[f];
        if (count < 3) { idx_offset += (uint32_t)count; continue; }
        if (idx_offset + (uint32_t)count > fvi_count) { free(buf); return -1; }

        uint32_t v0 = (uint32_t)face_vertex_indices[idx_offset];
        for (int32_t i = 1; i < count - 1; i++) {
            buf[out_idx++] = v0;
            buf[out_idx++] = (uint32_t)face_vertex_indices[idx_offset + i];
            buf[out_idx++] = (uint32_t)face_vertex_indices[idx_offset + i + 1];
        }
        idx_offset += (uint32_t)count;
    }

    *out_indices = buf;
    *out_count = out_idx;
    return 0;
}

static void vec3_cross(const float a[3], const float b[3], float out[3]) {
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}

static void vec3_normalize(float v[3]) {
    float len = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
    if (len > 0.0f) { v[0] /= len; v[1] /= len; v[2] /= len; }
    else { v[0] = 0.0f; v[1] = 1.0f; v[2] = 0.0f; }
}

int lydra_c_compute_flat_normals(const float* positions, uint32_t vertex_count,
                                 const uint32_t* indices, uint32_t index_count,
                                 float** out_normals, uint32_t* out_count) {
    *out_normals = NULL;
    *out_count = 0;
    if (index_count % 3 != 0) return -1;

    uint32_t n_verts = index_count; /* one normal per index */
    float* normals = (float*)malloc((size_t)n_verts * 3 * sizeof(float));
    if (!normals) return -1;

    for (uint32_t i = 0; i < index_count; i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            free(normals); return -1;
        }
        const float* p0 = positions + i0*3;
        const float* p1 = positions + i1*3;
        const float* p2 = positions + i2*3;

        float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        float n[3];
        vec3_cross(e1, e2, n);
        vec3_normalize(n);

        for (int v = 0; v < 3; v++) {
            normals[(i+v)*3+0] = n[0];
            normals[(i+v)*3+1] = n[1];
            normals[(i+v)*3+2] = n[2];
        }
    }

    *out_normals = normals;
    *out_count = n_verts;
    return 0;
}

int lydra_c_compute_smooth_normals(const float* positions, uint32_t vertex_count,
                                   const uint32_t* indices, uint32_t index_count,
                                   float** out_normals) {
    *out_normals = NULL;
    if (index_count % 3 != 0 || vertex_count == 0) return -1;

    float* normals = (float*)calloc((size_t)vertex_count * 3, sizeof(float));
    if (!normals) return -1;

    for (uint32_t i = 0; i < index_count; i += 3) {
        uint32_t i0 = indices[i], i1 = indices[i+1], i2 = indices[i+2];
        if (i0 >= vertex_count || i1 >= vertex_count || i2 >= vertex_count) {
            free(normals); return -1;
        }
        const float* p0 = positions + i0*3;
        const float* p1 = positions + i1*3;
        const float* p2 = positions + i2*3;

        float e1[3] = { p1[0]-p0[0], p1[1]-p0[1], p1[2]-p0[2] };
        float e2[3] = { p2[0]-p0[0], p2[1]-p0[1], p2[2]-p0[2] };
        float fn[3];
        vec3_cross(e1, e2, fn); /* area-weighted, not normalized */

        for (int c = 0; c < 3; c++) {
            normals[i0*3+c] += fn[c];
            normals[i1*3+c] += fn[c];
            normals[i2*3+c] += fn[c];
        }
    }

    for (uint32_t i = 0; i < vertex_count; i++)
        vec3_normalize(normals + i*3);

    *out_normals = normals;
    return 0;
}

void lydra_c_compute_bounds(const float* positions, uint32_t vertex_count,
                            float out_min[3], float out_max[3]) {
    out_min[0] = out_min[1] = out_min[2] =  3.402823466e+38f;
    out_max[0] = out_max[1] = out_max[2] = -3.402823466e+38f;

    for (uint32_t i = 0; i < vertex_count; i++) {
        float x = positions[i*3+0], y = positions[i*3+1], z = positions[i*3+2];
        if (x < out_min[0]) out_min[0] = x;
        if (y < out_min[1]) out_min[1] = y;
        if (z < out_min[2]) out_min[2] = z;
        if (x > out_max[0]) out_max[0] = x;
        if (y > out_max[1]) out_max[1] = y;
        if (z > out_max[2]) out_max[2] = z;
    }
}
