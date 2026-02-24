/*
 * lusd_internal.h - Internal types not in public API
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_INTERNAL_H
#define LUSD_INTERNAL_H

#include "lightusd/lusd_platform.h"
#include "lightusd/lusd_result.h"
#include "lightusd/lusd_handles.h"
#include "lightusd/lusd_enums.h"
#include "lightusd/lusd_allocator.h"
#include "lightusd/lusd_diagnostics.h"
#include "lusd_handle_table.h"
#include "lusd_dynamic_array.h"

#include <string.h>

/* -------------------------------------------------------------------
 * Internal allocator wrappers
 * ------------------------------------------------------------------- */

void* lusd_alloc(const LusdAllocationCallbacks* alloc, size_t size, size_t alignment);
void* lusd_realloc(const LusdAllocationCallbacks* alloc, void* ptr, size_t size, size_t alignment);
void  lusd_free(const LusdAllocationCallbacks* alloc, void* ptr);
char* lusd_strdup(const LusdAllocationCallbacks* alloc, const char* s);

/* -------------------------------------------------------------------
 * Internal string utilities
 * ------------------------------------------------------------------- */

uint64_t lusd_hash_string(const char* s);
uint64_t lusd_hash_bytes(const void* data, size_t len);
int      lusd_strcmp(const char* a, const char* b);

/* -------------------------------------------------------------------
 * Token pool entry (interned string)
 * ------------------------------------------------------------------- */

typedef struct LusdTokenEntry {
    char*    text;       /* Owned, null-terminated */
    uint64_t hash;
    uint32_t length;
} LusdTokenEntry;

/* -------------------------------------------------------------------
 * Token pool (open-addressing hash table)
 * ------------------------------------------------------------------- */

typedef struct LusdTokenPool {
    LusdTokenEntry*  entries;
    uint32_t         capacity;
    uint32_t         count;
    const LusdAllocationCallbacks* alloc;
} LusdTokenPool;

LusdResult lusd_token_pool_init(LusdTokenPool* pool, const LusdAllocationCallbacks* alloc);
void       lusd_token_pool_destroy(LusdTokenPool* pool);
LusdResult lusd_token_pool_intern(LusdTokenPool* pool, const char* text, uint32_t* pIndex);
const char* lusd_token_pool_get(const LusdTokenPool* pool, uint32_t index);

/* -------------------------------------------------------------------
 * Path internal data
 * ------------------------------------------------------------------- */

typedef struct LusdPathData {
    char*    text;           /* Full path string, owned */
    char*    elementName;    /* Points into text or separately allocated */
    char*    propertyName;   /* Points into text or NULL */
    uint32_t primEnd;        /* Byte offset where property part starts (after '.') */
    bool     isAbsolute;
    bool     isProperty;
} LusdPathData;

/* -------------------------------------------------------------------
 * Value internal data
 *
 * 32-byte inline storage for small types. Heap-allocated for large
 * types and arrays.
 * ------------------------------------------------------------------- */

#define LUSD_VALUE_INLINE_SIZE 24

typedef struct LusdValueData {
    LusdValueType type;
    uint64_t      arrayCount;   /* 0 for scalars */
    union {
        uint8_t   inlineData[LUSD_VALUE_INLINE_SIZE];
        struct {
            void*    ptr;
            uint64_t size;   /* bytes */
        } heap;
    } storage;
    bool useHeap;
} LusdValueData;

/* -------------------------------------------------------------------
 * Instance internal data
 * ------------------------------------------------------------------- */

struct LusdInstance_T {
    LusdAllocationCallbacks  alloc;
    LusdHandleTable          handleTable;
    LusdTokenPool            tokenPool;
    char                     lastError[1024];

    /* Diagnostics */
    PFN_lusdDiagnosticCallback diagCallback;
    void*                      diagUserData;

    /* API version from creation */
    uint32_t                 apiVersion;
};

/* -------------------------------------------------------------------
 * Handle table type tags (stored in handle table entries)
 * ------------------------------------------------------------------- */

typedef enum LusdHandleType {
    LUSD_HANDLE_TYPE_STAGE       = 1,
    LUSD_HANDLE_TYPE_PRIM        = 2,
    LUSD_HANDLE_TYPE_LAYER       = 3,
    LUSD_HANDLE_TYPE_VALUE       = 4,
    LUSD_HANDLE_TYPE_PATH        = 5,
    LUSD_HANDLE_TYPE_TOKEN       = 6,
    LUSD_HANDLE_TYPE_TIMESAMPLES = 7,
    LUSD_HANDLE_TYPE_ARENA       = 8,
    LUSD_HANDLE_TYPE_WRITER      = 9,
    LUSD_HANDLE_TYPE_STREAM      = 10
} LusdHandleType;

/* -------------------------------------------------------------------
 * Instance helper: set last error
 * ------------------------------------------------------------------- */

void lusd_set_error(struct LusdInstance_T* inst, const char* msg);
void lusd_set_errorf(struct LusdInstance_T* inst, const char* fmt, ...);

/* -------------------------------------------------------------------
 * Instance helper: emit diagnostic
 * ------------------------------------------------------------------- */

void lusd_diag(struct LusdInstance_T* inst, LusdDiagnosticSeverity sev, const char* msg);

#endif /* LUSD_INTERNAL_H */
