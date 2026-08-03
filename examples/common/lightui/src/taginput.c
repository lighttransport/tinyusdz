/*
 * taginput.c — Multi-tag entry widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/taginput.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stddef.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Constants
 * ------------------------------------------------------------------------- */

#define TAG_PAD_X       8   /* horizontal padding inside tag pill  */
#define TAG_PAD_Y       4   /* vertical padding inside tag pill    */
#define TAG_HEIGHT      18  /* total height of a tag pill          */
#define TAG_SPACING     6   /* gap between tag pills               */
#define TAG_CLOSE_SIZE  8   /* close button hit area               */
#define TAG_RADIUS      9   /* pill corner radius                  */
#define WIDGET_PAD      6   /* padding inside widget border        */
#define ROW_SPACING     4   /* vertical gap between wrapped rows   */
#define INPUT_MIN_W     40  /* minimum width for input cursor area */

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Compute pixel width of tag pill: padding + text + spacing + close + padding */
static int tag_pill_width(const char *text)
{
    int len = (int)strlen(text);
    int text_w = len * 7;
    /* pad_x + text + gap + close_size + pad_x */
    return TAG_PAD_X + text_w + 4 + TAG_CLOSE_SIZE + TAG_PAD_X;
}

/* Draw text as 5x10 character rectangles. */
static void draw_text(lvg_canvas_t *canvas, int x, int y,
                       const char *text, int max_x, lvg_color_t color)
{
    for (int i = 0; text[i] && x + 5 <= max_x; i++) {
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, x, y, 5, 10, color);
        x += 7;
    }
}

/* Compute tag layout positions. Returns number of rows used. */
typedef struct {
    int x, y, w;  /* position and width of each tag pill */
} tag_pos_t;

static int compute_tag_layout(const lui_taginput_t *ti, int avail_w,
                               tag_pos_t *positions)
{
    int cx = WIDGET_PAD;
    int cy = WIDGET_PAD;
    int rows = 1;
    int max_w = avail_w - WIDGET_PAD * 2;
    if (max_w < 1) max_w = 1;

    for (int i = 0; i < ti->tag_count; i++) {
        int pw = tag_pill_width(ti->tags[i].text);
        if (cx + pw > max_w + WIDGET_PAD && cx > WIDGET_PAD) {
            /* Wrap to next row */
            cx = WIDGET_PAD;
            cy += TAG_HEIGHT + ROW_SPACING;
            rows++;
        }
        if (positions) {
            positions[i].x = cx;
            positions[i].y = cy;
            positions[i].w = pw;
        }
        cx += pw + TAG_SPACING;
    }
    return rows;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int ti_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)user;
    const lui_taginput_t *ti = (const lui_taginput_t *)w;

    *out_w = 280;
    /* Compute rows needed at default width */
    int rows = compute_tag_layout(ti, 280, NULL);
    ((lui_taginput_t *)ti)->tag_rows = rows;
    *out_h = WIDGET_PAD * 2 + rows * TAG_HEIGHT + (rows - 1) * ROW_SPACING;
    if (*out_h < TAG_HEIGHT + WIDGET_PAD * 2)
        *out_h = TAG_HEIGHT + WIDGET_PAD * 2;
    return 0;
}

static void ti_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_taginput_t *ti = (lui_taginput_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rounded_rect(canvas, r.x, r.y, r.width, r.height, 4,
                                  ti->bg_color);

    /* Border (focus-aware) */
    lvg_color_t border = ti->border_color;
    if (w->flags & LUI_WIDGET_FOCUSABLE) {
        /* Check if any parent context has focus on us — use focus_border */
        /* Simple approach: always use border_color, user can set manually */
    }
    lvg_canvas_stroke_rounded_rect(canvas, r.x, r.y, r.width, r.height, 4,
                                    border, 1);

    /* Compute tag positions */
    tag_pos_t positions[LUI_TAGINPUT_MAX];
    ti->tag_rows = compute_tag_layout(ti, r.width, positions);

    /* Draw tag pills */
    for (int i = 0; i < ti->tag_count; i++) {
        int px = r.x + positions[i].x;
        int py = r.y + positions[i].y;
        int pw = positions[i].w;

        /* Pill background */
        lvg_canvas_fill_rounded_rect(canvas, px, py, pw, TAG_HEIGHT,
                                      TAG_RADIUS, ti->tag_bg);

        /* Tag text */
        int tx = px + TAG_PAD_X;
        int ty = py + (TAG_HEIGHT - 10) / 2;
        draw_text(canvas, tx, ty, ti->tags[i].text,
                  px + pw - TAG_PAD_X - TAG_CLOSE_SIZE - 2, ti->tag_text);

        /* Close "x" button */
        int close_x = px + pw - TAG_PAD_X - TAG_CLOSE_SIZE + 1;
        int close_y = py + (TAG_HEIGHT - TAG_CLOSE_SIZE) / 2;
        lvg_color_t close_col = (i == ti->hovered_close)
                              ? ti->tag_text : ti->tag_close_color;
        /* Draw X as two small lines */
        lvg_canvas_draw_line(canvas,
                              close_x, close_y,
                              close_x + TAG_CLOSE_SIZE - 2,
                              close_y + TAG_CLOSE_SIZE - 2,
                              close_col, 1);
        lvg_canvas_draw_line(canvas,
                              close_x + TAG_CLOSE_SIZE - 2, close_y,
                              close_x,
                              close_y + TAG_CLOSE_SIZE - 2,
                              close_col, 1);
    }

    /* Input cursor area (after last tag) */
    if (ti->input_len > 0) {
        /* Find position after last tag */
        int cx = WIDGET_PAD;
        int cy = WIDGET_PAD;
        if (ti->tag_count > 0) {
            cx = positions[ti->tag_count - 1].x +
                 positions[ti->tag_count - 1].w + TAG_SPACING;
            cy = positions[ti->tag_count - 1].y;
            if (cx + INPUT_MIN_W > r.width - WIDGET_PAD) {
                cx = WIDGET_PAD;
                cy += TAG_HEIGHT + ROW_SPACING;
            }
        }
        int tx = r.x + cx;
        int ty = r.y + cy + (TAG_HEIGHT - 10) / 2;
        draw_text(canvas, tx, ty, ti->input_buf,
                  r.x + r.width - WIDGET_PAD, ti->input_text);
    }
}

