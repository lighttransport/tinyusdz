/*
 * lusd_instance.h - Global instance management
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_INSTANCE_H
#define LUSD_INSTANCE_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"
#include "lusd_structs.h"
#include "lusd_allocator.h"
#include "lusd_diagnostics.h"

LUSD_EXTERN_C_BEGIN

/*
 * Create a lightusd instance. The instance holds global state (allocator,
 * diagnostics, handle table). All other objects are created through an instance.
 *
 * pCreateInfo: Must have sType = LUSD_STRUCTURE_TYPE_INSTANCE_CREATE_INFO
 * pAllocator:  NULL for default malloc/free
 * pInstance:   Receives the created instance handle
 */
LUSD_API LusdResult lusdCreateInstance(
    const LusdInstanceCreateInfo*    pCreateInfo,
    const LusdAllocationCallbacks*   pAllocator,
    LusdInstance*                    pInstance);

/*
 * Destroy a lightusd instance. All objects created through this instance
 * must be destroyed first.
 */
LUSD_API void lusdDestroyInstance(
    LusdInstance                     instance,
    const LusdAllocationCallbacks*   pAllocator);

/*
 * Set the diagnostic callback for an instance.
 */
LUSD_API LusdResult lusdInstanceSetDiagnosticCallback(
    LusdInstance                     instance,
    PFN_lusdDiagnosticCallback       pfnCallback,
    void*                            pUserData);

/*
 * Get the last error message string. Returns "" if no error.
 * The returned pointer is valid until the next error occurs on this instance.
 */
LUSD_API const char* lusdGetLastError(LusdInstance instance);

LUSD_EXTERN_C_END

#endif /* LUSD_INSTANCE_H */
