/*
 * lusd_write_internal.h - Internal write-mode prim/stage/writer types
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_WRITE_INTERNAL_H
#define LUSD_WRITE_INTERNAL_H

#include "lightusd/lusd_enums.h"
#include "lusd_internal.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/*
 * Magic number stored at offset 0 of every LusdWritePrim_T.
 * Used to distinguish write-mode prims (LusdWritePrim_T) from
 * read-mode prims (LusdPrim_T, whose first field is a char* pointer).
 * On 64-bit a heap char* will never have its low 32 bits equal to this.
 */
#define LUSD_WRITE_PRIM_MAGIC 0x574C5554u  /* 'W','L','U','T' */

/* -----------------------------------------------------------------------
 * LusdWriteAttr_T — one attribute on a write-mode prim
 *
 * Owns:  name string, and the heap buffer of default_value (if used).
 * ----------------------------------------------------------------------- */
typedef struct LusdWriteAttr_T {
    char*           name;           /* owned, null-terminated */
    LusdValueType   type;           /* base type (may have ARRAY_BIT set) */
    LusdVariability variability;
    bool            custom;
    bool            has_default;    /* true if default_value is populated */

    /* Inline copy of LusdValueData (from lusd_internal.h).
     * For heap types (arrays, large scalars), the heap buffer is owned here. */
    LusdValueData   default_value;
} LusdWriteAttr_T;

/* -----------------------------------------------------------------------
 * LusdWritePrim_T — write-mode prim node
 *
 * Returned as opaque LusdPrim handle.
 * First field MUST be `magic` so all lusd_prim.c helpers can type-check.
 * ----------------------------------------------------------------------- */
typedef struct LusdWritePrim_T {
    uint32_t            magic;          /* always LUSD_WRITE_PRIM_MAGIC */
    LusdSpecifier       specifier;      /* def / over / class */
    bool                active;

    char*               name;           /* owned, null-terminated */
    char*               type_name;      /* owned, or NULL for typeless */

    /* Children (non-owning list; child prims may be owned by caller or stage) */
    struct LusdWritePrim_T** children;
    uint32_t                 child_count;
    uint32_t                 child_cap;

    /* Attributes */
    LusdWriteAttr_T*    attrs;
    uint32_t            attr_count;
    uint32_t            attr_cap;
} LusdWritePrim_T;

/* -----------------------------------------------------------------------
 * LusdStage_T — in-memory write-mode scene graph
 *
 * Stage owns root prims and recursively destroys them on lusdDestroyStage.
 * ----------------------------------------------------------------------- */
typedef struct LusdStage_T {
    LusdWritePrim_T**   root_prims;
    uint32_t            root_prim_count;
    uint32_t            root_prim_cap;

    LusdUpAxis  up_axis;
    double      meters_per_unit;
    double      start_time_code;
    double      end_time_code;
    double      frames_per_second;
} LusdStage_T;

/* -----------------------------------------------------------------------
 * LusdWriter_T — file-write context
 * ----------------------------------------------------------------------- */
typedef struct LusdWriter_T {
    LusdFormat  format;
    char*       file_path;   /* owned, null-terminated; NULL for to-string only */
} LusdWriter_T;

/* -----------------------------------------------------------------------
 * Helpers
 * ----------------------------------------------------------------------- */

/* Returns true if the prim handle is a write-mode prim. */
static inline bool lusd_is_write_prim(LusdPrim prim) {
    if (!prim) return false;
    /* Inspect the first 4 bytes of the pointed-to struct */
    uint32_t first32;
    __builtin_memcpy(&first32, prim, sizeof(uint32_t));
    return first32 == LUSD_WRITE_PRIM_MAGIC;
}

static inline LusdWritePrim_T* lusd_to_write_prim(LusdPrim prim) {
    return (LusdWritePrim_T*)(void*)prim;
}

/* Deep-copy a LusdValueData from src → dst.
 * For heap-allocated storage, allocates a new buffer (malloc). */
static inline bool lusd_value_data_deep_copy(LusdValueData* dst,
                                              const LusdValueData* src) {
    *dst = *src;   /* shallow copy first */
    if (src->useHeap && src->storage.heap.ptr && src->storage.heap.size > 0) {
        dst->storage.heap.ptr = malloc(src->storage.heap.size);
        if (!dst->storage.heap.ptr) return false;
        memcpy(dst->storage.heap.ptr, src->storage.heap.ptr,
               src->storage.heap.size);
    } else if (!src->useHeap && src->type == LUSD_VALUE_TYPE_STRING) {
        /* String: inline stores a char* that points to heap-alloc'd string */
        char* str_ptr;
        memcpy(&str_ptr, src->storage.inlineData, sizeof(char*));
        if (str_ptr) {
            char* dup = (char*)malloc(strlen(str_ptr) + 1);
            if (!dup) return false;
            strcpy(dup, str_ptr);
            memcpy(dst->storage.inlineData, &dup, sizeof(char*));
        }
    }
    return true;
}

/* Free heap resources inside a LusdValueData (does NOT free the struct). */
static inline void lusd_value_data_free(LusdValueData* vd) {
    if (!vd) return;
    if (vd->useHeap && vd->storage.heap.ptr) {
        free(vd->storage.heap.ptr);
        vd->storage.heap.ptr = NULL;
    } else if (!vd->useHeap && vd->type == LUSD_VALUE_TYPE_STRING) {
        char* str_ptr;
        memcpy(&str_ptr, vd->storage.inlineData, sizeof(char*));
        if (str_ptr) { free(str_ptr); }
        memset(vd->storage.inlineData, 0, sizeof(char*));
    }
}

/* Recursively destroy a write-mode prim and all its descendants. */
static inline void lusd_write_prim_destroy(LusdWritePrim_T* p) {
    if (!p) return;
    /* Destroy children first */
    for (uint32_t i = 0; i < p->child_count; i++)
        lusd_write_prim_destroy(p->children[i]);
    free(p->children);
    /* Destroy attributes */
    for (uint32_t i = 0; i < p->attr_count; i++) {
        free(p->attrs[i].name);
        if (p->attrs[i].has_default)
            lusd_value_data_free(&p->attrs[i].default_value);
    }
    free(p->attrs);
    free(p->name);
    free(p->type_name);
    free(p);
}

#endif /* LUSD_WRITE_INTERNAL_H */
