/*
 * curves.c — Spline curves editor widget
 *
 * Implements a Catmull-Rom spline through user-placed control points.
 * Each channel (master, R, G, B) has its own set of points.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/curves.h>
#include <string.h>
#include <stddef.h>

static inline float cv_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Catmull-Rom spline evaluation -------------------------------------- */

/*
 * Evaluate Catmull-Rom spline at parameter t (0..1 between p1 and p2).
 * p0, p1, p2, p3 are the four control values.
 */
static float catmull_rom(float p0, float p1, float p2, float p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;
    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}

/* Sort points by x coordinate (simple insertion sort) */
static void sort_points(lui_curve_point_t *pts, int count)
{
    for (int i = 1; i < count; i++) {
        lui_curve_point_t key = pts[i];
        int j = i - 1;
        while (j >= 0 && pts[j].x > key.x) {
            pts[j + 1] = pts[j];
            j--;
        }
        pts[j + 1] = key;
    }
}

/* ---- Curve evaluation --------------------------------------------------- */

float lui_curves_evaluate(const lui_curves_t *c, lui_curves_channel_t ch,
                           float x)
{
    if (!c || ch >= LUI_CURVES_CHANNEL_COUNT) return x;
    int n = c->point_count[ch];
    const lui_curve_point_t *pts = c->points[ch];

    if (n == 0) return x;
    if (n == 1) return pts[0].y;

    /* Clamp to first/last point */
    if (x <= pts[0].x) return pts[0].y;
    if (x >= pts[n - 1].x) return pts[n - 1].y;

    /* Find segment */
    int seg = 0;
    for (int i = 0; i < n - 1; i++) {
        if (x >= pts[i].x && x <= pts[i + 1].x) {
            seg = i;
            break;
        }
    }

    float dx = pts[seg + 1].x - pts[seg].x;
    float t = (dx > 1e-6f) ? (x - pts[seg].x) / dx : 0.0f;

    /* Get 4 points for Catmull-Rom (clamp at boundaries) */
    float p0 = (seg > 0) ? pts[seg - 1].y : pts[seg].y;
    float p1 = pts[seg].y;
    float p2 = pts[seg + 1].y;
    float p3 = (seg + 2 < n) ? pts[seg + 2].y : pts[seg + 1].y;

    return cv_clampf(catmull_rom(p0, p1, p2, p3, t), 0.0f, 1.0f);
}

void lui_curves_build_lut(const lui_curves_t *c, lui_curves_channel_t ch,
                            uint8_t *out)
{
    if (!c || !out) return;
    for (int i = 0; i < 256; i++) {
        float x = (float)i / 255.0f;
        float y = lui_curves_evaluate(c, ch, x);
        int v = (int)(y * 255.0f + 0.5f);
        if (v < 0) v = 0;
        if (v > 255) v = 255;
        out[i] = (uint8_t)v;
    }
}

/* ---- Drawing ------------------------------------------------------------ */

