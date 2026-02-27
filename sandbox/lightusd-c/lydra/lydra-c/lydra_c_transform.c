/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c transform utilities — pure C11 implementation.
 */
#include "lydra_c_transform.h"

#include <math.h>
#include <string.h>

void lydra_c_mat4_identity(LydraCMat4* out) {
    memset(out->m, 0, sizeof(out->m));
    out->m[0] = 1.0f; out->m[5] = 1.0f; out->m[10] = 1.0f; out->m[15] = 1.0f;
}

void lydra_c_mat4_multiply(const LydraCMat4* a, const LydraCMat4* b, LydraCMat4* out) {
    LydraCMat4 r;
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            float sum = 0.0f;
            for (int k = 0; k < 4; k++)
                sum += a->m[k*4 + row] * b->m[col*4 + k];
            r.m[col*4 + row] = sum;
        }
    }
    *out = r;
}

void lydra_c_mat4_transform_points(const LydraCMat4* mat, const float* in,
                                   float* out, uint32_t count) {
    const float* m = mat->m;
    for (uint32_t i = 0; i < count; i++) {
        float px = in[i*3+0], py = in[i*3+1], pz = in[i*3+2];
        float x = m[0]*px + m[4]*py + m[8]*pz  + m[12];
        float y = m[1]*px + m[5]*py + m[9]*pz  + m[13];
        float z = m[2]*px + m[6]*py + m[10]*pz + m[14];
        float w = m[3]*px + m[7]*py + m[11]*pz + m[15];
        if (w != 0.0f && w != 1.0f) { x /= w; y /= w; z /= w; }
        out[i*3+0] = x; out[i*3+1] = y; out[i*3+2] = z;
    }
}

void lydra_c_mat4_transform_normals(const LydraCMat4* mat, const float* in,
                                    float* out, uint32_t count) {
    const float* m = mat->m;
    for (uint32_t i = 0; i < count; i++) {
        float nx = in[i*3+0], ny = in[i*3+1], nz = in[i*3+2];
        float x = m[0]*nx + m[4]*ny + m[8]*nz;
        float y = m[1]*nx + m[5]*ny + m[9]*nz;
        float z = m[2]*nx + m[6]*ny + m[10]*nz;
        float len = sqrtf(x*x + y*y + z*z);
        if (len > 0.0f) { x /= len; y /= len; z /= len; }
        out[i*3+0] = x; out[i*3+1] = y; out[i*3+2] = z;
    }
}

void lydra_c_transform_aabb(const float in_min[3], const float in_max[3],
                            const LydraCMat4* mat,
                            float out_min[3], float out_max[3]) {
    float corners[8][3] = {
        {in_min[0], in_min[1], in_min[2]},
        {in_max[0], in_min[1], in_min[2]},
        {in_min[0], in_max[1], in_min[2]},
        {in_max[0], in_max[1], in_min[2]},
        {in_min[0], in_min[1], in_max[2]},
        {in_max[0], in_min[1], in_max[2]},
        {in_min[0], in_max[1], in_max[2]},
        {in_max[0], in_max[1], in_max[2]},
    };

    out_min[0] = out_min[1] = out_min[2] =  3.402823466e+38f;
    out_max[0] = out_max[1] = out_max[2] = -3.402823466e+38f;

    for (int c = 0; c < 8; c++) {
        float t[3];
        lydra_c_mat4_transform_points(mat, corners[c], t, 1);
        for (int j = 0; j < 3; j++) {
            if (t[j] < out_min[j]) out_min[j] = t[j];
            if (t[j] > out_max[j]) out_max[j] = t[j];
        }
    }
}
