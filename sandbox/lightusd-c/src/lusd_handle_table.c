/*
 * lusd_handle_table.c - Generation-counted handle table implementation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "internal/lusd_handle_table.h"
#include "lightusd/lusd_platform.h"
#include <string.h>

/* Forward declare alloc wrappers */
void* lusd_alloc(const LusdAllocationCallbacks* alloc, size_t size, size_t alignment);
void  lusd_free(const LusdAllocationCallbacks* alloc, void* ptr);

/* Encode a handle from index and generation */
static uint64_t encode_handle(uint32_t index, uint32_t generation) {
    /* +1 to index so that slot 0 doesn't produce a zero handle */
    return ((uint64_t)generation << 32) | ((uint64_t)(index + 1));
}

/* Decode a handle into index and generation */
static void decode_handle(uint64_t handle, uint32_t* pIndex, uint32_t* pGeneration) {
    *pIndex = (uint32_t)(handle & 0xFFFFFFFFULL) - 1;
    *pGeneration = (uint32_t)(handle >> 32);
}

LusdResult lusd_handle_table_init(
    LusdHandleTable* table,
    uint32_t initialCapacity,
    const LusdAllocationCallbacks* alloc)
{
    if (!table) return LUSD_ERROR_INVALID_ARGUMENT;
    if (initialCapacity == 0) initialCapacity = 64;

    table->alloc = alloc;
    table->capacity = initialCapacity;
    table->count = 0;
    table->freeHead = 0;

    table->slots = (LusdHandleSlot*)lusd_alloc(
        alloc, sizeof(LusdHandleSlot) * initialCapacity, sizeof(void*));
    if (!table->slots) return LUSD_ERROR_OUT_OF_MEMORY;

    memset(table->slots, 0, sizeof(LusdHandleSlot) * initialCapacity);
    return LUSD_SUCCESS;
}

void lusd_handle_table_destroy(LusdHandleTable* table) {
    if (!table) return;
    if (table->slots) {
        lusd_free(table->alloc, table->slots);
        table->slots = NULL;
    }
    table->capacity = 0;
    table->count = 0;
}

static LusdResult grow_table(LusdHandleTable* table) {
    uint32_t newCap = table->capacity * 2;
    LusdHandleSlot* newSlots = (LusdHandleSlot*)lusd_alloc(
        table->alloc, sizeof(LusdHandleSlot) * newCap, sizeof(void*));
    if (!newSlots) return LUSD_ERROR_OUT_OF_MEMORY;

    memcpy(newSlots, table->slots, sizeof(LusdHandleSlot) * table->capacity);
    memset(newSlots + table->capacity, 0,
           sizeof(LusdHandleSlot) * (newCap - table->capacity));

    lusd_free(table->alloc, table->slots);
    table->slots = newSlots;
    table->freeHead = table->capacity;
    table->capacity = newCap;
    return LUSD_SUCCESS;
}

LusdResult lusd_handle_table_alloc(
    LusdHandleTable* table,
    void* data,
    uint32_t type,
    uint64_t* pHandle)
{
    if (!table || !pHandle) return LUSD_ERROR_INVALID_ARGUMENT;

    /* Find a free slot */
    uint32_t idx = table->freeHead;

    /* Linear scan from freeHead to find an unoccupied slot */
    while (idx < table->capacity && table->slots[idx].occupied) {
        idx++;
    }

    if (idx >= table->capacity) {
        LusdResult res = grow_table(table);
        if (res != LUSD_SUCCESS) return res;
        idx = table->freeHead;
        while (idx < table->capacity && table->slots[idx].occupied) {
            idx++;
        }
    }

    LusdHandleSlot* slot = &table->slots[idx];
    slot->data = data;
    slot->type = type;
    slot->occupied = true;
    /* generation was set to 0 on init, or incremented on free */

    table->count++;
    table->freeHead = idx + 1;

    *pHandle = encode_handle(idx, slot->generation);
    return LUSD_SUCCESS;
}

LusdResult lusd_handle_table_lookup(
    const LusdHandleTable* table,
    uint64_t handle,
    void** ppData,
    uint32_t* pType)
{
    if (!table || !ppData || handle == 0) return LUSD_ERROR_INVALID_HANDLE;

    uint32_t idx, gen;
    decode_handle(handle, &idx, &gen);

    if (idx >= table->capacity) return LUSD_ERROR_INVALID_HANDLE;

    const LusdHandleSlot* slot = &table->slots[idx];
    if (!slot->occupied) return LUSD_ERROR_USE_AFTER_FREE;
    if (slot->generation != gen) return LUSD_ERROR_USE_AFTER_FREE;

    *ppData = slot->data;
    if (pType) *pType = slot->type;
    return LUSD_SUCCESS;
}

LusdResult lusd_handle_table_free(
    LusdHandleTable* table,
    uint64_t handle)
{
    if (!table || handle == 0) return LUSD_ERROR_INVALID_HANDLE;

    uint32_t idx, gen;
    decode_handle(handle, &idx, &gen);

    if (idx >= table->capacity) return LUSD_ERROR_INVALID_HANDLE;

    LusdHandleSlot* slot = &table->slots[idx];
    if (!slot->occupied) return LUSD_ERROR_USE_AFTER_FREE;
    if (slot->generation != gen) return LUSD_ERROR_USE_AFTER_FREE;

    slot->data = NULL;
    slot->occupied = false;
    slot->generation++;  /* Increment generation to invalidate stale handles */
    table->count--;

    /* Update free head if this slot is before the current head */
    if (idx < table->freeHead) {
        table->freeHead = idx;
    }

    return LUSD_SUCCESS;
}
