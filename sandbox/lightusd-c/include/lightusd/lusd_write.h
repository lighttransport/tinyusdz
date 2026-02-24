/*
 * lusd_write.h - USDA/USDC export
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_WRITE_H
#define LUSD_WRITE_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

LUSD_API LusdResult lusdCreateWriter(
    LusdInstance                   instance,
    const LusdWriterCreateInfo*    pCreateInfo,
    LusdWriter*                    pWriter);

LUSD_API void lusdDestroyWriter(
    LusdInstance    instance,
    LusdWriter      writer);

/*
 * Export a stage to USDA string. Caller must free *ppOutput with
 * the same allocator used for the instance.
 */
LUSD_API LusdResult lusdStageExportToString(
    LusdInstance    instance,
    LusdStage       stage,
    char**          ppOutput,
    uint64_t*       pLength);

/*
 * Export a stage to a file (format determined by WriterCreateInfo).
 */
LUSD_API LusdResult lusdWriterWriteStage(
    LusdWriter      writer,
    LusdStage       stage);

LUSD_EXTERN_C_END

#endif /* LUSD_WRITE_H */
