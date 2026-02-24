/*
 * lusd_stream.h - Progressive/streaming loader
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_STREAM_H
#define LUSD_STREAM_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

LUSD_API LusdResult lusdCreateStreamLoader(
    LusdInstance                        instance,
    const LusdStreamLoaderCreateInfo*   pCreateInfo,
    LusdStreamLoader*                   pLoader);

LUSD_API void lusdDestroyStreamLoader(
    LusdInstance        instance,
    LusdStreamLoader    loader);

/*
 * Feed data chunks to the stream loader.
 */
LUSD_API LusdResult lusdStreamLoaderFeed(
    LusdStreamLoader    loader,
    const void*         pData,
    uint64_t            dataSize);

/*
 * Signal end-of-stream and finalize the stage.
 */
LUSD_API LusdResult lusdStreamLoaderFinish(
    LusdStreamLoader    loader,
    LusdStage*          pStage);

LUSD_EXTERN_C_END

#endif /* LUSD_STREAM_H */
