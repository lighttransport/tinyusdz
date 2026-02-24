/*
 * lusd_attribute.h - Attribute creation, get/set, connections
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_ATTRIBUTE_H
#define LUSD_ATTRIBUTE_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * Create attribute on a prim
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimCreateAttribute(
    LusdPrim                         prim,
    const LusdAttributeCreateInfo*   pCreateInfo);

/* -------------------------------------------------------------------
 * Default value
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetAttributeDefault(
    LusdPrim        prim,
    const char*     pAttrName,
    LusdValue*      pValue);

LUSD_API LusdResult lusdPrimSetAttributeDefault(
    LusdPrim        prim,
    const char*     pAttrName,
    LusdValue       value);

/* -------------------------------------------------------------------
 * Time samples
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetAttributeTimeSamples(
    LusdPrim            prim,
    const char*         pAttrName,
    LusdTimeSamples*    pTimeSamples);

LUSD_API LusdResult lusdPrimSetAttributeTimeSamples(
    LusdPrim            prim,
    const char*         pAttrName,
    LusdTimeSamples     timeSamples);

/* -------------------------------------------------------------------
 * Attribute queries
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetAttributeType(
    LusdPrim        prim,
    const char*     pAttrName,
    LusdValueType*  pType);

LUSD_API LusdResult lusdPrimGetAttributeVariability(
    LusdPrim            prim,
    const char*         pAttrName,
    LusdVariability*    pVar);

LUSD_API LusdResult lusdPrimIsAttributeBlocked(
    LusdPrim        prim,
    const char*     pAttrName,
    bool*           pBlocked);

LUSD_API LusdResult lusdPrimBlockAttribute(
    LusdPrim        prim,
    const char*     pAttrName);

/* -------------------------------------------------------------------
 * Connections (two-call pattern)
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetAttributeConnectionCount(
    LusdPrim        prim,
    const char*     pAttrName,
    uint32_t*       pCount);

LUSD_API LusdResult lusdPrimGetAttributeConnections(
    LusdPrim        prim,
    const char*     pAttrName,
    uint32_t        count,
    LusdPath*       pPaths);

LUSD_API LusdResult lusdPrimAddAttributeConnection(
    LusdPrim        prim,
    const char*     pAttrName,
    LusdPath        targetPath,
    LusdListEditOp  op);

LUSD_EXTERN_C_END

#endif /* LUSD_ATTRIBUTE_H */
