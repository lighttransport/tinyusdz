/*
 * lusd_material.h - Material query API (OpenPBR model)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_MATERIAL_H
#define LUSD_MATERIAL_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"

LUSD_EXTERN_C_BEGIN

/*
 * Full OpenPBR material parameter block.
 * Colors are linear RGB in float[3].
 * Field names follow OpenPBR Surface Specification 1.0 naming.
 */
typedef struct LusdOpenPBRMaterial {
    /* Base */
    float  base_weight;
    float  base_color[3];
    float  base_roughness;
    float  base_metalness;
    float  base_diffuse_roughness;

    /* Specular */
    float  specular_weight;
    float  specular_color[3];
    float  specular_roughness;
    float  specular_ior;
    float  specular_ior_level;
    float  specular_anisotropy;
    float  specular_rotation;

    /* Transmission */
    float  transmission_weight;
    float  transmission_color[3];
    float  transmission_depth;
    float  transmission_scatter[3];
    float  transmission_scatter_anisotropy;
    float  transmission_dispersion;

    /* Subsurface */
    float  subsurface_weight;
    float  subsurface_color[3];
    float  subsurface_radius;
    float  subsurface_radius_scale[3];
    float  subsurface_scale;
    float  subsurface_anisotropy;

    /* Sheen */
    float  sheen_weight;
    float  sheen_color[3];
    float  sheen_roughness;

    /* Fuzz */
    float  fuzz_weight;
    float  fuzz_color[3];
    float  fuzz_roughness;

    /* Thin Film */
    float  thin_film_weight;
    float  thin_film_thickness;
    float  thin_film_ior;

    /* Coat */
    float  coat_weight;
    float  coat_color[3];
    float  coat_roughness;
    float  coat_anisotropy;
    float  coat_rotation;
    float  coat_ior;
    float  coat_affect_color[3];
    float  coat_affect_roughness;

    /* Emission */
    float  emission_luminance;
    float  emission_color[3];

    /* Geometry */
    float  opacity;
} LusdOpenPBRMaterial;

/*
 * Returns the number of materials resolved from this stage.
 * Call before lusdStageGetMaterials to size your array.
 * Triggers lazy tydra conversion on first call.
 */
LUSD_API LusdResult lusdStageGetMaterialCount(
    LusdStage   stage,
    uint32_t*   pCount);

/*
 * Fill pMaterials[0..count-1] with OpenPBR material blocks.
 * count must not exceed the value from lusdStageGetMaterialCount.
 */
LUSD_API LusdResult lusdStageGetMaterials(
    LusdStage              stage,
    uint32_t               count,
    LusdOpenPBRMaterial*   pMaterials);

/*
 * Copy the display name of material[index] into buf (NUL-terminated).
 * Returns LUSD_ERROR_INVALID_ARGUMENT if index >= count or buf == NULL.
 * Returns LUSD_INCOMPLETE if bufLen is insufficient
 * (buf still receives a NUL-terminated prefix).
 */
LUSD_API LusdResult lusdStageGetMaterialName(
    LusdStage   stage,
    uint32_t    index,
    char*       buf,
    uint32_t    bufLen);

/*
 * Copy the absolute USD path of material[index] into buf (NUL-terminated).
 * Returns LUSD_ERROR_INVALID_ARGUMENT if index >= count or buf == NULL.
 * Returns LUSD_INCOMPLETE if bufLen is insufficient.
 */
LUSD_API LusdResult lusdStageGetMaterialPath(
    LusdStage   stage,
    uint32_t    index,
    char*       buf,
    uint32_t    bufLen);

/* -------------------------------------------------------------------------
 * Light query API
 * ------------------------------------------------------------------------- */

typedef enum LusdLightType {
    LUSD_LIGHT_TYPE_DISTANT  = 0,
    LUSD_LIGHT_TYPE_POINT    = 1,
    LUSD_LIGHT_TYPE_SPHERE   = 2,
    LUSD_LIGHT_TYPE_DOME     = 3,
    LUSD_LIGHT_TYPE_OTHER    = 255
} LusdLightType;

typedef struct LusdLight {
    LusdLightType type;
    float  color[3];        /* linear RGB */
    float  intensity;       /* physical intensity (already multiplied by 2^exposure) */
    float  position[3];     /* world-space position (Point/Sphere lights) */
    float  direction[3];    /* world-space direction (Distant lights, normalized) */
    float  radius;          /* sphere radius (SphereLight) */
    char   name[128];
} LusdLight;

LUSD_API LusdResult lusdStageGetLightCount(
    LusdStage   stage,
    uint32_t*   pCount);

LUSD_API LusdResult lusdStageGetLights(
    LusdStage    stage,
    uint32_t     count,
    LusdLight*   pLights);

LUSD_EXTERN_C_END

#endif /* LUSD_MATERIAL_H */
