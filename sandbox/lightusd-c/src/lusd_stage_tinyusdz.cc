/*
 * lusd_stage_tinyusdz.cc - Stage/Prim/Attribute implementation using tinyusdz
 *
 * Implements lusd_stage.h, lusd_prim.h, and lusd_attribute.h APIs by wrapping
 * the tinyusdz C++ USD parser library.
 *
 * Replaces the C stub files (lusd_stage.c, lusd_prim.c, lusd_attribute.c)
 * when LUSD_TINYUSDZ_DIR is set in CMake.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* C API headers — all have LUSD_EXTERN_C_BEGIN/END guards */
#include "lightusd/lusd_stage.h"
#include "lightusd/lusd_prim.h"
#include "lightusd/lusd_attribute.h"
#include "lightusd/lusd_value.h"
#include "lightusd/lusd_write.h"
#include "lightusd/lusd_material.h"

/* Internal C header — wrap in extern "C" so all declared helper functions
 * (lusd_alloc, lusd_set_errorf, lusd_diag, …) get C linkage in C++ builds.
 * Nested LUSD_EXTERN_C_BEGIN/END from included headers are valid in C++. */
extern "C" {
#include "internal/lusd_internal.h"
}

/* C++ standard library */
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cstdarg>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

/* tinyusdz C++ API — single include pulls in Stage, Prim, GeomMesh, etc. */
#include "tinyusdz.hh"
#include "tydra/render-data.hh"
#include "tydra/scene-access.hh"
#include <unordered_map>

/* ============================================================
 * Write-mode prim / attribute types
 *
 * LusdWritePrim_T is returned by lusdCreatePrim as an opaque LusdPrim handle.
 * The magic number at offset 0 lets all LusdPrim-receiving functions
 * distinguish write prims from read prims (LusdPrim_T).
 * ============================================================ */

static const uint32_t WRITE_PRIM_MAGIC = 0x574C5554u;  /* 'WLUT' */

struct LusdWriteAttr_T {
    char*            name;          /* owned */
    LusdValueType    type;
    LusdVariability  variability;
    bool             custom;
    bool             has_default;
    LusdValueData    default_value; /* deep-copy; heap buffer owned here */
};

/* Free only the heap storage inside a LusdValueData (does NOT free the struct). */
static void write_value_data_free(LusdValueData& vd) {
    if (vd.useHeap && vd.storage.heap.ptr) {
        std::free(vd.storage.heap.ptr);
        vd.storage.heap.ptr = nullptr;
    } else if (!vd.useHeap && vd.type == LUSD_VALUE_TYPE_STRING) {
        char* sp;
        std::memcpy(&sp, vd.storage.inlineData, sizeof(char*));
        if (sp) std::free(sp);
        std::memset(vd.storage.inlineData, 0, sizeof(char*));
    }
}

static void write_attr_free(LusdWriteAttr_T& a) {
    std::free(a.name);
    if (a.has_default)
        write_value_data_free(a.default_value);
}

struct LusdWritePrim_T {
    uint32_t           magic;        /* always WRITE_PRIM_MAGIC */
    LusdSpecifier      specifier;
    bool               active;
    char*              name;         /* owned */
    char*              type_name;    /* owned, or nullptr */
    std::vector<LusdWritePrim_T*> children;  /* non-owning in stage context */
    std::vector<LusdWriteAttr_T> attrs;
};

static void write_prim_destroy(LusdWritePrim_T* p) {
    if (!p) return;
    for (auto* c : p->children) write_prim_destroy(c);
    for (auto& a : p->attrs)    write_attr_free(a);
    std::free(p->name);
    std::free(p->type_name);
    delete p;
}

static bool is_write_prim(LusdPrim prim) {
    if (!prim) return false;
    uint32_t magic;
    std::memcpy(&magic, prim, sizeof(uint32_t));
    return magic == WRITE_PRIM_MAGIC;
}

static LusdWritePrim_T* to_write_prim(LusdPrim prim) {
    return reinterpret_cast<LusdWritePrim_T*>(prim);
}

static LusdWriteAttr_T* find_attr(LusdWritePrim_T* p, const char* name) {
    for (auto& a : p->attrs)
        if (std::strcmp(a.name, name) == 0) return &a;
    return nullptr;
}

static bool value_data_deep_copy(LusdValueData& dst, const LusdValueData& src) {
    dst = src;
    if (src.useHeap && src.storage.heap.ptr && src.storage.heap.size > 0) {
        dst.storage.heap.ptr = std::malloc(src.storage.heap.size);
        if (!dst.storage.heap.ptr) return false;
        std::memcpy(dst.storage.heap.ptr, src.storage.heap.ptr,
                    src.storage.heap.size);
    } else if (!src.useHeap && src.type == LUSD_VALUE_TYPE_STRING) {
        char* sp;
        std::memcpy(&sp, src.storage.inlineData, sizeof(char*));
        if (sp) {
            auto len = std::strlen(sp);
            char* dup = static_cast<char*>(std::malloc(len + 1));
            if (!dup) return false;
            std::memcpy(dup, sp, len + 1);
            std::memcpy(dst.storage.inlineData, &dup, sizeof(char*));
        }
    }
    return true;
}

/* ============================================================
 * LusdWriter_T — file-write context
 * ============================================================ */
struct LusdWriter_T {
    LusdFormat  format;
    char*       file_path;   /* owned, null-terminated; NULL for to-string only */
};

/* ============================================================
 * Internal struct definitions
 * LusdStage_T and LusdPrim_T must be defined before use because
 * lusd_handles.h declares them as opaque pointer types (struct name_T*).
 * ============================================================ */

struct LusdPrim_T;  /* forward */

struct LusdStage_T {
    bool            is_write;  /* true = write-mode, false = tinyusdz read-mode */
    LusdInstance    inst;      /* non-owning back-reference to parent instance */

    /* Read-mode (tinyusdz): */
    tinyusdz::Stage stage;
    std::vector<std::unique_ptr<LusdPrim_T>> primArena;
    LusdPrim primHandle(const tinyusdz::Prim* p);

    /* Write-mode: */
    std::vector<LusdWritePrim_T*> write_roots;
    LusdUpAxis  write_up_axis           = LUSD_UP_AXIS_Y;
    double      write_mpu               = 0.01;
    double      write_start_tc          = 0.0;
    double      write_end_tc            = 0.0;
    double      write_fps               = 24.0;
};

struct LusdPrim_T {
    const tinyusdz::Prim* prim;       /* non-owning pointer into stage.stage */
    LusdStage_T*          stageData;  /* non-owning back-reference */
};

LusdPrim LusdStage_T::primHandle(const tinyusdz::Prim* p) {
    auto h = std::make_unique<LusdPrim_T>();
    h->prim      = p;
    h->stageData = this;
    LusdPrim handle = h.get();
    primArena.push_back(std::move(h));
    return handle;
}

/* ============================================================
 * Material cache (lazy tydra conversion, keyed by stage pointer)
 * ============================================================ */

struct LusdMatEntry {
    LusdOpenPBRMaterial mat;
    char name[256];
    char abs_path[512];
};

static std::unordered_map<uintptr_t, std::vector<LusdMatEntry>> g_mat_cache;
static std::unordered_map<uintptr_t, std::vector<LusdLight>>    g_light_cache;

