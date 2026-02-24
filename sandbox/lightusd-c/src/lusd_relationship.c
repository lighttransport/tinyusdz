/*
 * lusd_relationship.c - Relationship operations (stub)
 */
#include "lightusd/lusd_relationship.h"
#include "internal/lusd_internal.h"

LusdResult lusdPrimCreateRelationship(LusdPrim prim, const LusdRelationshipCreateInfo* pCI) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pCI); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetRelationshipTargetCount(LusdPrim prim, const char* pRelName, uint32_t* pCount) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pRelName); LUSD_UNUSED(pCount); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimGetRelationshipTargets(LusdPrim prim, const char* pRelName, uint32_t count, LusdPath* pTargets) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pRelName); LUSD_UNUSED(count); LUSD_UNUSED(pTargets); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
LusdResult lusdPrimAddRelationshipTarget(LusdPrim prim, const char* pRelName, LusdPath targetPath, LusdListEditOp op) {
    LUSD_UNUSED(prim); LUSD_UNUSED(pRelName); LUSD_UNUSED(targetPath); LUSD_UNUSED(op); return LUSD_ERROR_FEATURE_NOT_PRESENT;
}
