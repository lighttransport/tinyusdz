/*
 * lusd_instance.c - Instance create/destroy, diagnostics, error state
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lightusd/lusd_instance.h"
#include "lightusd/lusd_version.h"
#include "internal/lusd_internal.h"
#include <string.h>
#include <stdio.h>
#include <stdarg.h>


void lusd_set_error(struct LusdInstance_T* inst, const char* msg) {
    if (!inst) return;
    if (msg) {
        size_t len = strlen(msg);
        if (len >= sizeof(inst->lastError)) {
            len = sizeof(inst->lastError) - 1;
        }
        memcpy(inst->lastError, msg, len);
        inst->lastError[len] = '\0';
    } else {
        inst->lastError[0] = '\0';
    }
}

void lusd_set_errorf(struct LusdInstance_T* inst, const char* fmt, ...) {
    if (!inst || !fmt) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(inst->lastError, sizeof(inst->lastError), fmt, args);
    va_end(args);
}

void lusd_diag(struct LusdInstance_T* inst, LusdDiagnosticSeverity sev, const char* msg) {
    if (!inst || !inst->diagCallback) return;
    inst->diagCallback(sev, msg, inst->diagUserData);
}

LusdResult lusdCreateInstance(
    const LusdInstanceCreateInfo*  pCreateInfo,
    const LusdAllocationCallbacks* pAllocator,
    LusdInstance*                  pInstance)
{
    if (!pCreateInfo || !pInstance) return LUSD_ERROR_INVALID_ARGUMENT;
    if (pCreateInfo->sType != LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO) {
        return LUSD_ERROR_INVALID_STRUCTURE_TYPE;
    }

    struct LusdInstance_T* inst = (struct LusdInstance_T*)lusd_alloc(
        pAllocator, sizeof(struct LusdInstance_T), sizeof(void*));
    if (!inst) return LUSD_ERROR_OUT_OF_MEMORY;

    memset(inst, 0, sizeof(*inst));

    /* Store the allocator */
    if (pAllocator) {
        inst->alloc = *pAllocator;
    }
    /* If pAllocator is NULL, alloc is zeroed -> lusd_alloc/free will use defaults */

    inst->apiVersion = pCreateInfo->apiVersion;

    /* Initialize handle table */
    LusdResult res = lusd_handle_table_init(&inst->handleTable, 256,
        pAllocator ? &inst->alloc : NULL);
    if (res != LUSD_SUCCESS) {
        lusd_free(pAllocator, inst);
        return res;
    }

    /* Initialize token pool */
    res = lusd_token_pool_init(&inst->tokenPool,
        pAllocator ? &inst->alloc : NULL);
    if (res != LUSD_SUCCESS) {
        lusd_handle_table_destroy(&inst->handleTable);
        lusd_free(pAllocator, inst);
        return res;
    }

    *pInstance = inst;
    return LUSD_SUCCESS;
}

void lusdDestroyInstance(
    LusdInstance                   instance,
    const LusdAllocationCallbacks* pAllocator)
{
    if (!instance) return;

    struct LusdInstance_T* inst = instance;

    /* Destroy token pool */
    lusd_token_pool_destroy(&inst->tokenPool);

    /* Destroy handle table */
    lusd_handle_table_destroy(&inst->handleTable);

    /* Free the instance itself */
    lusd_free(pAllocator, inst);
}

LusdResult lusdInstanceSetDiagnosticCallback(
    LusdInstance                 instance,
    PFN_lusdDiagnosticCallback   pfnCallback,
    void*                        pUserData)
{
    if (!instance) return LUSD_ERROR_INVALID_HANDLE;
    instance->diagCallback = pfnCallback;
    instance->diagUserData = pUserData;
    return LUSD_SUCCESS;
}

const char* lusdGetLastError(LusdInstance instance) {
    if (!instance) return "";
    return instance->lastError;
}
