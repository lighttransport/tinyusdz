/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c transform utilities — pure C11, no C++ dependencies.
 */
#ifndef LYDRA_C_TRANSFORM_H
#define LYDRA_C_TRANSFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Column-major 4x4 matrix: m[col*4 + row] */
typedef struct LydraCMat4 {
    float m[16];
} LydraCMat4;

void lydra_c_mat4_identity(LydraCMat4* out);
void lydra_c_mat4_multiply(const LydraCMat4* a, const LydraCMat4* b, LydraCMat4* out);
void lydra_c_mat4_transform_points(const LydraCMat4* mat, const float* in,
                                   float* out, uint32_t count);
void lydra_c_mat4_transform_normals(const LydraCMat4* mat, const float* in,
                                    float* out, uint32_t count);

/* Transform AABB (min/max float[3]) by matrix, writing new min/max */
void lydra_c_transform_aabb(const float in_min[3], const float in_max[3],
                            const LydraCMat4* mat,
                            float out_min[3], float out_max[3]);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_TRANSFORM_H */
