/*
 * accordion.c — Collapsible accordion (expandable panels) widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/accordion.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/** Compute total height of the accordion based on current expansion state. */
static int accordion_total_height(const lui_accordion_t *acc)
{
    int h = 0;
    for (int i = 0; i < acc->section_count; i++) {
        h += acc->header_height;
        if (acc->sections[i].expanded)
            h += acc->sections[i].content_height;
    }
    return h;
}

/** Return y-offset of section @index relative to top of accordion. */
static int section_y_offset(const lui_accordion_t *acc, int index)
{
    int y = 0;
    for (int i = 0; i < index; i++) {
        y += acc->header_height;
        if (acc->sections[i].expanded)
            y += acc->sections[i].content_height;
    }
    return y;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int accordion_measure(const lui_widget_t *w, int *out_w, int *out_h,
                               void *user)
{
    const lui_accordion_t *acc = (const lui_accordion_t *)w;
    (void)user;
    *out_w = 200;
    *out_h = accordion_total_height(acc);
    return 0;
}

static void draw_arrow_right(lvg_canvas_t *canvas, int x, int y, int size,
                               lvg_color_t color)
{
    /* Right-pointing triangle: collapsed state */
    int half = size / 2;
    lvg_canvas_fill_triangle(canvas,
                              x, y,
                              x, y + size,
                              x + half, y + half,
                              color);
}

static void draw_arrow_down(lvg_canvas_t *canvas, int x, int y, int size,
                              lvg_color_t color)
{
    /* Down-pointing triangle: expanded state */
    int half = size / 2;
    lvg_canvas_fill_triangle(canvas,
                              x, y,
                              x + size, y,
                              x + half, y + half,
                              color);
}

static void accordion_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_accordion_t *acc = (lui_accordion_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    int cy = wr.y;

    for (int i = 0; i < acc->section_count; i++) {
        lui_accordion_section_t *sec = &acc->sections[i];

        /* Header background */
        lvg_canvas_fill_rect(canvas, wr.x, cy, wr.width, acc->header_height,
                              acc->header_bg);

        /* Header bottom border */
        lvg_canvas_fill_rect(canvas, wr.x, cy + acc->header_height - 1,
                              wr.width, 1, acc->border_color);

        /* Arrow */
        int arrow_size = 8;
        int ax = wr.x + 8;
        int ay = cy + (acc->header_height - arrow_size) / 2;
        if (sec->expanded)
            draw_arrow_down(canvas, ax, ay, arrow_size, acc->arrow_color);
        else
            draw_arrow_right(canvas, ax, ay, arrow_size, acc->arrow_color);

        /* Title text */
        int tx = wr.x + 8 + arrow_size + 6;
        int ty = cy + (acc->header_height - 10) / 2;
        for (int c = 0; c < sec->title_len; c++) {
            if (tx + 5 > wr.x + wr.width - 4) break;
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, acc->header_text);
            tx += 7;
        }

        cy += acc->header_height;

        /* Content area */
        if (sec->expanded) {
            lvg_canvas_fill_rect(canvas, wr.x, cy, wr.width,
                                  sec->content_height, acc->content_bg);

            /* Draw content widget if present */
            if (sec->content) {
                /* Position content widget within the content area */
                sec->content->computed.x = 0;
                sec->content->computed.y = cy - wr.y;
                sec->content->computed.width = wr.width;
                sec->content->computed.height = sec->content_height;
            }

            /* Content bottom border */
            lvg_canvas_fill_rect(canvas, wr.x,
                                  cy + sec->content_height - 1,
                                  wr.width, 1, acc->border_color);

            cy += sec->content_height;
        }
    }
}

static int accordion_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_accordion_t *acc = (lui_accordion_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        lvg_rect_t wr = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        /* Check which header was clicked */
        for (int i = 0; i < acc->section_count; i++) {
            int sy = wr.y + section_y_offset(acc, i);
            lvg_rect_t hr = lvg_rect_make(wr.x, sy, wr.width,
                                            acc->header_height);
            if (lvg_rect_contains_point(&hr, mx, my)) {
                lui_accordion_toggle(acc, i);
                return 1;
            }
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_accordion_init(lui_accordion_t *acc)
{
    if (!acc) return;

    lui_widget_init(&acc->widget);
    acc->widget.width    = lvg_size_fill(1);
    acc->widget.height   = lvg_size_hug(0);
    acc->widget.measure  = accordion_measure;
    acc->widget.draw     = accordion_draw;
    acc->widget.on_event = accordion_event;
    acc->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    acc->section_count = 0;
    acc->header_height = 28;
    acc->exclusive     = false;

    acc->header_bg   = LVG_COLOR_RGB(0x28, 0x2C, 0x34);
    acc->header_text = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    acc->content_bg  = LVG_COLOR_RGB(0x1E, 0x21, 0x26);
    acc->border_color = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    acc->arrow_color = LVG_COLOR_RGB(0x90, 0x94, 0x9A);
}

int lui_accordion_add_section(lui_accordion_t *acc, const char *title,
                              int content_height)
{
    if (!acc || !title || acc->section_count >= LUI_ACCORDION_MAX_SECTIONS)
        return -1;

    lui_accordion_section_t *sec = &acc->sections[acc->section_count];

    int len = (int)strlen(title);
    if (len > 63) len = 63;
    memcpy(sec->title, title, len);
    sec->title[len] = '\0';
    sec->title_len  = len;

    sec->expanded       = false;
    sec->content_height = content_height > 0 ? content_height : 60;
    sec->content        = NULL;

    return acc->section_count++;
}

void lui_accordion_set_expanded(lui_accordion_t *acc, int index, bool expanded)
{
    if (!acc || index < 0 || index >= acc->section_count) return;

    if (expanded && acc->exclusive) {
        /* Collapse all others */
        for (int i = 0; i < acc->section_count; i++)
            acc->sections[i].expanded = false;
    }

    acc->sections[index].expanded = expanded;
}

void lui_accordion_toggle(lui_accordion_t *acc, int index)
{
    if (!acc || index < 0 || index >= acc->section_count) return;

    bool new_state = !acc->sections[index].expanded;

    if (new_state && acc->exclusive) {
        /* Collapse all others */
        for (int i = 0; i < acc->section_count; i++)
            acc->sections[i].expanded = false;
    }

    acc->sections[index].expanded = new_state;
}
