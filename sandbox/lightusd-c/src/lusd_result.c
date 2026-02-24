/*
 * lusd_result.c - Result code to string conversion
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_result.h"

const char* lusdResultToString(LusdResult result) {
    switch (result) {
    case LUSD_SUCCESS:                      return "LUSD_SUCCESS";
    case LUSD_NOT_READY:                    return "LUSD_NOT_READY";
    case LUSD_INCOMPLETE:                   return "LUSD_INCOMPLETE";
    case LUSD_ERROR_INVALID_HANDLE:         return "LUSD_ERROR_INVALID_HANDLE";
    case LUSD_ERROR_OUT_OF_MEMORY:          return "LUSD_ERROR_OUT_OF_MEMORY";
    case LUSD_ERROR_INVALID_ARGUMENT:       return "LUSD_ERROR_INVALID_ARGUMENT";
    case LUSD_ERROR_FILE_NOT_FOUND:         return "LUSD_ERROR_FILE_NOT_FOUND";
    case LUSD_ERROR_PARSE_FAILED:           return "LUSD_ERROR_PARSE_FAILED";
    case LUSD_ERROR_TYPE_MISMATCH:          return "LUSD_ERROR_TYPE_MISMATCH";
    case LUSD_ERROR_NOT_FOUND:              return "LUSD_ERROR_NOT_FOUND";
    case LUSD_ERROR_ALREADY_EXISTS:         return "LUSD_ERROR_ALREADY_EXISTS";
    case LUSD_ERROR_FEATURE_NOT_PRESENT:    return "LUSD_ERROR_FEATURE_NOT_PRESENT";
    case LUSD_ERROR_MEMORY_BUDGET_EXCEEDED: return "LUSD_ERROR_MEMORY_BUDGET_EXCEEDED";
    case LUSD_ERROR_RECURSION_LIMIT:        return "LUSD_ERROR_RECURSION_LIMIT";
    case LUSD_ERROR_INVALID_PATH:           return "LUSD_ERROR_INVALID_PATH";
    case LUSD_ERROR_IO_FAILED:              return "LUSD_ERROR_IO_FAILED";
    case LUSD_ERROR_SCHEMA_VIOLATION:       return "LUSD_ERROR_SCHEMA_VIOLATION";
    case LUSD_ERROR_COMPOSITION_FAILED:     return "LUSD_ERROR_COMPOSITION_FAILED";
    case LUSD_ERROR_USE_AFTER_FREE:         return "LUSD_ERROR_USE_AFTER_FREE";
    case LUSD_ERROR_INVALID_STRUCTURE_TYPE: return "LUSD_ERROR_INVALID_STRUCTURE_TYPE";
    case LUSD_ERROR_UNKNOWN:                return "LUSD_ERROR_UNKNOWN";
    default:                                return "LUSD_ERROR_UNKNOWN";
    }
}
