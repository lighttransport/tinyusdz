/*
 * lusd_attribute.c - Attribute operations on write-mode prims
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_attribute.h"
#include "internal/lusd_internal.h"
#include "internal/lusd_write_internal.h"
#include <stdlib.h>
#include <string.h>

/* Find attribute by name; returns NULL if not found. */
static LusdWriteAttr_T* find_attr(LusdWritePrim_T* p, const char* name) {
    for (uint32_t i = 0; i < p->attr_count; i++)
        if (strcmp(p->attrs[i].name, name) == 0)
            return &p->attrs[i];
    return NULL;
}

/* -------------------------------------------------------------------
 * Create attribute
 * ------------------------------------------------------------------- */

LusdResult lusdPrimCreateAttribute(LusdPrim prim, const LusdAttributeCreateInfo* pCI) {
    if (!prim || !pCI || !pCI->pName) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;

    LusdWritePrim_T* p = lusd_to_write_prim(prim);

    /* Check for duplicate */
    if (find_attr(p, pCI->pName)) return LUSD_ERROR_INVALID_ARGUMENT;

    /* Grow attrs array if needed */
    if (p->attr_count >= p->attr_cap) {
        uint32_t new_cap = p->attr_cap ? p->attr_cap * 2 : 8;
        LusdWriteAttr_T* na = (LusdWriteAttr_T*)realloc(
            p->attrs, new_cap * sizeof(LusdWriteAttr_T));
        if (!na) return LUSD_ERROR_OUT_OF_MEMORY;
        p->attrs = na;
        p->attr_cap = new_cap;
    }

    LusdWriteAttr_T* a = &p->attrs[p->attr_count++];
    memset(a, 0, sizeof(*a));

    a->name = (char*)malloc(strlen(pCI->pName) + 1);
    if (!a->name) { p->attr_count--; return LUSD_ERROR_OUT_OF_MEMORY; }
    strcpy(a->name, pCI->pName);
    a->type        = pCI->valueType;
    a->variability = pCI->variability;
    a->custom      = pCI->custom;
    a->has_default = false;

    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Default value
 * ------------------------------------------------------------------- */

LusdResult lusdPrimSetAttributeDefault(LusdPrim prim, const char* pAttrName, LusdValue value) {
    if (!prim || !pAttrName || !value) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;

    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    LusdWriteAttr_T* a = find_attr(p, pAttrName);
    if (!a) return LUSD_ERROR_INVALID_ARGUMENT;

    /* value handle is actually a LusdValueData* (see lusd_value.c) */
    const LusdValueData* src = (const LusdValueData*)(uintptr_t)value;

    /* Free old default if any */
    if (a->has_default)
        lusd_value_data_free(&a->default_value);

    if (!lusd_value_data_deep_copy(&a->default_value, src))
        return LUSD_ERROR_OUT_OF_MEMORY;

    a->has_default = true;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetAttributeDefault(LusdPrim prim, const char* pAttrName, LusdValue* pValue) {
    if (!prim || !pAttrName || !pValue) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;

    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    LusdWriteAttr_T* a = find_attr(p, pAttrName);
    if (!a || !a->has_default) return LUSD_ERROR_INVALID_ARGUMENT;

    /* Return a reference — caller should NOT destroy this value */
    *pValue = (LusdValue)(uintptr_t)&a->default_value;
    return LUSD_SUCCESS;
}

/* -------------------------------------------------------------------
 * Time samples (stub)
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetAttributeTimeSamples(LusdPrim prim, const char* pAttrName, LusdTimeSamples* pTS) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pTS);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetAttributeTimeSamples(LusdPrim prim, const char* pAttrName, LusdTimeSamples ts) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(ts);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* -------------------------------------------------------------------
 * Attribute queries
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetAttributeType(LusdPrim prim, const char* pAttrName, LusdValueType* pType) {
    if (!prim || !pAttrName || !pType) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;
    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    LusdWriteAttr_T* a = find_attr(p, pAttrName);
    if (!a) return LUSD_ERROR_INVALID_ARGUMENT;
    *pType = a->type;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimGetAttributeVariability(LusdPrim prim, const char* pAttrName, LusdVariability* pVar) {
    if (!prim || !pAttrName || !pVar) return LUSD_ERROR_INVALID_ARGUMENT;
    if (!lusd_is_write_prim(prim)) return LUSD_ERROR_INVALID_HANDLE;
    LusdWritePrim_T* p = lusd_to_write_prim(prim);
    LusdWriteAttr_T* a = find_attr(p, pAttrName);
    if (!a) return LUSD_ERROR_INVALID_ARGUMENT;
    *pVar = a->variability;
    return LUSD_SUCCESS;
}

LusdResult lusdPrimIsAttributeBlocked(LusdPrim prim, const char* pAttrName, bool* pBlocked) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pBlocked);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimBlockAttribute(LusdPrim prim, const char* pAttrName) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

/* -------------------------------------------------------------------
 * Connections (stub)
 * ------------------------------------------------------------------- */

LusdResult lusdPrimGetAttributeConnectionCount(LusdPrim prim, const char* pAttrName, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pCount);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetAttributeConnections(LusdPrim prim, const char* pAttrName, uint32_t count, LusdPath* pPaths) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(count); LUSD_UNUSED(pPaths);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimAddAttributeConnection(LusdPrim prim, const char* pAttrName, LusdPath targetPath, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(targetPath); LUSD_UNUSED(op);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
