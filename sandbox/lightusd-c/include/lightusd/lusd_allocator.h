/*
 * lusd_allocator.h - Custom allocator callbacks
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_ALLOCATOR_H
#define LUSD_ALLOCATOR_H

#include "lusd_platform.h"

LUSD_EXTERN_C_BEGIN

/* Function pointer types for custom allocation */
typedef void* (*PFN_lusdAllocation)(void* pUserData, size_t size, size_t alignment);
typedef void* (*PFN_lusdReallocation)(void* pUserData, void* pOriginal, size_t size, size_t alignment);
typedef void  (*PFN_lusdFree)(void* pUserData, void* pMemory);

/*
 * Custom allocator callbacks. Pass NULL to any lusd function that
 * accepts this to use the default malloc/free allocator.
 */
typedef struct LusdAllocationCallbacks {
    void*                pUserData;
    PFN_lusdAllocation   pfnAllocation;
    PFN_lusdReallocation pfnReallocation;
    PFN_lusdFree         pfnFree;
} LusdAllocationCallbacks;

LUSD_EXTERN_C_END

#endif /* LUSD_ALLOCATOR_H */
