/*
 * lusd_prim.c - Prim creation and operations
 *
 * Handles two kinds of LusdPrim:
 *   - Write prims: created by lusdCreatePrim, backed by LusdWritePrim_T.
 *     Identified by magic == LUSD_WRITE_PRIM_MAGIC at offset 0.
 *   - Read prims:  LusdPrim_T nodes from a parsed layer (returned by
 *     layer query helpers).  These are not yet accessible through the
 *     public prim API (tests access LusdPrim_T directly).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_prim.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_write_internal.h"
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------
 * Create / Destroy
 * ------------------------------------------------------------------- */

LusdResult lusdCreatePrim(LusdInstance instance, const LusdPrimCreateInfo* pCI, LusdPrim* pPrim) {
    LUSD_UNUSED(instance);
    if (!pCI || !pPrim || !pCI->pName) return LUSD_ERROR_INVALID_ARGUMENT;

    LusdWritePrim_T* p = (LusdWritePrim_T*)calloc(1, sizeof(LusdWritePrim_T));
    if (!p) return LUSD_ERROR_OUT_OF_MEMORY;

    p->magic = LUSD_WRITE_PRIM_MAGIC;
    p->specifier = pCI->specifier;
    p->active = true;

    p->name = (char*)malloc(strlen(pCI->pName) + 1);
    if (!p->name) { free(p); return LUSD_ERROR_OUT_OF_MEMORY; }
    strcpy(p->name, pCI->pName);

    if (pCI->pTypeName && pCI->pTypeName[0]) {
        p->type_name = (char*)malloc(strlen(pCI->pTypeName) + 1);
        if (!p->type_name) { free(p->name); free(p); return LUSD_ERROR_OUT_OF_MEMORY; }
        strcpy(p->type_name, pCI->pTypeName);
    }

    *pPrim = (LusdPrim)p;
    return LUSD_SUCCESS;
}

void lusdDestroyPrim(LusdInstance instance, LusdPrim prim) {
    LUSD_UNUSED(instance);
    if (!prim || !lusd_is_write_prim(prim)) return;
    lusd_write_prim_destroy(lusd_to_write_prim(prim));
}

/* -------------------------------------------------------------------
 * Queries
 * ------------------------------------------------------------------- */

const char* lusdPrimGetName(LusdPrim prim) {
    if (!prim) return "";
    if (lusd_is_write_prim(prim))
        return lusd_to_write_prim(prim)->name;
    /* Read prim: first field is const char* name */
    return *(const char* const*)prim;
}

const char* lusdPrimGetTypeName(LusdPrim prim) {
    if (!prim) return "";
    if (lusd_is_write_prim(prim)) {
        const char* tn = lusd_to_write_prim(prim)->type_name;
        return tn ? tn : "";
    }
    return "";
}

LusdResult lusdPrimGetPath(LusdPrim prim, LusdPath* pPath) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPath);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetSpecifier(LusdPrim prim, LusdSpecifier* pSpec) {
    if (!prim || !pSpec) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    *pSpec = lusd_to_write_prim(prim)->specifier;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimIsActive(LusdPrim prim, bool* pActive) {
    if (!prim || !pActive) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    *pActive = lusd_to_write_prim(prim)->active;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimSetActive(LusdPrim prim, bool active) {
    if (!prim) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    lusd_to_write_prim(prim)->active = active;
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Children
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetChildCount(LusdPrim prim, uint32_t* pCount) {
    if (!prim || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    *pCount = lusd_to_write_prim(prim)->child_count;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetChildren(LusdPrim prim, uint32_t count, LusdPrim* pChildren) {
    if (!prim || !pChildren) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    uint32_t n = count < p->child_count ? count : p->child_count;
    for (uint32_t i = 0; i < n; i++)
        pChildren[i] = (LusdPrim)p->children[i];
    return LUSD_SUCCESS;
}

LusdResult lusdPrimAddChild(LusdPrim parent, LusdPrim child) {
    if (!parent || !child) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(parent) || !lusd_is_write_prim(child))
        return LUSD_ERROR_INVALID_HANDLE;

    LusdWritePrim_T* p = lusd_to_write_prim(parent);
    if (p->child_count >= p->child_cap) {
        uint32_t new_cap = p->child_cap ? p->child_cap * 2 : 4;
        LusdWritePrim_T** nc = (LusdWritePrim_T**)realloc(
            p->children, new_cap * sizeof(LusdWritePrim_T*));
        if (!nc) return LUSD_ERROR_OUT_OF_MEMORY;
        p->children = nc;
        p->child_cap = new_cap;
    }
    p->children[p->child_count++] = lusd_to_write_prim(child);
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Property enumeration
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetPropertyCount(LusdPrim prim, uint32_t* pCount) {
    if (!prim || !pCount) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    *pCount = lusd_to_write_prim(prim)->attr_count;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyNames(LusdPrim prim, uint32_t count, const char** pNames) {
    if (!prim || !pNames) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    uint32_t n = count < p->attr_count ? count : p->attr_count;
    for (uint32_t i = 0; i < n; i++)
        pNames[i] = p->attrs[i].name;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetPropertyKind(LusdPrim prim, const char* pName, LusdPropertyKind* pKind) {
    if (!prim || !pName || !pKind) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_FEATURE_NOT_PRESENT;
    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    for (uint32_t i = 0; i < p->attr_count; i++) {
        if (strcmp(p->attrs[i].name, pName) == 0) {
            *pKind = LUSD_PROPERTY_KIND_ATTRIBUTE;
            return LUSD_SUCCESS;
        }
    }
    return LUSD_ERROR_INVALID_ARGUMENT;
}

/* -------------------------------------------------------------------
 * Metadata (stub)
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetMetadata(LusdPrim prim, const char* pKey, LusdValue* pValue) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(pValue);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetMetadata(LusdPrim prim, const char* pKey, LusdValue value) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(value);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
