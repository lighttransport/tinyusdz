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

typedef struct LydraCMaterialData {
    float diffuse_color[3];      /* default (0.18, 0.18, 0.18) */
    float metallic;              /* default 0.0 */
    float roughness;             /* default 0.5 */
    float ior;                   /* default 1.5 */
    float opacity;               /* default 1.0 */
    float specular_color[3];     /* default (0,0,0) */
    float clearcoat;             /* default 0.0 */
    float clearcoat_roughness;   /* default 0.01 */
    float emissive_color[3];     /* default (0,0,0) */
} LydraCMaterialData;

/*
 * Extract UsdPreviewSurface material data from a Material prim.
 * Walks children to find Shader with info:id == "UsdPreviewSurface".
 * On success, *out is filled with material properties.
 */
LusdResult lydra_c_extract_material(LusdLayer layer, LusdPrim material_prim,
                                     LydraCMaterialData* out);

/*
 * Resolve material:binding relationship on a prim.
 * Returns the target path string (e.g. "/Root/Material") or NULL if none.
 * The returned pointer is valid for the lifetime of the layer.
 */
const char* lydra_c_resolve_material_binding(LusdLayer layer, LusdPrim prim);

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

/* ================================================================
 * GeomSubset (per-face material) support
 * ================================================================ */

typedef struct LydraCGeomSubset {
    int32_t*    face_indices;    /* malloc'd face index array */
    uint32_t    face_count;
    const char* material_path;   /* target of material:binding (layer-lifetime) */
} LydraCGeomSubset;

/*
 * Extract GeomSubset children from a Mesh prim.
 * Returns the number of subsets found. On success, *out_subsets points
 * to a malloc'd array of LydraCGeomSubset structs.
 * Caller must call lydra_c_free_geom_subsets() when done.
 */
uint32_t lydra_c_extract_geom_subsets(LusdLayer layer, LusdPrim mesh_prim,
                                      LydraCGeomSubset** out_subsets);

/* Free a malloc'd array of LydraCGeomSubset structs. */
void lydra_c_free_geom_subsets(LydraCGeomSubset* subsets, uint32_t count);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_SCENE_H */
