/*
 * lusd_allocator.c - Default allocator implementation (malloc/free/realloc)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_allocator.h"
#include <stdlib.h>
#include <string.h>

/* Default allocator using standard library */

static void* lusd_default_alloc(void* pUserData, size_t size, size_t alignment) {
    (void)pUserData;
    (void)alignment;
    /* Standard malloc provides sufficient alignment for all basic types.
     * For stricter alignment, use aligned_alloc on C11 or posix_memalign. */
    return malloc(size);
}

static void* lusd_default_realloc(void* pUserData, void* pOriginal, size_t size, size_t alignment) {
    (void)pUserData;
    (void)alignment;
    return realloc(pOriginal, size);
}

static void lusd_default_free(void* pUserData, void* pMemory) {
    (void)pUserData;
    free(pMemory);
}

/* Internal default callbacks instance */
static const LusdAllocationCallbacks g_defaultAllocator = {
    NULL,                   /* pUserData */
    lusd_default_alloc,
    lusd_default_realloc,
    lusd_default_free
};

/* -------------------------------------------------------------------
 * Internal wrappers used throughout the library
 * ------------------------------------------------------------------- */

void* lusd_alloc(const LusdAllocationCallbacks* alloc, size_t size, size_t alignment) {
    if (!alloc) alloc = &g_defaultAllocator;
    return alloc->pfnAllocation(alloc->pUserData, size, alignment);
}

void* lusd_realloc(const LusdAllocationCallbacks* alloc, void* ptr, size_t size, size_t alignment) {
    if (!alloc) alloc = &g_defaultAllocator;
    return alloc->pfnReallocation(alloc->pUserData, ptr, size, alignment);
}

void lusd_free(const LusdAllocationCallbacks* alloc, void* ptr) {
    if (!alloc) alloc = &g_defaultAllocator;
    if (ptr) {
        alloc->pfnFree(alloc->pUserData, ptr);
    }
}

char* lusd_strdup(const LusdAllocationCallbacks* alloc, const char* s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char* dup = (char*)lusd_alloc(alloc, len, 1);
    if (dup) {
        memcpy(dup, s, len);
    }
    return dup;
}
