/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c mesh utilities — pure C11, no C++ dependencies.
 */
#ifndef LYDRA_C_MESH_H
#define LYDRA_C_MESH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Fan triangulation: polygon soup -> triangle indices.
 * out_indices is malloc'd; caller must free().
 * Returns 0 on success, non-zero on failure.
 */
int lydra_c_triangulate(const int32_t* face_vertex_indices, uint32_t fvi_count,
                        const int32_t* face_vertex_counts, uint32_t fvc_count,
                        uint32_t** out_indices, uint32_t* out_count);

/*
 * Flat normals: one normal per triangle vertex (3 normals per triangle).
 * out_normals is malloc'd (out_count * 3 floats); caller must free().
 */
int lydra_c_compute_flat_normals(const float* positions, uint32_t vertex_count,
                                 const uint32_t* indices, uint32_t index_count,
                                 float** out_normals, uint32_t* out_count);

/*
 * Smooth normals: area-weighted average at each vertex.
 * out_normals is malloc'd (vertex_count * 3 floats); caller must free().
 */
int lydra_c_compute_smooth_normals(const float* positions, uint32_t vertex_count,
                                   const uint32_t* indices, uint32_t index_count,
                                   float** out_normals);

/*
 * Compute axis-aligned bounding box.
 */
void lydra_c_compute_bounds(const float* positions, uint32_t vertex_count,
                            float out_min[3], float out_max[3]);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_MESH_H */
