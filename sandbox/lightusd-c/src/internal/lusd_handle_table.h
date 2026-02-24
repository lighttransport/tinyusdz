/*
 * lusd_handle_table.h - Generation-counted handle table
 *
 * Provides O(1) lookup with use-after-free detection via generation counters.
 * Each slot has a generation number that increments on free. A handle encodes
 * both the slot index and generation, so stale handles are detected.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_HANDLE_TABLE_H
#define LUSD_HANDLE_TABLE_H

#include "lightusd/lusd_platform.h"
#include "lightusd/lusd_result.h"
#include "lightusd/lusd_allocator.h"

/*
 * Handle encoding:
 *   bits [0..31]  = slot index
 *   bits [32..63] = generation
 *
 * A handle value of 0 is reserved as "null handle".
 */

typedef struct LusdHandleSlot {
    void*    data;        /* Pointer to user data */
    uint32_t generation;  /* Incremented on free */
    uint32_t type;        /* LusdHandleType tag */
    bool     occupied;
} LusdHandleSlot;

typedef struct LusdHandleTable {
    LusdHandleSlot*  slots;
    uint32_t         capacity;
    uint32_t         count;
    uint32_t         freeHead;   /* Index of first free slot, or capacity if none */
    const LusdAllocationCallbacks* alloc;
} LusdHandleTable;

/* Initialize a handle table with initial capacity */
LusdResult lusd_handle_table_init(
    LusdHandleTable* table,
    uint32_t initialCapacity,
    const LusdAllocationCallbacks* alloc);

/* Destroy a handle table */
void lusd_handle_table_destroy(LusdHandleTable* table);

/*
 * Allocate a slot and return a handle.
 * data: pointer to store
 * type: LusdHandleType tag
 * pHandle: receives the 64-bit handle (cast to opaque pointer by caller)
 */
LusdResult lusd_handle_table_alloc(
    LusdHandleTable* table,
    void* data,
    uint32_t type,
    uint64_t* pHandle);

/*
 * Look up a handle. Returns LUSD_ERROR_USE_AFTER_FREE if generation mismatch.
 * ppData: receives the stored pointer
 * pType: receives the type tag (may be NULL)
 */
LusdResult lusd_handle_table_lookup(
    const LusdHandleTable* table,
    uint64_t handle,
    void** ppData,
    uint32_t* pType);

/*
 * Free a slot. Increments generation counter.
 */
LusdResult lusd_handle_table_free(
    LusdHandleTable* table,
    uint64_t handle);

#endif /* LUSD_HANDLE_TABLE_H */
