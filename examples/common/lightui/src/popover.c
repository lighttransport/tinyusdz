/*
 * popover.c — Floating content panel anchored to a position
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/popover.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Compute the popover panel rect based on anchor and position. */
static lvg_rect_t popover_panel_rect(const lui_popover_t *po)
{
    int w = po->content_width  + po->padding * 2;
    int h = po->content_height + po->padding * 2;
    int ax = po->anchor_x;
    int ay = po->anchor_y;
    int arrow = po->arrow_size;
    int px, py;

    switch (po->position) {
    case LUI_POPOVER_ABOVE:
        px = ax - w / 2;
        py = ay - h - arrow;
        break;
    case LUI_POPOVER_BELOW:
        px = ax - w / 2;
        py = ay + arrow;
        break;
    case LUI_POPOVER_LEFT:
        px = ax - w - arrow;
        py = ay - h / 2;
        break;
    case LUI_POPOVER_RIGHT:
        px = ax + arrow;
        py = ay - h / 2;
        break;
    default:
        px = ax - w / 2;
        py = ay + arrow;
        break;
    }

    return lvg_rect_make(px, py, w, h);
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int po_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)user;
    const lui_popover_t *po = (const lui_popover_t *)w;
    if (!po->visible) {
        *out_w = 0;
        *out_h = 0;
        return 0;
    }
    *out_w = po->content_width  + po->padding * 2;
    *out_h = po->content_height + po->padding * 2 + po->arrow_size;
    return 0;
}

static void po_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_popover_t *po = (lui_popover_t *)w;
    if (!po->visible) return;

    lvg_rect_t pr = popover_panel_rect(po);
    if (lvg_rect_is_empty(&pr)) return;

    int cr = po->corner_radius;
    int arrow = po->arrow_size;

    /* Shadow (offset by 2px) */
    if (LVG_COLOR_A(po->shadow_color) > 0) {
        lvg_canvas_fill_rounded_rect(canvas,
                                      pr.x + 2, pr.y + 2,
                                      pr.width, pr.height,
                                      cr, po->shadow_color);
    }

    /* Panel background */
    lvg_canvas_fill_rounded_rect(canvas, pr.x, pr.y, pr.width, pr.height,
                                  cr, po->bg_color);
    lvg_canvas_stroke_rounded_rect(canvas, pr.x, pr.y, pr.width, pr.height,
                                    cr, po->border_color, 1);

    /* Arrow triangle pointing toward anchor */
    {
        int ax = po->anchor_x;
        int ay = po->anchor_y;
        int hs = arrow;

        switch (po->position) {
        case LUI_POPOVER_ABOVE:
            /* Arrow at bottom of panel pointing down */
            lvg_canvas_fill_triangle(canvas,
                                      ax - hs, pr.y + pr.height,
                                      ax + hs, pr.y + pr.height,
                                      ax,      pr.y + pr.height + arrow,
                                      po->arrow_color);
            break;
        case LUI_POPOVER_BELOW:
            /* Arrow at top of panel pointing up */
            lvg_canvas_fill_triangle(canvas,
                                      ax - hs, pr.y,
                                      ax + hs, pr.y,
                                      ax,      pr.y - arrow,
                                      po->arrow_color);
            break;
        case LUI_POPOVER_LEFT:
            /* Arrow at right of panel pointing right */
            lvg_canvas_fill_triangle(canvas,
                                      pr.x + pr.width, ay - hs,
                                      pr.x + pr.width, ay + hs,
                                      pr.x + pr.width + arrow, ay,
                                      po->arrow_color);
            break;
        case LUI_POPOVER_RIGHT:
            /* Arrow at left of panel pointing left */
            lvg_canvas_fill_triangle(canvas,
                                      pr.x, ay - hs,
                                      pr.x, ay + hs,
                                      pr.x - arrow, ay,
                                      po->arrow_color);
            break;
        }
    }

    /* Draw content widget if present */
    if (po->content && po->content->draw) {
        /* Position content within the panel */
        po->content->computed.x = pr.x + po->padding;
        po->content->computed.y = pr.y + po->padding;
        po->content->computed.width  = po->content_width;
        po->content->computed.height = po->content_height;
        po->content->draw(po->content, canvas);
    }
}

static int po_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_popover_t *po = (lui_popover_t *)w;
    if (!po->visible) return 0;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        lvg_rect_t pr = popover_panel_rect(po);

        if (lvg_rect_contains_point(&pr, mx, my)) {
            /* Pass to content if present */
            if (po->content && po->content->on_event)
                return po->content->on_event(po->content, event);
            return 1;
        }

        /* Click outside: dismiss */
        lui_popover_hide(po);
        if (po->on_dismiss)
            po->on_dismiss(po->on_dismiss_user);
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        lvg_rect_t pr = popover_panel_rect(po);

        if (lvg_rect_contains_point(&pr, mx, my)) {
            if (po->content && po->content->on_event)
                return po->content->on_event(po->content, event);
            return 1;
        }
        return 0;
    }

    /* Key events: pass to content */
    if ((event->type == LUI_EVENT_KEY_DOWN ||
         event->type == LUI_EVENT_KEY_UP) &&
        po->content && po->content->on_event) {
        return po->content->on_event(po->content, event);
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_popover_init(lui_popover_t *po)
{
    if (!po) return;

    lui_widget_init(&po->widget);
    po->widget.width    = lvg_size_hug(0);
    po->widget.height   = lvg_size_hug(0);
    po->widget.measure  = po_measure;
    po->widget.draw     = po_draw;
    po->widget.on_event = po_event;
    po->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    po->visible         = false;
    po->anchor_x        = 0;
    po->anchor_y        = 0;
    po->content_width   = 160;
    po->content_height  = 80;
    po->position        = LUI_POPOVER_BELOW;
    po->content         = NULL;

    po->corner_radius   = 6;
    po->arrow_size      = 8;
    po->padding         = 8;

    po->bg_color        = LVG_COLOR_RGB(0x30, 0x34, 0x3A);
    po->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    po->arrow_color     = LVG_COLOR_RGB(0x30, 0x34, 0x3A);
    po->shadow_color    = LVG_COLOR_ARGB(0x60, 0x00, 0x00, 0x00);

    po->on_dismiss      = NULL;
    po->on_dismiss_user = NULL;
}

void lui_popover_show(lui_popover_t *po, int anchor_x, int anchor_y,
                       lui_popover_position_t position)
{
    if (!po) return;
    po->anchor_x = anchor_x;
    po->anchor_y = anchor_y;
    po->position = position;
    po->visible  = true;
}

void lui_popover_hide(lui_popover_t *po)
{
    if (!po) return;
    po->visible = false;
}

void lui_popover_set_content(lui_popover_t *po, lui_widget_t *content)
{
    if (!po) return;
    po->content = content;
}

bool lui_popover_is_visible(const lui_popover_t *po)
{
    if (!po) return false;
    return po->visible;
}
