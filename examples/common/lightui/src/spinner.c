/*
 * spinner.c — Animated spinner / loading indicator widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/spinner.h>
#include <lightvg/canvas.h>

#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int spinner_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_spinner_t *sp = (const lui_spinner_t *)w;
    (void)user;
    *out_w = sp->size;
    *out_h = sp->size;
    return 0;
}

static void draw_arc_spinner(lui_spinner_t *sp, lvg_canvas_t *canvas,
                               int cx, int cy, int radius)
{
    float angle_start = sp->phase * 2.0f * (float)M_PI;
    float sweep = sp->arc_length * 2.0f * (float)M_PI;

    /* Draw trail (full circle, faded) */
    int steps_trail = (int)((float)radius * 3.0f);
    if (steps_trail < 16) steps_trail = 16;
    if (steps_trail > 120) steps_trail = 120;
    for (int s = 0; s <= steps_trail; s++) {
        float angle = 2.0f * (float)M_PI * (float)s / (float)steps_trail;
        float ca = cosf(angle);
        float sa = sinf(angle);
        int px = cx + (int)(ca * radius);
        int py = cy + (int)(sa * radius);
        int t = sp->thickness;
        lvg_canvas_fill_rect(canvas, px - t / 2, py - t / 2, t, t,
                              sp->trail_color);
    }

    /* Draw active arc */
    int steps = (int)(sweep * (float)radius * 0.5f);
    if (steps < 8) steps = 8;
    if (steps > 100) steps = 100;
    for (int s = 0; s <= steps; s++) {
        float frac = (float)s / (float)steps;
        float angle = angle_start + sweep * frac;
        float ca = cosf(angle);
        float sa = sinf(angle);
        int px = cx + (int)(ca * radius);
        int py = cy + (int)(sa * radius);
        int t = sp->thickness;

        /* Taper: thinner at the trailing end */
        int tw = t;
        if (frac < 0.3f)
            tw = 1 + (int)((float)(t - 1) * frac / 0.3f);

        /* Fade color at trailing end */
        int alpha = 255;
        if (frac < 0.3f)
            alpha = 80 + (int)(175.0f * frac / 0.3f);

        lvg_color_t c = LVG_COLOR_ARGB(alpha,
                                         LVG_COLOR_R(sp->color),
                                         LVG_COLOR_G(sp->color),
                                         LVG_COLOR_B(sp->color));
        lvg_canvas_fill_rect(canvas, px - tw / 2, py - tw / 2, tw, tw, c);
    }
}

static void draw_dot_spinner(lui_spinner_t *sp, lvg_canvas_t *canvas,
                               int cx, int cy, int radius)
{
    int n = sp->dot_count;
    if (n < 2) n = 2;
    float base_angle = sp->phase * 2.0f * (float)M_PI;

    for (int i = 0; i < n; i++) {
        float angle = base_angle + 2.0f * (float)M_PI * (float)i / (float)n;
        int px = cx + (int)(cosf(angle) * radius);
        int py = cy + (int)(sinf(angle) * radius);

        /* Dots fade based on distance from leading dot */
        float frac = (float)i / (float)n;
        int alpha = 255 - (int)(200.0f * frac);
        if (alpha < 55) alpha = 55;
        int dr = sp->dot_radius;
        /* Leading dots are larger */
        if (frac < 0.25f)
            dr = sp->dot_radius;
        else
            dr = sp->dot_radius - (int)((float)(sp->dot_radius - 1) * (frac - 0.25f) / 0.75f);
        if (dr < 1) dr = 1;

        lvg_color_t c = LVG_COLOR_ARGB(alpha,
                                         LVG_COLOR_R(sp->color),
                                         LVG_COLOR_G(sp->color),
                                         LVG_COLOR_B(sp->color));

        /* Draw dot as filled circle approximation */
        for (int dy = -dr; dy <= dr; dy++) {
            int hw = (int)sqrtf((float)(dr * dr - dy * dy));
            if (hw <= 0) continue;
            lvg_canvas_fill_rect(canvas, px - hw, py + dy, hw * 2, 1, c);
        }
    }
}

static void spinner_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_spinner_t *sp = (lui_spinner_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    if (LVG_COLOR_A(sp->bg) > 0)
        lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, sp->bg);

    int sz = r.width < r.height ? r.width : r.height;
    int cx = r.x + r.width / 2;
    int cy = r.y + r.height / 2;
    int radius = sz / 2 - sp->thickness - 1;
    if (radius < 4) radius = 4;

    if (sp->style == LUI_SPINNER_DOTS)
        draw_dot_spinner(sp, canvas, cx, cy, radius);
    else
        draw_arc_spinner(sp, canvas, cx, cy, radius);
}

static bool spinner_animate(lui_widget_t *w, float dt)
{
    lui_spinner_t *sp = (lui_spinner_t *)w;
    if (!sp->spinning) return false;

    sp->phase += dt * sp->speed;
    if (sp->phase >= 1.0f)
        sp->phase -= (int)sp->phase; /* wrap to 0.0–1.0 */
    return true;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_spinner_init(lui_spinner_t *sp)
{
    if (!sp) return;

    lui_widget_init(&sp->widget);
    sp->widget.width   = lvg_size_hug(24);
    sp->widget.height  = lvg_size_hug(24);
    sp->widget.measure = spinner_measure;
    sp->widget.draw    = spinner_draw;
    sp->widget.animate = spinner_animate;
    sp->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN | LUI_WIDGET_ANIMATING;

    sp->phase       = 0.0f;
    sp->speed       = 1.2f;
    sp->spinning    = true;

    sp->style       = LUI_SPINNER_ARC;
    sp->size        = 24;
    sp->thickness   = 3;
    sp->arc_length  = 0.7f;
    sp->dot_count   = 8;
    sp->dot_radius  = 3;

    sp->color       = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    sp->trail_color = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    sp->bg          = 0; /* transparent */
}

void lui_spinner_start(lui_spinner_t *sp)
{
    if (!sp) return;
    sp->spinning = true;
    sp->widget.flags |= LUI_WIDGET_ANIMATING;
}

void lui_spinner_stop(lui_spinner_t *sp)
{
    if (!sp) return;
    sp->spinning = false;
    sp->widget.flags &= ~LUI_WIDGET_ANIMATING;
}
