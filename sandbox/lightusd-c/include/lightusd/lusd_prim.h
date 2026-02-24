/*
 * lusd_prim.h - Prim creation, query, children, properties
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_PRIM_H
#define LUSD_PRIM_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * Owning prim creation (must be added to stage or destroyed)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdCreatePrim(
    LusdInstance                instance,
    const LusdPrimCreateInfo*   pCreateInfo,
    LusdPrim*                   pPrim);

LUSD_API void lusdDestroyPrim(
    LusdInstance                instance,
    LusdPrim                    prim);

/* -------------------------------------------------------------------
 * Prim queries
 * ------------------------------------------------------------------- */

LUSD_API const char* lusdPrimGetName(LusdPrim prim);
LUSD_API const char* lusdPrimGetTypeName(LusdPrim prim);
LUSD_API LusdResult  lusdPrimGetPath(LusdPrim prim, LusdPath* pPath);
LUSD_API LusdResult  lusdPrimGetSpecifier(LusdPrim prim, LusdSpecifier* pSpec);
LUSD_API LusdResult  lusdPrimIsActive(LusdPrim prim, bool* pActive);
LUSD_API LusdResult  lusdPrimSetActive(LusdPrim prim, bool active);

/* -------------------------------------------------------------------
 * Children (two-call pattern)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetChildCount(
    LusdPrim    prim,
    uint32_t*   pCount);

LUSD_API LusdResult lusdPrimGetChildren(
    LusdPrim    prim,
    uint32_t    count,
    LusdPrim*   pChildren);

LUSD_API LusdResult lusdPrimAddChild(
    LusdPrim    parent,
    LusdPrim    child);

/* -------------------------------------------------------------------
 * Property enumeration (two-call pattern)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetPropertyCount(
    LusdPrim    prim,
    uint32_t*   pCount);

LUSD_API LusdResult lusdPrimGetPropertyNames(
    LusdPrim       prim,
    uint32_t       count,
    const char**   pNames);

LUSD_API LusdResult lusdPrimGetPropertyKind(
    LusdPrim            prim,
    const char*         pName,
    LusdPropertyKind*   pKind);

/* -------------------------------------------------------------------
 * Metadata
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetMetadata(
    LusdPrim        prim,
    const char*     pKey,
    LusdValue*      pValue);

LUSD_API LusdResult lusdPrimSetMetadata(
    LusdPrim        prim,
    const char*     pKey,
    LusdValue       value);

LUSD_EXTERN_C_END

#endif /* LUSD_PRIM_H */
