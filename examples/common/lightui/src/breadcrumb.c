/*
 * breadcrumb.c — Navigation breadcrumb path display
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/breadcrumb.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Width of a segment label in pixels (7px advance per character). */
static int segment_text_width(const char *label)
{
    return (int)strlen(label) * 7;
}

/* Width of the separator string in pixels. */
static int separator_width(const lui_breadcrumb_t *bc)
{
    return (int)strlen(bc->separator) * 7;
}

/* Total content width: sum of all segments + separators + padding. */
static int breadcrumb_content_width(const lui_breadcrumb_t *bc)
{
    if (bc->count <= 0) return 0;

    int w = 8; /* left padding */
    for (int i = 0; i < bc->count; i++) {
        if (i > 0) {
            w += 6; /* space before separator */
            w += separator_width(bc);
            w += 6; /* space after separator */
        }
        w += segment_text_width(bc->segments[i].label);
    }
    w += 8; /* right padding */
    return w;
}

/* Get the x-range of segment @idx relative to the widget's absolute x. */
static void segment_x_range(const lui_breadcrumb_t *bc, int idx,
                              int *out_x, int *out_w)
{
    int x = 8; /* left padding */
    for (int i = 0; i < bc->count; i++) {
        if (i > 0) {
            x += 6 + separator_width(bc) + 6;
        }
        int sw = segment_text_width(bc->segments[i].label);
        if (i == idx) {
            *out_x = x;
            *out_w = sw;
            return;
        }
        x += sw;
    }
    *out_x = 0;
    *out_w = 0;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int breadcrumb_measure(const lui_widget_t *w, int *out_w, int *out_h,
                               void *user)
{
    const lui_breadcrumb_t *bc = (const lui_breadcrumb_t *)w;
    (void)user;
    *out_w = breadcrumb_content_width(bc);
    *out_h = 24;
    return 0;
}

static void breadcrumb_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_breadcrumb_t *bc = (lui_breadcrumb_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, bc->bg_color);

    int text_y = r.y + (r.height - 10) / 2;  /* vertically centre 10px text */
    int x = r.x + 8;

    for (int i = 0; i < bc->count; i++) {
        /* Draw separator before all segments except the first */
        if (i > 0) {
            x += 6;
            const char *sep = bc->separator;
            int sep_len = (int)strlen(sep);
            for (int c = 0; c < sep_len; c++) {
                lvg_canvas_fill_rect(canvas, x, text_y, 5, 10,
                                      bc->separator_color);
                x += 7;
            }
            x += 6;
        }

        /* Draw segment label */
        const char *label = bc->segments[i].label;
        int label_len = (int)strlen(label);
        lvg_color_t tc = (i == bc->hovered) ? bc->hover_color : bc->text_color;

        /* Hover highlight background */
        if (i == bc->hovered) {
            int sw = segment_text_width(label);
            lvg_canvas_fill_rounded_rect(canvas, x - 3, r.y + 2,
                                          sw + 6, r.height - 4, 3,
                                          LVG_COLOR_ARGB(0x30, 0xFF, 0xFF, 0xFF));
        }

        for (int c = 0; c < label_len; c++) {
            lvg_canvas_fill_rect(canvas, x, text_y, 5, 10, tc);
            x += 7;
        }
    }
}

static int breadcrumb_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_breadcrumb_t *bc = (lui_breadcrumb_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int old_hovered = bc->hovered;
        bc->hovered = -1;

        if (lvg_rect_contains_point(&r, mx, my)) {
            int rel_x = mx - r.x;
            for (int i = 0; i < bc->count; i++) {
                int sx, sw;
                segment_x_range(bc, i, &sx, &sw);
                if (rel_x >= sx && rel_x < sx + sw) {
                    bc->hovered = i;
                    break;
                }
            }
        }

        return (bc->hovered != old_hovered) ? 1 : 0;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (lvg_rect_contains_point(&r, mx, my)) {
            int rel_x = mx - r.x;
            for (int i = 0; i < bc->count; i++) {
                int sx, sw;
                segment_x_range(bc, i, &sx, &sw);
                if (rel_x >= sx && rel_x < sx + sw) {
                    if (bc->on_click)
                        bc->on_click(i, bc->on_click_user);
                    return 1;
                }
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_breadcrumb_init(lui_breadcrumb_t *bc)
{
    if (!bc) return;

    lui_widget_init(&bc->widget);
    bc->widget.width    = lvg_size_hug(0);
    bc->widget.height   = lvg_size_fixed(24);
    bc->widget.measure  = breadcrumb_measure;
    bc->widget.draw     = breadcrumb_draw;
    bc->widget.on_event = breadcrumb_event;

    bc->count   = 0;
    bc->hovered = -1;
    memcpy(bc->separator, ">", 2);

    bc->text_color      = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    bc->hover_color     = LVG_COLOR_RGB(0x89, 0xB4, 0xFA);
    bc->separator_color = LVG_COLOR_RGB(0x70, 0x74, 0x7A);
    bc->bg_color        = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);

    bc->on_click      = NULL;
    bc->on_click_user = NULL;
}

int lui_breadcrumb_push(lui_breadcrumb_t *bc, const char *label)
{
    if (!bc || !label || bc->count >= LUI_BREADCRUMB_MAX)
        return -1;

    int len = (int)strlen(label);
    if (len > 63) len = 63;
    memcpy(bc->segments[bc->count].label, label, len);
    bc->segments[bc->count].label[len] = '\0';
    bc->count++;
    return 0;
}

int lui_breadcrumb_pop(lui_breadcrumb_t *bc)
{
    if (!bc || bc->count <= 0)
        return -1;

    bc->count--;
    if (bc->hovered >= bc->count)
        bc->hovered = -1;
    return 0;
}

void lui_breadcrumb_clear(lui_breadcrumb_t *bc)
{
    if (!bc) return;
    bc->count = 0;
    bc->hovered = -1;
}

void lui_breadcrumb_set_path(lui_breadcrumb_t *bc,
                              const char *const *labels, int count)
{
    if (!bc) return;

    lui_breadcrumb_clear(bc);
    if (!labels) return;

    int n = count;
    if (n > LUI_BREADCRUMB_MAX) n = LUI_BREADCRUMB_MAX;
    for (int i = 0; i < n; i++) {
        if (labels[i])
            lui_breadcrumb_push(bc, labels[i]);
    }
}
