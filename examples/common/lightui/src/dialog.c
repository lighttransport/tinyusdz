/*
 * dialog.c — Modal dialog box overlay widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/dialog.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int text_len_safe(const char *s, int max)
{
    int len = (int)strlen(s);
    return len > max ? max : len;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int dialog_measure(const lui_widget_t *w, int *out_w, int *out_h,
                            void *user)
{
    const lui_dialog_t *dlg = (const lui_dialog_t *)w;
    (void)user;
    *out_w = dlg->dialog_width;
    *out_h = dlg->dialog_height;
    return 0;
}

static void dialog_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_dialog_t *dlg = (lui_dialog_t *)w;
    if (!dlg->visible) return;

    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    /* Semi-transparent overlay (modal backdrop) */
    if (dlg->modal)
        lvg_canvas_fill_rect(canvas, wr.x, wr.y, wr.width, wr.height,
                              dlg->overlay_color);

    /* Center the dialog within the widget area */
    int dx = wr.x + (wr.width  - dlg->dialog_width)  / 2;
    int dy = wr.y + (wr.height - dlg->dialog_height) / 2;
    int dw = dlg->dialog_width;
    int dh = dlg->dialog_height;

    /* Dialog background (rounded rect) */
    lvg_canvas_fill_rounded_rect(canvas, dx, dy, dw, dh, 6, dlg->bg_color);
    lvg_canvas_stroke_rounded_rect(canvas, dx, dy, dw, dh, 6,
                                    dlg->border_color, 1);

    /* Title bar */
    lvg_canvas_fill_rounded_rect(canvas, dx, dy, dw, dlg->title_height + 6,
                                  6, dlg->title_bg);
    /* Flatten the bottom corners of the title bar */
    lvg_canvas_fill_rect(canvas, dx, dy + dlg->title_height,
                          dw, 6, dlg->title_bg);
    /* Title bar bottom edge */
    lvg_canvas_fill_rect(canvas, dx, dy + dlg->title_height,
                          dw, 1, dlg->border_color);

    /* Title text */
    {
        int tlen = text_len_safe(dlg->title, 127);
        int tx = dx + 8;
        int ty = dy + (dlg->title_height - 10) / 2;
        for (int i = 0; i < tlen; i++) {
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, dlg->title_color);
            tx += 7;
        }
    }

    /* Message text (word-wrap approximation: just draw chars, wrap at width) */
    {
        int mlen = text_len_safe(dlg->message, 255);
        int mx = dx + 12;
        int my = dy + dlg->title_height + 12;
        int max_x = dx + dw - 12;
        for (int i = 0; i < mlen; i++) {
            if (mx + 5 > max_x) {
                mx = dx + 12;
                my += 14;
            }
            lvg_canvas_fill_rect(canvas, mx, my, 5, 10, dlg->text_color);
            mx += 7;
        }
    }

    /* Buttons at bottom */
    if (dlg->button_count > 0) {
        int btn_spacing = 8;
        int total_btn_w = 0;
        int btn_widths[LUI_DIALOG_MAX_BUTTONS];
        for (int i = 0; i < dlg->button_count; i++) {
            int lbl_len = text_len_safe(dlg->button_labels[i], 31);
            btn_widths[i] = lbl_len * 7 + 16;
            if (btn_widths[i] < 60) btn_widths[i] = 60;
            total_btn_w += btn_widths[i];
        }
        total_btn_w += btn_spacing * (dlg->button_count - 1);

        int bx = dx + (dw - total_btn_w) / 2;
        int by = dy + dh - dlg->button_height - 10;

        for (int i = 0; i < dlg->button_count; i++) {
            /* Button background */
            lvg_canvas_fill_rounded_rect(canvas, bx, by,
                                          btn_widths[i], dlg->button_height,
                                          4, dlg->button_bg);
            lvg_canvas_stroke_rounded_rect(canvas, bx, by,
                                            btn_widths[i], dlg->button_height,
                                            4, dlg->border_color, 1);

            /* Button label */
            int lbl_len = text_len_safe(dlg->button_labels[i], 31);
            int lbl_w = lbl_len * 7;
            int lx = bx + (btn_widths[i] - lbl_w) / 2;
            int ly = by + (dlg->button_height - 10) / 2;
            for (int c = 0; c < lbl_len; c++) {
                lvg_canvas_fill_rect(canvas, lx, ly, 5, 10, dlg->button_text);
                lx += 7;
            }

            bx += btn_widths[i] + btn_spacing;
        }
    }
}

