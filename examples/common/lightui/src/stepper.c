/*
 * stepper.c — Multi-step workflow indicator (wizard)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/stepper.h>
#include <lightvg/canvas.h>
#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define STEP_CHAR_W    5
#define STEP_CHAR_H   10
#define STEP_CHAR_ADV  7
#define STEP_LABEL_GAP 6    /* gap between circle and label */
#define STEP_SPACING  60    /* distance between step centres (horizontal) */
#define STEP_VSPACING 60    /* distance between step centres (vertical)   */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static lvg_color_t stepper_status_color(const lui_stepper_t *s,
                                         lui_step_status_t status)
{
    switch (status) {
    case LUI_STEP_PENDING:  return s->pending_color;
    case LUI_STEP_ACTIVE:   return s->active_color;
    case LUI_STEP_COMPLETE: return s->complete_color;
    case LUI_STEP_ERROR:    return s->error_color;
    default:                return s->pending_color;
    }
}

/* Draw a character rectangle (placeholder text). */
static void step_draw_char(lvg_canvas_t *canvas, int x, int y,
                            lvg_color_t color)
{
    lvg_canvas_fill_rect(canvas, x, y, STEP_CHAR_W, STEP_CHAR_H, color);
}

/* Draw a short text string. */
static void step_draw_text(lvg_canvas_t *canvas, int x, int y,
                            const char *text, lvg_color_t color)
{
    int len = (int)strlen(text);
    for (int i = 0; i < len; i++) {
        step_draw_char(canvas, x + i * STEP_CHAR_ADV, y, color);
    }
}

/* Compute centre position of step circle. */
static void stepper_circle_pos(const lui_stepper_t *s, const lvg_rect_t *rect,
                                int index, int *cx, int *cy)
{
    if (s->orientation == LUI_STEPPER_HORIZONTAL) {
        int total_w = (s->step_count - 1) * STEP_SPACING;
        int start_x = rect->x + (rect->width - total_w) / 2;
        *cx = start_x + index * STEP_SPACING;
        *cy = rect->y + s->circle_radius + 4;
    } else {
        int total_h = (s->step_count - 1) * STEP_VSPACING;
        int start_y = rect->y + (rect->height - total_h) / 2;
        *cx = rect->x + s->circle_radius + 4;
        *cy = start_y + index * STEP_VSPACING;
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int stepper_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_stepper_t *s = (const lui_stepper_t *)w;
    (void)user;

    int n = s->step_count > 0 ? s->step_count : 1;
    int diam = s->circle_radius * 2;

    if (s->orientation == LUI_STEPPER_HORIZONTAL) {
        *out_w = (n - 1) * STEP_SPACING + diam + 8;
        *out_h = diam + STEP_LABEL_GAP + STEP_CHAR_H + 12;
    } else {
        *out_w = diam + STEP_LABEL_GAP + STEP_CHAR_ADV * 12 + 8;
        *out_h = (n - 1) * STEP_VSPACING + diam + 8;
    }
    return 0;
}

static void stepper_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_stepper_t *s = (lui_stepper_t *)w;
    lvg_rect_t rect = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&rect)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, rect.x, rect.y,
                          rect.width, rect.height, s->bg_color);

    int n = s->step_count;
    if (n <= 0) return;

    /* Draw connector lines between steps */
    for (int i = 0; i < n - 1; i++) {
        int cx0, cy0, cx1, cy1;
        stepper_circle_pos(s, &rect, i,     &cx0, &cy0);
        stepper_circle_pos(s, &rect, i + 1, &cx1, &cy1);

        /* Colour based on whether the connection is "completed" */
        lvg_color_t lc = s->connector_color;
        if (s->steps[i].status == LUI_STEP_COMPLETE) {
            lc = s->complete_color;
        }

        if (s->orientation == LUI_STEPPER_HORIZONTAL) {
            lvg_canvas_draw_line(canvas,
                                  cx0 + s->circle_radius, cy0,
                                  cx1 - s->circle_radius, cy1,
                                  lc, s->connector_width);
        } else {
            lvg_canvas_draw_line(canvas,
                                  cx0, cy0 + s->circle_radius,
                                  cx1, cy1 - s->circle_radius,
                                  lc, s->connector_width);
        }
    }

    /* Draw each step circle + label */
    for (int i = 0; i < n; i++) {
        int cx, cy;
        stepper_circle_pos(s, &rect, i, &cx, &cy);

        lui_step_status_t st = s->steps[i].status;
        lvg_color_t col = stepper_status_color(s, st);

        switch (st) {
        case LUI_STEP_COMPLETE:
            /* Filled circle */
            lvg_canvas_fill_circle(canvas, cx, cy, s->circle_radius, col);
            /* Checkmark "V" using two lines */
            {
                int r2 = s->circle_radius / 2;
                lvg_canvas_draw_line(canvas,
                                      cx - r2, cy,
                                      cx - r2 / 2, cy + r2,
                                      LVG_COLOR_WHITE, 2);
                lvg_canvas_draw_line(canvas,
                                      cx - r2 / 2, cy + r2,
                                      cx + r2, cy - r2,
                                      LVG_COLOR_WHITE, 2);
            }
            break;

        case LUI_STEP_ACTIVE:
            /* Highlighted ring */
            lvg_canvas_stroke_circle(canvas, cx, cy,
                                      s->circle_radius, col, 3);
            /* Step number */
            {
                char num[4];
                num[0] = '0' + (char)((i + 1) % 10);
                num[1] = '\0';
                step_draw_text(canvas,
                                cx - STEP_CHAR_W / 2,
                                cy - STEP_CHAR_H / 2,
                                num, s->text_color);
            }
            break;

        case LUI_STEP_ERROR:
            /* Red filled circle */
            lvg_canvas_fill_circle(canvas, cx, cy, s->circle_radius, col);
            /* "X" using two lines */
            {
                int r2 = s->circle_radius / 2;
                lvg_canvas_draw_line(canvas,
                                      cx - r2, cy - r2,
                                      cx + r2, cy + r2,
                                      LVG_COLOR_WHITE, 2);
                lvg_canvas_draw_line(canvas,
                                      cx + r2, cy - r2,
                                      cx - r2, cy + r2,
                                      LVG_COLOR_WHITE, 2);
            }
            break;

        case LUI_STEP_PENDING:
        default:
            /* Outlined circle */
            lvg_canvas_stroke_circle(canvas, cx, cy,
                                      s->circle_radius, col, 1);
            /* Step number */
            {
                char num[4];
                num[0] = '0' + (char)((i + 1) % 10);
                num[1] = '\0';
                step_draw_text(canvas,
                                cx - STEP_CHAR_W / 2,
                                cy - STEP_CHAR_H / 2,
                                num, s->text_color);
            }
            break;
        }

        /* Label */
        int label_len = (int)strlen(s->steps[i].label);
        if (label_len > 0) {
            if (s->orientation == LUI_STEPPER_HORIZONTAL) {
                /* Below the circle, centred */
                int lw = label_len * STEP_CHAR_ADV;
                int lx = cx - lw / 2;
                int ly = cy + s->circle_radius + STEP_LABEL_GAP;
                step_draw_text(canvas, lx, ly,
                                s->steps[i].label, s->text_color);
            } else {
                /* To the right of the circle */
                int lx = cx + s->circle_radius + STEP_LABEL_GAP;
                int ly = cy - STEP_CHAR_H / 2;
                step_draw_text(canvas, lx, ly,
                                s->steps[i].label, s->text_color);
            }
        }
    }
}

