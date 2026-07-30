/*
 * colorwheel.c — HSV color wheel widget
 *
 * Renders a hue ring around the outside and a saturation/value disc inside.
 * The hue ring is drawn by filling pie-slice colored sectors.
 * The inner disc uses a simple per-pixel HSV->RGB conversion.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/colorwheel.h>
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- HSV <-> RGB -------------------------------------------------------- */

lvg_color_t lui_hsv_to_rgb(float h, float s, float v)
{
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    float c = v * s;
    float x = c * (1.0f - fabsf(fmodf(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;

    float r, g, b;
    if      (h < 60)  { r = c; g = x; b = 0; }
    else if (h < 120) { r = x; g = c; b = 0; }
    else if (h < 180) { r = 0; g = c; b = x; }
    else if (h < 240) { r = 0; g = x; b = c; }
    else if (h < 300) { r = x; g = 0; b = c; }
    else              { r = c; g = 0; b = x; }

    int ri = (int)((r + m) * 255.0f + 0.5f);
    int gi = (int)((g + m) * 255.0f + 0.5f);
    int bi = (int)((b + m) * 255.0f + 0.5f);
    return LVG_COLOR_RGB(ri, gi, bi);
}

void lui_rgb_to_hsv(lvg_color_t rgb, float *h, float *s, float *v)
{
    float r = LVG_COLOR_R(rgb) / 255.0f;
    float g = LVG_COLOR_G(rgb) / 255.0f;
    float b = LVG_COLOR_B(rgb) / 255.0f;

    float max = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float min = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float d = max - min;

    *v = max;
    *s = (max > 0.0f) ? d / max : 0.0f;

    if (d < 1e-6f) {
        *h = 0.0f;
    } else if (max == r) {
        *h = 60.0f * fmodf((g - b) / d, 6.0f);
    } else if (max == g) {
        *h = 60.0f * ((b - r) / d + 2.0f);
    } else {
        *h = 60.0f * ((r - g) / d + 4.0f);
    }
    if (*h < 0.0f) *h += 360.0f;
}

/* ---- Helpers ------------------------------------------------------------ */

static inline float cw_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Drawing ------------------------------------------------------------ */

static void colorwheel_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_colorwheel_t *cw = (lui_colorwheel_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int dim = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width / 2;
    int cy = r.y + r.height / 2;
    int outer_r = dim / 2 - 1;
    int inner_r = outer_r - cw->ring_width;

    /* Background */
    if (LVG_COLOR_A(cw->bg) > 0)
        lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, cw->bg);

    /* Draw the hue ring and SV disc pixel-by-pixel using the surface */
    lvg_surface_t *surf = canvas->_surface;
    lvg_rect_t clip = canvas->_clip;

    for (int py = r.y; py < r.y + r.height; py++) {
        if (py < clip.y || py >= clip.y + clip.height) continue;
        for (int px = r.x; px < r.x + r.width; px++) {
            if (px < clip.x || px >= clip.x + clip.width) continue;

            int dx = px - cx;
            int dy = py - cy;
            int dist2 = dx * dx + dy * dy;
            int dist = (int)(sqrtf((float)dist2) + 0.5f);

            if (dist > outer_r) continue;

            if (dist >= inner_r) {
                /* Hue ring: angle -> hue, full saturation+value */
                float angle = atan2f((float)dy, (float)dx);
                float hue = angle * (180.0f / (float)M_PI);
                if (hue < 0) hue += 360.0f;
                lvg_color_t col = lui_hsv_to_rgb(hue, 1.0f, 1.0f);
                surf->pixels[py * surf->stride + px] = col;
            } else {
                /* Inner disc: saturation = distance from center / inner_r,
                 * value = vertical position mapped, using current hue */
                float fx = (float)dx / (float)inner_r; /* -1..1 */
                float fy = (float)dy / (float)inner_r; /* -1..1 */
                float fdist = sqrtf(fx * fx + fy * fy);
                if (fdist > 1.0f) continue;

                float sat = fdist;
                /* Map vertical to value: top=bright, bottom=dark */
                float val = 1.0f - (fy + 1.0f) * 0.5f;
                lvg_color_t col = lui_hsv_to_rgb(cw->hue, sat, val);
                surf->pixels[py * surf->stride + px] = col;
            }
        }
    }

    /* Draw hue indicator on the ring */
    {
        float hrad = cw->hue * (float)M_PI / 180.0f;
        int ring_mid = (outer_r + inner_r) / 2;
        int hx = cx + (int)(cosf(hrad) * ring_mid);
        int hy = cy + (int)(sinf(hrad) * ring_mid);
        lvg_canvas_stroke_circle(canvas, hx, hy, cw->indicator_r + 1,
                                  LVG_COLOR_BLACK, 1);
        lvg_canvas_stroke_circle(canvas, hx, hy, cw->indicator_r,
                                  cw->indicator, 2);
    }

    /* Draw SV indicator on the disc */
    {
        (void)0;
        /* Compute angle from saturation direction: we need to find where
         * the current S,V maps in the disc. We use a simple mapping:
         * x = sat * cos(hue_angle_of_sv_position) but for a uniform disc
         * the SV indicator angle is arbitrary. Use a direct mapping:
         * x = sat * inner_r * some_direction, y = (1-val)*2-1 * inner_r */
        float val_y = (1.0f - cw->value) * 2.0f - 1.0f;
        /* Compute x from saturation at this y level */
        float max_x = sqrtf(1.0f - val_y * val_y);
        float sx_norm = (max_x > 0.001f) ? cw->saturation : 0.0f;
        if (sx_norm > max_x) sx_norm = max_x;

        int sx = cx + (int)(sx_norm * (inner_r - 2));
        int sy = cy + (int)(val_y * (inner_r - 2));

        lvg_canvas_stroke_circle(canvas, sx, sy, cw->indicator_r + 1,
                                  LVG_COLOR_BLACK, 1);
        lvg_canvas_stroke_circle(canvas, sx, sy, cw->indicator_r,
                                  cw->indicator, 2);
    }
}

/* ---- Event handling ----------------------------------------------------- */

static int colorwheel_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_colorwheel_t *cw = (lui_colorwheel_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    int dim = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width / 2;
    int cy = r.y + r.height / 2;
    int outer_r = dim / 2 - 1;
    int inner_r = outer_r - cw->ring_width;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        int dx = mx - cx;
        int dy = my - cy;
        float dist = sqrtf((float)(dx * dx + dy * dy));

        if (dist >= inner_r && dist <= outer_r) {
            /* Hue ring */
            cw->drag_mode = 1;
            float angle = atan2f((float)dy, (float)dx);
            cw->hue = angle * (180.0f / (float)M_PI);
            if (cw->hue < 0) cw->hue += 360.0f;
            if (cw->on_change)
                cw->on_change(cw->hue, cw->saturation, cw->value,
                              cw->on_change_user);
            return 1;
        } else if (dist < inner_r) {
            /* SV disc */
            cw->drag_mode = 2;
            float fx = (float)dx / (float)(inner_r - 2);
            float fy = (float)dy / (float)(inner_r - 2);
            float fdist = sqrtf(fx * fx + fy * fy);
            cw->saturation = cw_clampf(fdist, 0.0f, 1.0f);
            cw->value = cw_clampf(1.0f - (fy + 1.0f) * 0.5f, 0.0f, 1.0f);
            if (cw->on_change)
                cw->on_change(cw->hue, cw->saturation, cw->value,
                              cw->on_change_user);
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        if (cw->drag_mode == 0) break;
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int dx = mx - cx;
        int dy = my - cy;

        if (cw->drag_mode == 1) {
            float angle = atan2f((float)dy, (float)dx);
            cw->hue = angle * (180.0f / (float)M_PI);
            if (cw->hue < 0) cw->hue += 360.0f;
        } else {
            float fx = (float)dx / (float)(inner_r - 2);
            float fy = (float)dy / (float)(inner_r - 2);
            float fdist = sqrtf(fx * fx + fy * fy);
            cw->saturation = cw_clampf(fdist, 0.0f, 1.0f);
            cw->value = cw_clampf(1.0f - (fy + 1.0f) * 0.5f, 0.0f, 1.0f);
        }
        if (cw->on_change)
            cw->on_change(cw->hue, cw->saturation, cw->value,
                          cw->on_change_user);
        return 1;
    }

    case LUI_EVENT_MOUSE_UP:
        if (cw->drag_mode != 0) {
            cw->drag_mode = 0;
            return 1;
        }
        break;

    default:
        break;
    }

    return 0;
}

/* ---- Measure ------------------------------------------------------------ */

static int colorwheel_measure(const lui_widget_t *w, int *out_w, int *out_h,
                                void *user)
{
    const lui_colorwheel_t *cw = (const lui_colorwheel_t *)w;
    (void)user;
    *out_w = cw->size;
    *out_h = cw->size;
    return 0;
}

/* ---- Public API --------------------------------------------------------- */

void lui_colorwheel_init(lui_colorwheel_t *cw)
{
    if (!cw) return;

    lui_widget_init(&cw->widget);
    cw->widget.width    = lvg_size_hug(160);
    cw->widget.height   = lvg_size_hug(160);
    cw->widget.measure  = colorwheel_measure;
    cw->widget.draw     = colorwheel_draw;
    cw->widget.on_event = colorwheel_event;

    cw->hue        = 0.0f;
    cw->saturation = 1.0f;
    cw->value      = 1.0f;
    cw->ring_width = 16;
    cw->size       = 160;
    cw->drag_mode  = 0;

    cw->bg         = LVG_COLOR_TRANSPARENT;
    cw->indicator  = LVG_COLOR_WHITE;
    cw->indicator_r = 5;

    cw->on_change      = NULL;
    cw->on_change_user = NULL;
}

void lui_colorwheel_set_hsv(lui_colorwheel_t *cw, float h, float s, float v)
{
    if (!cw) return;
    cw->hue        = fmodf(h, 360.0f);
    if (cw->hue < 0) cw->hue += 360.0f;
    cw->saturation = cw_clampf(s, 0.0f, 1.0f);
    cw->value      = cw_clampf(v, 0.0f, 1.0f);
}

lvg_color_t lui_colorwheel_get_rgb(const lui_colorwheel_t *cw)
{
    if (!cw) return LVG_COLOR_BLACK;
    return lui_hsv_to_rgb(cw->hue, cw->saturation, cw->value);
}

void lui_colorwheel_set_rgb(lui_colorwheel_t *cw, lvg_color_t rgb)
{
    if (!cw) return;
    lui_rgb_to_hsv(rgb, &cw->hue, &cw->saturation, &cw->value);
}
