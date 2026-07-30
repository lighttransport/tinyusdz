/*
 * console.c — Console / log output widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/console.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Get the actual line at visual index (accounting for ring buffer). */
static const lui_console_line_t *get_line(const lui_console_t *con, int index)
{
    int total = con->line_count < con->max_lines
              ? con->line_count : con->max_lines;
    if (index < 0 || index >= total) return NULL;

    int start;
    if (con->line_count <= con->max_lines)
        start = 0;
    else
        start = con->head;

    int actual = (start + index) % con->max_lines;
    return &con->lines[actual];
}

static bool line_visible(const lui_console_t *con,
                          const lui_console_line_t *line)
{
    switch (line->level) {
    case LUI_LOG_INFO:    return con->show_info;
    case LUI_LOG_WARNING: return con->show_warning;
    case LUI_LOG_ERROR:   return con->show_error;
    case LUI_LOG_DEBUG:   return con->show_debug;
    default:              return true;
    }
}

static int visible_line_count(const lui_console_t *con)
{
    int total = con->line_count < con->max_lines
              ? con->line_count : con->max_lines;
    int count = 0;
    for (int i = 0; i < total; i++) {
        const lui_console_line_t *line = get_line(con, i);
        if (line && line_visible(con, line)) count++;
    }
    return count;
}

static lvg_color_t level_color(const lui_console_t *con,
                                lui_log_level_t level)
{
    switch (level) {
    case LUI_LOG_INFO:    return con->info_color;
    case LUI_LOG_WARNING: return con->warning_color;
    case LUI_LOG_ERROR:   return con->error_color;
    case LUI_LOG_DEBUG:   return con->debug_color;
    default:              return con->info_color;
    }
}

/* Level prefix tag. */
static const char *level_tag(lui_log_level_t level)
{
    switch (level) {
    case LUI_LOG_INFO:    return "I";
    case LUI_LOG_WARNING: return "W";
    case LUI_LOG_ERROR:   return "E";
    case LUI_LOG_DEBUG:   return "D";
    default:              return "?";
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int con_measure(const lui_widget_t *w, int *out_w, int *out_h,
                        void *user)
{
    (void)w; (void)user;
    *out_w = 300;
    *out_h = 200;
    return 0;
}

static void con_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_console_t *con = (lui_console_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, con->bg);

    /* Clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    int vis_count = visible_line_count(con);
    con->content_height = vis_count * con->line_height;

    /* Clamp scroll */
    int max_scroll = con->content_height - r.height;
    if (max_scroll < 0) max_scroll = 0;
    if (con->scroll_y < 0) con->scroll_y = 0;
    if (con->scroll_y > max_scroll) con->scroll_y = max_scroll;

    /* Draw lines */
    int total = con->line_count < con->max_lines
              ? con->line_count : con->max_lines;
    int y = r.y - con->scroll_y;
    for (int i = 0; i < total; i++) {
        const lui_console_line_t *line = get_line(con, i);
        if (!line || !line_visible(con, line)) continue;

        if (y + con->line_height > r.y && y < r.y + r.height) {
            lvg_color_t tc = level_color(con, line->level);
            int tx = r.x + 4;
            int ty = y + (con->line_height - 10) / 2;

            /* Level indicator */
            const char *tag = level_tag(line->level);
            (void)tag;
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, tc);
            tx += 10;

            /* Text (char rectangles) */
            int max_x = r.x + r.width - con->scrollbar_width - 4;
            for (int c = 0; c < line->text_len && tx < max_x; c++) {
                lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, tc);
                tx += 7;
            }
        }
        y += con->line_height;
    }

    /* Scrollbar */
    if (con->content_height > r.height) {
        int sb_x = r.x + r.width - con->scrollbar_width;
        int track_h = r.height;
        int thumb_h = (r.height * r.height) / con->content_height;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = r.y;
        if (max_scroll > 0)
            thumb_y += (con->scroll_y * (track_h - thumb_h)) / max_scroll;
        lvg_canvas_fill_rounded_rect(canvas, sb_x, thumb_y,
                                      con->scrollbar_width, thumb_h,
                                      con->scrollbar_width / 2,
                                      con->scrollbar_color);
    }

    canvas->_clip = old_clip;
}

