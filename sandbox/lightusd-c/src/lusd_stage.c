/*
 * lusd_stage.c - Stage operations (stub)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_stage.h"
#include "internal/lusd_internal.h"

LusdResult lusdLoadStage(LusdInstance instance, const LusdStageLoadInfo* pLoadInfo, LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pLoadInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdLoadStageFromMemory(LusdInstance instance, const LusdStageLoadFromMemoryInfo* pLoadInfo, LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pLoadInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdCreateStage(LusdInstance instance, const LusdStageCreateInfo* pCreateInfo, LusdStage* pStage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pCreateInfo); LUSD_UNUSED(pStage);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

void lusdDestroyStage(LusdInstance instance, LusdStage stage) {
    LUSD_UNUSED(instance); LUSD_UNUSED(stage);
}

LusdResult lusdStageGetRootPrimCount(LusdStage stage, uint32_t* pCount) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pCount);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetRootPrims(LusdStage stage, uint32_t count, LusdPrim* pPrims) {
    LUSD_UNUSED(stage); LUSD_UNUSED(count); LUSD_UNUSED(pPrims);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetPrimAtPath(LusdStage stage, LusdPath path, LusdPrim* pPrim) {
    LUSD_UNUSED(stage); LUSD_UNUSED(path); LUSD_UNUSED(pPrim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageTraverse(LusdStage stage, PFN_lusdTraversalCallback pfnCallback, void* pUserData, LusdTraversalFlags flags) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pfnCallback); LUSD_UNUSED(pUserData); LUSD_UNUSED(flags);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetUpAxis(LusdStage stage, LusdUpAxis* pUpAxis) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pUpAxis);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageSetUpAxis(LusdStage stage, LusdUpAxis upAxis) {
    LUSD_UNUSED(stage); LUSD_UNUSED(upAxis);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetMetersPerUnit(LusdStage stage, double* pMetersPerUnit) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pMetersPerUnit);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageSetMetersPerUnit(LusdStage stage, double metersPerUnit) {
    LUSD_UNUSED(stage); LUSD_UNUSED(metersPerUnit);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetStartTimeCode(LusdStage stage, double* pTimeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pTimeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageSetStartTimeCode(LusdStage stage, double timeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(timeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetEndTimeCode(LusdStage stage, double* pTimeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pTimeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageSetEndTimeCode(LusdStage stage, double timeCode) {
    LUSD_UNUSED(stage); LUSD_UNUSED(timeCode);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageGetFramesPerSecond(LusdStage stage, double* pFPS) {
    LUSD_UNUSED(stage); LUSD_UNUSED(pFPS);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageSetFramesPerSecond(LusdStage stage, double fps) {
    LUSD_UNUSED(stage); LUSD_UNUSED(fps);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdStageAddRootPrim(LusdStage stage, LusdPrim prim) {
    LUSD_UNUSED(stage); LUSD_UNUSED(prim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
