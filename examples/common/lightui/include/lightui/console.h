/*
 * lightui/console.h — Console / log output widget
 *
 * A scrollable text output area with severity-based coloring,
 * line filtering, and auto-scroll.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_CONSOLE_H
#define LIGHTUI_CONSOLE_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default ring-buffer capacity used by lui_console_init().
 * Use lui_console_init_ex() to pick a different size. */
#define LUI_CONSOLE_MAX_LINES    1024
#define LUI_CONSOLE_MAX_LINE_LEN  255  /* excl. NUL */

/* ---- Severity levels ---------------------------------------------------- */

typedef enum {
    LUI_LOG_INFO    = 0,
    LUI_LOG_WARNING = 1,
    LUI_LOG_ERROR   = 2,
    LUI_LOG_DEBUG   = 3,
} lui_log_level_t;

/* ---- Console line ------------------------------------------------------- */

typedef struct {
    char             text[LUI_CONSOLE_MAX_LINE_LEN + 1];
    int              text_len;
    lui_log_level_t  level;
} lui_console_line_t;

/* ---- Console widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t       widget;

    /* Lines (ring buffer) — heap-allocated, capacity = max_lines. */
    lui_console_line_t *lines;
    int                max_lines;
    int                line_count;   /* total lines added (may wrap)       */
    int                head;         /* ring buffer write position         */

    /* Allocator paired with `lines` — used by destroy. */
    lui_alloc_fn       alloc_fn;
    lui_free_fn        free_fn;
    void              *alloc_user;

    /* Scroll state */
    int                scroll_y;     /* scroll offset in pixels            */
    int                content_height;
    bool               auto_scroll;  /* scroll to bottom on new line       */

    /* Filtering */
    bool               show_info;
    bool               show_warning;
    bool               show_error;
    bool               show_debug;

    /* Appearance */
    int                line_height;  /* height per line (16)               */
    lvg_color_t        bg;
    lvg_color_t        info_color;
    lvg_color_t        warning_color;
    lvg_color_t        error_color;
    lvg_color_t        debug_color;
    lvg_color_t        scrollbar_color;
    int                scrollbar_width;
} lui_console_t;

/* ---- API ---------------------------------------------------------------- */

/**
 * Initialise a console widget with default capacity (LUI_CONSOLE_MAX_LINES)
 * using malloc/free for the ring buffer. Pair with lui_console_destroy().
 * Returns false if the buffer allocation fails.
 */
bool lui_console_init(lui_console_t *con);

/** Initialise with caller-supplied capacity and allocator (NULL/NULL = default). */
bool lui_console_init_ex(lui_console_t *con, int max_lines,
                          lui_alloc_fn alloc_fn,
                          lui_free_fn  free_fn,
                          void        *alloc_user);

/** Free the heap buffer owned by `con`. Safe on a zero-initialised struct. */
void lui_console_destroy(lui_console_t *con);

/** Add a line to the console. */
void lui_console_log(lui_console_t *con, lui_log_level_t level,
                      const char *text);

/** Convenience: log at INFO level. */
void lui_console_info(lui_console_t *con, const char *text);

/** Convenience: log at WARNING level. */
void lui_console_warn(lui_console_t *con, const char *text);

/** Convenience: log at ERROR level. */
void lui_console_error(lui_console_t *con, const char *text);

/** Clear all lines. */
void lui_console_clear(lui_console_t *con);

/** Scroll to the bottom. */
void lui_console_scroll_to_bottom(lui_console_t *con);

/** Get the total number of lines (including wrapped-out lines). */
static inline int lui_console_total_lines(const lui_console_t *con) {
    return con->line_count;
}

/** Get the widget node. */
static inline lui_widget_t *lui_console_widget(lui_console_t *con) {
    return &con->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_CONSOLE_H */
