/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024 Light Transport Entertainment Inc.
 *
 * lydra-c scene module — pure C11, no C++ dependencies.
 *
 * Extracts mesh data from lightusd-c Layer/Prim internals.
 */
#ifndef LYDRA_C_SCENE_H
#define LYDRA_C_SCENE_H

#include <stdint.h>
#include "lightusd/lusd_handles.h"
#include "lightusd/lusd_result.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LydraCMeshData {
    float*    points;               /* N*3 floats */
    uint32_t  point_count;          /* number of vertices (N) */
    int32_t*  face_vertex_counts;
    uint32_t  fvc_count;
    int32_t*  face_vertex_indices;
    uint32_t  fvi_count;
    float*    normals;              /* optional, N*3 floats (NULL if absent) */
    uint32_t  normal_count;
    float*    uvs;                  /* optional, N*2 floats (NULL if absent) */
    uint32_t  uv_count;
} LydraCMeshData;

/*
 * Extract mesh data from a Mesh prim.
 * On success, all non-NULL pointer fields in *out are malloc'd; caller
 * must call lydra_c_free_mesh_data() when done.
 * Returns LUSD_SUCCESS on success, error code on failure.
 */
LusdResult lydra_c_extract_mesh(LusdLayer layer, LusdPrim prim,
                                LydraCMeshData* out);

/* Free all malloc'd buffers inside a LydraCMeshData. */
void lydra_c_free_mesh_data(LydraCMeshData* data);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_SCENE_H */
