/*
 * src/internal/lui_log.h — Library-internal logging macros
 *
 * Private header. Sources under src/ use these macros instead of writing
 * to stderr directly so that callers can install their own log handler
 * via lui_log_set_handler() (declared in <lightui/lightui.h>).
 *
 * The macros expand to lui_log__emit() with a level integer; the public
 * API uses the same level codes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUI_INTERNAL_LOG_H
#define LUI_INTERNAL_LOG_H

#include <lightui/log.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Library-internal emitter — formats with vsnprintf into a stack buffer
 * (max 1024 bytes; longer messages are truncated with a trailing "..."),
 * then dispatches to the user-installed handler (or the stderr default). */
void lui_log__emit(int level, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
;

#define LUI_LOG_ERR(...)  lui_log__emit(LUI_LOG_LEVEL_ERR,  __VA_ARGS__)
#define LUI_LOG_WARN(...) lui_log__emit(LUI_LOG_LEVEL_WARN, __VA_ARGS__)
#define LUI_LOG_INFO(...) lui_log__emit(LUI_LOG_LEVEL_INFO, __VA_ARGS__)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LUI_INTERNAL_LOG_H */