static void curves_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_curves_t *c = (lui_curves_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    int pad = c->point_radius + 2;
    int gx = r.x + pad;
    int gy = r.y + pad;
    int gw = r.width - 2 * pad;
    int gh = r.height - 2 * pad;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, c->bg);

    /* Histogram overlay */
    if (c->histogram && gw > 0 && gh > 0) {
        for (int i = 0; i < 256 && i < gw; i++) {
            int xi = gx + (i * gw) / 256;
            float hval = c->histogram[i];
            if (hval <= 0.0f) continue;
            int bar_h = (int)(hval * gh);
            if (bar_h < 1) bar_h = 1;
            lvg_canvas_fill_rect(canvas, xi, gy + gh - bar_h, 1, bar_h,
                                  c->histogram_color);
        }
    }

    /* Grid lines */
    int divs = c->grid_divisions;
    if (divs > 0) {
        for (int i = 1; i < divs; i++) {
            int lx = gx + (gw * i) / divs;
            int ly = gy + (gh * i) / divs;
            lvg_canvas_fill_rect(canvas, lx, gy, 1, gh, c->grid_color);
            lvg_canvas_fill_rect(canvas, gx, ly, gw, 1, c->grid_color);
        }
    }

    /* Border */
    lvg_canvas_stroke_rect(canvas, gx, gy, gw, gh, c->grid_color, 1);

    /* Draw identity line (diagonal) */
    lvg_canvas_draw_line(canvas, gx, gy + gh, gx + gw, gy,
                          c->grid_color, 1);

    /* Draw curves for all channels (inactive first, active on top) */
    int active_ch = (int)c->active_channel;
    for (int pass = 0; pass < 2; pass++) {
        for (int ch = 0; ch < LUI_CURVES_CHANNEL_COUNT; ch++) {
            if ((pass == 0 && ch == active_ch) ||
                (pass == 1 && ch != active_ch))
                continue;
            if (c->point_count[ch] < 2) continue;

            lvg_color_t col = c->curve_colors[ch];
            /* Dim inactive channels */
            if (ch != active_ch) {
                int a = LVG_COLOR_A(col);
                col = (col & 0x00FFFFFF) | ((uint32_t)(a / 3) << 24);
            }

            /* Sample the curve and draw as polyline */
            int steps = gw > 128 ? 128 : gw;
            lvg_point_t pts[129];
            for (int i = 0; i <= steps; i++) {
                float x = (float)i / (float)steps;
                float y = lui_curves_evaluate(c, (lui_curves_channel_t)ch, x);
                pts[i].x = gx + (int)(x * gw);
                pts[i].y = gy + (int)((1.0f - y) * gh);
            }
            lvg_canvas_draw_polyline(canvas, pts, steps + 1, col,
                                      ch == active_ch
                                          ? c->curve_width : 1);
        }
    }

    /* Draw control points for active channel */
    int ach = c->active_channel;
    for (int i = 0; i < c->point_count[ach]; i++) {
        int px = gx + (int)(c->points[ach][i].x * gw);
        int py = gy + (int)((1.0f - c->points[ach][i].y) * gh);
        lvg_color_t pc = (i == c->drag_point || i == c->hover_point)
                             ? c->point_active : c->point_color;
        lvg_canvas_fill_circle(canvas, px, py, c->point_radius, pc);
        lvg_canvas_stroke_circle(canvas, px, py, c->point_radius,
                                  LVG_COLOR_BLACK, 1);
    }
}

/* ---- Event handling ----------------------------------------------------- */

static int curves_find_point(const lui_curves_t *c, int mx, int my,
                               int gx, int gy, int gw, int gh)
{
    int ach = c->active_channel;
    int best = -1;
    int best_dist2 = (c->point_radius + 4) * (c->point_radius + 4);

    for (int i = 0; i < c->point_count[ach]; i++) {
        int px = gx + (int)(c->points[ach][i].x * gw);
        int py = gy + (int)((1.0f - c->points[ach][i].y) * gh);
        int dx = mx - px;
        int dy = my - py;
        int d2 = dx * dx + dy * dy;
        if (d2 < best_dist2) {
            best_dist2 = d2;
            best = i;
        }
    }
    return best;
}

static int curves_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_curves_t *c = (lui_curves_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    int pad = c->point_radius + 2;
    int gx = r.x + pad;
    int gy = r.y + pad;
    int gw = r.width - 2 * pad;
    int gh = r.height - 2 * pad;

    if (gw <= 0 || gh <= 0) return 0;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        int hit = curves_find_point(c, mx, my, gx, gy, gw, gh);
        if (hit >= 0) {
            /* Don't allow dragging endpoints off the edges */
            c->drag_point = hit;
            return 1;
        }

        /* Click on graph area: add a new point */
        if (mx >= gx && mx <= gx + gw && my >= gy && my <= gy + gh) {
            float nx = (float)(mx - gx) / (float)gw;
            float ny = 1.0f - (float)(my - gy) / (float)gh;
            int idx = lui_curves_add_point(c, nx, ny);
            if (idx >= 0) {
                c->drag_point = idx;
                if (c->on_change)
                    c->on_change(c->active_channel, c->on_change_user);
            }
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        if (c->drag_point >= 0) {
            int ach = c->active_channel;
            float nx = cv_clampf((float)(mx - gx) / (float)gw, 0.0f, 1.0f);
            float ny = cv_clampf(1.0f - (float)(my - gy) / (float)gh,
                                  0.0f, 1.0f);

            /* Don't let point cross its neighbours */
            int pi = c->drag_point;
            if (pi > 0 && nx <= c->points[ach][pi - 1].x + 0.01f)
                nx = c->points[ach][pi - 1].x + 0.01f;
            if (pi < c->point_count[ach] - 1 &&
                nx >= c->points[ach][pi + 1].x - 0.01f)
                nx = c->points[ach][pi + 1].x - 0.01f;

            c->points[ach][pi].x = nx;
            c->points[ach][pi].y = ny;

            if (c->on_change)
                c->on_change(c->active_channel, c->on_change_user);
            return 1;
        }

        /* Hover detection */
        c->hover_point = curves_find_point(c, mx, my, gx, gy, gw, gh);
        break;
    }

    case LUI_EVENT_MOUSE_UP:
        if (c->drag_point >= 0) {
            c->drag_point = -1;
            return 1;
        }
        break;

    default:
        break;
    }

    return 0;
}

