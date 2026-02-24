/*
 * lusd_layer.h - Layer creation and loading
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_LAYER_H
#define LUSD_LAYER_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

LUSD_API LusdResult lusdCreateLayer(
    LusdInstance                 instance,
    const LusdLayerCreateInfo*   pCreateInfo,
    LusdLayer*                   pLayer);

LUSD_API void lusdDestroyLayer(
    LusdInstance    instance,
    LusdLayer       layer);

LUSD_API LusdResult lusdLayerGetIdentifier(
    LusdLayer       layer,
    const char**    ppIdentifier);

LUSD_API LusdResult lusdStageGetRootLayer(
    LusdStage       stage,
    LusdLayer*      pLayer);

LUSD_API LusdResult lusdStageGetSubLayerCount(
    LusdStage       stage,
    uint32_t*       pCount);

LUSD_API LusdResult lusdStageGetSubLayers(
    LusdStage       stage,
    uint32_t        count,
    LusdLayer*      pLayers);

LUSD_EXTERN_C_END

#endif /* LUSD_LAYER_H */
