/*
 * propgrid.c -- Key-value property grid widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/propgrid.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline float pg_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline int pg_clampi(int v, int lo, int hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* Draw a string as 5x10 filled rectangles with 7px advance. */
static void pg_draw_text(lvg_canvas_t *canvas, int x, int y,
                         const char *text, lvg_color_t color, int max_x)
{
    if (!text) return;
    for (int i = 0; text[i] && x + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* Format a float into a short buffer. */
static void pg_fmt_float(char *buf, int sz, float v)
{
    int whole = (int)v;
    int frac  = (int)((v - (float)whole) * 100.0f);
    if (frac < 0) frac = -frac;
    if (v < 0.0f && whole == 0) {
        /* e.g. -0.5 */
        buf[0] = '-'; buf[1] = '0'; buf[2] = '.';
        buf[3] = (char)('0' + frac / 10);
        buf[4] = (char)('0' + frac % 10);
        buf[5] = '\0';
    } else {
        /* simple integer.fraction */
        int neg = 0;
        int pos = 0;
        if (whole < 0) { buf[pos++] = '-'; whole = -whole; neg = 1; }
        /* digits of whole */
        char tmp[16];
        int n = 0;
        if (whole == 0) { tmp[n++] = '0'; }
        else { while (whole > 0 && n < 14) { tmp[n++] = (char)('0' + whole % 10); whole /= 10; } }
        for (int i = n - 1; i >= 0 && pos < sz - 4; i--) buf[pos++] = tmp[i];
        buf[pos++] = '.';
        buf[pos++] = (char)('0' + frac / 10);
        buf[pos++] = (char)('0' + frac % 10);
        buf[pos] = '\0';
        (void)neg;
    }
}

static void pg_fmt_int(char *buf, int sz, int v)
{
    int pos = 0;
    if (v < 0) { buf[pos++] = '-'; v = -v; }
    char tmp[16];
    int n = 0;
    if (v == 0) { tmp[n++] = '0'; }
    else { while (v > 0 && n < 14) { tmp[n++] = (char)('0' + v % 10); v /= 10; } }
    for (int i = n - 1; i >= 0 && pos < sz - 1; i--) buf[pos++] = tmp[i];
    buf[pos] = '\0';
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int propgrid_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_propgrid_t *pg = (const lui_propgrid_t *)w;
    (void)user;
    *out_w = 240;
    *out_h = pg->prop_count * pg->row_height;
    return 0;
}

static void propgrid_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_propgrid_t *pg = (lui_propgrid_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, pg->bg_color);

    int name_w = pg->name_width;
    int val_x  = r.x + name_w;
    int val_w  = r.width - name_w;

    for (int i = 0; i < pg->prop_count; i++) {
        int row_y = r.y + i * pg->row_height - pg->scroll_offset;
        if (row_y + pg->row_height < r.y || row_y > r.y + r.height)
            continue;

        lui_prop_t *p = &pg->props[i];
        int text_y = row_y + (pg->row_height - 10) / 2;

        if (p->type == LUI_PROP_SEPARATOR) {
            /* Full-width separator header */
            lvg_canvas_fill_rect(canvas, r.x, row_y, r.width, pg->row_height,
                                  pg->separator_color);
            pg_draw_text(canvas, r.x + 4, text_y, p->name, pg->text_color,
                         r.x + r.width);
            continue;
        }

        /* Name column */
        lvg_canvas_fill_rect(canvas, r.x, row_y, name_w, pg->row_height,
                              pg->name_bg);
        pg_draw_text(canvas, r.x + 4, text_y, p->name, pg->text_color,
                     r.x + name_w - 2);

        /* Value column background */
        lvg_canvas_fill_rect(canvas, val_x, row_y, val_w, pg->row_height,
                              pg->value_bg);

        /* Value visualisation */
        switch (p->type) {
        case LUI_PROP_FLOAT: {
            char buf[24];
            pg_fmt_float(buf, sizeof(buf), p->value.f);
            pg_draw_text(canvas, val_x + 4, text_y, buf, pg->text_color,
                         r.x + r.width - 2);
            break;
        }
        case LUI_PROP_INT: {
            char buf[24];
            pg_fmt_int(buf, sizeof(buf), p->value.i);
            pg_draw_text(canvas, val_x + 4, text_y, buf, pg->text_color,
                         r.x + r.width - 2);
            break;
        }
        case LUI_PROP_BOOL: {
            /* Checkbox: 12x12 box */
            int bx = val_x + 4;
            int by = row_y + (pg->row_height - 12) / 2;
            lvg_color_t bc = p->value.b ? pg->bool_true_color
                                        : pg->bool_false_color;
            lvg_canvas_fill_rect(canvas, bx, by, 12, 12, bc);
            lvg_canvas_stroke_rect(canvas, bx, by, 12, 12,
                                    pg->border_color, 1);
            if (p->value.b) {
                /* checkmark: two small lines */
                lvg_canvas_draw_line(canvas, bx + 2, by + 6,
                                      bx + 5, by + 9, pg->text_color, 1);
                lvg_canvas_draw_line(canvas, bx + 5, by + 9,
                                      bx + 10, by + 2, pg->text_color, 1);
            }
            break;
        }
        case LUI_PROP_COLOR: {
            /* Colour swatch */
            int sx = val_x + 4;
            int sy = row_y + (pg->row_height - 14) / 2;
            lvg_canvas_fill_rect(canvas, sx, sy, 24, 14, p->value.color);
            lvg_canvas_stroke_rect(canvas, sx, sy, 24, 14,
                                    pg->border_color, 1);
            break;
        }
        case LUI_PROP_STRING:
            pg_draw_text(canvas, val_x + 4, text_y, p->value.str,
                         pg->text_color, r.x + r.width - 2);
            break;
        default:
            break;
        }

        /* Row border */
        lvg_canvas_fill_rect(canvas, r.x, row_y + pg->row_height - 1,
                              r.width, 1, pg->border_color);
    }

    /* Outer border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            pg->border_color, 1);
}

static int propgrid_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_propgrid_t *pg = (lui_propgrid_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&r, mx, my)) break;

        int row = (my - r.y + pg->scroll_offset) / pg->row_height;
        if (row < 0 || row >= pg->prop_count) break;

        lui_prop_t *p = &pg->props[row];
        int val_x = r.x + pg->name_width;

        if (mx >= val_x) {
            /* Click in value column */
            if (p->type == LUI_PROP_BOOL) {
                p->value.b = !p->value.b;
                if (pg->on_change)
                    pg->on_change(row, pg->on_change_user);
                return 1;
            }
            if (p->type == LUI_PROP_FLOAT) {
                pg->drag_index = row;
                pg->drag_start_x = mx;
                pg->drag_start_val = p->value.f;
                return 1;
            }
            if (p->type == LUI_PROP_INT) {
                pg->drag_index = row;
                pg->drag_start_x = mx;
                pg->drag_start_val = (float)p->value.i;
                return 1;
            }
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        if (pg->drag_index < 0) break;
        int mx = event->data.mouse_move.x;
        int dx = mx - pg->drag_start_x;
        lui_prop_t *p = &pg->props[pg->drag_index];

        if (p->type == LUI_PROP_FLOAT) {
            float range = p->max_f - p->min_f;
            float sensitivity = range / 200.0f;
            if (sensitivity < 0.001f) sensitivity = 0.01f;
            float nv = pg->drag_start_val + (float)dx * sensitivity;
            nv = pg_clampf(nv, p->min_f, p->max_f);
            if (nv != p->value.f) {
                p->value.f = nv;
                if (pg->on_change)
                    pg->on_change(pg->drag_index, pg->on_change_user);
            }
        } else if (p->type == LUI_PROP_INT) {
            int nv = (int)(pg->drag_start_val + (float)dx * 0.1f);
            nv = pg_clampi(nv, (int)p->min_f, (int)p->max_f);
            if (nv != p->value.i) {
                p->value.i = nv;
                if (pg->on_change)
                    pg->on_change(pg->drag_index, pg->on_change_user);
            }
        }
        return 1;
    }

    case LUI_EVENT_MOUSE_UP:
        if (pg->drag_index >= 0) {
            pg->drag_index = -1;
            return 1;
        }
        break;

    case LUI_EVENT_SCROLL: {
        int sx = event->data.scroll.x;
        int sy = event->data.scroll.y;
        if (!lvg_rect_contains_point(&r, sx, sy)) break;
        pg->scroll_offset -= (int)(event->data.scroll.delta_y * 20.0f);
        int max_scroll = pg->prop_count * pg->row_height - r.height;
        if (max_scroll < 0) max_scroll = 0;
        pg->scroll_offset = pg_clampi(pg->scroll_offset, 0, max_scroll);
        return 1;
    }

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_propgrid_init(lui_propgrid_t *pg)
{
    if (!pg) return;

    lui_widget_init(&pg->widget);
    pg->widget.width    = lvg_size_hug(240);
    pg->widget.height   = lvg_size_hug(0);
    pg->widget.measure  = propgrid_measure;
    pg->widget.draw     = propgrid_draw;
    pg->widget.on_event = propgrid_event;

    pg->prop_count    = 0;
    pg->row_height    = 24;
    pg->name_width    = 120;
    pg->scroll_offset = 0;
    pg->drag_index    = -1;

    pg->bg_color         = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    pg->name_bg          = LVG_COLOR_RGB(0x28, 0x28, 0x3C);
    pg->value_bg         = LVG_COLOR_RGB(0x30, 0x30, 0x46);
    pg->text_color       = LVG_COLOR_RGB(0xCD, 0xD6, 0xF4);
    pg->separator_color  = LVG_COLOR_RGB(0x45, 0x47, 0x5A);
    pg->border_color     = LVG_COLOR_RGB(0x58, 0x5B, 0x70);
    pg->bool_true_color  = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    pg->bool_false_color = LVG_COLOR_RGB(0x40, 0x44, 0x4B);

    pg->on_change      = NULL;
    pg->on_change_user = NULL;
}

int lui_propgrid_add_float(lui_propgrid_t *pg, const char *name,
                           float value, float min_val, float max_val)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (name) { strncpy(p->name, name, sizeof(p->name) - 1); }
    p->type    = LUI_PROP_FLOAT;
    p->value.f = pg_clampf(value, min_val, max_val);
    p->min_f   = min_val;
    p->max_f   = max_val;
    return idx;
}

