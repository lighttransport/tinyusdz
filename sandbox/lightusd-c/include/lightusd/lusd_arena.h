/*
 * lusd_arena.h - Arena (batch/bump) allocator
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_ARENA_H
#define LUSD_ARENA_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

LUSD_API LusdResult lusdCreateArena(
    LusdInstance                 instance,
    const LusdArenaCreateInfo*   pCreateInfo,
    LusdArena*                   pArena);

LUSD_API void lusdDestroyArena(
    LusdInstance    instance,
    LusdArena       arena);

/*
 * Allocate memory from the arena. Memory is freed when the arena is destroyed.
 */
LUSD_API void* lusdArenaAlloc(
    LusdArena   arena,
    size_t      size,
    size_t      alignment);

/*
 * Reset the arena, freeing all allocations but keeping the memory.
 */
LUSD_API void lusdArenaReset(LusdArena arena);

/*
 * Get total bytes allocated from the arena.
 */
LUSD_API uint64_t lusdArenaGetUsedBytes(LusdArena arena);

LUSD_EXTERN_C_END

#endif /* LUSD_ARENA_H */
