/*
 * lusd_composition.c - Composition arc operations (stub)
 */
#include "lightusd/lusd_composition.h"
#include "internal/lusd_internal.h"

LusdResult lusdPrimAddReference(LusdPrim prim, const LusdReference* pRef, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pRef); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetReferenceCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetReferences(LusdPrim prim, uint32_t count, LusdReference* pRefs) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pRefs); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimAddPayload(LusdPrim prim, const LusdPayload* pPayload, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pPayload); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetPayloadCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetPayloads(LusdPrim prim, uint32_t count, LusdPayload* pPayloads) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pPayloads); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetVariantSetCount(LusdPrim prim, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetVariantSetNames(LusdPrim prim, uint32_t count, const char** pNames) {
    LUSD_UNUSED(prim); LUSD_UNUSED(count); LUSD_UNUSED(pNames); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetVariantSelection(LusdPrim prim, const char* pVSName, const char** ppSel) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pVSName); LUSD_UNUSED(ppSel); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimSetVariantSelection(LusdPrim prim, const char* pVSName, const char* pSel) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pVSName); LUSD_UNUSED(pSel); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimAddInherit(LusdPrim prim, LusdPath classPath, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(classPath); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimAddSpecialize(LusdPrim prim, LusdPath targetPath, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(targetPath); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