static void lusd__stage_build_material_cache(LusdStage_T* sd) {
    using namespace tinyusdz::tydra;

    RenderSceneConverter converter;
    RenderSceneConverterEnv env(sd->stage);
    RenderScene render_scene;
    std::string warn, err;
    bool ok = converter.ConvertToRenderScene(env, &render_scene);
    if (!ok) return; /* leave cache empty on error */

    /* --- lights --- */
    {
        std::vector<LusdLight> lights;
        lights.reserve(render_scene.lights.size());
        for (const auto& rl : render_scene.lights) {
            LusdLight l;
            std::memset(&l, 0, sizeof(l));
            float mult = rl.intensity * std::pow(2.0f, rl.exposure);
            l.color[0] = rl.color[0] * mult;
            l.color[1] = rl.color[1] * mult;
            l.color[2] = rl.color[2] * mult;
            l.intensity = mult;
            std::snprintf(l.name, sizeof(l.name), "%s", rl.name.c_str());
            if (rl.type == RenderLight::Type::Distant) {
                l.type = LUSD_LIGHT_TYPE_DISTANT;
                /* -Z in light-local space transformed by rotation matrix */
                l.direction[0] = -(float)rl.transform.m[0][2];
                l.direction[1] = -(float)rl.transform.m[1][2];
                l.direction[2] = -(float)rl.transform.m[2][2];
                float len = std::sqrt(l.direction[0]*l.direction[0] +
                                      l.direction[1]*l.direction[1] +
                                      l.direction[2]*l.direction[2]);
                if (len < 1e-8f) {
                    l.direction[0] = rl.direction[0];
                    l.direction[1] = rl.direction[1];
                    l.direction[2] = rl.direction[2];
                } else {
                    l.direction[0] /= len;
                    l.direction[1] /= len;
                    l.direction[2] /= len;
                }
            } else if (rl.type == RenderLight::Type::Point) {
                l.type = LUSD_LIGHT_TYPE_POINT;
                l.position[0] = (float)rl.transform.m[3][0];
                l.position[1] = (float)rl.transform.m[3][1];
                l.position[2] = (float)rl.transform.m[3][2];
            } else if (rl.type == RenderLight::Type::Sphere) {
                l.type = LUSD_LIGHT_TYPE_SPHERE;
                l.position[0] = (float)rl.transform.m[3][0];
                l.position[1] = (float)rl.transform.m[3][1];
                l.position[2] = (float)rl.transform.m[3][2];
                l.radius = rl.radius;
            } else if (rl.type == RenderLight::Type::Dome) {
                l.type = LUSD_LIGHT_TYPE_DOME;
            } else {
                l.type = LUSD_LIGHT_TYPE_OTHER;
            }
            lights.push_back(l);
        }
        g_light_cache[reinterpret_cast<uintptr_t>(sd)] = std::move(lights);
    }

    std::vector<LusdMatEntry> entries;
    entries.reserve(render_scene.materials.size());

    for (size_t i = 0; i < render_scene.materials.size(); i++) {
        const RenderMaterial& rm = render_scene.materials[i];
        LusdMatEntry e;
        std::memset(&e, 0, sizeof(e));

        /* copy name and path */
        std::snprintf(e.name,     sizeof(e.name),     "%s", rm.name.c_str());
        std::snprintf(e.abs_path, sizeof(e.abs_path), "%s", rm.abs_path.c_str());

        auto v3 = [](float* dst, const tinyusdz::value::float3& src) {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2];
        };

        if (rm.hasOpenPBR()) {
            const OpenPBRSurfaceShader& p = rm.openPBRShader.value();
            e.mat.base_weight           = p.base_weight.value;
            v3(e.mat.base_color,          p.base_color.value);
            e.mat.base_roughness        = p.base_roughness.value;
            e.mat.base_metalness        = p.base_metalness.value;
            e.mat.base_diffuse_roughness= p.base_diffuse_roughness.value;

            e.mat.specular_weight       = p.specular_weight.value;
            v3(e.mat.specular_color,      p.specular_color.value);
            e.mat.specular_roughness    = p.specular_roughness.value;
            e.mat.specular_ior          = p.specular_ior.value;
            e.mat.specular_ior_level    = p.specular_ior_level.value;
            e.mat.specular_anisotropy   = p.specular_anisotropy.value;
            e.mat.specular_rotation     = p.specular_rotation.value;

            e.mat.transmission_weight   = p.transmission_weight.value;
            v3(e.mat.transmission_color,  p.transmission_color.value);
            e.mat.transmission_depth    = p.transmission_depth.value;
            v3(e.mat.transmission_scatter, p.transmission_scatter.value);
            e.mat.transmission_scatter_anisotropy = p.transmission_scatter_anisotropy.value;
            e.mat.transmission_dispersion = p.transmission_dispersion.value;

            e.mat.subsurface_weight     = p.subsurface_weight.value;
            v3(e.mat.subsurface_color,    p.subsurface_color.value);
            e.mat.subsurface_radius     = p.subsurface_radius.value;
            v3(e.mat.subsurface_radius_scale, p.subsurface_radius_scale.value);
            e.mat.subsurface_scale      = p.subsurface_scale.value;
            e.mat.subsurface_anisotropy = p.subsurface_anisotropy.value;

            e.mat.sheen_weight          = p.sheen_weight.value;
            v3(e.mat.sheen_color,         p.sheen_color.value);
            e.mat.sheen_roughness       = p.sheen_roughness.value;

            e.mat.fuzz_weight           = p.fuzz_weight.value;
            v3(e.mat.fuzz_color,          p.fuzz_color.value);
            e.mat.fuzz_roughness        = p.fuzz_roughness.value;

            e.mat.thin_film_weight      = p.thin_film_weight.value;
            e.mat.thin_film_thickness   = p.thin_film_thickness.value;
            e.mat.thin_film_ior         = p.thin_film_ior.value;

            e.mat.coat_weight           = p.coat_weight.value;
            v3(e.mat.coat_color,          p.coat_color.value);
            e.mat.coat_roughness        = p.coat_roughness.value;
            e.mat.coat_anisotropy       = p.coat_anisotropy.value;
            e.mat.coat_rotation         = p.coat_rotation.value;
            e.mat.coat_ior              = p.coat_ior.value;
            v3(e.mat.coat_affect_color,   p.coat_affect_color.value);
            e.mat.coat_affect_roughness  = p.coat_affect_roughness.value;

            e.mat.emission_luminance    = p.emission_luminance.value;
            v3(e.mat.emission_color,      p.emission_color.value);
            e.mat.opacity               = p.opacity.value;
        } else if (rm.hasUsdPreviewSurface()) {
            const PreviewSurfaceShader& p = rm.surfaceShader.value();
            /* map UsdPreviewSurface subset to base+specular */
            e.mat.base_weight           = 1.0f;
            v3(e.mat.base_color,          p.diffuseColor.value);
            e.mat.base_metalness        = p.metallic.value;
            e.mat.specular_weight       = 1.0f;
            v3(e.mat.specular_color,      p.specularColor.value);
            e.mat.specular_roughness    = p.roughness.value;
            e.mat.specular_ior          = p.ior.value;
            e.mat.coat_weight           = p.clearcoat.value;
            e.mat.coat_roughness        = p.clearcoatRoughness.value;
            v3(e.mat.emission_color,      p.emissiveColor.value);
            float esum = p.emissiveColor.value[0] +
                         p.emissiveColor.value[1] +
                         p.emissiveColor.value[2];
            e.mat.emission_luminance    = (esum > 0.0f) ? 1.0f : 0.0f;
            e.mat.opacity               = p.opacity.value;
        } else {
            /* default OpenPBR material (grey dielectric) */
            e.mat.base_weight    = 1.0f;
            e.mat.base_color[0]  = e.mat.base_color[1] = e.mat.base_color[2] = 0.8f;
            e.mat.specular_weight = 1.0f;
            e.mat.specular_roughness = 0.3f;
            e.mat.specular_ior   = 1.5f;
            e.mat.opacity        = 1.0f;
        }
        entries.push_back(e);
    }

    g_mat_cache[reinterpret_cast<uintptr_t>(sd)] = std::move(entries);
}

