/*
 * lusd_stage.h - Stage loading, creation, traversal, metadata
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_STAGE_H
#define LUSD_STAGE_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"
#include "lusd_allocator.h"

LUSD_EXTERN_C_BEGIN

/* Traversal callback */
typedef LusdTraversalAction (*PFN_lusdTraversalCallback)(
    LusdPrim    prim,
    LusdPath    absolutePath,
    uint32_t    depth,
    void*       pUserData);

/* -------------------------------------------------------------------
 * Stage lifecycle
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdLoadStage(
    LusdInstance                   instance,
    const LusdStageLoadInfo*       pLoadInfo,
    LusdStage*                     pStage);

LUSD_API LusdResult lusdLoadStageFromMemory(
    LusdInstance                       instance,
    const LusdStageLoadFromMemoryInfo* pLoadInfo,
    LusdStage*                         pStage);

LUSD_API LusdResult lusdCreateStage(
    LusdInstance                   instance,
    const LusdStageCreateInfo*     pCreateInfo,
    LusdStage*                     pStage);

LUSD_API void lusdDestroyStage(
    LusdInstance                   instance,
    LusdStage                      stage);

/* -------------------------------------------------------------------
 * Root prim access (two-call pattern)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdStageGetRootPrimCount(
    LusdStage   stage,
    uint32_t*   pCount);

LUSD_API LusdResult lusdStageGetRootPrims(
    LusdStage   stage,
    uint32_t    count,
    LusdPrim*   pPrims);

/* -------------------------------------------------------------------
 * Prim lookup
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdStageGetPrimAtPath(
    LusdStage   stage,
    LusdPath    path,
    LusdPrim*   pPrim);

/* -------------------------------------------------------------------
 * Traversal
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdStageTraverse(
    LusdStage                   stage,
    PFN_lusdTraversalCallback   pfnCallback,
    void*                       pUserData,
    LusdTraversalFlags          flags);

/* -------------------------------------------------------------------
 * Stage metadata
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdStageGetUpAxis(LusdStage stage, LusdUpAxis* pUpAxis);
LUSD_API LusdResult lusdStageSetUpAxis(LusdStage stage, LusdUpAxis upAxis);

LUSD_API LusdResult lusdStageGetMetersPerUnit(LusdStage stage, double* pMetersPerUnit);
LUSD_API LusdResult lusdStageSetMetersPerUnit(LusdStage stage, double metersPerUnit);

LUSD_API LusdResult lusdStageGetStartTimeCode(LusdStage stage, double* pTimeCode);
LUSD_API LusdResult lusdStageSetStartTimeCode(LusdStage stage, double timeCode);

LUSD_API LusdResult lusdStageGetEndTimeCode(LusdStage stage, double* pTimeCode);
LUSD_API LusdResult lusdStageSetEndTimeCode(LusdStage stage, double timeCode);

LUSD_API LusdResult lusdStageGetFramesPerSecond(LusdStage stage, double* pFPS);
LUSD_API LusdResult lusdStageSetFramesPerSecond(LusdStage stage, double fps);

/* -------------------------------------------------------------------
 * Add prim to stage (takes ownership of an owning prim)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdStageAddRootPrim(
    LusdStage   stage,
    LusdPrim    prim);

LUSD_EXTERN_C_END

#endif /* LUSD_STAGE_H */