static int dialog_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_dialog_t *dlg = (lui_dialog_t *)w;
    if (!dlg->visible) return 0;

    /* Consume all clicks when modal */
    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        lvg_rect_t wr = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        /* Check button clicks */
        if (dlg->button_count > 0) {
            int dx = wr.x + (wr.width  - dlg->dialog_width)  / 2;
            int dy = wr.y + (wr.height - dlg->dialog_height) / 2;
            int dw = dlg->dialog_width;
            int dh = dlg->dialog_height;

            int btn_spacing = 8;
            int total_btn_w = 0;
            int btn_widths[LUI_DIALOG_MAX_BUTTONS];
            for (int i = 0; i < dlg->button_count; i++) {
                int lbl_len = text_len_safe(dlg->button_labels[i], 31);
                btn_widths[i] = lbl_len * 7 + 16;
                if (btn_widths[i] < 60) btn_widths[i] = 60;
                total_btn_w += btn_widths[i];
            }
            total_btn_w += btn_spacing * (dlg->button_count - 1);

            int bx = dx + (dw - total_btn_w) / 2;
            int by = dy + dh - dlg->button_height - 10;

            for (int i = 0; i < dlg->button_count; i++) {
                lvg_rect_t br = lvg_rect_make(bx, by,
                                               btn_widths[i], dlg->button_height);
                if (lvg_rect_contains_point(&br, mx, my)) {
                    if (dlg->on_action)
                        dlg->on_action(i, dlg->on_action_user);
                    return 1;
                }
                bx += btn_widths[i] + btn_spacing;
            }
        }

        /* Modal: consume click even if not on a button */
        if (dlg->modal) return 1;
    }

    /* Modal dialogs consume all mouse events */
    if (dlg->modal &&
        (event->type == LUI_EVENT_MOUSE_DOWN ||
         event->type == LUI_EVENT_MOUSE_UP   ||
         event->type == LUI_EVENT_MOUSE_MOVE ||
         event->type == LUI_EVENT_SCROLL))
        return 1;

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_dialog_init(lui_dialog_t *dlg)
{
    if (!dlg) return;

    lui_widget_init(&dlg->widget);
    dlg->widget.width    = lvg_size_fill(1);
    dlg->widget.height   = lvg_size_fill(1);
    dlg->widget.measure  = dialog_measure;
    dlg->widget.draw     = dialog_draw;
    dlg->widget.on_event = dialog_event;
    dlg->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    dlg->title[0]    = '\0';
    dlg->message[0]  = '\0';
    dlg->visible     = false;
    dlg->modal       = true;
    dlg->button_count = 0;

    dlg->dialog_width  = 320;
    dlg->dialog_height = 180;
    dlg->title_height  = 28;
    dlg->button_height = 28;

    dlg->bg_color      = LVG_COLOR_RGB(0x2A, 0x2D, 0x32);
    dlg->title_bg      = LVG_COLOR_RGB(0x22, 0x25, 0x29);
    dlg->title_color   = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    dlg->text_color    = LVG_COLOR_RGB(0xB0, 0xB3, 0xB7);
    dlg->border_color  = LVG_COLOR_RGB(0x50, 0x54, 0x5A);
    dlg->overlay_color = LVG_COLOR_ARGB(0x80, 0x00, 0x00, 0x00);
    dlg->button_bg     = LVG_COLOR_RGB(0x3A, 0x3D, 0x44);
    dlg->button_text   = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);

    dlg->on_action      = NULL;
    dlg->on_action_user = NULL;
}

void lui_dialog_show(lui_dialog_t *dlg, const char *title, const char *message)
{
    if (!dlg) return;

    if (title) {
        int len = text_len_safe(title, 127);
        memcpy(dlg->title, title, len);
        dlg->title[len] = '\0';
    }
    if (message) {
        int len = text_len_safe(message, 255);
        memcpy(dlg->message, message, len);
        dlg->message[len] = '\0';
    }
    dlg->visible = true;
}

void lui_dialog_add_button(lui_dialog_t *dlg, const char *label)
{
    if (!dlg || !label || dlg->button_count >= LUI_DIALOG_MAX_BUTTONS)
        return;

    int len = text_len_safe(label, 31);
    memcpy(dlg->button_labels[dlg->button_count], label, len);
    dlg->button_labels[dlg->button_count][len] = '\0';
    dlg->button_count++;
}

void lui_dialog_hide(lui_dialog_t *dlg)
{
    if (!dlg) return;
    dlg->visible = false;
}