static int con_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_console_t *con = (lui_console_t *)w;

    if (event->type == LUI_EVENT_SCROLL) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.scroll.x, event->data.scroll.y)) {
            int delta = (int)(event->data.scroll.delta_y * -3.0f)
                      * con->line_height;
            con->scroll_y += delta;
            int max_scroll = con->content_height - r.height;
            if (max_scroll < 0) max_scroll = 0;
            if (con->scroll_y < 0) con->scroll_y = 0;
            if (con->scroll_y > max_scroll) con->scroll_y = max_scroll;
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/* Default allocator — passes through to stdlib malloc/free. */
static void *console_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  console_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_console_init_ex(lui_console_t *con, int max_lines,
                          lui_alloc_fn alloc_fn,
                          lui_free_fn  free_fn,
                          void        *alloc_user)
{
    if (!con || max_lines <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = console_default_alloc;
        free_fn    = console_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_lines * sizeof(lui_console_line_t);
    if (bytes / sizeof(lui_console_line_t) != (size_t)max_lines) return false;

    lui_console_line_t *lines = (lui_console_line_t *)alloc_fn(alloc_user, bytes);
    if (!lines) return false;
    memset(lines, 0, bytes);

    memset(con, 0, sizeof(*con));
    con->lines      = lines;
    con->max_lines  = max_lines;
    con->alloc_fn   = alloc_fn;
    con->free_fn    = free_fn;
    con->alloc_user = alloc_user;

    lui_widget_init(&con->widget);
    con->widget.width   = lvg_size_fill(1);
    con->widget.height  = lvg_size_fill(1);
    con->widget.measure = con_measure;
    con->widget.draw    = con_draw;
    con->widget.on_event = con_event;
    con->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    con->line_count    = 0;
    con->head          = 0;
    con->scroll_y      = 0;
    con->content_height = 0;
    con->auto_scroll   = true;

    con->show_info     = true;
    con->show_warning  = true;
    con->show_error    = true;
    con->show_debug    = true;

    con->line_height   = 16;
    con->bg            = LVG_COLOR_RGB(0x14, 0x14, 0x1E);
    con->info_color    = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    con->warning_color = LVG_COLOR_RGB(0xE0, 0xC0, 0x40);
    con->error_color   = LVG_COLOR_RGB(0xE0, 0x50, 0x50);
    con->debug_color   = LVG_COLOR_RGB(0x60, 0x90, 0xC0);
    con->scrollbar_color = LVG_COLOR_ARGB(0x80, 0x60, 0x64, 0x6C);
    con->scrollbar_width = 6;

    return true;
}

bool lui_console_init(lui_console_t *con)
{
    return lui_console_init_ex(con, LUI_CONSOLE_MAX_LINES, NULL, NULL, NULL);
}

void lui_console_destroy(lui_console_t *con)
{
    if (!con) return;
    if (con->free_fn && con->lines)
        con->free_fn(con->alloc_user, con->lines);
    con->lines     = NULL;
    con->max_lines = 0;
    con->line_count = 0;
}

void lui_console_log(lui_console_t *con, lui_log_level_t level,
                      const char *text)
{
    if (!con || !text) return;

    lui_console_line_t *line = &con->lines[con->head];
    line->level = level;

    int len = (int)strlen(text);
    if (len > LUI_CONSOLE_MAX_LINE_LEN) len = LUI_CONSOLE_MAX_LINE_LEN;
    memcpy(line->text, text, len);
    line->text[len] = '\0';
    line->text_len = len;

    con->head = (con->head + 1) % con->max_lines;
    con->line_count++;

    if (con->auto_scroll) {
        int vis_count = visible_line_count(con);
        con->content_height = vis_count * con->line_height;
        lvg_rect_t r = lui_widget_absolute_rect(&con->widget);
        int view_h = r.height > 0 ? r.height : 200;
        con->scroll_y = con->content_height - view_h;
        if (con->scroll_y < 0) con->scroll_y = 0;
    }
}

void lui_console_info(lui_console_t *con, const char *text)
{
    lui_console_log(con, LUI_LOG_INFO, text);
}

void lui_console_warn(lui_console_t *con, const char *text)
{
    lui_console_log(con, LUI_LOG_WARNING, text);
}

void lui_console_error(lui_console_t *con, const char *text)
{
    lui_console_log(con, LUI_LOG_ERROR, text);
}

void lui_console_clear(lui_console_t *con)
{
    if (!con) return;
    con->line_count = 0;
    con->head = 0;
    con->scroll_y = 0;
    con->content_height = 0;
}

void lui_console_scroll_to_bottom(lui_console_t *con)
{
    if (!con) return;
    int vis_count = visible_line_count(con);
    con->content_height = vis_count * con->line_height;
    lvg_rect_t r = lui_widget_absolute_rect(&con->widget);
    int view_h = r.height > 0 ? r.height : 200;
    con->scroll_y = con->content_height - view_h;
    if (con->scroll_y < 0) con->scroll_y = 0;
}