int lui_propgrid_add_int(lui_propgrid_t *pg, const char *name,
                         int value, int min_val, int max_val)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (name) { strncpy(p->name, name, sizeof(p->name) - 1); }
    p->type    = LUI_PROP_INT;
    p->value.i = pg_clampi(value, min_val, max_val);
    p->min_f   = (float)min_val;
    p->max_f   = (float)max_val;
    return idx;
}

int lui_propgrid_add_bool(lui_propgrid_t *pg, const char *name, bool value)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (name) { strncpy(p->name, name, sizeof(p->name) - 1); }
    p->type    = LUI_PROP_BOOL;
    p->value.b = value;
    return idx;
}

int lui_propgrid_add_color(lui_propgrid_t *pg, const char *name,
                           lvg_color_t color)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (name) { strncpy(p->name, name, sizeof(p->name) - 1); }
    p->type        = LUI_PROP_COLOR;
    p->value.color = color;
    return idx;
}

int lui_propgrid_add_string(lui_propgrid_t *pg, const char *name,
                            const char *text)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (name) { strncpy(p->name, name, sizeof(p->name) - 1); }
    p->type = LUI_PROP_STRING;
    if (text) { strncpy(p->value.str, text, sizeof(p->value.str) - 1); }
    return idx;
}

int lui_propgrid_add_separator(lui_propgrid_t *pg, const char *label)
{
    if (!pg || pg->prop_count >= LUI_PROPGRID_MAX) return -1;
    int idx = pg->prop_count++;
    lui_prop_t *p = &pg->props[idx];
    memset(p, 0, sizeof(*p));
    if (label) { strncpy(p->name, label, sizeof(p->name) - 1); }
    p->type     = LUI_PROP_SEPARATOR;
    p->expanded = true;
    return idx;
}

void lui_propgrid_set_float(lui_propgrid_t *pg, int index, float value)
{
    if (!pg || index < 0 || index >= pg->prop_count) return;
    lui_prop_t *p = &pg->props[index];
    if (p->type != LUI_PROP_FLOAT) return;
    p->value.f = pg_clampf(value, p->min_f, p->max_f);
}

float lui_propgrid_get_float(const lui_propgrid_t *pg, int index)
{
    if (!pg || index < 0 || index >= pg->prop_count) return 0.0f;
    if (pg->props[index].type != LUI_PROP_FLOAT) return 0.0f;
    return pg->props[index].value.f;
}

void lui_propgrid_clear(lui_propgrid_t *pg)
{
    if (!pg) return;
    pg->prop_count    = 0;
    pg->scroll_offset = 0;
    pg->drag_index    = -1;
}
