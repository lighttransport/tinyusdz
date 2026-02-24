/*
 * lusd_token.h - Interned string tokens
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_TOKEN_H
#define LUSD_TOKEN_H

#include "lusd_platform.h"
#include "lusd_result.h"
#include "lusd_handles.h"

LUSD_EXTERN_C_BEGIN

/*
 * Create (or retrieve) an interned token. Tokens are owned by the instance
 * and live until the instance is destroyed. Two tokens with the same string
 * are pointer-equal.
 */
LUSD_API LusdResult lusdCreateToken(
    LusdInstance  instance,
    const char*   pText,
    LusdToken*    pToken);

/*
 * Get the null-terminated string for a token.
 * The returned pointer is valid for the lifetime of the instance.
 */
LUSD_API const char* lusdTokenGetText(LusdToken token);

/*
 * Compare two tokens for equality. O(1) pointer comparison.
 */
LUSD_API bool lusdTokenEqual(LusdToken a, LusdToken b);

/*
 * Get a hash value for a token. Useful for user hash tables.
 */
LUSD_API uint64_t lusdTokenHash(LusdToken token);

LUSD_EXTERN_C_END

#endif /* LUSD_TOKEN_H */
