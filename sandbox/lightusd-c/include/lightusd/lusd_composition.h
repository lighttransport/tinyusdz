/*
 * lusd_composition.h - Composition arcs (references, payloads, variants, etc.)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_COMPOSITION_H
#define LUSD_COMPOSITION_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"
#include "lusd_structs.h"

LUSD_EXTERN_C_BEGIN

/* -------------------------------------------------------------------
 * References
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimAddReference(
    LusdPrim                prim,
    const LusdReference*    pRef,
    LusdListEditOp          op);

LUSD_API LusdResult lusdPrimGetReferenceCount(
    LusdPrim    prim,
    uint32_t*   pCount);

LUSD_API LusdResult lusdPrimGetReferences(
    LusdPrim        prim,
    uint32_t        count,
    LusdReference*  pRefs);

/* -------------------------------------------------------------------
 * Payloads
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimAddPayload(
    LusdPrim             prim,
    const LusdPayload*   pPayload,
    LusdListEditOp       op);

LUSD_API LusdResult lusdPrimGetPayloadCount(
    LusdPrim    prim,
    uint32_t*   pCount);

LUSD_API LusdResult lusdPrimGetPayloads(
    LusdPrim       prim,
    uint32_t       count,
    LusdPayload*   pPayloads);

/* -------------------------------------------------------------------
 * Variant sets
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimGetVariantSetCount(
    LusdPrim    prim,
    uint32_t*   pCount);

LUSD_API LusdResult lusdPrimGetVariantSetNames(
    LusdPrim       prim,
    uint32_t       count,
    const char**   pNames);

LUSD_API LusdResult lusdPrimGetVariantSelection(
    LusdPrim       prim,
    const char*    pVariantSetName,
    const char**   ppSelection);

LUSD_API LusdResult lusdPrimSetVariantSelection(
    LusdPrim       prim,
    const char*    pVariantSetName,
    const char*    pSelection);

/* -------------------------------------------------------------------
 * Inherits / Specializes
 * ------------------------------------------------------------------- */

LUSD_API LusdResult lusdPrimAddInherit(
    LusdPrim        prim,
    LusdPath        classPath,
    LusdListEditOp  op);

LUSD_API LusdResult lusdPrimAddSpecialize(
    LusdPrim        prim,
    LusdPath        targetPath,
    LusdListEditOp  op);

LUSD_EXTERN_C_END

#endif /* LUSD_COMPOSITION_H */
