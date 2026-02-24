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

/* Internal C header — wrap in extern "C" so all declared helper functions
 * (lusd_alloc, lusd_set_errorf, lusd_diag, …) get C linkage in C++ builds.
 * Nested LUSD_EXTERN_C_BEGIN/END from included headers are valid in C++. */
extern "C" {
#include "internal/lusd_internal.h"
}

/* C++ standard library */
#include <algorithm>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

/* tinyusdz C++ API — single include pulls in Stage, Prim, GeomMesh, etc. */
#include "tinyusdz.hh"

/* ============================================================
 * Internal struct definitions
 * LusdStage_T and LusdPrim_T must be defined before use because
 * lusd_handles.h declares them as opaque pointer types (struct name_T*).
 * ============================================================ */

struct LusdPrim_T;  /* forward */

struct LusdStage_T {
    tinyusdz::Stage stage;
    LusdInstance    inst;  /* non-owning back-reference to parent instance */

    /* Arena of prim handle objects — valid for the lifetime of this stage */
    std::vector<std::unique_ptr<LusdPrim_T>> primArena;

    /* Allocate a prim handle in the arena and return it */
    LusdPrim primHandle(const tinyusdz::Prim* p);
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
    LUSD_UNUSED(pCreateInfo);
    auto* sd = new LusdStage_T();
    sd->inst = instance;
    *pStage = sd;
    return LUSD_SUCCESS;
}

void lusdDestroyStage(LusdInstance instance, LusdStage stage) {
    LUSD_UNUSED(instance);
    if (!stage) return;
    delete stage;
}

/* ============================================================
 * Stage root prim access
 * ============================================================ */

LusdResult lusdStageGetRootPrimCount(LusdStage stage, uint32_t* pCount) {
    if (!stage || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    *pCount = static_cast<uint32_t>(stage->stage.root_prims().size());
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetRootPrims(LusdStage stage, uint32_t count,
                                   LusdPrim* pPrims) {
    if (!stage || !pPrims) return LUSD_ERROR_INVALID_ARGUMENT;
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
    LUSD_UNUSED(stage); LUSD_UNUSED(upAxis);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetMetersPerUnit(LusdStage stage, double* pMetersPerUnit) {
    if (!stage || !pMetersPerUnit) return LUSD_ERROR_INVALID_ARGUMENT;
    *pMetersPerUnit = stage->stage.metas().metersPerUnit.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetMetersPerUnit(LusdStage stage, double metersPerUnit) {
    LUSD_UNUSED(stage); LUSD_UNUSED(metersPerUnit);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetStartTimeCode(LusdStage stage, double* pTimeCode) {
    if (!stage || !pTimeCode) return LUSD_ERROR_INVALID_ARGUMENT;
    *pTimeCode = stage->stage.metas().startTimeCode.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetStartTimeCode(LusdStage stage, double timeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(timeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetEndTimeCode(LusdStage stage, double* pTimeCode) {
    if (!stage || !pTimeCode) return LUSD_ERROR_INVALID_ARGUMENT;
    *pTimeCode = stage->stage.metas().endTimeCode.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetEndTimeCode(LusdStage stage, double timeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(timeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetFramesPerSecond(LusdStage stage, double* pFPS) {
    if (!stage || !pFPS) return LUSD_ERROR_INVALID_ARGUMENT;
    *pFPS = stage->stage.metas().framesPerSecond.get_value();
    return LUSD_SUCCESS;
}

LusdResult lusdStageSetFramesPerSecond(LusdStage stage, double fps) {
    LUSD_UNUSED(stage); LUSD_UNUSED(fps);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageAddRootPrim(LusdStage stage, LusdPrim prim) {
    LUSD_UNUSED(stage); LUSD_UNUSED(prim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* ============================================================
 * Prim lifecycle
 * ============================================================ */

LusdResult lusdCreatePrim(LusdInstance instance,
                           const LusdPrimCreateInfo* pCreateInfo,
                           LusdPrim* pPrim) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pCreateInfo); LUSD_UNUSED(pPrim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

void lusdDestroyPrim(LusdInstance instance, LusdPrim prim) {
    /* Prims returned by stage traversal/query are owned by the stage arena;
     * standalone destroy is a no-op. */
    LUSD_UNUSED(instance); LUSD_UNUSED(prim);
}

/* ============================================================
 * Prim property accessors
 * ============================================================ */

const char* lusdPrimGetName(LusdPrim prim) {
    if (!prim || !prim->prim) return "";
    return prim->prim->element_name().c_str();
}

const char* lusdPrimGetTypeName(LusdPrim prim) {
    if (!prim || !prim->prim) return "";
    return prim->prim->prim_type_name().c_str();
}

LusdResult lusdPrimGetPath(LusdPrim prim, LusdPath* pPath) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPath);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetSpecifier(LusdPrim prim, LusdSpecifier* pSpec) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pSpec);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimIsActive(LusdPrim prim, bool* pActive) {
    LUSD_UNUSED(prim);
    if (pActive) *pActive = true;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimSetActive(LusdPrim prim, bool active) {
    LUSD_UNUSED(prim); LUSD_UNUSED(active);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetChildCount(LusdPrim prim, uint32_t* pCount) {
    if (!prim || !prim->prim || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    *pCount = static_cast<uint32_t>(prim->prim->children().size());
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetChildren(LusdPrim prim, uint32_t count,
                                LusdPrim* pChildren) {
    if (!prim || !prim->prim || !pChildren) return LUSD_ERROR_INVALID_ARGUMENT;
    const auto& kids = prim->prim->children();
    uint32_t n = std::min(count, static_cast<uint32_t>(kids.size()));
    for (uint32_t i = 0; i < n; i++) {
        pChildren[i] = prim->stageData->primHandle(&kids[i]);
    }
    return LUSD_SUCCESS;
}

LusdResult lusdPrimAddChild(LusdPrim parent, LusdPrim child) {
    LUSD_UNUSED(parent); LUSD_UNUSED(child);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetPropertyCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim);
    if (pCount) *pCount = 0;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyNames(LusdPrim prim, uint32_t count,
                                     const char** pNames) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pNames);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
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
    LUSD_UNUSED(prim); LUSD_UNUSED(pCreateInfo);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
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
    if (!prim || !prim->prim || !pAttrName || !pValue)
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
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(value);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
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
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pType);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetAttributeVariability(LusdPrim prim,
                                            const char* pAttrName,
                                            LusdVariability* pVar) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pVar);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
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
