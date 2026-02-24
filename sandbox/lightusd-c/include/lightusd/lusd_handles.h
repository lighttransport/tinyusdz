/*
 * lusd_handles.h - Opaque handle type definitions
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_HANDLES_H
#define LUSD_HANDLES_H

#include "lusd_platform.h"

/*
 * All handles are opaque pointers to internal *_T structs.
 * Handles from lusdCreate* are owning (caller must destroy).
 * Handles from Stage queries are non-owning views.
 */

#define LUSD_DEFINE_HANDLE(name) typedef struct name##_T* name

LUSD_DEFINE_HANDLE(LusdInstance);
LUSD_DEFINE_HANDLE(LusdStage);
LUSD_DEFINE_HANDLE(LusdPrim);
LUSD_DEFINE_HANDLE(LusdLayer);
LUSD_DEFINE_HANDLE(LusdValue);
LUSD_DEFINE_HANDLE(LusdPath);
LUSD_DEFINE_HANDLE(LusdToken);
LUSD_DEFINE_HANDLE(LusdTimeSamples);
LUSD_DEFINE_HANDLE(LusdArena);
LUSD_DEFINE_HANDLE(LusdWriter);
LUSD_DEFINE_HANDLE(LusdStreamLoader);

/* Null handle sentinel */
#define LUSD_NULL_HANDLE NULL

#endif /* LUSD_HANDLES_H */
