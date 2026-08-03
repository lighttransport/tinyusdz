/*
 * scroll.c — Scrollable container widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/scroll.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline int lui__s_max(int a, int b) { return a > b ? a : b; }

static void lui__scroll_clamp(lui_scroll_t *s)
{
    int max_x = lui__s_max(0, s->content_width  - s->widget.computed.width);
    int max_y = lui__s_max(0, s->content_height - s->widget.computed.height);

    if (s->scroll_x < 0) s->scroll_x = 0;
    if (s->scroll_x > max_x) s->scroll_x = max_x;
    if (s->scroll_y < 0) s->scroll_y = 0;
    if (s->scroll_y > max_y) s->scroll_y = max_y;
}

/* -------------------------------------------------------------------------
 * Draw callback
 *
 * Clips to viewport, offsets content by scroll, draws children, then
 * renders scrollbar overlays.
 * ------------------------------------------------------------------------- */

static void lui__draw_subtree_offset(lui_widget_t *w, lvg_canvas_t *canvas,
                                      int ox, int oy)
{
    if (!w) return;

    /* Temporarily adjust only this subtree root. Descendant absolute
     * positions already include their parent position, so applying the same
     * offset recursively would double-shift nested widgets. */
    w->computed.x += ox;
    w->computed.y += oy;

    if (w->draw)
        w->draw(w, canvas);

    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        lui__draw_subtree_offset(c, canvas, 0, 0);

    /* Restore */
    w->computed.x -= ox;
    w->computed.y -= oy;
}

static void scroll_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_scroll_t *s = (lui_scroll_t *)w;
    lvg_rect_t vp = lui_widget_absolute_rect(w);

    /* Background */
    if (LVG_COLOR_A(s->bg) > 0)
        lvg_canvas_fill_rect(canvas, vp.x, vp.y, vp.width, vp.height, s->bg);

    /* Clip to viewport */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_canvas_set_clip(canvas, &vp);

    /* Draw content children with scroll offset */
    int ox = vp.x - s->scroll_x;
    int oy = vp.y - s->scroll_y;

    for (lui_widget_t *c = s->content.first_child; c; c = c->next_sibling)
        lui__draw_subtree_offset(c, canvas, ox, oy);

    /* Restore clip */
    canvas->_clip = old_clip;

    /* Draw vertical scrollbar if content overflows */
    if (s->content_height > vp.height && s->scrollbar_width > 0) {
        int track_x = vp.x + vp.width - s->scrollbar_width;
        int track_h = vp.height;

        /* Thumb proportional to visible fraction */
        int thumb_h = lui__s_max(16,
            (int)((long long)vp.height * vp.height / s->content_height));
        int max_scroll = s->content_height - vp.height;
        int thumb_y = vp.y;
        if (max_scroll > 0)
            thumb_y += (int)((long long)s->scroll_y * (track_h - thumb_h) / max_scroll);

        /* Track background */
        lvg_canvas_fill_rect(canvas, track_x, vp.y,
                              s->scrollbar_width, track_h,
                              LVG_COLOR_ARGB(0x30, 0xFF, 0xFF, 0xFF));
        /* Thumb */
        lvg_canvas_fill_rounded_rect(canvas, track_x + 1, thumb_y,
                                      s->scrollbar_width - 2, thumb_h,
                                      (s->scrollbar_width - 2) / 2,
                                      s->scrollbar_color);
    }

    /* Draw horizontal scrollbar if content overflows */
    if (s->content_width > vp.width && s->scrollbar_width > 0) {
        int track_y = vp.y + vp.height - s->scrollbar_width;
        int track_w = vp.width;
        if (s->content_height > vp.height)
            track_w -= s->scrollbar_width;  /* avoid overlap with vertical */

        int thumb_w = lui__s_max(16,
            (int)((long long)vp.width * track_w / s->content_width));
        int max_scroll = s->content_width - vp.width;
        int thumb_x = vp.x;
        if (max_scroll > 0)
            thumb_x += (int)((long long)s->scroll_x * (track_w - thumb_w) / max_scroll);

        lvg_canvas_fill_rect(canvas, vp.x, track_y,
                              track_w, s->scrollbar_width,
                              LVG_COLOR_ARGB(0x30, 0xFF, 0xFF, 0xFF));
        lvg_canvas_fill_rounded_rect(canvas, thumb_x, track_y + 1,
                                      thumb_w, s->scrollbar_width - 2,
                                      (s->scrollbar_width - 2) / 2,
                                      s->scrollbar_color);
    }
}

/* -------------------------------------------------------------------------
 * Event callback — handle scroll wheel
 * ------------------------------------------------------------------------- */

static int scroll_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_scroll_t *s = (lui_scroll_t *)w;

    if (event->type == LUI_EVENT_SCROLL) {
        int dx = (int)(event->data.scroll.delta_x * s->scroll_speed);
        int dy = (int)(event->data.scroll.delta_y * s->scroll_speed);
        int old_x = s->scroll_x;
        int old_y = s->scroll_y;
        lui_scroll_by(s, dx, dy);
        /* Consume only if scroll position actually changed */
        return (s->scroll_x != old_x || s->scroll_y != old_y) ? 1 : 0;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_scroll_init(lui_scroll_t *s)
{
    if (!s) return;

    lui_widget_init(&s->widget);
    s->widget.width  = lvg_size_fill(1);
    s->widget.height = lvg_size_fill(1);
    s->widget.draw     = scroll_draw;
    s->widget.on_event = scroll_event;
    s->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    /* The content pane is a child of the viewport widget.
     * It uses HUG so it grows to fit its children. */
    lui_widget_init(&s->content);
    s->content.direction = LUI_LAYOUT_COLUMN;
    s->content.width  = lvg_size_hug(0);
    s->content.height = lvg_size_hug(0);
    lui_widget_add_child(&s->widget, &s->content);

    s->scroll_x = 0;
    s->scroll_y = 0;
    s->content_width  = 0;
    s->content_height = 0;
    s->scroll_speed   = 32;
    s->bg             = LVG_COLOR_TRANSPARENT;
    s->scrollbar_color = LVG_COLOR_ARGB(0x80, 0xA0, 0xA0, 0xA0);
    s->scrollbar_width = 8;
}

void lui_scroll_update(lui_scroll_t *s)
{
    if (!s) return;

    /* Content dimensions = the content widget's desired size (before clamp) */
    s->content_width  = s->content._desired_w;
    s->content_height = s->content._desired_h;

    lui__scroll_clamp(s);
}

void lui_scroll_set(lui_scroll_t *s, int x, int y)
{
    if (!s) return;
    s->scroll_x = x;
    s->scroll_y = y;
    lui__scroll_clamp(s);
}

void lui_scroll_by(lui_scroll_t *s, int dx, int dy)
{
    if (!s) return;
    s->scroll_x += dx;
    s->scroll_y += dy;
    lui__scroll_clamp(s);
}