static int curves_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    (void)w; (void)user;
    *out_w = 256;
    *out_h = 256;
    return 0;
}

/* ---- Public API --------------------------------------------------------- */

void lui_curves_init(lui_curves_t *c)
{
    if (!c) return;
    memset(c, 0, sizeof(*c));

    lui_widget_init(&c->widget);
    c->widget.width    = lvg_size_hug(256);
    c->widget.height   = lvg_size_hug(256);
    c->widget.measure  = curves_measure;
    c->widget.draw     = curves_draw;
    c->widget.on_event = curves_event;

    c->active_channel = LUI_CURVES_MASTER;
    c->drag_point     = -1;
    c->hover_point    = -1;
    c->point_radius   = 4;
    c->grid_divisions = 4;
    c->curve_width    = 2;

    c->bg             = LVG_COLOR_RGB(0x1E, 0x1E, 0x1E);
    c->grid_color     = LVG_COLOR_RGB(0x3A, 0x3A, 0x3A);
    c->curve_colors[LUI_CURVES_MASTER] = LVG_COLOR_RGB(0xD0, 0xD0, 0xD0);
    c->curve_colors[LUI_CURVES_RED]    = LVG_COLOR_RGB(0xE0, 0x50, 0x50);
    c->curve_colors[LUI_CURVES_GREEN]  = LVG_COLOR_RGB(0x50, 0xE0, 0x50);
    c->curve_colors[LUI_CURVES_BLUE]   = LVG_COLOR_RGB(0x50, 0x70, 0xE0);
    c->point_color    = LVG_COLOR_WHITE;
    c->point_active   = LVG_COLOR_RGB(0xFF, 0xCC, 0x00);

    c->histogram       = NULL;
    c->histogram_color = LVG_COLOR_ARGB(0x40, 0x80, 0x80, 0x80);

    c->on_change      = NULL;
    c->on_change_user = NULL;

    /* Initialise all channels to identity (two endpoints) */
    lui_curves_reset_all(c);
}

void lui_curves_reset_channel(lui_curves_t *c, lui_curves_channel_t ch)
{
    if (!c || ch >= LUI_CURVES_CHANNEL_COUNT) return;
    c->points[ch][0] = (lui_curve_point_t){0.0f, 0.0f};
    c->points[ch][1] = (lui_curve_point_t){1.0f, 1.0f};
    c->point_count[ch] = 2;
}

void lui_curves_reset_all(lui_curves_t *c)
{
    if (!c) return;
    for (int i = 0; i < LUI_CURVES_CHANNEL_COUNT; i++)
        lui_curves_reset_channel(c, (lui_curves_channel_t)i);
}

void lui_curves_set_channel(lui_curves_t *c, lui_curves_channel_t ch)
{
    if (!c || ch >= LUI_CURVES_CHANNEL_COUNT) return;
    c->active_channel = ch;
    c->drag_point = -1;
    c->hover_point = -1;
}

int lui_curves_add_point(lui_curves_t *c, float x, float y)
{
    if (!c) return -1;
    int ch = c->active_channel;
    if (c->point_count[ch] >= LUI_CURVES_MAX_POINTS) return -1;

    int n = c->point_count[ch];
    c->points[ch][n].x = cv_clampf(x, 0.0f, 1.0f);
    c->points[ch][n].y = cv_clampf(y, 0.0f, 1.0f);
    c->point_count[ch] = n + 1;

    sort_points(c->points[ch], c->point_count[ch]);

    /* Find the index of the newly added point */
    for (int i = 0; i < c->point_count[ch]; i++) {
        float dx = c->points[ch][i].x - cv_clampf(x, 0.0f, 1.0f);
        float dy = c->points[ch][i].y - cv_clampf(y, 0.0f, 1.0f);
        if (dx * dx + dy * dy < 1e-6f)
            return i;
    }
    return n;
}

void lui_curves_remove_point(lui_curves_t *c, int index)
{
    if (!c) return;
    int ch = c->active_channel;
    if (index < 0 || index >= c->point_count[ch]) return;
    /* Don't remove if only 2 points left */
    if (c->point_count[ch] <= 2) return;

    for (int i = index; i < c->point_count[ch] - 1; i++)
        c->points[ch][i] = c->points[ch][i + 1];
    c->point_count[ch]--;
}
