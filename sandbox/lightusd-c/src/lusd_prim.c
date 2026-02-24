/*
 * lusd_prim.c - Prim operations (stub)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_prim.h"
#include "internal/lusd_internal.h"

LusdResult lusdCreatePrim(LusdInstance instance, const LusdPrimCreateInfo* pCreateInfo, LusdPrim* pPrim) {
    LUSD_UNUSED(instance); LUSD_UNUSED(pCreateInfo); LUSD_UNUSED(pPrim);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

void lusdDestroyPrim(LusdInstance instance, LusdPrim prim) {
    LUSD_UNUSED(instance); LUSD_UNUSED(prim);
}

const char* lusdPrimGetName(LusdPrim prim) {
    LUSD_UNUSED(prim);
    return "";
}

const char* lusdPrimGetTypeName(LusdPrim prim) {
    LUSD_UNUSED(prim);
    return "";
}

LusdResult lusdPrimGetPath(LusdPrim prim, LusdPath* pPath) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPath);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetSpecifier(LusdPrim prim, LusdSpecifier* pSpec) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pSpec);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimIsActive(LusdPrim prim, bool* pActive) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pActive);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetActive(LusdPrim prim, bool active) {
    LUSD_UNUSED(prim); LUSD_UNUSED(active);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetChildCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCount);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetChildren(LusdPrim prim, uint32_t count, LusdPrim* pChildren) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pChildren);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimAddChild(LusdPrim parent, LusdPrim child) {
    LUSD_UNUSED(parent); LUSD_UNUSED(child);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetPropertyCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCount);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetPropertyNames(LusdPrim prim, uint32_t count, const char** pNames) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pNames);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetPropertyKind(LusdPrim prim, const char* pName, LusdPropertyKind* pKind) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pName); LUSD_UNUSED(pKind);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimGetMetadata(LusdPrim prim, const char* pKey, LusdValue* pValue) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(pValue);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}

LusdResult lusdPrimSetMetadata(LusdPrim prim, const char* pKey, LusdValue value) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pKey); LUSD_UNUSED(value);
    return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
