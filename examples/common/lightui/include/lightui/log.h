/*
 * lightui/log.h — Logger hook for the library
 *
 * Tiny standalone header (no transitive widget dependencies) so that
 * library-internal sources can include it without dragging in font /
 * widget headers. The full <lightui/lightui.h> umbrella re-exports the
 * same symbols.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_LOG_H
#define LIGHTUI_LOG_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Log levels. lightui only emits LUI_LOG_LEVEL_INFO and above; negative
 * values are reserved for the host application.
 */
enum {
    LUI_LOG_LEVEL_INFO  = 0,
    LUI_LOG_LEVEL_WARN  = 1,
    LUI_LOG_LEVEL_ERR   = 2,
};

/**
 * User log handler. `msg` is a NUL-terminated, already-formatted line
 * (without a trailing newline); the host adds its own formatting.
 * `userdata` is the cookie passed to set_handler.
 */
typedef void (*lui_log_fn)(int level, const char *msg, void *userdata);

/**
 * Install a logger. Pass NULL to restore the default handler, which
 * writes to stderr with a "lightui[level]: " prefix and a trailing
 * newline. Thread-safety: the handler is read by every TU that emits;
 * install it once at startup before launching threads.
 */
void lui_log_set_handler(lui_log_fn handler, void *userdata);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_LOG_H */
