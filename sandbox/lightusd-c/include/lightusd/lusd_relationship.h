/*
 * lusd_relationship.h - Relationship creation and target management
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_RELATIONSHIP_H
#define LUSD_RELATIONSHIP_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * Create relationship on a prim
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimCreateRelationship(
    LusdPrim                              prim,
    const LusdRelationshipCreateInfo*     pCreateInfo);

/* -------------------------------------------------------------------
 * Targets (two-call pattern)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetRelationshipTargetCount(
    LusdPrim        prim,
    const char*     pRelName,
    uint32_t*       pCount);

LUSD_API LusdResult lusdPrimGetRelationshipTargets(
    LusdPrim        prim,
    const char*     pRelName,
    uint32_t        count,
    LusdPath*       pTargets);

LUSD_API LusdResult lusdPrimAddRelationshipTarget(
    LusdPrim        prim,
    const char*     pRelName,
    LusdPath        targetPath,
    LusdListEditOp  op);

LUSD_EXTERN_C_END

#endif /* LUSD_RELATIONSHIP_H */
