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
 * Xform extraction
 * ================================================================ */

/*
 * Extract xformOp:transform (matrix4d) from a prim.
 * Output is row-major double[16]: out[row*4+col]. Translation at [12..14].
 * Returns 0 on success, -1 if no xformOp:transform found (caller uses identity).
 */
int lydra_c_extract_xform(LusdLayer layer, LusdPrim prim, double out_matrix[16]);

/* ================================================================
 * Camera extraction
 * ================================================================ */

typedef struct LydraCCameraData {
    float focal_length;        /* mm, default 50 */
    float vertical_aperture;   /* mm, default 15.2908 */
    float horizontal_aperture; /* mm, default 20.965 */
    float znear;               /* default 0.1 */
    float zfar;                /* default 1000000 */
    double shutter_open;       /* default 0.0 */
    double shutter_close;      /* default 0.0 */
} LydraCCameraData;

LusdResult lydra_c_extract_camera(LusdLayer layer, LusdPrim prim,
                                   LydraCCameraData* out);

/* ================================================================
 * Light extraction
 * ================================================================ */

typedef struct LydraCLightData {
    int type;           /* 0=Distant, 1=Sphere, 2=Rect */
    float color[3];     /* default (1,1,1) */
    float intensity;    /* default 1 */
    float exposure;     /* default 0 */
    float radius;       /* SphereLight, default 0.5 */
    float width;        /* RectLight, default 1 */
    float height;       /* RectLight, default 1 */
} LydraCLightData;

#define LYDRA_C_LIGHT_DISTANT  0
#define LYDRA_C_LIGHT_SPHERE   1
#define LYDRA_C_LIGHT_RECT     2

LusdResult lydra_c_extract_light(LusdLayer layer, LusdPrim prim,
                                  LydraCLightData* out);

/* ================================================================
 * DomeLight / Envmap extraction
 * ================================================================ */

typedef struct LydraCDomeLightData {
    float intensity;
    float exposure;
    float color[3];
    const char* texture_file;  /* asset path string, or NULL */
} LydraCDomeLightData;

LusdResult lydra_c_extract_dome_light(LusdLayer layer, LusdPrim prim,
                                       LydraCDomeLightData* out);

/* ================================================================
 * Time-sampled xform (motion blur)
 * ================================================================ */

/*
 * Extract composed xform at a specific time code.
 * Falls back to non-time-sampled values when time samples not found.
 * Returns 0 on success, -1 if no xform found.
 */
int lydra_c_extract_xform_at(LusdLayer layer, LusdPrim prim,
                              double time_code, double out_matrix[16]);

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

/* ================================================================
 * OpenPBR material extraction
 * ================================================================ */

typedef struct LydraCOpenPBRData {
    /* Base */
    float base_weight;              /* default 1.0 */
    float base_color[3];            /* default (0.8, 0.8, 0.8) */
    float base_roughness;           /* default 0.0 */
    float base_metalness;           /* default 0.0 */
    float base_diffuse_roughness;   /* default 0.0 */
    /* Specular */
    float specular_weight;          /* default 1.0 */
    float specular_color[3];        /* default (1,1,1) */
    float specular_roughness;       /* default 0.3 */
    float specular_ior;             /* default 1.5 */
    float specular_ior_level;       /* default 0.5 */
    float specular_anisotropy;      /* default 0.0 */
    float specular_rotation;        /* default 0.0 */
    /* Transmission */
    float transmission_weight;      /* default 0.0 */
    float transmission_color[3];    /* default (1,1,1) */
    float transmission_depth;       /* default 0.0 */
    float transmission_scatter[3];  /* default (0,0,0) */
    float transmission_scatter_anisotropy; /* default 0.0 */
    float transmission_dispersion;  /* default 0.0 */
    /* Subsurface */
    float subsurface_weight;        /* default 0.0 */
    float subsurface_color[3];      /* default (0.8,0.8,0.8) */
    float subsurface_radius;        /* default 1.0 */
    float subsurface_radius_scale[3]; /* default (1,1,1) */
    float subsurface_scale;         /* default 1.0 */
    float subsurface_anisotropy;    /* default 0.0 */
    /* Sheen */
    float sheen_weight;             /* default 0.0 */
    float sheen_color[3];           /* default (1,1,1) */
    float sheen_roughness;          /* default 0.3 */
    /* Fuzz */
    float fuzz_weight;              /* default 0.0 */
    float fuzz_color[3];            /* default (1,1,1) */
    float fuzz_roughness;           /* default 0.5 */
    /* Thin Film */
    float thin_film_weight;         /* default 0.0 */
    float thin_film_thickness;      /* default 0.5 */
    float thin_film_ior;            /* default 1.5 */
    /* Coat */
    float coat_weight;              /* default 0.0 */
    float coat_color[3];            /* default (1,1,1) */
    float coat_roughness;           /* default 0.1 */
    float coat_anisotropy;          /* default 0.0 */
    float coat_rotation;            /* default 0.0 */
    float coat_ior;                 /* default 1.6 */
    float coat_affect_color[3];     /* default (0,0,0) */
    float coat_affect_roughness;    /* default 0.0 */
    /* Emission */
    float emission_luminance;       /* default 0.0 */
    float emission_color[3];        /* default (1,1,1) */
    /* Geometry */
    float opacity;                  /* default 1.0 */

    int is_openpbr;  /* 1 if OpenPBR_Surface found, 0 if UsdPreviewSurface */

    /* Texture asset paths (NULL if no texture connected).
     * Pointers are valid for the lifetime of the layer. */
    const char* base_color_tex;
    const char* metalness_tex;
    const char* roughness_tex;
    const char* normal_tex;
    const char* emissive_tex;
    const char* opacity_tex;
} LydraCOpenPBRData;

/*
 * Extract material data as OpenPBR fields.
 * If shader is OpenPBR_Surface, reads native fields.
 * If shader is UsdPreviewSurface, maps to OpenPBR equivalents.
 * On success, *out is filled with material properties.
 */
LusdResult lydra_c_extract_openpbr(LusdLayer layer, LusdPrim material_prim,
                                    LydraCOpenPBRData* out);

/* ================================================================
 * Asset path resolution utility
 * ================================================================ */

/*
 * Join base_dir and asset_path into out_buf.
 * If asset_path is absolute, copies as-is.
 * Otherwise: base_dir + "/" + asset_path (with "./" and "\" normalization).
 * Returns out_buf on success, NULL if buffer too small.
 */
const char* lydra_c_resolve_asset_path(const char* base_dir,
                                        const char* asset_path,
                                        char* out_buf, uint32_t buf_size);

#ifdef __cplusplus
}
#endif

#endif /* LYDRA_C_SCENE_H */
