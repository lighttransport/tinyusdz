/*
 * lusd_stage.c - Stage operations (write-mode scene graph)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_stage.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_write_internal.h"
#include <stdlib.h>
#include <string.h>

/* Read-path stubs (not yet implemented) */
LusdResult lusdLoadStage(LusdInstance instance, const LusdStageLoadInfo* pLoadInfo, LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pLoadInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdLoadStageFromMemory(LusdInstance instance, const LusdStageLoadFromMemoryInfo* pLoadInfo, LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pLoadInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* -------------------------------------------------------------------
 * Stage create / destroy
 * ------------------------------------------------------------------- */

LusdResult lusdCreateStage(LusdInstance instance, const LusdStageCreateInfo* pCI, LusdStage* pStage) {
    LUSD_UNUSED(instance);
    if (!pCI || !pStage) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdStage_T* S = (LusdStage_T*)calloc(1, sizeof(LusdStage_T));
    if (!S) return LUSD_ERROR_OUT_OF_MEMORY;

    S->up_axis           = pCI->upAxis;
    S->meters_per_unit   = pCI->metersPerUnit != 0.0 ? pCI->metersPerUnit : 0.01;
    S->start_time_code   = pCI->startTimeCode;
    S->end_time_code     = pCI->endTimeCode;
    S->frames_per_second = pCI->framesPerSecond != 0.0 ? pCI->framesPerSecond : 24.0;

    *pStage = (LusdStage)S;
    return LUSD_SUCCESS;
}

void lusdDestroyStage(LusdInstance instance, LusdStage stage) {
    LUSD_UNUSED(instance);
    if (!stage) return;
    LusdStage_T* S = (LusdStage_T*)stage;
    /* Recursively destroy all root prims */
    for (uint32_t i = 0; i < S->root_prim_count; i++)
        lusd_write_prim_destroy(S->root_prims[i]);
    free(S->root_prims);
    free(S);
}

/* -------------------------------------------------------------------
 * Root prim management
 * ------------------------------------------------------------------- */

LusdResult lusdStageAddRootPrim(LusdStage stage, LusdPrim prim) {
    if (!stage || !prim) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;

    LusdStage_T* S = (LusdStage_T*)stage;
    if (S->root_prim_count >= S->root_prim_cap) {
        uint32_t new_cap = S->root_prim_cap ? S->root_prim_cap * 2 : 8;
        LusdWritePrim_T** np = (LusdWritePrim_T**)realloc(
            S->root_prims, new_cap * sizeof(LusdWritePrim_T*));
        if (!np) return LUSD_ERROR_OUT_OF_MEMORY;
        S->root_prims = np;
        S->root_prim_cap = new_cap;
    }
    S->root_prims[S->root_prim_count++] = lusd_to_write_prim(prim);
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetRootPrimCount(LusdStage stage, uint32_t* pCount) {
    if (!stage || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    *pCount = ((LusdStage_T*)stage)->root_prim_count;
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetRootPrims(LusdStage stage, uint32_t count, LusdPrim* pPrims) {
    if (!stage || !pPrims) return LUSD_ERROR_INVALID_ARGUMENT;
    LusdStage_T* S = (LusdStage_T*)stage;
    uint32_t n = count < S->root_prim_count ? count : S->root_prim_count;
    for (uint32_t i = 0; i < n; i++)
        pPrims[i] = (LusdPrim)S->root_prims[i];
    return LUSD_SUCCESS;
}

LusdResult lusdStageGetPrimAtPath(LusdStage stage, LusdPath path, LusdPrim* pPrim) {
    LUSD_UNUSED(stage); LUSD_UNUSED(path); LUSD_UNUSED(pPrim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageTraverse(LusdStage stage, PFN_lusdTraversalCallback pfnCallback, void* pUserData, LusdTraversalFlags flags) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pfnCallback); LUSD_UNUSED(pUserData); LUSD_UNUSED(flags);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* -------------------------------------------------------------------
 * Stage metadata
 * ------------------------------------------------------------------- */

LusdResult lusdStageGetUpAxis(LusdStage stage, LusdUpAxis* pUpAxis) {
    if (!stage || !pUpAxis) return LUSD_ERROR_INVALID_ARGUMENT;
    *pUpAxis = ((LusdStage_T*)stage)->up_axis;
    return LUSD_SUCCESS;
}
LusdResult lusdStageSetUpAxis(LusdStage stage, LusdUpAxis upAxis) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    ((LusdStage_T*)stage)->up_axis = upAxis;
    return LUSD_SUCCESS;
}
LusdResult lusdStageGetMetersPerUnit(LusdStage stage, double* pMPU) {
    if (!stage || !pMPU) return LUSD_ERROR_INVALID_ARGUMENT;
    *pMPU = ((LusdStage_T*)stage)->meters_per_unit;
    return LUSD_SUCCESS;
}
LusdResult lusdStageSetMetersPerUnit(LusdStage stage, double mpu) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    ((LusdStage_T*)stage)->meters_per_unit = mpu;
    return LUSD_SUCCESS;
}
LusdResult lusdStageGetStartTimeCode(LusdStage stage, double* pTC) {
    if (!stage || !pTC) return LUSD_ERROR_INVALID_ARGUMENT;
    *pTC = ((LusdStage_T*)stage)->start_time_code;
    return LUSD_SUCCESS;
}
LusdResult lusdStageSetStartTimeCode(LusdStage stage, double tc) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    ((LusdStage_T*)stage)->start_time_code = tc;
    return LUSD_SUCCESS;
}
LusdResult lusdStageGetEndTimeCode(LusdStage stage, double* pTC) {
    if (!stage || !pTC) return LUSD_ERROR_INVALID_ARGUMENT;
    *pTC = ((LusdStage_T*)stage)->end_time_code;
    return LUSD_SUCCESS;
}
LusdResult lusdStageSetEndTimeCode(LusdStage stage, double tc) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    ((LusdStage_T*)stage)->end_time_code = tc;
    return LUSD_SUCCESS;
}
LusdResult lusdStageGetFramesPerSecond(LusdStage stage, double* pFPS) {
    if (!stage || !pFPS) return LUSD_ERROR_INVALID_ARGUMENT;
    *pFPS = ((LusdStage_T*)stage)->frames_per_second;
    return LUSD_SUCCESS;
}
LusdResult lusdStageSetFramesPerSecond(LusdStage stage, double fps) {
    if (!stage) return LUSD_ERROR_INVALID_ARGUMENT;
    ((LusdStage_T*)stage)->frames_per_second = fps;
    return LUSD_SUCCESS;
}
