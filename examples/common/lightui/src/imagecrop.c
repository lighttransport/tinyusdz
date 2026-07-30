/*
 * imagecrop.c — Crop region selector overlay for image editing
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/imagecrop.h>
#include <lightvg/canvas.h>
#include <stddef.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int crop_max(int a, int b) { return a > b ? a : b; }

/* Minimum crop dimension */
#define CROP_MIN_SIZE 8

/* Map image coordinates to widget pixel coordinates. */
static void crop_img_to_widget(const lui_imagecrop_t *c, const lvg_rect_t *wr,
                                int ix, int iy, int *wx, int *wy)
{
    *wx = wr->x + (ix * wr->width)  / c->image_w;
    *wy = wr->y + (iy * wr->height) / c->image_h;
}

/* Get the crop rectangle in widget coordinates. */
static lvg_rect_t crop_widget_rect(const lui_imagecrop_t *c,
                                    const lvg_rect_t *wr)
{
    int x0, y0, x1, y1;
    crop_img_to_widget(c, wr, c->crop_x, c->crop_y, &x0, &y0);
    crop_img_to_widget(c, wr, c->crop_x + c->crop_w,
                       c->crop_y + c->crop_h, &x1, &y1);
    return lvg_rect_make(x0, y0, x1 - x0, y1 - y0);
}

/* Test if point is near a handle. Returns the drag mode. */
static lui_crop_drag_mode_t crop_hit_handle(const lui_imagecrop_t *c,
                                             const lvg_rect_t *cr,
                                             int mx, int my)
{
    int hs = c->handle_size;
    int hw = hs / 2;

    /* 8 handle positions: corners + edge midpoints */
    struct { int x, y; lui_crop_drag_mode_t mode; } handles[] = {
        { cr->x,                      cr->y,                       LUI_CROP_RESIZE_TL     },
        { cr->x + cr->width,          cr->y,                       LUI_CROP_RESIZE_TR     },
        { cr->x,                      cr->y + cr->height,          LUI_CROP_RESIZE_BL     },
        { cr->x + cr->width,          cr->y + cr->height,          LUI_CROP_RESIZE_BR     },
        { cr->x + cr->width / 2,      cr->y,                       LUI_CROP_RESIZE_TOP    },
        { cr->x + cr->width / 2,      cr->y + cr->height,          LUI_CROP_RESIZE_BOTTOM },
        { cr->x,                      cr->y + cr->height / 2,      LUI_CROP_RESIZE_LEFT   },
        { cr->x + cr->width,          cr->y + cr->height / 2,      LUI_CROP_RESIZE_RIGHT  },
    };

    for (int i = 0; i < 8; i++) {
        lvg_rect_t hr = lvg_rect_make(handles[i].x - hw, handles[i].y - hw,
                                       hs, hs);
        if (lvg_rect_contains_point(&hr, mx, my)) {
            return handles[i].mode;
        }
    }

    return LUI_CROP_NONE;
}

/* Clamp crop rect to image bounds and enforce minimum size. */
static void crop_clamp_rect(lui_imagecrop_t *c)
{
    if (c->crop_w < CROP_MIN_SIZE) c->crop_w = CROP_MIN_SIZE;
    if (c->crop_h < CROP_MIN_SIZE) c->crop_h = CROP_MIN_SIZE;
    if (c->crop_x < 0) c->crop_x = 0;
    if (c->crop_y < 0) c->crop_y = 0;
    if (c->crop_x + c->crop_w > c->image_w)
        c->crop_x = c->image_w - c->crop_w;
    if (c->crop_y + c->crop_h > c->image_h)
        c->crop_y = c->image_h - c->crop_h;
    if (c->crop_x < 0) { c->crop_x = 0; c->crop_w = c->image_w; }
    if (c->crop_y < 0) { c->crop_y = 0; c->crop_h = c->image_h; }
}

