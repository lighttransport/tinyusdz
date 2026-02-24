/*
 * lusd_token.c - Token interning pool implementation
 *
 * Open-addressing hash table for string interning. Once interned,
 * tokens store a direct pointer to the interned string, enabling
 * O(1) pointer-equality comparison and lusdTokenGetText without
 * needing the instance.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_token.h"
#include "internal/lusd_internal.h"
#include <string.h>

#define LUSD_TOKEN_POOL_INITIAL_CAPACITY 256
#define LUSD_TOKEN_POOL_LOAD_FACTOR_NUM  3
#define LUSD_TOKEN_POOL_LOAD_FACTOR_DEN  4

LusdResult lusd_token_pool_init(LusdTokenPool* pool, const LusdAllocationCallbacks* alloc) {
    if (!pool) return LUSD_ERROR_INVALID_ARGUMENT;
    pool->alloc = alloc;
    pool->count = 0;
    pool->capacity = LUSD_TOKEN_POOL_INITIAL_CAPACITY;
    pool->entries = (LusdTokenEntry*)lusd_alloc(
        alloc, sizeof(LusdTokenEntry) * pool->capacity, sizeof(void*));
    if (!pool->entries) return LUSD_ERROR_OUT_OF_MEMORY;
    memset(pool->entries, 0, sizeof(LusdTokenEntry) * pool->capacity);
    return LUSD_SUCCESS;
}

void lusd_token_pool_destroy(LusdTokenPool* pool) {
    if (!pool) return;
    if (pool->entries) {
        for (uint32_t i = 0; i < pool->capacity; i++) {
            if (pool->entries[i].text) {
                lusd_free(pool->alloc, pool->entries[i].text);
            }
        }
        lusd_free(pool->alloc, pool->entries);
        pool->entries = NULL;
    }
    pool->count = 0;
    pool->capacity = 0;
}

static LusdResult token_pool_grow(LusdTokenPool* pool) {
    uint32_t newCap = pool->capacity * 2;
    LusdTokenEntry* newEntries = (LusdTokenEntry*)lusd_alloc(
        pool->alloc, sizeof(LusdTokenEntry) * newCap, sizeof(void*));
    if (!newEntries) return LUSD_ERROR_OUT_OF_MEMORY;
    memset(newEntries, 0, sizeof(LusdTokenEntry) * newCap);

    /* Rehash all existing entries */
    for (uint32_t i = 0; i < pool->capacity; i++) {
        if (!pool->entries[i].text) continue;
        uint32_t idx = (uint32_t)(pool->entries[i].hash % newCap);
        while (newEntries[idx].text != NULL) {
            idx = (idx + 1) % newCap;
        }
        newEntries[idx] = pool->entries[i];
    }

    lusd_free(pool->alloc, pool->entries);
    pool->entries = newEntries;
    pool->capacity = newCap;
    return LUSD_SUCCESS;
}

LusdResult lusd_token_pool_intern(LusdTokenPool* pool, const char* text, uint32_t* pIndex) {
    if (!pool || !text || !pIndex) return LUSD_ERROR_INVALID_ARGUMENT;

    uint64_t hash = lusd_hash_string(text);
    uint32_t idx = (uint32_t)(hash % pool->capacity);

    /* Linear probe to find existing or empty slot */
    while (pool->entries[idx].text != NULL) {
        if (pool->entries[idx].hash == hash &&
            strcmp(pool->entries[idx].text, text) == 0) {
            /* Already interned */
            *pIndex = idx;
            return LUSD_SUCCESS;
        }
        idx = (idx + 1) % pool->capacity;
    }

    /* Need to insert. Check load factor first. */
    if (pool->count * LUSD_TOKEN_POOL_LOAD_FACTOR_DEN >=
        pool->capacity * LUSD_TOKEN_POOL_LOAD_FACTOR_NUM) {
        LusdResult res = token_pool_grow(pool);
        if (res != LUSD_SUCCESS) return res;
        /* Recompute index after grow */
        idx = (uint32_t)(hash % pool->capacity);
        while (pool->entries[idx].text != NULL) {
            idx = (idx + 1) % pool->capacity;
        }
    }

    /* Insert */
    uint32_t len = (uint32_t)strlen(text);
    pool->entries[idx].text = lusd_strdup(pool->alloc, text);
    if (!pool->entries[idx].text) return LUSD_ERROR_OUT_OF_MEMORY;
    pool->entries[idx].hash = hash;
    pool->entries[idx].length = len;
    pool->count++;

    *pIndex = idx;
    return LUSD_SUCCESS;
}

const char* lusd_token_pool_get(const LusdTokenPool* pool, uint32_t index) {
    if (!pool || index >= pool->capacity) return NULL;
    return pool->entries[index].text;
}

/* -------------------------------------------------------------------
 * Public API
 *
 * Token handle = direct pointer to the interned string (char*).
 * This allows lusdTokenGetText to work without the instance, and
 * two tokens with the same text are pointer-equal since they point
 * to the same interned entry.
 * ------------------------------------------------------------------- */

LusdResult lusdCreateToken(
    LusdInstance instance,
    const char*  pText,
    LusdToken*   pToken)
{
    if (!instance || !pText || !pToken) return LUSD_ERROR_INVALID_ARGUMENT;

    uint32_t poolIndex;
    LusdResult res = lusd_token_pool_intern(&instance->tokenPool, pText, &poolIndex);
    if (res != LUSD_SUCCESS) return res;

    /* The token handle is the pointer to the interned string.
     * Cast char* -> LusdToken (which is LusdToken_T*). This is safe
     * because we only ever cast back to char* in lusdTokenGetText. */
    const char* internedText = instance->tokenPool.entries[poolIndex].text;
    *pToken = (LusdToken)(uintptr_t)internedText;
    return LUSD_SUCCESS;
}

const char* lusdTokenGetText(LusdToken token) {
    if (!token) return "";
    /* Token handle IS the pointer to the interned string */
    return (const char*)(uintptr_t)token;
}

bool lusdTokenEqual(LusdToken a, LusdToken b) {
    /* Pointer equality: same interned string -> same pointer */
    return a == b;
}

uint64_t lusdTokenHash(LusdToken token) {
    if (!token) return 0;
    const char* text = (const char*)(uintptr_t)token;
    return lusd_hash_string(text);
}
