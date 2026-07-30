/*
 * filebrowser.c — Virtual file/directory browser widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/filebrowser.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define PATH_BAR_HEIGHT  24
#define ICON_WIDTH       16
#define TEXT_LEFT_PAD     4
#define DOUBLE_CLICK_MS   0.4f   /* 400ms threshold */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int fb_visible_count(const lui_filebrowser_t *fb, int widget_h)
{
    int list_h = widget_h - PATH_BAR_HEIGHT;
    if (list_h <= 0) return 0;
    return list_h / fb->item_height;
}

/* Compare two entries for sorting. */
static int entry_cmp_name(const void *a, const void *b)
{
    const lui_fb_entry_t *ea = (const lui_fb_entry_t *)a;
    const lui_fb_entry_t *eb = (const lui_fb_entry_t *)b;
    return strcmp(ea->name, eb->name);
}

static int entry_cmp_type(const void *a, const void *b)
{
    const lui_fb_entry_t *ea = (const lui_fb_entry_t *)a;
    const lui_fb_entry_t *eb = (const lui_fb_entry_t *)b;
    /* Directories first */
    if (ea->type != eb->type)
        return ea->type == LUI_ENTRY_DIRECTORY ? -1 : 1;
    return strcmp(ea->name, eb->name);
}

/* Simple qsort (use stdlib) */
#include <stdlib.h>

/* Draw a folder icon: small filled triangle + rect */
static void draw_folder_icon(lvg_canvas_t *canvas, int x, int y,
                              lvg_color_t color)
{
    /* Tab on top-left of folder */
    lvg_canvas_fill_rect(canvas, x, y + 2, 6, 2, color);
    /* Folder body */
    lvg_canvas_fill_rect(canvas, x, y + 4, 12, 8, color);
}

/* Draw a document icon: small rect with corner */
static void draw_file_icon(lvg_canvas_t *canvas, int x, int y,
                             lvg_color_t color)
{
    lvg_canvas_fill_rect(canvas, x + 1, y + 1, 9, 11, color);
    lvg_canvas_stroke_rect(canvas, x + 1, y + 1, 9, 11, color, 1);
    /* Corner fold */
    lvg_canvas_fill_rect(canvas, x + 7, y + 1, 3, 3, color);
}

/* Draw text as 5x10 character rectangles with 7px advance. */
static void draw_text(lvg_canvas_t *canvas, int x, int y,
                       const char *text, int max_x, lvg_color_t color)
{
    for (int i = 0; text[i] && x + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int fb_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)user;
    const lui_filebrowser_t *fb = (const lui_filebrowser_t *)w;
    *out_w = 260;
    *out_h = PATH_BAR_HEIGHT + fb->item_height * 8;
    return 0;
}

static void fb_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_filebrowser_t *fb = (lui_filebrowser_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, fb->bg_color);
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            fb->border_color, 1);

    /* Path bar */
    lvg_canvas_fill_rect(canvas, r.x + 1, r.y + 1,
                          r.width - 2, PATH_BAR_HEIGHT - 1, fb->path_bg);
    {
        int tx = r.x + 6;
        int ty = r.y + (PATH_BAR_HEIGHT - 10) / 2;
        draw_text(canvas, tx, ty, fb->current_path,
                  r.x + r.width - 6, fb->text_color);
    }

    /* Separator below path bar */
    lvg_canvas_fill_rect(canvas, r.x, r.y + PATH_BAR_HEIGHT,
                          r.width, 1, fb->border_color);

    /* Item list */
    int list_top = r.y + PATH_BAR_HEIGHT + 1;
    int visible  = fb_visible_count(fb, r.height);

    for (int i = 0; i < visible; i++) {
        int idx = fb->scroll_offset + i;
        if (idx >= fb->entry_count) break;

        const lui_fb_entry_t *entry = &fb->entries[idx];
        int iy = list_top + i * fb->item_height;

        /* Row background */
        lvg_color_t row_bg = fb->item_bg;
        if (entry->selected)
            row_bg = fb->item_selected_bg;
        else if (idx == fb->hovered)
            row_bg = fb->item_hover_bg;

        lvg_canvas_fill_rect(canvas, r.x + 1, iy,
                              r.width - 2, fb->item_height, row_bg);

        /* Icon */
        int icon_x = r.x + 6;
        int icon_y = iy + (fb->item_height - 12) / 2;
        if (entry->type == LUI_ENTRY_DIRECTORY)
            draw_folder_icon(canvas, icon_x, icon_y, fb->dir_color);
        else
            draw_file_icon(canvas, icon_x, icon_y, fb->text_color);

        /* Name text */
        int tx = r.x + 6 + ICON_WIDTH + TEXT_LEFT_PAD;
        int ty = iy + (fb->item_height - 10) / 2;
        lvg_color_t tc = (entry->type == LUI_ENTRY_DIRECTORY)
                       ? fb->dir_color : fb->text_color;
        draw_text(canvas, tx, ty, entry->name, r.x + r.width - 6, tc);
    }
}

