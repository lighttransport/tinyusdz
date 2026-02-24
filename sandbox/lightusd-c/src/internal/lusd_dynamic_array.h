/*
 * lusd_dynamic_array.h - Type-generic growing array (replaces std::vector)
 *
 * Usage:
 *   lusd_vec_t(float) arr;
 *   lusd_vec_init(&arr);
 *   lusd_vec_push(&arr, 3.14f, alloc);
 *   float v = lusd_vec_at(&arr, 0);
 *   lusd_vec_destroy(&arr, alloc);
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_DYNAMIC_ARRAY_H
#define LUSD_DYNAMIC_ARRAY_H

#include "lightusd/lusd_platform.h"
#include "lightusd/lusd_allocator.h"
#include <string.h>

/* Forward declare internal alloc wrappers */
void* lusd_alloc(const LusdAllocationCallbacks* alloc, size_t size, size_t alignment);
void* lusd_realloc(const LusdAllocationCallbacks* alloc, void* ptr, size_t size, size_t alignment);
void  lusd_free(const LusdAllocationCallbacks* alloc, void* ptr);

/*
 * Generic dynamic array struct. Use lusd_vec_t(T) macro to declare typed ones.
 * Internally it's just a void* with count/capacity.
 */
typedef struct lusd_vec_header {
    void*    data;
    uint32_t count;
    uint32_t capacity;
    uint32_t elemSize;
} lusd_vec_header;

/* Declare a typed dynamic array */
#define lusd_vec_t(T) \
    struct { T* data; uint32_t count; uint32_t capacity; uint32_t elemSize; }

/* Initialize */
#define lusd_vec_init(v) \
    do { (v)->data = NULL; (v)->count = 0; (v)->capacity = 0; \
         (v)->elemSize = sizeof(*(v)->data); } while(0)

/* Destroy */
#define lusd_vec_destroy(v, alloc) \
    do { if ((v)->data) { lusd_free(alloc, (v)->data); (v)->data = NULL; } \
         (v)->count = 0; (v)->capacity = 0; } while(0)

/* Get element count */
#define lusd_vec_count(v) ((v)->count)

/* Access element at index (no bounds check in release) */
#define lusd_vec_at(v, i) ((v)->data[(i)])

/* Get raw data pointer */
#define lusd_vec_data(v) ((v)->data)

/* Reserve capacity (may reallocate) */
#define lusd_vec_reserve(v, newcap, alloc) \
    do { \
        if ((newcap) > (v)->capacity) { \
            uint32_t nc = (newcap); \
            void* nd = lusd_realloc(alloc, (v)->data, \
                nc * (v)->elemSize, sizeof(void*)); \
            if (nd) { (v)->data = nd; (v)->capacity = nc; } \
        } \
    } while(0)

/* Push element (grows if needed) */
#define lusd_vec_push(v, val, alloc) \
    do { \
        if ((v)->count >= (v)->capacity) { \
            uint32_t nc = (v)->capacity ? (v)->capacity * 2 : 8; \
            lusd_vec_reserve(v, nc, alloc); \
        } \
        if ((v)->count < (v)->capacity) { \
            (v)->data[(v)->count++] = (val); \
        } \
    } while(0)

/* Clear (keep memory) */
#define lusd_vec_clear(v) do { (v)->count = 0; } while(0)

/* Pop last element */
#define lusd_vec_pop(v) ((v)->data[--(v)->count])

/* Last element */
#define lusd_vec_back(v) ((v)->data[(v)->count - 1])

#endif /* LUSD_DYNAMIC_ARRAY_H */