static int stepper_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_stepper_t *s = (lui_stepper_t *)w;

    if (event->type != LUI_EVENT_MOUSE_DOWN) return 0;
    if (event->data.mouse_button.button != LUI_MOUSE_LEFT) return 0;

    lvg_rect_t rect = lui_widget_absolute_rect(w);
    int mx = event->data.mouse_button.x;
    int my = event->data.mouse_button.y;

    if (!lvg_rect_contains_point(&rect, mx, my)) return 0;

    /* Check if click is on a completed step circle */
    for (int i = 0; i < s->step_count; i++) {
        if (s->steps[i].status != LUI_STEP_COMPLETE) continue;

        int cx, cy;
        stepper_circle_pos(s, &rect, i, &cx, &cy);

        int dx = mx - cx;
        int dy = my - cy;
        int r = s->circle_radius + 4; /* small hit margin */
        if (dx * dx + dy * dy <= r * r) {
            if (s->on_click) {
                s->on_click(i, s->on_click_user);
            }
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_stepper_init(lui_stepper_t *s)
{
    if (!s) return;

    lui_widget_init(&s->widget);
    s->widget.width    = lvg_size_hug(0);
    s->widget.height   = lvg_size_hug(0);
    s->widget.measure  = stepper_measure;
    s->widget.draw     = stepper_draw;
    s->widget.on_event = stepper_event;

    s->step_count   = 0;
    s->current_step = 0;
    s->orientation  = LUI_STEPPER_HORIZONTAL;

    s->circle_radius   = 12;
    s->connector_width = 2;

    s->pending_color   = LVG_COLOR_RGB(0x60, 0x64, 0x6C);
    s->active_color    = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    s->complete_color  = LVG_COLOR_RGB(0x4C, 0xAF, 0x50);
    s->error_color     = LVG_COLOR_RGB(0xE0, 0x3E, 0x3E);
    s->text_color      = LVG_COLOR_RGB(0xD0, 0xD0, 0xD0);
    s->connector_color = LVG_COLOR_RGB(0x50, 0x52, 0x56);
    s->bg_color        = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);

    s->on_click      = NULL;
    s->on_click_user = NULL;

    memset(s->steps, 0, sizeof(s->steps));
}

int lui_stepper_add_step(lui_stepper_t *s, const char *label)
{
    if (!s || s->step_count >= LUI_STEPPER_MAX) return -1;

    int idx = s->step_count;
    s->steps[idx].status = LUI_STEP_PENDING;
    s->steps[idx].label[0] = '\0';

    if (label) {
        int len = (int)strlen(label);
        if (len > 47) len = 47;
        memcpy(s->steps[idx].label, label, len);
        s->steps[idx].label[len] = '\0';
    }

    s->step_count++;
    return idx;
}

void lui_stepper_set_current(lui_stepper_t *s, int index)
{
    if (!s || index < 0 || index >= s->step_count) return;

    s->current_step = index;

    /* Update statuses: complete before current, active at current, pending after */
    for (int i = 0; i < s->step_count; i++) {
        /* Don't overwrite ERROR status */
        if (s->steps[i].status == LUI_STEP_ERROR) continue;

        if (i < index)       s->steps[i].status = LUI_STEP_COMPLETE;
        else if (i == index) s->steps[i].status = LUI_STEP_ACTIVE;
        else                 s->steps[i].status = LUI_STEP_PENDING;
    }
}

void lui_stepper_set_status(lui_stepper_t *s, int index,
                             lui_step_status_t status)
{
    if (!s || index < 0 || index >= s->step_count) return;
    s->steps[index].status = status;
}