static int fb_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_filebrowser_t *fb = (lui_filebrowser_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    int list_top = r.y + PATH_BAR_HEIGHT + 1;

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        if (!lvg_rect_contains_point(&r, mx, my)) {
            fb->hovered = -1;
            return 0;
        }
        if (my < list_top) {
            fb->hovered = -1;
            return 1;
        }
        int rel = (my - list_top) / fb->item_height;
        int idx = fb->scroll_offset + rel;
        fb->hovered = (idx >= 0 && idx < fb->entry_count) ? idx : -1;
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&r, mx, my))
            return 0;
        if (my < list_top)
            return 1;

        int rel = (my - list_top) / fb->item_height;
        int idx = fb->scroll_offset + rel;
        if (idx < 0 || idx >= fb->entry_count)
            return 1;

        /* Deselect all, select clicked */
        for (int i = 0; i < fb->entry_count; i++)
            fb->entries[i].selected = false;
        fb->entries[idx].selected = true;

        /* Double-click detection */
        if (idx == fb->last_click_index && fb->click_time < DOUBLE_CLICK_MS) {
            /* Double-click: OPEN action */
            if (fb->on_action)
                fb->on_action(idx, LUI_FB_OPEN, fb->on_action_user);
            fb->last_click_index = -1;
            fb->click_time = DOUBLE_CLICK_MS + 1.0f;
        } else {
            fb->last_click_index = idx;
            fb->click_time = 0.0f;
            /* Single click: SELECT action */
            if (fb->on_action)
                fb->on_action(idx, LUI_FB_SELECT, fb->on_action_user);
        }
        return 1;
    }

    if (event->type == LUI_EVENT_SCROLL) {
        int mx = event->data.scroll.x;
        int my = event->data.scroll.y;
        if (!lvg_rect_contains_point(&r, mx, my))
            return 0;
        int visible = fb_visible_count(fb, r.height);
        int max_scroll = fb->entry_count - visible;
        if (max_scroll < 0) max_scroll = 0;

        int delta = event->data.scroll.delta_y > 0 ? 1 : -1;
        fb->scroll_offset += delta;
        if (fb->scroll_offset < 0) fb->scroll_offset = 0;
        if (fb->scroll_offset > max_scroll) fb->scroll_offset = max_scroll;
        return 1;
    }

    return 0;
}