static int ti_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_taginput_t *ti = (lui_taginput_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        ti->hovered_close = -1;
        if (!lvg_rect_contains_point(&r, mx, my))
            return 0;

        /* Check close button hit areas */
        tag_pos_t positions[LUI_TAGINPUT_MAX];
        compute_tag_layout(ti, r.width, positions);

        for (int i = 0; i < ti->tag_count; i++) {
            int px = r.x + positions[i].x;
            int py = r.y + positions[i].y;
            int pw = positions[i].w;
            int close_x = px + pw - TAG_PAD_X - TAG_CLOSE_SIZE;
            int close_y = py + (TAG_HEIGHT - TAG_CLOSE_SIZE) / 2;
            lvg_rect_t close_r = lvg_rect_make(close_x, close_y,
                                                TAG_CLOSE_SIZE + 2,
                                                TAG_CLOSE_SIZE + 2);
            if (lvg_rect_contains_point(&close_r, mx, my)) {
                ti->hovered_close = i;
                break;
            }
        }
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&r, mx, my))
            return 0;

        /* Check close button clicks */
        tag_pos_t positions[LUI_TAGINPUT_MAX];
        compute_tag_layout(ti, r.width, positions);

        for (int i = 0; i < ti->tag_count; i++) {
            int px = r.x + positions[i].x;
            int py = r.y + positions[i].y;
            int pw = positions[i].w;
            int close_x = px + pw - TAG_PAD_X - TAG_CLOSE_SIZE;
            int close_y = py + (TAG_HEIGHT - TAG_CLOSE_SIZE) / 2;
            lvg_rect_t close_r = lvg_rect_make(close_x, close_y,
                                                TAG_CLOSE_SIZE + 4,
                                                TAG_CLOSE_SIZE + 4);
            if (lvg_rect_contains_point(&close_r, mx, my)) {
                if (ti->on_remove)
                    ti->on_remove(i, ti->on_remove_user);
                lui_taginput_remove_tag(ti, i);
                return 1;
            }
        }
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_taginput_init(lui_taginput_t *ti)
{
    if (!ti) return;

    lui_widget_init(&ti->widget);
    ti->widget.width    = lvg_size_hug(280);
    ti->widget.height   = lvg_size_hug(0);
    ti->widget.measure  = ti_measure;
    ti->widget.draw     = ti_draw;
    ti->widget.on_event = ti_event;
    ti->widget.flags    = LUI_WIDGET_FOCUSABLE;

    ti->tag_count       = 0;
    ti->input_buf[0]    = '\0';
    ti->input_len       = 0;
    ti->cursor          = 0;
    ti->tag_rows        = 1;
    ti->hovered_close   = -1;

    ti->bg_color        = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    ti->tag_bg          = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    ti->tag_text        = LVG_COLOR_RGB(0xE8, 0xEB, 0xF0);
    ti->tag_close_color = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    ti->input_text      = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    ti->border_color    = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    ti->focus_border    = LVG_COLOR_RGB(0x58, 0x8E, 0xCC);

    ti->on_add          = NULL;
    ti->on_add_user     = NULL;
    ti->on_remove       = NULL;
    ti->on_remove_user  = NULL;
}

int lui_taginput_add_tag(lui_taginput_t *ti, const char *text)
{
    if (!ti || !text || ti->tag_count >= LUI_TAGINPUT_MAX)
        return -1;

    int idx = ti->tag_count;
    int len = (int)strlen(text);
    if (len > 31) len = 31;
    memcpy(ti->tags[idx].text, text, len);
    ti->tags[idx].text[len] = '\0';
    ti->tag_count++;

    if (ti->on_add)
        ti->on_add(ti->tags[idx].text, ti->on_add_user);

    return idx;
}

void lui_taginput_remove_tag(lui_taginput_t *ti, int index)
{
    if (!ti || index < 0 || index >= ti->tag_count)
        return;

    /* Shift remaining tags down */
    for (int i = index; i < ti->tag_count - 1; i++)
        ti->tags[i] = ti->tags[i + 1];
    ti->tag_count--;
    ti->hovered_close = -1;
}

void lui_taginput_clear(lui_taginput_t *ti)
{
    if (!ti) return;
    ti->tag_count     = 0;
    ti->hovered_close = -1;
}

int lui_taginput_tag_count(const lui_taginput_t *ti)
{
    if (!ti) return 0;
    return ti->tag_count;
}
