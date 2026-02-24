/*
 * lusd_diagnostics.h - Diagnostic callback types
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_DIAGNOSTICS_H
#define LUSD_DIAGNOSTICS_H

#include "lusd_platform.h"
#include "lusd_enums.h"

LUSD_EXTERN_C_BEGIN

/*
 * Diagnostic callback. Called by the library when warnings/errors occur.
 *
 * severity:  The severity level
 * pMessage:  Null-terminated message string (valid only during callback)
 * pUserData: User data pointer passed to lusdInstanceSetDiagnosticCallback
 */
typedef void (*PFN_lusdDiagnosticCallback)(
    LusdDiagnosticSeverity  severity,
    const char*             pMessage,
    void*                   pUserData);

LUSD_EXTERN_C_END

#endif /* LUSD_DIAGNOSTICS_H */