/* Enforce aspect ratio on crop dimensions (adjusts height to match). */
static void crop_enforce_aspect(lui_imagecrop_t *c)
{
    if (!c->lock_aspect || c->aspect_ratio <= 0.0f) return;

    int new_h = (int)((float)c->crop_w / c->aspect_ratio);
    if (new_h < CROP_MIN_SIZE) {
        new_h = CROP_MIN_SIZE;
        c->crop_w = (int)((float)new_h * c->aspect_ratio);
    }
    c->crop_h = new_h;
}

static void crop_fire_callback(lui_imagecrop_t *c)
{
    if (c->on_change) {
        c->on_change(c->crop_x, c->crop_y, c->crop_w, c->crop_h,
                      c->on_change_user);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int imagecrop_measure(const lui_widget_t *w, int *out_w, int *out_h,
                              void *user)
{
    const lui_imagecrop_t *c = (const lui_imagecrop_t *)w;
    (void)user;
    *out_w = c->image_w;
    *out_h = c->image_h;
    return 0;
}

static void imagecrop_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_imagecrop_t *c = (lui_imagecrop_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    lvg_rect_t cr = crop_widget_rect(c, &wr);

    /* Draw darkened overlay outside crop rect (4 rectangles) */
    lvg_color_t ov = c->overlay_color;

    /* Top strip */
    if (cr.y > wr.y) {
        lvg_canvas_fill_rect(canvas, wr.x, wr.y,
                              wr.width, cr.y - wr.y, ov);
    }
    /* Bottom strip */
    int crop_bot = cr.y + cr.height;
    int wr_bot = wr.y + wr.height;
    if (crop_bot < wr_bot) {
        lvg_canvas_fill_rect(canvas, wr.x, crop_bot,
                              wr.width, wr_bot - crop_bot, ov);
    }
    /* Left strip (between top and bottom strips) */
    if (cr.x > wr.x) {
        lvg_canvas_fill_rect(canvas, wr.x, cr.y,
                              cr.x - wr.x, cr.height, ov);
    }
    /* Right strip */
    int crop_right = cr.x + cr.width;
    int wr_right = wr.x + wr.width;
    if (crop_right < wr_right) {
        lvg_canvas_fill_rect(canvas, crop_right, cr.y,
                              wr_right - crop_right, cr.height, ov);
    }

    /* Crop border */
    lvg_canvas_stroke_rect(canvas, cr.x, cr.y,
                            cr.width, cr.height,
                            c->border_color, 1);

    /* Grid lines inside crop */
    if (c->show_grid && cr.width > 0 && cr.height > 0) {
        if (c->grid_type == LUI_GRID_THIRDS) {
            /* Rule of thirds: 2 vertical + 2 horizontal lines */
            for (int i = 1; i <= 2; i++) {
                int gx = cr.x + cr.width * i / 3;
                lvg_canvas_draw_line(canvas, gx, cr.y, gx, cr.y + cr.height,
                                      c->grid_color, 1);
                int gy = cr.y + cr.height * i / 3;
                lvg_canvas_draw_line(canvas, cr.x, gy, cr.x + cr.width, gy,
                                      c->grid_color, 1);
            }
        } else { /* LUI_GRID_CENTER */
            /* Crosshair through centre */
            int cx = cr.x + cr.width / 2;
            int cy = cr.y + cr.height / 2;
            lvg_canvas_draw_line(canvas, cx, cr.y, cx, cr.y + cr.height,
                                  c->grid_color, 1);
            lvg_canvas_draw_line(canvas, cr.x, cy, cr.x + cr.width, cy,
                                  c->grid_color, 1);
        }
    }

    /* 8 resize handles */
    int hs = c->handle_size;
    int hw = hs / 2;
    struct { int x, y; } hpos[] = {
        { cr.x,                  cr.y                   },
        { cr.x + cr.width,       cr.y                   },
        { cr.x,                  cr.y + cr.height       },
        { cr.x + cr.width,       cr.y + cr.height       },
        { cr.x + cr.width / 2,   cr.y                   },
        { cr.x + cr.width / 2,   cr.y + cr.height       },
        { cr.x,                  cr.y + cr.height / 2   },
        { cr.x + cr.width,       cr.y + cr.height / 2   },
    };
    for (int i = 0; i < 8; i++) {
        lvg_canvas_fill_rect(canvas,
                              hpos[i].x - hw, hpos[i].y - hw,
                              hs, hs, c->handle_color);
    }
}

static int imagecrop_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_imagecrop_t *c = (lui_imagecrop_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&wr, mx, my)) break;

        lvg_rect_t cr = crop_widget_rect(c, &wr);

        /* Check handles first */
        lui_crop_drag_mode_t mode = crop_hit_handle(c, &cr, mx, my);
        if (mode != LUI_CROP_NONE) {
            c->drag_mode = mode;
            c->drag_start_x = mx;
            c->drag_start_y = my;
            c->drag_start_crop.x = c->crop_x;
            c->drag_start_crop.y = c->crop_y;
            c->drag_start_crop.w = c->crop_w;
            c->drag_start_crop.h = c->crop_h;
            return 1;
        }

        /* Check if inside crop for move */
        if (lvg_rect_contains_point(&cr, mx, my)) {
            c->drag_mode = LUI_CROP_MOVE;
            c->drag_start_x = mx;
            c->drag_start_y = my;
            c->drag_start_crop.x = c->crop_x;
            c->drag_start_crop.y = c->crop_y;
            c->drag_start_crop.w = c->crop_w;
            c->drag_start_crop.h = c->crop_h;
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        if (c->drag_mode == LUI_CROP_NONE) break;

        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        /* Convert mouse delta to image-space delta */
        int dx_px = mx - c->drag_start_x;
        int dy_px = my - c->drag_start_y;
        int dx_img = (dx_px * c->image_w) / (wr.width  > 0 ? wr.width  : 1);
        int dy_img = (dy_px * c->image_h) / (wr.height > 0 ? wr.height : 1);

        lui_crop_rect_t s = c->drag_start_crop;

        switch (c->drag_mode) {
        case LUI_CROP_MOVE:
            c->crop_x = s.x + dx_img;
            c->crop_y = s.y + dy_img;
            c->crop_w = s.w;
            c->crop_h = s.h;
            break;

        case LUI_CROP_RESIZE_TL:
            c->crop_x = s.x + dx_img;
            c->crop_y = s.y + dy_img;
            c->crop_w = s.w - dx_img;
            c->crop_h = s.h - dy_img;
            break;

        case LUI_CROP_RESIZE_TR:
            c->crop_y = s.y + dy_img;
            c->crop_w = s.w + dx_img;
            c->crop_h = s.h - dy_img;
            break;

        case LUI_CROP_RESIZE_BL:
            c->crop_x = s.x + dx_img;
            c->crop_w = s.w - dx_img;
            c->crop_h = s.h + dy_img;
            break;

        case LUI_CROP_RESIZE_BR:
            c->crop_w = s.w + dx_img;
            c->crop_h = s.h + dy_img;
            break;

        case LUI_CROP_RESIZE_TOP:
            c->crop_y = s.y + dy_img;
            c->crop_h = s.h - dy_img;
            break;

        case LUI_CROP_RESIZE_BOTTOM:
            c->crop_h = s.h + dy_img;
            break;

        case LUI_CROP_RESIZE_LEFT:
            c->crop_x = s.x + dx_img;
            c->crop_w = s.w - dx_img;
            break;

        case LUI_CROP_RESIZE_RIGHT:
            c->crop_w = s.w + dx_img;
            break;

        default:
            break;
        }

        /* Enforce minimum size */
        if (c->crop_w < CROP_MIN_SIZE) {
            if (c->drag_mode == LUI_CROP_RESIZE_TL ||
                c->drag_mode == LUI_CROP_RESIZE_BL ||
                c->drag_mode == LUI_CROP_RESIZE_LEFT) {
                c->crop_x = s.x + s.w - CROP_MIN_SIZE;
            }
            c->crop_w = CROP_MIN_SIZE;
        }
        if (c->crop_h < CROP_MIN_SIZE) {
            if (c->drag_mode == LUI_CROP_RESIZE_TL ||
                c->drag_mode == LUI_CROP_RESIZE_TR ||
                c->drag_mode == LUI_CROP_RESIZE_TOP) {
                c->crop_y = s.y + s.h - CROP_MIN_SIZE;
            }
            c->crop_h = CROP_MIN_SIZE;
        }

        crop_enforce_aspect(c);
        crop_clamp_rect(c);
        crop_fire_callback(c);
        return 1;
    }

    case LUI_EVENT_MOUSE_UP: {
        if (c->drag_mode != LUI_CROP_NONE &&
            event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            c->drag_mode = LUI_CROP_NONE;
            return 1;
        }
        break;
    }

    default:
        break;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_imagecrop_init(lui_imagecrop_t *c, int image_w, int image_h)
{
    if (!c) return;

    lui_widget_init(&c->widget);
    c->widget.width    = lvg_size_hug(0);
    c->widget.height   = lvg_size_hug(0);
    c->widget.measure  = imagecrop_measure;
    c->widget.draw     = imagecrop_draw;
    c->widget.on_event = imagecrop_event;

    c->image_w = image_w > 0 ? image_w : 1;
    c->image_h = image_h > 0 ? image_h : 1;

    /* Default crop is the full image */
    c->crop_x = 0;
    c->crop_y = 0;
    c->crop_w = c->image_w;
    c->crop_h = c->image_h;

    c->drag_mode    = LUI_CROP_NONE;
    c->drag_start_x = 0;
    c->drag_start_y = 0;
    c->drag_start_crop.x = 0;
    c->drag_start_crop.y = 0;
    c->drag_start_crop.w = 0;
    c->drag_start_crop.h = 0;

    c->lock_aspect  = false;
    c->aspect_ratio = 0.0f;

    c->handle_size = 8;

    c->show_grid = true;
    c->grid_type = LUI_GRID_THIRDS;

    c->overlay_color = LVG_COLOR_ARGB(0x80, 0x00, 0x00, 0x00);
    c->border_color  = LVG_COLOR_WHITE;
    c->handle_color  = LVG_COLOR_WHITE;
    c->grid_color    = LVG_COLOR_ARGB(0x60, 0xFF, 0xFF, 0xFF);

    c->on_change      = NULL;
    c->on_change_user = NULL;
}

void lui_imagecrop_set_crop(lui_imagecrop_t *c, int x, int y, int w, int h)
{
    if (!c) return;
    c->crop_x = x;
    c->crop_y = y;
    c->crop_w = crop_max(w, CROP_MIN_SIZE);
    c->crop_h = crop_max(h, CROP_MIN_SIZE);
    crop_enforce_aspect(c);
    crop_clamp_rect(c);
}

void lui_imagecrop_set_aspect(lui_imagecrop_t *c, float ratio)
{
    if (!c) return;
    if (ratio <= 0.0f) {
        c->lock_aspect  = false;
        c->aspect_ratio = 0.0f;
    } else {
        c->lock_aspect  = true;
        c->aspect_ratio = ratio;
        crop_enforce_aspect(c);
        crop_clamp_rect(c);
    }
}

void lui_imagecrop_get_crop(const lui_imagecrop_t *c,
                             int *out_x, int *out_y,
                             int *out_w, int *out_h)
{
    if (!c) return;
    if (out_x) *out_x = c->crop_x;
    if (out_y) *out_y = c->crop_y;
    if (out_w) *out_w = c->crop_w;
    if (out_h) *out_h = c->crop_h;
}
