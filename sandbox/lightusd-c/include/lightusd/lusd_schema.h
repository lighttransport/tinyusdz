/*
 * lusd_schema.h - Schema validation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_SCHEMA_H
#define LUSD_SCHEMA_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"

LUSD_EXTERN_C_BEGIN

/*
 * Validate a prim against its declared schema.
 * Returns LUSD_SUCCESS if valid, LUSD_ERROR_SCHEMA_VIOLATION with
 * diagnostic messages otherwise.
 */
LUSD_API LusdResult lusdPrimValidateSchema(
    LusdPrim    prim);

/*
 * Validate an entire stage.
 */
LUSD_API LusdResult lusdStageValidateSchema(
    LusdStage   stage);

LUSD_EXTERN_C_END

#endif /* LUSD_SCHEMA_H */