/* ============================================================
 * lusdLoadStage / lusdLoadStageFromMemory / lusdCreateStage
 * ============================================================ */

LusdResult lusdLoadStage(LusdInstance instance,
                          const LusdStageLoadInfo* pLoadInfo,
                          LusdStage* pStage) {
    if (!instance || !pLoadInfo || !pStage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!pLoadInfo->pFilePath)              return LUSD_ERROR_INVALID_ARGUMENT;

    auto* sd = new LusdStage_T();
    sd->inst = instance;

    std::string warn, err;
    bool ok = tinyusdz::LoadUSDFromFile(pLoadInfo->pFilePath, &sd->stage,
                                        &warn, &err);
    if (!ok) {
        lusd_set_errorf(instance, "tinyusdz load failed: %s", err.c_str());
        delete sd;
        return LUSD_ERROR_PARSE_FAILED;
    }
    if (!warn.empty()) {
        lusd_diag(instance, LUSD_DIAGNOSTIC_SEVERITY_WARNING, warn.c_str());
    }

    *pStage = sd;
    return LUSD_SUCCESS;
}

LusdResult lusdLoadStageFromMemory(LusdInstance instance,
                                    const LusdStageLoadFromMemoryInfo* pLoadInfo,
                                    LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pLoadInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdCreateStage(LusdInstance instance,
                            const LusdStageCreateInfo* pCreateInfo,
                            LusdStage* pStage) {
    if (!instance || !pStage) return LUSD_ERROR_INVALID_ARGUMENT;
    auto* sd = new LusdStage_T();
    sd->is_write = true;
    sd->inst     = instance;
    if (pCreateInfo) {
        sd->write_up_axis  = pCreateInfo->upAxis;
        sd->write_mpu      = pCreateInfo->metersPerUnit  != 0.0 ? pCreateInfo->metersPerUnit  : 0.01;
        sd->write_start_tc = pCreateInfo->startTimeCode;
        sd->write_end_tc   = pCreateInfo->endTimeCode;
        sd->write_fps      = pCreateInfo->framesPerSecond != 0.0 ? pCreateInfo->framesPerSecond : 24.0;
    }
    *pStage = sd;
    return LUSD_SUCCESS;
}

void lusdDestroyStage(LusdInstance instance, LusdStage stage) {
    LUSD_UNUSED(instance);
    if (!stage) return;
    if (stage->is_write) {
        for (auto* p : stage->write_roots) write_prim_destroy(p);
    }
    g_mat_cache.erase(reinterpret_cast<uintptr_t>(stage));
    g_light_cache.erase(reinterpret_cast<uintptr_t>(stage));
    delete stage;
}

/* ============================================================
 * Stage root prim access
 * ============================================================ */

LusdResult lusdStageGetRootPrimCount(LusdStage stage, uint32_t* pCount) {
    if (!stage || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) {
        *pCount = static_cast<uint32_t>(stage->write_roots.size());
    } else {
        *pCount = static_cast<uint32_t>(stage->stage.root_prims().size());
    }
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetRootPrims(LusdStage stage, uint32_t count,
                                   LusdPrim* pPrims) {
    if (!stage || !pPrims) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) {
        uint32_t n = std::min(count, static_cast<uint32_t>(stage->write_roots.size()));
        for (uint32_t i = 0; i < n; i++)
            pPrims[i] = reinterpret_cast<LusdPrim>(stage->write_roots[i]);
        return LUSD_SUCCESS;
    }
    const auto& roots = stage->stage.root_prims();
    uint32_t n = std::min(count, static_cast<uint32_t>(roots.size()));
    for (uint32_t i = 0; i < n; i++) {
        pPrims[i] = stage->primHandle(&roots[i]);
    }
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetPrimAtPath(LusdStage stage, LusdPath path,
                                   LusdPrim* pPrim) {
    LUSD_UNUSED(stage); LUSD_UNUSED(path); LUSD_UNUSED(pPrim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* ============================================================
 * Traversal (depth-first)
 * ============================================================ */

static LusdTraversalAction traverseRecursive(
    LusdStage_T* sd,
    const tinyusdz::Prim& prim,
    PFN_lusdTraversalCallback pfnCallback,
    void* pUserData,
    uint32_t depth)
{
    LusdPrim primHandle = sd->primHandle(&prim);

    LusdTraversalAction action =
        pfnCallback(primHandle, LUSD_NULL_HANDLE, depth, pUserData);

    if (action == LUSD_TRAVERSAL_STOP) return LUSD_TRAVERSAL_STOP;
    if (action == LUSD_TRAVERSAL_SKIP) return LUSD_TRAVERSAL_CONTINUE;

    for (const auto& child : prim.children()) {
        LusdTraversalAction childAction =
            traverseRecursive(sd, child, pfnCallback, pUserData, depth + 1);
        if (childAction == LUSD_TRAVERSAL_STOP) return LUSD_TRAVERSAL_STOP;
    }
    return LUSD_TRAVERSAL_CONTINUE;
}

LusdResult lusdStageTraverse(LusdStage stage,
                              PFN_lusdTraversalCallback pfnCallback,
                              void* pUserData,
                              LusdTraversalFlags flags) {
    if (!stage || !pfnCallback) return LUSD_ERROR_INVALID_ARGUMENT;
    LUSD_UNUSED(flags);  /* always depth-first */

    for (const auto& root : stage->stage.root_prims()) {
        LusdTraversalAction action =
            traverseRecursive(stage, root, pfnCallback, pUserData, 0);
        if (action == LUSD_TRAVERSAL_STOP) break;
    }
    return LUSD_SUCCESS;
}

/* ============================================================
 * Stage metadata accessors
 * ============================================================ */

LusdResult lusdStageGetUpAxis(LusdStage stage, LusdUpAxis* pUpAxis) {
    if (!stage || !pUpAxis) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pUpAxis = stage->write_up_axis; return LUSD_SUCCESS; }
    tinyusdz::Axis ax = stage->stage.metas().upAxis.get_value();
    switch (ax) {
        case tinyusdz::Axis::Y: *pUpAxis = LUSD_UP_AXIS_Y; break;
        case tinyusdz::Axis::Z: *pUpAxis = LUSD_UP_AXIS_Z; break;
        case tinyusdz::Axis::X: *pUpAxis = LUSD_UP_AXIS_X; break;
        default:                *pUpAxis = LUSD_UP_AXIS_Y; break;
    }
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetUpAxis(LusdStage stage, LusdUpAxis upAxis) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { stage->write_up_axis = upAxis; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetMetersPerUnit(LusdStage stage, double* pMetersPerUnit) {
    if (!stage || !pMetersPerUnit) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pMetersPerUnit = stage->write_mpu; return LUSD_SUCCESS; }
    *pMetersPerUnit = stage->stage.metas().metersPerUnit.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetMetersPerUnit(LusdStage stage, double metersPerUnit) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { stage->write_mpu = metersPerUnit; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetStartTimeCode(LusdStage stage, double* pTimeCode) {
    if (!stage || !pTimeCode) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pTimeCode = stage->write_start_tc; return LUSD_SUCCESS; }
    *pTimeCode = stage->stage.metas().startTimeCode.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetStartTimeCode(LusdStage stage, double timeCode) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { stage->write_start_tc = timeCode; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetEndTimeCode(LusdStage stage, double* pTimeCode) {
    if (!stage || !pTimeCode) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pTimeCode = stage->write_end_tc; return LUSD_SUCCESS; }
    *pTimeCode = stage->stage.metas().endTimeCode.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetEndTimeCode(LusdStage stage, double timeCode) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { stage->write_end_tc = timeCode; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetFramesPerSecond(LusdStage stage, double* pFPS) {
    if (!stage || !pFPS) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pFPS = stage->write_fps; return LUSD_SUCCESS; }
    *pFPS = stage->stage.metas().framesPerSecond.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetFramesPerSecond(LusdStage stage, double fps) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { stage->write_fps = fps; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageAddRootPrim(LusdStage stage, LusdPrim prim) {
    if (!stage || !prim) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!stage->is_write || !is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;
    stage->write_roots.push_back(to_write_prim(prim));
    return LUSD_SUCCESS;
}

/* ============================================================
 * Prim lifecycle
 * ============================================================ */

LusdResult lusdCreatePrim(LusdInstance instance,
                           const LusdPrimCreateInfo* pCreateInfo,
                           LusdPrim* pPrim) {
    LUSD_UNUSED(instance);
    if (!pCreateInfo || !pPrim || !pCreateInfo->pName) return LUSD_ERROR_INVALID_ARGUMENT;
    auto* p = new LusdWritePrim_T();
    p->magic     = WRITE_PRIM_MAGIC;
    p->specifier = pCreateInfo->specifier;
    p->active    = true;
    p->name      = static_cast<char*>(std::malloc(std::strlen(pCreateInfo->pName) + 1));
    if (!p->name) { delete p; return LUSD_ERROR_OUT_OF_MEMORY; }
    std::strcpy(p->name, pCreateInfo->pName);
    if (pCreateInfo->pTypeName && pCreateInfo->pTypeName[0]) {
        p->type_name = static_cast<char*>(std::malloc(std::strlen(pCreateInfo->pTypeName) + 1));
        if (!p->type_name) { std::free(p->name); delete p; return LUSD_ERROR_OUT_OF_MEMORY; }
        std::strcpy(p->type_name, pCreateInfo->pTypeName);
    }
    *pPrim = reinterpret_cast<LusdPrim>(p);
    return LUSD_SUCCESS;
}

void lusdDestroyPrim(LusdInstance instance, LusdPrim prim) {
    LUSD_UNUSED(instance);
    if (!prim || !is_write_prim(prim)) return;
    write_prim_destroy(to_write_prim(prim));
}

/* ============================================================
 * Prim property accessors
 * ============================================================ */

const char* lusdPrimGetName(LusdPrim prim) {
    if (!prim) return "";
    if (is_write_prim(prim)) return to_write_prim(prim)->name;
    if (!prim->prim) return "";
    return prim->prim->element_name().c_str();
}

const char* lusdPrimGetTypeName(LusdPrim prim) {
    if (!prim) return "";
    if (is_write_prim(prim)) {
        const char* tn = to_write_prim(prim)->type_name;
        return tn ? tn : "";
    }
    if (!prim->prim) return "";
    return prim->prim->prim_type_name().c_str();
}

LusdResult lusdPrimGetPath(LusdPrim prim, LusdPath* pPath) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPath);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetSpecifier(LusdPrim prim, LusdSpecifier* pSpec) {
    if (!prim || !pSpec) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) { *pSpec = to_write_prim(prim)->specifier; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimIsActive(LusdPrim prim, bool* pActive) {
    if (!prim || !pActive) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) { *pActive = to_write_prim(prim)->active; return LUSD_SUCCESS; }
    if (pActive) *pActive = true;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimSetActive(LusdPrim prim, bool active) {
    if (!prim) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) { to_write_prim(prim)->active = active; return LUSD_SUCCESS; }
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetChildCount(LusdPrim prim, uint32_t* pCount) {
    if (!prim || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) {
        *pCount = static_cast<uint32_t>(to_write_prim(prim)->children.size());
        return LUSD_SUCCESS;
    }
    if (!prim->prim) return LUSD_ERROR_INVALID_HANDLE;
    *pCount = static_cast<uint32_t>(prim->prim->children().size());
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetChildren(LusdPrim prim, uint32_t count,
                                LusdPrim* pChildren) {
    if (!prim || !pChildren) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) {
        auto& ch = to_write_prim(prim)->children;
        uint32_t n = std::min(count, static_cast<uint32_t>(ch.size()));
        for (uint32_t i = 0; i < n; i++)
            pChildren[i] = reinterpret_cast<LusdPrim>(ch[i]);
        return LUSD_SUCCESS;
    }
    if (!prim->prim) return LUSD_ERROR_INVALID_HANDLE;
    const auto& kids = prim->prim->children();
    uint32_t n = std::min(count, static_cast<uint32_t>(kids.size()));
    for (uint32_t i = 0; i < n; i++) {
        pChildren[i] = prim->stageData->primHandle(&kids[i]);
    }
    return LUSD_SUCCESS;
}

LusdResult lusdPrimAddChild(LusdPrim parent, LusdPrim child) {
    if (!parent || !child) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(parent) || !is_write_prim(child)) return LUSD_ERROR_INVALID_HANDLE;
    to_write_prim(parent)->children.push_back(to_write_prim(child));
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyCount(LusdPrim prim, uint32_t* pCount) {
    if (!prim || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (is_write_prim(prim)) {
        *pCount = static_cast<uint32_t>(to_write_prim(prim)->attrs.size());
        return LUSD_SUCCESS;
    }
    *pCount = 0;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyNames(LusdPrim prim, uint32_t count,
                                     const char** pNames) {
    if (!prim || !pNames) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    auto& attrs = to_write_prim(prim)->attrs;
    uint32_t n = std::min(count, static_cast<uint32_t>(attrs.size()));
    for (uint32_t i = 0; i < n; i++) pNames[i] = attrs[i].name;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyKind(LusdPrim prim, const char* pName,
                                    LusdPropertyKind* pKind) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pName); LUSD_UNUSED(pKind);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetMetadata(LusdPrim prim, const char* pKey,
                                LusdValue* pValue) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(pValue);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetMetadata(LusdPrim prim, const char* pKey,
                                LusdValue value) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(value);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* ============================================================
 * Attribute operations
 * ============================================================ */

LusdResult lusdPrimCreateAttribute(LusdPrim prim,
                                    const LusdAttributeCreateInfo* pCreateInfo) {
    if (!prim || !pCreateInfo || !pCreateInfo->pName) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;
    LusdWritePrim_T* p = to_write_prim(prim);

    /* Reject duplicate attribute names */
    if (find_attr(p, pCreateInfo->pName)) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdWriteAttr_T a;
    std::memset(&a, 0, sizeof(a));
    a.name = static_cast<char*>(std::malloc(std::strlen(pCreateInfo->pName) + 1));
    if (!a.name) return LUSD_ERROR_OUT_OF_MEMORY;
    std::strcpy(a.name, pCreateInfo->pName);
    a.type        = pCreateInfo->valueType;
    a.variability = pCreateInfo->variability;
    a.custom      = pCreateInfo->custom;
    a.has_default = false;
    p->attrs.push_back(a);
    return LUSD_SUCCESS;
}

/*
 * lusdPrimGetAttributeDefault — extract the default (non-time-sampled) value
 * of a named attribute from a prim loaded via tinyusdz.
 *
 * Currently supports GeomMesh attributes:
 *   "points"            -> float3[] (LusdFloat3 array)
 *   "faceVertexCounts"  -> int[]    (int32 array)
 *   "faceVertexIndices" -> int[]    (int32 array)
 *
 * Returns LUSD_ERROR_NOT_FOUND for unknown attributes or prim types.
 */
LusdResult lusdPrimGetAttributeDefault(LusdPrim prim,
                                        const char* pAttrName,
                                        LusdValue* pValue) {
    if (!prim || !pAttrName || !pValue) return LUSD_ERROR_INVALID_ARGUMENT;

    /* Write-mode prim: return stored default */
    if (is_write_prim(prim)) {
        LusdWritePrim_T* p = to_write_prim(prim);
        LusdWriteAttr_T* a = find_attr(p, pAttrName);
        if (!a || !a->has_default) return LUSD_ERROR_NOT_FOUND;
        *pValue = reinterpret_cast<LusdValue>(reinterpret_cast<uintptr_t>(&a->default_value));
        return LUSD_SUCCESS;
    }

    if (!prim->prim)
        return LUSD_ERROR_INVALID_ARGUMENT;

    *pValue = LUSD_NULL_HANDLE;

    const tinyusdz::Prim& p   = *prim->prim;
    LusdInstance          inst = prim->stageData->inst;

    if (p.prim_type_name() == "Mesh") {
        const tinyusdz::GeomMesh* mesh =
            p.data().as<tinyusdz::GeomMesh>();
        if (!mesh) return LUSD_ERROR_INVALID_HANDLE;

        if (strcmp(pAttrName, "points") == 0) {
            auto pts = mesh->get_points();
            if (pts.empty()) return LUSD_ERROR_NOT_FOUND;
            /* point3f { float x, y, z } and LusdFloat3 { float x, y, z }
             * have the same size and layout — safe to reinterpret. */
            static_assert(sizeof(tinyusdz::value::point3f) == sizeof(LusdFloat3),
                          "point3f / LusdFloat3 layout mismatch");
            return lusdCreateValueArrayFloat3(
                inst,
                static_cast<uint64_t>(pts.size()),
                reinterpret_cast<const LusdFloat3*>(pts.data()),
                pValue);
        }
        if (strcmp(pAttrName, "faceVertexCounts") == 0) {
            auto counts = mesh->get_faceVertexCounts();
            if (counts.empty()) return LUSD_ERROR_NOT_FOUND;
            return lusdCreateValueArrayInt32(
                inst,
                static_cast<uint64_t>(counts.size()),
                counts.data(),
                pValue);
        }
        if (strcmp(pAttrName, "faceVertexIndices") == 0) {
            auto indices = mesh->get_faceVertexIndices();
            if (indices.empty()) return LUSD_ERROR_NOT_FOUND;
            return lusdCreateValueArrayInt32(
                inst,
                static_cast<uint64_t>(indices.size()),
                indices.data(),
                pValue);
        }
    }

    /* Other prim types / attribute names not yet supported */
    return LUSD_ERROR_NOT_FOUND;
}

LusdResult lusdPrimSetAttributeDefault(LusdPrim prim,
                                        const char* pAttrName,
                                        LusdValue value) {
    if (!prim || !pAttrName || !value) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;
    LusdWritePrim_T* p = to_write_prim(prim);
    LusdWriteAttr_T* a = find_attr(p, pAttrName);
    if (!a) return LUSD_ERROR_INVALID_ARGUMENT;

    /* LusdValue handle is actually a LusdValueData* (see lusd_value.c) */
    const LusdValueData* src =
        reinterpret_cast<const LusdValueData*>(reinterpret_cast<uintptr_t>(value));

    /* Free old default if any */
    if (a->has_default) write_value_data_free(a->default_value);

    if (!value_data_deep_copy(a->default_value, *src))
        return LUSD_ERROR_OUT_OF_MEMORY;
    a->has_default = true;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetAttributeTimeSamples(LusdPrim prim,
                                            const char* pAttrName,
                                            LusdTimeSamples* pTimeSamples) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pTimeSamples);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetAttributeTimeSamples(LusdPrim prim,
                                            const char* pAttrName,
                                            LusdTimeSamples timeSamples) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(timeSamples);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetAttributeType(LusdPrim prim, const char* pAttrName,
                                     LusdValueType* pType) {
    if (!prim || !pAttrName || !pType) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    LusdWriteAttr_T* a = find_attr(to_write_prim(prim), pAttrName);
    if (!a) return LUSD_ERROR_NOT_FOUND;
    *pType = a->type;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetAttributeVariability(LusdPrim prim,
                                            const char* pAttrName,
                                            LusdVariability* pVar) {
    if (!prim || !pAttrName || !pVar) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    LusdWriteAttr_T* a = find_attr(to_write_prim(prim), pAttrName);
    if (!a) return LUSD_ERROR_NOT_FOUND;
    *pVar = a->variability;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimIsAttributeBlocked(LusdPrim prim, const char* pAttrName,
                                       bool* pBlocked) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    if (pBlocked) *pBlocked = false;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimBlockAttribute(LusdPrim prim, const char* pAttrName) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetAttributeConnectionCount(LusdPrim prim,
                                                const char* pAttrName,
                                                uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    if (pCount) *pCount = 0;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetAttributeConnections(LusdPrim prim,
                                            const char* pAttrName,
                                            uint32_t count,
                                            LusdPath* pPaths) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    LUSD_UNUSED(count); LUSD_UNUSED(pPaths);
    return LUSD_SUCCESS;
}

LusdResult lusdPrimAddAttributeConnection(LusdPrim prim,
                                           const char* pAttrName,
                                           LusdPath targetPath,
                                           LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    LUSD_UNUSED(targetPath); LUSD_UNUSED(op);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* ============================================================
 * USDA serializer — write-mode stage export
 * ============================================================ */

/* ----------------------------------------------------------
 * Growing string buffer
 * ---------------------------------------------------------- */
struct StrBuf {
    char*  data;
    size_t used;
    size_t cap;
};

static bool sb_reserve(StrBuf* b, size_t extra) {
    size_t need = b->used + extra + 1;
    if (need <= b->cap) return true;
    size_t new_cap = b->cap ? b->cap * 2 : 4096;
    while (new_cap < need) new_cap *= 2;
    char* np = static_cast<char*>(std::realloc(b->data, new_cap));
    if (!np) return false;
    b->data = np;
    b->cap  = new_cap;
    return true;
}

static bool sb_append(StrBuf* b, const char* s) {
    size_t len = std::strlen(s);
    if (!sb_reserve(b, len)) return false;
    std::memcpy(b->data + b->used, s, len);
    b->used += len;
    b->data[b->used] = '\0';
    return true;
}

static bool sb_appendf(StrBuf* b, const char* fmt, ...) {
    char tmp[256];
    va_list ap;
    va_start(ap, fmt);
    int n = std::vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n < 0) return false;
    if (static_cast<size_t>(n) < sizeof(tmp)) return sb_append(b, tmp);
    char* big = static_cast<char*>(std::malloc(static_cast<size_t>(n) + 1));
    if (!big) return false;
    va_start(ap, fmt);
    std::vsnprintf(big, static_cast<size_t>(n) + 1, fmt, ap);
    va_end(ap);
    bool ok = sb_append(b, big);
    std::free(big);
    return ok;
}

static void sb_free(StrBuf* b) {
    std::free(b->data);
    b->data = nullptr;
    b->used = b->cap = 0;
}

/* ----------------------------------------------------------
 * Type keyword table
 * ---------------------------------------------------------- */
static const char* usda_type_keyword(uint32_t base_type) {
    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL:        return "bool";
    case LUSD_VALUE_TYPE_INT32:       return "int";
    case LUSD_VALUE_TYPE_UINT32:      return "uint";
    case LUSD_VALUE_TYPE_INT64:       return "int64";
    case LUSD_VALUE_TYPE_UINT64:      return "uint64";
    case LUSD_VALUE_TYPE_HALF:        return "half";
    case LUSD_VALUE_TYPE_FLOAT:       return "float";
    case LUSD_VALUE_TYPE_DOUBLE:      return "double";
    case LUSD_VALUE_TYPE_STRING:      return "string";
    case LUSD_VALUE_TYPE_TOKEN:       return "token";
    case LUSD_VALUE_TYPE_ASSET_PATH:  return "asset";
    case LUSD_VALUE_TYPE_TIMECODE:    return "timecode";
    case LUSD_VALUE_TYPE_INT2:        return "int2";
    case LUSD_VALUE_TYPE_INT3:        return "int3";
    case LUSD_VALUE_TYPE_INT4:        return "int4";
    case LUSD_VALUE_TYPE_FLOAT2:      return "float2";
    case LUSD_VALUE_TYPE_FLOAT3:      return "float3";
    case LUSD_VALUE_TYPE_FLOAT4:      return "float4";
    case LUSD_VALUE_TYPE_DOUBLE2:     return "double2";
    case LUSD_VALUE_TYPE_DOUBLE3:     return "double3";
    case LUSD_VALUE_TYPE_DOUBLE4:     return "double4";
    case LUSD_VALUE_TYPE_QUATF:       return "quatf";
    case LUSD_VALUE_TYPE_QUATD:       return "quatd";
    case LUSD_VALUE_TYPE_MATRIX4F:    return "matrix4f";
    case LUSD_VALUE_TYPE_MATRIX4D:    return "matrix4d";
    case LUSD_VALUE_TYPE_COLOR3F:     return "color3f";
    case LUSD_VALUE_TYPE_COLOR4F:     return "color4f";
    case LUSD_VALUE_TYPE_POINT3F:     return "point3f";
    case LUSD_VALUE_TYPE_POINT3D:     return "point3d";
    case LUSD_VALUE_TYPE_VECTOR3F:    return "vector3f";
    case LUSD_VALUE_TYPE_NORMAL3F:    return "normal3f";
    case LUSD_VALUE_TYPE_TEXCOORD2F:  return "texCoord2f";
    case LUSD_VALUE_TYPE_TEXCOORD2D:  return "texCoord2d";
    default: return "unknown";
    }
}

static size_t elem_byte_size(uint32_t base_type) {
    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL:   return sizeof(bool);
    case LUSD_VALUE_TYPE_INT32:
    case LUSD_VALUE_TYPE_UINT32:
    case LUSD_VALUE_TYPE_FLOAT:  return 4;
    case LUSD_VALUE_TYPE_INT64:
    case LUSD_VALUE_TYPE_UINT64:
    case LUSD_VALUE_TYPE_DOUBLE: return 8;
    case LUSD_VALUE_TYPE_HALF:   return 2;
    case LUSD_VALUE_TYPE_INT2:   return 2*4;
    case LUSD_VALUE_TYPE_INT3:   return 3*4;
    case LUSD_VALUE_TYPE_INT4:   return 4*4;
    case LUSD_VALUE_TYPE_FLOAT2:
    case LUSD_VALUE_TYPE_TEXCOORD2F: return 2*4;
    case LUSD_VALUE_TYPE_FLOAT3:
    case LUSD_VALUE_TYPE_POINT3F:
    case LUSD_VALUE_TYPE_NORMAL3F:
    case LUSD_VALUE_TYPE_VECTOR3F:
    case LUSD_VALUE_TYPE_COLOR3F: return 3*4;
    case LUSD_VALUE_TYPE_FLOAT4:
    case LUSD_VALUE_TYPE_QUATF:
    case LUSD_VALUE_TYPE_COLOR4F: return 4*4;
    case LUSD_VALUE_TYPE_DOUBLE2:
    case LUSD_VALUE_TYPE_TEXCOORD2D: return 2*8;
    case LUSD_VALUE_TYPE_DOUBLE3:
    case LUSD_VALUE_TYPE_POINT3D:
    case LUSD_VALUE_TYPE_NORMAL3F + 0x1000: /* avoid dup */ return 3*8;
    case LUSD_VALUE_TYPE_DOUBLE4:
    case LUSD_VALUE_TYPE_QUATD:  return 4*8;
    case LUSD_VALUE_TYPE_MATRIX4F: return 16*4;
    case LUSD_VALUE_TYPE_MATRIX4D: return 16*8;
    default: return 0;
    }
}

/* Format a float compactly: e.g. "1" → "1.0", "1.5" → "1.5" */
static bool append_float(StrBuf* b, float v) {
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%g", static_cast<double>(v));
    bool has_dot = (std::strchr(tmp, '.') != nullptr) || (std::strchr(tmp, 'e') != nullptr);
    if (!has_dot) {
        size_t len = std::strlen(tmp);
        if (len + 2 < sizeof(tmp)) { tmp[len] = '.'; tmp[len+1] = '0'; tmp[len+2] = '\0'; }
    }
    return sb_append(b, tmp);
}

static bool append_double(StrBuf* b, double v) {
    char tmp[64];
    std::snprintf(tmp, sizeof(tmp), "%g", v);
    bool has_dot = (std::strchr(tmp, '.') != nullptr) || (std::strchr(tmp, 'e') != nullptr);
    if (!has_dot) {
        size_t len = std::strlen(tmp);
        if (len + 2 < sizeof(tmp)) { tmp[len] = '.'; tmp[len+1] = '0'; tmp[len+2] = '\0'; }
    }
    return sb_append(b, tmp);
}

/* Write one element of base_type starting at ptr */
static bool write_element(StrBuf* b, uint32_t base_type, const void* ptr) {
    const float*   fp  = static_cast<const float*>(ptr);
    const double*  dp  = static_cast<const double*>(ptr);
    const int32_t* ip  = static_cast<const int32_t*>(ptr);
    const int64_t* i64 = static_cast<const int64_t*>(ptr);

    switch (base_type) {
    case LUSD_VALUE_TYPE_BOOL: {
        bool v; std::memcpy(&v, ptr, sizeof(bool));
        return sb_appendf(b, "%d", static_cast<int>(v));
    }
    case LUSD_VALUE_TYPE_INT32:
        return sb_appendf(b, "%d", ip[0]);
    case LUSD_VALUE_TYPE_UINT32: {
        uint32_t v; std::memcpy(&v, ptr, 4);
        return sb_appendf(b, "%u", static_cast<unsigned>(v));
    }
    case LUSD_VALUE_TYPE_INT64:
        return sb_appendf(b, "%lld", static_cast<long long>(i64[0]));
    case LUSD_VALUE_TYPE_UINT64: {
        uint64_t v; std::memcpy(&v, ptr, 8);
        return sb_appendf(b, "%llu", static_cast<unsigned long long>(v));
    }
    case LUSD_VALUE_TYPE_FLOAT:
        return append_float(b, fp[0]);
    case LUSD_VALUE_TYPE_DOUBLE:
        return append_double(b, dp[0]);
    case LUSD_VALUE_TYPE_FLOAT2:
    case LUSD_VALUE_TYPE_TEXCOORD2F:
        return sb_append(b,"(") && append_float(b,fp[0]) &&
               sb_append(b,", ") && append_float(b,fp[1]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_FLOAT3:
    case LUSD_VALUE_TYPE_POINT3F:
    case LUSD_VALUE_TYPE_NORMAL3F:
    case LUSD_VALUE_TYPE_VECTOR3F:
    case LUSD_VALUE_TYPE_COLOR3F:
        return sb_append(b,"(") && append_float(b,fp[0]) &&
               sb_append(b,", ") && append_float(b,fp[1]) &&
               sb_append(b,", ") && append_float(b,fp[2]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_FLOAT4:
    case LUSD_VALUE_TYPE_QUATF:
    case LUSD_VALUE_TYPE_COLOR4F:
        return sb_append(b,"(") && append_float(b,fp[0]) &&
               sb_append(b,", ") && append_float(b,fp[1]) &&
               sb_append(b,", ") && append_float(b,fp[2]) &&
               sb_append(b,", ") && append_float(b,fp[3]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_DOUBLE2:
    case LUSD_VALUE_TYPE_TEXCOORD2D:
        return sb_append(b,"(") && append_double(b,dp[0]) &&
               sb_append(b,", ") && append_double(b,dp[1]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_DOUBLE3:
    case LUSD_VALUE_TYPE_POINT3D:
        return sb_append(b,"(") && append_double(b,dp[0]) &&
               sb_append(b,", ") && append_double(b,dp[1]) &&
               sb_append(b,", ") && append_double(b,dp[2]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_DOUBLE4:
    case LUSD_VALUE_TYPE_QUATD:
        return sb_append(b,"(") && append_double(b,dp[0]) &&
               sb_append(b,", ") && append_double(b,dp[1]) &&
               sb_append(b,", ") && append_double(b,dp[2]) &&
               sb_append(b,", ") && append_double(b,dp[3]) && sb_append(b,")");
    case LUSD_VALUE_TYPE_INT2:
        return sb_appendf(b,"(%d, %d)", ip[0], ip[1]);
    case LUSD_VALUE_TYPE_INT3:
        return sb_appendf(b,"(%d, %d, %d)", ip[0], ip[1], ip[2]);
    case LUSD_VALUE_TYPE_INT4:
        return sb_appendf(b,"(%d, %d, %d, %d)", ip[0], ip[1], ip[2], ip[3]);
    case LUSD_VALUE_TYPE_MATRIX4D: {
        if (!sb_append(b,"( ")) return false;
        for (int r = 0; r < 4; r++) {
            if (!sb_append(b,"(")) return false;
            for (int c = 0; c < 4; c++) {
                if (!append_double(b, dp[r*4+c])) return false;
                if (c < 3 && !sb_append(b,", ")) return false;
            }
            if (!sb_append(b, r < 3 ? "), " : ")")) return false;
        }
        return sb_append(b," )");
    }
    case LUSD_VALUE_TYPE_STRING:
    case LUSD_VALUE_TYPE_TOKEN: {
        char* s;
        std::memcpy(&s, ptr, sizeof(char*));
        return sb_append(b,"\"") && (s ? sb_append(b,s) : true) && sb_append(b,"\"");
    }
    default:
        return sb_appendf(b,"/* unsupported type %u */", base_type);
    }
}

/* Write full value (scalar or array) */
static bool write_value(StrBuf* b, const LusdValueData* vd) {
    uint32_t base = static_cast<uint32_t>(vd->type) & ~LUSD_VALUE_TYPE_ARRAY_BIT;
    bool is_array  = (static_cast<uint32_t>(vd->type) & LUSD_VALUE_TYPE_ARRAY_BIT) != 0;
    size_t esz = elem_byte_size(base);

    if (!is_array) {
        const void* ptr = vd->useHeap ? vd->storage.heap.ptr
                                       : static_cast<const void*>(vd->storage.inlineData);
        return write_element(b, base, ptr);
    }

    if (!sb_append(b,"[")) return false;
    if (esz > 0 && vd->arrayCount > 0 && vd->storage.heap.ptr) {
        const uint8_t* data = static_cast<const uint8_t*>(vd->storage.heap.ptr);
        for (uint64_t i = 0; i < vd->arrayCount; i++) {
            if (i > 0 && !sb_append(b,", ")) return false;
            if (!write_element(b, base, data + i * esz)) return false;
        }
    }
    return sb_append(b,"]");
}

static bool write_indent(StrBuf* b, int depth) {
    for (int i = 0; i < depth; i++)
        if (!sb_append(b,"    ")) return false;
    return true;
}

/* Serialize one attribute */
static bool write_attr(StrBuf* b, const LusdWriteAttr_T& a, int depth) {
    if (!write_indent(b, depth)) return false;
    if (a.variability == LUSD_VARIABILITY_UNIFORM)
        if (!sb_append(b,"uniform ")) return false;

    uint32_t base     = static_cast<uint32_t>(a.type) & ~LUSD_VALUE_TYPE_ARRAY_BIT;
    bool     is_array = (static_cast<uint32_t>(a.type) & LUSD_VALUE_TYPE_ARRAY_BIT) != 0;

    if (!sb_append(b, usda_type_keyword(base))) return false;
    if (is_array && !sb_append(b,"[]")) return false;
    if (!sb_append(b," ")) return false;
    if (!sb_append(b, a.name)) return false;
    if (a.has_default) {
        if (!sb_append(b," = ")) return false;
        if (!write_value(b, &a.default_value)) return false;
    }
    return sb_append(b,"\n");
}

/* Serialize one prim (recursive) */
static bool write_prim_usda(StrBuf* b, const LusdWritePrim_T* p, int depth) {
    if (!write_indent(b, depth)) return false;
    const char* spec_kw =
        p->specifier == LUSD_SPECIFIER_OVER  ? "over"  :
        p->specifier == LUSD_SPECIFIER_CLASS  ? "class" : "def";
    if (!sb_append(b, spec_kw)) return false;
    if (!sb_append(b," ")) return false;
    if (p->type_name && p->type_name[0]) {
        if (!sb_append(b, p->type_name)) return false;
        if (!sb_append(b," ")) return false;
    }
    if (!sb_appendf(b,"\"%s\"", p->name)) return false;
    if (!sb_append(b," {\n")) return false;

    for (const auto& a : p->attrs)
        if (!write_attr(b, a, depth + 1)) return false;

    bool first_child = true;
    for (const auto* c : p->children) {
        if (first_child && !p->attrs.empty())
            if (!sb_append(b,"\n")) return false;
        first_child = false;
        if (!write_prim_usda(b, c, depth + 1)) return false;
    }

    if (!write_indent(b, depth)) return false;
    return sb_append(b,"}\n");
}

/* Layer metadata block */
static bool write_layer_metas(StrBuf* b, const LusdStage_T* S) {
    /* Always emit a metadata block so upAxis/metersPerUnit are present */
    if (!sb_append(b,"(\n")) return false;

    const char* axis_str =
        S->write_up_axis == LUSD_UP_AXIS_Z ? "Z" :
        S->write_up_axis == LUSD_UP_AXIS_X ? "X" : "Y";
    if (!sb_appendf(b,"    upAxis = \"%s\"\n", axis_str)) return false;

    if (S->write_mpu != 0.0)
        if (!sb_appendf(b,"    metersPerUnit = %g\n", S->write_mpu)) return false;

    if (S->write_start_tc != 0.0 || S->write_end_tc != 0.0) {
        if (!sb_appendf(b,"    startTimeCode = %g\n", S->write_start_tc)) return false;
        if (!sb_appendf(b,"    endTimeCode = %g\n",   S->write_end_tc))   return false;
    }

    if (S->write_fps != 0.0)
        if (!sb_appendf(b,"    framesPerSecond = %g\n", S->write_fps)) return false;

    return sb_append(b,")\n");
}

/* ============================================================
 * Public writer API
 * ============================================================ */

LusdResult lusdCreateWriter(LusdInstance inst,
                             const LusdWriterCreateInfo* pCI,
                             LusdWriter* pWriter) {
    LUSD_UNUSED(inst);
    if (!pCI || !pWriter) return LUSD_ERROR_INVALID_ARGUMENT;
    LusdWriter_T* w = static_cast<LusdWriter_T*>(std::calloc(1, sizeof(LusdWriter_T)));
    if (!w) return LUSD_ERROR_OUT_OF_MEMORY;
    w->format = pCI->format;
    if (pCI->pFilePath && pCI->pFilePath[0]) {
        w->file_path = static_cast<char*>(std::malloc(std::strlen(pCI->pFilePath) + 1));
        if (!w->file_path) { std::free(w); return LUSD_ERROR_OUT_OF_MEMORY; }
        std::strcpy(w->file_path, pCI->pFilePath);
    }
    *pWriter = w;
    return LUSD_SUCCESS;
}

void lusdDestroyWriter(LusdInstance inst, LusdWriter writer) {
    LUSD_UNUSED(inst);
    if (!writer) return;
    std::free(writer->file_path);
    std::free(writer);  /* allocated with calloc */
}

LusdResult lusdStageExportToString(LusdInstance inst, LusdStage stage,
                                    char** ppOutput, uint64_t* pLength) {
    LUSD_UNUSED(inst);
    if (!stage || !ppOutput || !pLength) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!stage->is_write) return LUSD_ERROR_FEATURE_NOT_PRESENT;

    StrBuf buf{nullptr, 0, 0};

    if (!sb_append(&buf,"#usda 1.0\n")) goto oom;
    if (!write_layer_metas(&buf, stage)) goto oom;
    if (!sb_append(&buf,"\n")) goto oom;

    for (size_t i = 0; i < stage->write_roots.size(); i++) {
        if (!write_prim_usda(&buf, stage->write_roots[i], 0)) goto oom;
        if (i + 1 < stage->write_roots.size())
            if (!sb_append(&buf,"\n")) goto oom;
    }

    *ppOutput = buf.data;
    *pLength  = static_cast<uint64_t>(buf.used);
    return LUSD_SUCCESS;

oom:
    sb_free(&buf);
    return LUSD_ERROR_OUT_OF_MEMORY;
}

LusdResult lusdWriterWriteStage(LusdWriter writer, LusdStage stage) {
    if (!writer || !stage) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!writer->file_path) return LUSD_ERROR_INVALID_ARGUMENT;

    char* text = nullptr;
    uint64_t length = 0;
    LusdResult r = lusdStageExportToString(nullptr, stage, &text, &length);
    if (r != LUSD_SUCCESS) return r;

    std::FILE* f = std::fopen(writer->file_path, "wb");
    if (!f) { std::free(text); return LUSD_ERROR_FILE_NOT_FOUND; }
    std::fwrite(text, 1, static_cast<size_t>(length), f);
    std::fclose(f);
    std::free(text);
    return LUSD_SUCCESS;
}

/* ============================================================
 * Material query API — lusd_material.h
 * ============================================================ */

static const std::vector<LusdMatEntry>* lusd__get_mat_entries(LusdStage stage) {
    auto key = reinterpret_cast<uintptr_t>(stage);
    auto it = g_mat_cache.find(key);
    if (it != g_mat_cache.end()) return &it->second;
    /* lazy build */
    lusd__stage_build_material_cache(stage);
    it = g_mat_cache.find(key);
    if (it != g_mat_cache.end()) return &it->second;
    return nullptr;
}

LusdResult lusdStageGetMaterialCount(LusdStage stage, uint32_t* pCount) {
    if (!stage || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pCount = 0; return LUSD_SUCCESS; }
    const auto* entries = lusd__get_mat_entries(stage);
    *pCount = entries ? static_cast<uint32_t>(entries->size()) : 0u;
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetMaterials(LusdStage stage, uint32_t count,
                                  LusdOpenPBRMaterial* pMaterials) {
    if (!stage || !pMaterials) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) return LUSD_SUCCESS;
    const auto* entries = lusd__get_mat_entries(stage);
    if (!entries) return LUSD_SUCCESS;
    uint32_t n = std::min(count, static_cast<uint32_t>(entries->size()));
    for (uint32_t i = 0; i < n; i++)
        pMaterials[i] = (*entries)[i].mat;
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetMaterialName(LusdStage stage, uint32_t index,
                                     char* buf, uint32_t bufLen) {
    if (!stage || !buf || bufLen == 0) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) return LUSD_ERROR_INVALID_ARGUMENT;
    const auto* entries = lusd__get_mat_entries(stage);
    if (!entries || index >= static_cast<uint32_t>(entries->size()))
        return LUSD_ERROR_INVALID_ARGUMENT;
    const char* src = (*entries)[index].name;
    size_t src_len = std::strlen(src);
    if (src_len + 1 <= bufLen) {
        std::memcpy(buf, src, src_len + 1);
        return LUSD_SUCCESS;
    }
    std::memcpy(buf, src, bufLen - 1);
    buf[bufLen - 1] = '\0';
    return LUSD_INCOMPLETE;
}

LusdResult lusdStageGetMaterialPath(LusdStage stage, uint32_t index,
                                     char* buf, uint32_t bufLen) {
    if (!stage || !buf || bufLen == 0) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) return LUSD_ERROR_INVALID_ARGUMENT;
    const auto* entries = lusd__get_mat_entries(stage);
    if (!entries || index >= static_cast<uint32_t>(entries->size()))
        return LUSD_ERROR_INVALID_ARGUMENT;
    const char* src = (*entries)[index].abs_path;
    size_t src_len = std::strlen(src);
    if (src_len + 1 <= bufLen) {
        std::memcpy(buf, src, src_len + 1);
        return LUSD_SUCCESS;
    }
    std::memcpy(buf, src, bufLen - 1);
    buf[bufLen - 1] = '\0';
    return LUSD_INCOMPLETE;
}

/* ============================================================
 * Light query API
 * ============================================================ */

static const std::vector<LusdLight>* lusd__get_light_entries(LusdStage stage) {
    auto key = reinterpret_cast<uintptr_t>(stage);
    auto it = g_light_cache.find(key);
    if (it != g_light_cache.end()) return &it->second;
    /* lazy build (shares the ConvertToRenderScene call with materials) */
    lusd__stage_build_material_cache(stage);
    it = g_light_cache.find(key);
    if (it != g_light_cache.end()) return &it->second;
    return nullptr;
}

LusdResult lusdStageGetLightCount(LusdStage stage, uint32_t* pCount) {
    if (!stage || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) { *pCount = 0; return LUSD_SUCCESS; }
    const auto* lights = lusd__get_light_entries(stage);
    *pCount = lights ? static_cast<uint32_t>(lights->size()) : 0u;
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetLights(LusdStage stage, uint32_t count, LusdLight* pLights) {
    if (!stage || !pLights) return LUSD_ERROR_INVALID_ARGUMENT;
    if (stage->is_write) return LUSD_SUCCESS;
    const auto* lights = lusd__get_light_entries(stage);
    if (!lights) return LUSD_SUCCESS;
    uint32_t n = std::min(count, static_cast<uint32_t>(lights->size()));
    for (uint32_t i = 0; i < n; i++) pLights[i] = (*lights)[i];
    return LUSD_SUCCESS;
}
