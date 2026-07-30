/*
 * src/internal/lui_log.c — Logger implementation
 *
 * Kept in its own TU so it can be linked into headless test binaries
 * without dragging in the platform layer.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "lui_log.h"

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>

static void lui__default_log(int level, const char *msg, void *ud)
{
    (void)ud;
    const char *tag = (level >= LUI_LOG_LEVEL_ERR)  ? "error"
                    : (level >= LUI_LOG_LEVEL_WARN) ? "warn"
                                                    : "info";
    fprintf(stderr, "lightui[%s]: %s\n", tag, msg);
}

static lui_log_fn  lui__log_handler = lui__default_log;
static void       *lui__log_userdata = NULL;

void lui_log_set_handler(lui_log_fn handler, void *userdata)
{
    lui__log_handler  = handler ? handler : lui__default_log;
    lui__log_userdata = handler ? userdata : NULL;
}

/* Library-internal emitter — keeps a 1 KB stack buffer; longer lines get a
 * "..." truncation tail so silent loss is visible to the reader. */
void lui_log__emit(int level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if ((size_t)n >= sizeof(buf)) {
        buf[sizeof(buf) - 4] = '.';
        buf[sizeof(buf) - 3] = '.';
        buf[sizeof(buf) - 2] = '.';
        buf[sizeof(buf) - 1] = '\0';
    }
    lui__log_handler(level, buf, lui__log_userdata);
}
