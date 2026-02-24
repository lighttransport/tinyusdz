/*
 * lusd_property.h - Property discriminated union (attribute or relationship)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_PROPERTY_H
#define LUSD_PROPERTY_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_enums.h"

LUSD_EXTERN_C_BEGIN

/*
 * Query whether a named property is an attribute or relationship.
 */
LUSD_API LusdResult lusdPrimGetPropertyKindByName(
    LusdPrim            prim,
    const char*         pPropertyName,
    LusdPropertyKind*   pKind);

/*
 * Check if a property is custom (user-defined vs schema-defined).
 */
LUSD_API LusdResult lusdPrimIsPropertyCustom(
    LusdPrim    prim,
    const char* pPropertyName,
    bool*       pCustom);

LUSD_EXTERN_C_END

#endif /* LUSD_PROPERTY_H */