static bool fb_animate(lui_widget_t *w, float dt)
{
    lui_filebrowser_t *fb = (lui_filebrowser_t *)w;
    /* Advance double-click timer */
    fb->click_time += dt;
    return false;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *fb_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  fb_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_filebrowser_init_ex(lui_filebrowser_t *fb, int max_entries,
                              lui_alloc_fn alloc_fn,
                              lui_free_fn  free_fn,
                              void        *alloc_user)
{
    if (!fb || max_entries <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = fb_default_alloc;
        free_fn    = fb_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_entries * sizeof(lui_fb_entry_t);
    if (bytes / sizeof(lui_fb_entry_t) != (size_t)max_entries) return false;
    lui_fb_entry_t *entries = (lui_fb_entry_t *)alloc_fn(alloc_user, bytes);
    if (!entries) return false;
    memset(entries, 0, bytes);

    memset(fb, 0, sizeof(*fb));
    fb->entries     = entries;
    fb->max_entries = max_entries;
    fb->alloc_fn    = alloc_fn;
    fb->free_fn     = free_fn;
    fb->alloc_user  = alloc_user;

    lui_widget_init(&fb->widget);
    fb->widget.width    = lvg_size_hug(260);
    fb->widget.height   = lvg_size_hug(0);
    fb->widget.measure  = fb_measure;
    fb->widget.draw     = fb_draw;
    fb->widget.on_event = fb_event;
    fb->widget.animate  = fb_animate;
    fb->widget.flags    = LUI_WIDGET_ANIMATING;

    fb->entry_count     = 0;
    fb->current_path[0] = '\0';
    fb->item_height     = 24;
    fb->scroll_offset   = 0;
    fb->hovered         = -1;
    fb->last_click_index = -1;
    fb->click_time      = DOUBLE_CLICK_MS + 1.0f;
    fb->sort_mode       = LUI_FB_SORT_NAME;

    fb->bg_color        = LVG_COLOR_RGB(0x28, 0x2B, 0x30);
    fb->item_bg         = LVG_COLOR_RGB(0x28, 0x2B, 0x30);
    fb->item_hover_bg   = LVG_COLOR_RGB(0x38, 0x3C, 0x44);
    fb->item_selected_bg = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    fb->text_color      = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    fb->dir_color       = LVG_COLOR_RGB(0x6E, 0xB5, 0xF0);
    fb->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    fb->path_bg         = LVG_COLOR_RGB(0x32, 0x36, 0x3C);

    fb->on_action       = NULL;
    fb->on_action_user  = NULL;

    return true;
}

bool lui_filebrowser_init(lui_filebrowser_t *fb)
{
    return lui_filebrowser_init_ex(fb, LUI_FILEBROWSER_MAX, NULL, NULL, NULL);
}

void lui_filebrowser_destroy(lui_filebrowser_t *fb)
{
    if (!fb) return;
    if (fb->free_fn && fb->entries)
        fb->free_fn(fb->alloc_user, fb->entries);
    fb->entries     = NULL;
    fb->entry_count = 0;
    fb->max_entries = 0;
}

void lui_filebrowser_set_path(lui_filebrowser_t *fb, const char *path)
{
    if (!fb || !path) return;
    int len = (int)strlen(path);
    if (len > 255) len = 255;
    memcpy(fb->current_path, path, len);
    fb->current_path[len] = '\0';
}

int lui_filebrowser_add_entry(lui_filebrowser_t *fb, const char *name,
                               lui_entry_type_t type, uint64_t size)
{
    if (!fb || !name || fb->entry_count >= fb->max_entries)
        return -1;

    int idx = fb->entry_count;
    lui_fb_entry_t *e = &fb->entries[idx];
    int len = (int)strlen(name);
    if (len > 63) len = 63;
    memcpy(e->name, name, len);
    e->name[len] = '\0';
    e->type     = type;
    e->size     = size;
    e->selected = false;
    fb->entry_count++;
    return idx;
}

void lui_filebrowser_clear(lui_filebrowser_t *fb)
{
    if (!fb) return;
    fb->entry_count   = 0;
    fb->scroll_offset = 0;
    fb->hovered       = -1;
}

int lui_filebrowser_get_selected(const lui_filebrowser_t *fb)
{
    if (!fb) return -1;
    for (int i = 0; i < fb->entry_count; i++) {
        if (fb->entries[i].selected)
            return i;
    }
    return -1;
}

void lui_filebrowser_sort(lui_filebrowser_t *fb)
{
    if (!fb || fb->entry_count <= 1) return;
    if (fb->sort_mode == LUI_FB_SORT_TYPE)
        qsort(fb->entries, fb->entry_count, sizeof(lui_fb_entry_t),
              entry_cmp_type);
    else
        qsort(fb->entries, fb->entry_count, sizeof(lui_fb_entry_t),
              entry_cmp_name);
}
