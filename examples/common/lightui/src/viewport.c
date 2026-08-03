/*
 * viewport.c — Image viewport with pan/zoom
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/viewport.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <math.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int vp_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 320;
    *out_h = 240;
    return 0;
}

static void vp_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_viewport_t *vp = (lui_viewport_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, vp->bg);

    /* Clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    if (!vp->image) {
        canvas->_clip = old_clip;
        return;
    }

    int iw = vp->image->width;
    int ih = vp->image->height;

    /* Image rect in widget space */
    float dx = r.x + r.width * 0.5f - (vp->pan_x + iw * 0.5f) * vp->zoom;
    float dy = r.y + r.height * 0.5f - (vp->pan_y + ih * 0.5f) * vp->zoom;
    int dst_w = (int)(iw * vp->zoom);
    int dst_h = (int)(ih * vp->zoom);

    /* Checker pattern for transparency */
    if (vp->show_checker) {
        int check_sz = 8;
        for (int cy = r.y; cy < r.y + r.height; cy += check_sz) {
            for (int cx = r.x; cx < r.x + r.width; cx += check_sz) {
                bool dark = ((cx / check_sz) + (cy / check_sz)) & 1;
                int cw = check_sz;
                int ch = check_sz;
                if (cx + cw > r.x + r.width) cw = r.x + r.width - cx;
                if (cy + ch > r.y + r.height) ch = r.y + r.height - cy;
                lvg_canvas_fill_rect(canvas, cx, cy, cw, ch,
                                      dark ? vp->checker_b : vp->checker_a);
            }
        }
    }

    /* Draw image (scaled blit) */
    lvg_canvas_draw_image(canvas, (int)dx, (int)dy, dst_w, dst_h,
                           vp->image, NULL, LVG_IMAGE_FILTER_BILINEAR);

    /* Pixel grid at high zoom */
    if (vp->show_grid && vp->zoom >= vp->grid_threshold) {
        float step = vp->zoom;
        /* Vertical lines */
        for (float gx = dx; gx < dx + dst_w; gx += step) {
            int igx = (int)gx;
            if (igx >= r.x && igx < r.x + r.width)
                lvg_canvas_fill_rect(canvas, igx, r.y, 1, r.height,
                                      vp->grid_color);
        }
        /* Horizontal lines */
        for (float gy = dy; gy < dy + dst_h; gy += step) {
            int igy = (int)gy;
            if (igy >= r.y && igy < r.y + r.height)
                lvg_canvas_fill_rect(canvas, r.x, igy, r.width, 1,
                                      vp->grid_color);
        }
    }

    /* Crosshair */
    if (vp->show_crosshair && vp->crosshair_x >= 0 && vp->crosshair_y >= 0) {
        int cwx, cwy;
        lui_viewport_image_to_widget(vp, (float)vp->crosshair_x,
                                      (float)vp->crosshair_y, &cwx, &cwy);
        if (cwx >= r.x && cwx < r.x + r.width)
            lvg_canvas_fill_rect(canvas, cwx, r.y, 1, r.height,
                                  vp->crosshair_color);
        if (cwy >= r.y && cwy < r.y + r.height)
            lvg_canvas_fill_rect(canvas, r.x, cwy, r.width, 1,
                                  vp->crosshair_color);
    }

    /* User overlay */
    if (vp->overlay)
        vp->overlay(canvas, vp->zoom, vp->pan_x, vp->pan_y,
                     vp->overlay_user);

    /* Border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            vp->border_color, 1);

    canvas->_clip = old_clip;
}

static int vp_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_viewport_t *vp = (lui_viewport_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    /* Pan start: middle button or left+space (space not tracked here) */
    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_MIDDLE) {
        if (lvg_rect_contains_point(&r,
                event->data.mouse_button.x,
                event->data.mouse_button.y)) {
            vp->panning = true;
            vp->pan_start_mx = event->data.mouse_button.x;
            vp->pan_start_my = event->data.mouse_button.y;
            vp->pan_start_px = vp->pan_x;
            vp->pan_start_py = vp->pan_y;
            return 1;
        }
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE && vp->panning) {
        int dx = event->data.mouse_move.x - vp->pan_start_mx;
        int dy = event->data.mouse_move.y - vp->pan_start_my;
        vp->pan_x = vp->pan_start_px - (float)dx / vp->zoom;
        vp->pan_y = vp->pan_start_py - (float)dy / vp->zoom;
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP &&
        event->data.mouse_button.button == LUI_MOUSE_MIDDLE) {
        vp->panning = false;
        return 1;
    }

    /* Track mouse for crosshair */
    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        if (lvg_rect_contains_point(&r,
                event->data.mouse_move.x,
                event->data.mouse_move.y)) {
            float ix, iy;
            lui_viewport_widget_to_image(vp,
                event->data.mouse_move.x,
                event->data.mouse_move.y, &ix, &iy);
            vp->crosshair_x = (int)ix;
            vp->crosshair_y = (int)iy;
        }
    }

    /* Zoom with scroll wheel */
    if (event->type == LUI_EVENT_SCROLL) {
        if (lvg_rect_contains_point(&r,
                event->data.scroll.x,
                event->data.scroll.y)) {
            float factor = event->data.scroll.delta_y > 0 ? 1.15f : 0.87f;
            lui_viewport_set_zoom(vp, vp->zoom * factor,
                                   event->data.scroll.x,
                                   event->data.scroll.y);
            return 1;
        }
    }

    /* Fit: Home key */
    if (event->type == LUI_EVENT_KEY_DOWN &&
        event->data.key.key == LUI_KEY_HOME) {
        lui_viewport_fit(vp);
        return 1;
    }

    /* 1:1 zoom: '1' key or End key */
    if (event->type == LUI_EVENT_KEY_DOWN &&
        event->data.key.key == LUI_KEY_END) {
        lui_viewport_reset(vp);
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_viewport_init(lui_viewport_t *vp)
{
    if (!vp) return;

    lui_widget_init(&vp->widget);
    vp->widget.width   = lvg_size_fill(1);
    vp->widget.height  = lvg_size_fill(1);
    vp->widget.measure = vp_measure;
    vp->widget.draw    = vp_draw;
    vp->widget.on_event = vp_event;
    vp->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN | LUI_WIDGET_FOCUSABLE;

    vp->image       = NULL;
    vp->pan_x       = 0.0f;
    vp->pan_y       = 0.0f;
    vp->zoom        = 1.0f;
    vp->zoom_min    = 0.1f;
    vp->zoom_max    = 32.0f;
    vp->panning     = false;

    vp->show_grid       = true;
    vp->grid_threshold  = 8.0f;
    vp->show_checker    = true;
    vp->show_crosshair  = false;
    vp->crosshair_x    = -1;
    vp->crosshair_y    = -1;

    vp->bg               = LVG_COLOR_RGB(0x18, 0x18, 0x22);
    vp->grid_color       = LVG_COLOR_RGB(0x40, 0x40, 0x50);
    vp->checker_a        = LVG_COLOR_RGB(0x30, 0x30, 0x30);
    vp->checker_b        = LVG_COLOR_RGB(0x28, 0x28, 0x28);
    vp->crosshair_color  = LVG_COLOR_RGB(0x80, 0xC0, 0xFF);
    vp->border_color     = LVG_COLOR_RGB(0x40, 0x44, 0x4C);

    vp->overlay      = NULL;
    vp->overlay_user = NULL;
}

void lui_viewport_set_image(lui_viewport_t *vp, const lvg_surface_t *image)
{
    if (!vp) return;
    vp->image = image;
}

void lui_viewport_set_zoom(lui_viewport_t *vp, float zoom,
                             int center_x, int center_y)
{
    if (!vp) return;
    lvg_rect_t r = lui_widget_absolute_rect(&vp->widget);

    /* Convert center point to image space before zoom change */
    float ix, iy;
    lui_viewport_widget_to_image(vp, center_x, center_y, &ix, &iy);

    float new_zoom = clampf(zoom, vp->zoom_min, vp->zoom_max);
    vp->zoom = new_zoom;

    /* Adjust pan so the same image point stays under the cursor */
    float hw = r.width * 0.5f;
    float hh = r.height * 0.5f;
    int iw = vp->image ? vp->image->width : 0;
    int ih = vp->image ? vp->image->height : 0;
    vp->pan_x = ix - iw * 0.5f + (hw - (center_x - r.x)) / new_zoom;
    vp->pan_y = iy - ih * 0.5f + (hh - (center_y - r.y)) / new_zoom;
}

void lui_viewport_fit(lui_viewport_t *vp)
{
    if (!vp || !vp->image) return;
    lvg_rect_t r = lui_widget_absolute_rect(&vp->widget);
    if (r.width <= 0 || r.height <= 0) return;

    float zx = (float)r.width / (float)vp->image->width;
    float zy = (float)r.height / (float)vp->image->height;
    vp->zoom = zx < zy ? zx : zy;
    vp->zoom = clampf(vp->zoom, vp->zoom_min, vp->zoom_max);
    vp->pan_x = 0.0f;
    vp->pan_y = 0.0f;
}

void lui_viewport_reset(lui_viewport_t *vp)
{
    if (!vp) return;
    vp->zoom = 1.0f;
    vp->pan_x = 0.0f;
    vp->pan_y = 0.0f;
}

void lui_viewport_widget_to_image(const lui_viewport_t *vp,
                                    int wx, int wy,
                                    float *ix, float *iy)
{
    if (!vp) return;
    lvg_rect_t r = lui_widget_absolute_rect((lui_widget_t *)&vp->widget);
    int iw = vp->image ? vp->image->width : 0;
    int ih = vp->image ? vp->image->height : 0;
    if (ix)
        *ix = vp->pan_x + iw * 0.5f + (wx - r.x - r.width * 0.5f) / vp->zoom;
    if (iy)
        *iy = vp->pan_y + ih * 0.5f + (wy - r.y - r.height * 0.5f) / vp->zoom;
}

void lui_viewport_image_to_widget(const lui_viewport_t *vp,
                                    float ix, float iy,
                                    int *wx, int *wy)
{
    if (!vp) return;
    lvg_rect_t r = lui_widget_absolute_rect((lui_widget_t *)&vp->widget);
    int iw = vp->image ? vp->image->width : 0;
    int ih = vp->image ? vp->image->height : 0;
    if (wx)
        *wx = (int)(r.x + r.width * 0.5f +
                     (ix - vp->pan_x - iw * 0.5f) * vp->zoom);
    if (wy)
        *wy = (int)(r.y + r.height * 0.5f +
                     (iy - vp->pan_y - ih * 0.5f) * vp->zoom);
}
