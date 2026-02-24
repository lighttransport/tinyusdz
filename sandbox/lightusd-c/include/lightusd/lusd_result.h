/*
 * lusd_result.h - Result codes for all fallible operations
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_RESULT_H
#define LUSD_RESULT_H

#include "lusd_platform.h"

LUSD_EXTERN_C_BEGIN

typedef enum LusdResult {
    /* Success */
    LUSD_SUCCESS                        =  0,
    /* Informational (positive) */
    LUSD_NOT_READY                      =  1,
    LUSD_INCOMPLETE                     =  2,

    /* Errors (negative) */
    LUSD_ERROR_INVALID_HANDLE           = -1,
    LUSD_ERROR_OUT_OF_MEMORY            = -2,
    LUSD_ERROR_INVALID_ARGUMENT         = -3,
    LUSD_ERROR_FILE_NOT_FOUND           = -4,
    LUSD_ERROR_PARSE_FAILED             = -5,
    LUSD_ERROR_TYPE_MISMATCH            = -6,
    LUSD_ERROR_NOT_FOUND                = -7,
    LUSD_ERROR_ALREADY_EXISTS           = -8,
    LUSD_ERROR_FEATURE_NOT_PRESENT      = -9,
    LUSD_ERROR_MEMORY_BUDGET_EXCEEDED   = -10,
    LUSD_ERROR_RECURSION_LIMIT          = -11,
    LUSD_ERROR_INVALID_PATH             = -12,
    LUSD_ERROR_IO_FAILED                = -13,
    LUSD_ERROR_SCHEMA_VIOLATION         = -14,
    LUSD_ERROR_COMPOSITION_FAILED       = -15,
    LUSD_ERROR_USE_AFTER_FREE           = -16,
    LUSD_ERROR_INVALID_STRUCTURE_TYPE   = -17,
    LUSD_ERROR_UNKNOWN                  = -1000
} LusdResult;

/* Convert result code to human-readable string */
LUSD_API const char* lusdResultToString(LusdResult result);

LUSD_EXTERN_C_END

#endif /* LUSD_RESULT_H */
