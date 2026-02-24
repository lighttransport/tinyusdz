/*
 * lusd_property.c - Property operations (stub)
 */
#include "lightusd/lusd_property.h"
#include "internal/lusd_internal.h"

LusdResult lusdPrimGetPropertyKindByName(LusdPrim prim, const char* pPropertyName, LusdPropertyKind* pKind) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPropertyName); LUSD_UNUSED(pKind); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimIsPropertyCustom(LusdPrim prim, const char* pPropertyName, bool* pCustom) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPropertyName); LUSD_UNUSED(pCustom); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
