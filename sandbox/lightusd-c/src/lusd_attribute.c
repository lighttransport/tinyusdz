/*
 * lusd_attribute.c - Attribute operations (stub)
 */
#include "lightusd/lusd_attribute.h"
#include "internal/lusd_internal.h"

LusdResult lusdPrimCreateAttribute(LusdPrim prim, const LusdAttributeCreateInfo* pCreateInfo) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCreateInfo); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeDefault(LusdPrim prim, const char* pAttrName, LusdValue* pValue) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pValue); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimSetAttributeDefault(LusdPrim prim, const char* pAttrName, LusdValue value) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(value); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeTimeSamples(LusdPrim prim, const char* pAttrName, LusdTimeSamples* pTS) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pTS); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimSetAttributeTimeSamples(LusdPrim prim, const char* pAttrName, LusdTimeSamples ts) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(ts); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeType(LusdPrim prim, const char* pAttrName, LusdValueType* pType) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pType); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeVariability(LusdPrim prim, const char* pAttrName, LusdVariability* pVar) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pVar); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimIsAttributeBlocked(LusdPrim prim, const char* pAttrName, bool* pBlocked) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pBlocked); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimBlockAttribute(LusdPrim prim, const char* pAttrName) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeConnectionCount(LusdPrim prim, const char* pAttrName, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetAttributeConnections(LusdPrim prim, const char* pAttrName, uint32_t count, LusdPath* pPaths) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(count); LUSD_UNUSED(pPaths); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimAddAttributeConnection(LusdPrim prim, const char* pAttrName, LusdPath targetPath, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pAttrName); LUSD_UNUSED(targetPath); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
