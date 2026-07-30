/*
 * toast.c — Non-blocking notification (toast) widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/toast.h>
#include <lightvg/canvas.h>

#include <string.h>

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int toast_measure(const lui_widget_t *w, int *out_w, int *out_h,
                           void *user)
{
    const lui_toast_t *t = (const lui_toast_t *)w;
    (void)user;
    *out_w = 300;
    *out_h = t->toast_count > 0
           ? t->toast_count * (t->toast_height + t->spacing) - t->spacing + t->margin * 2
           : 0;
    return 0;
}

static lvg_color_t toast_bg_for_type(const lui_toast_t *t, lui_toast_type_t type)
{
    switch (type) {
    case LUI_TOAST_SUCCESS: return t->success_bg;
    case LUI_TOAST_WARNING: return t->warning_bg;
    case LUI_TOAST_ERROR:   return t->error_bg;
    default:                return t->info_bg;
    }
}

static void toast_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_toast_t *t = (lui_toast_t *)w;
    if (t->toast_count <= 0) return;

    lvg_rect_t wr = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&wr)) return;

    int tw = wr.width - t->margin * 2;
    if (tw < 20) tw = 20;
    int tx = wr.x + t->margin;
    int ty = wr.y + t->margin;

    for (int i = 0; i < t->toast_count; i++) {
        lui_toast_entry_t *entry = &t->toasts[i];
        lvg_color_t bg = toast_bg_for_type(t, entry->type);

        /* Toast background */
        lvg_canvas_fill_rounded_rect(canvas, tx, ty, tw, t->toast_height,
                                      t->corner_radius, bg);

        /* Message text */
        int cx = tx + 8;
        int cy = ty + (t->toast_height - 10) / 2;
        int max_x = tx + tw - 8;
        for (int c = 0; c < entry->message_len && cx + 5 <= max_x; c++) {
            lvg_canvas_fill_rect(canvas, cx, cy, 5, 10, t->text_color);
            cx += 7;
        }

        ty += t->toast_height + t->spacing;
    }
}

static bool toast_animate(lui_widget_t *w, float dt)
{
    lui_toast_t *t = (lui_toast_t *)w;
    if (t->toast_count <= 0) return false;

    bool changed = false;
    int i = 0;
    while (i < t->toast_count) {
        t->toasts[i].elapsed += dt;
        if (t->toasts[i].elapsed >= t->toasts[i].duration) {
            /* Remove expired toast by shifting remaining entries down */
            for (int j = i; j < t->toast_count - 1; j++)
                t->toasts[j] = t->toasts[j + 1];
            t->toast_count--;
            changed = true;
            /* Don't increment i; re-check this slot */
        } else {
            i++;
        }
    }

    /* Clear ANIMATING flag when queue is empty */
    if (t->toast_count <= 0)
        t->widget.flags &= ~LUI_WIDGET_ANIMATING;

    return changed;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_toast_init(lui_toast_t *t)
{
    if (!t) return;

    lui_widget_init(&t->widget);
    t->widget.width   = lvg_size_fill(1);
    t->widget.height  = lvg_size_hug(0);
    t->widget.measure = toast_measure;
    t->widget.draw    = toast_draw;
    t->widget.animate = toast_animate;
    t->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    t->toast_count = 0;

    t->info_bg    = LVG_COLOR_RGB(0x2A, 0x3A, 0x50);
    t->success_bg = LVG_COLOR_RGB(0x2A, 0x50, 0x38);
    t->warning_bg = LVG_COLOR_RGB(0x50, 0x48, 0x2A);
    t->error_bg   = LVG_COLOR_RGB(0x50, 0x2A, 0x2A);
    t->text_color = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);

    t->toast_height  = 32;
    t->spacing       = 4;
    t->margin        = 8;
    t->corner_radius = 4;
}

void lui_toast_show(lui_toast_t *t, const char *message,
                    lui_toast_type_t type, float duration_sec)
{
    if (!t || !message) return;

    /* If queue is full, drop the oldest */
    if (t->toast_count >= LUI_TOAST_MAX) {
        for (int i = 0; i < t->toast_count - 1; i++)
            t->toasts[i] = t->toasts[i + 1];
        t->toast_count--;
    }

    lui_toast_entry_t *entry = &t->toasts[t->toast_count];
    int len = (int)strlen(message);
    if (len > 127) len = 127;
    memcpy(entry->message, message, len);
    entry->message[len] = '\0';
    entry->message_len = len;
    entry->type     = type;
    entry->duration = duration_sec > 0.0f ? duration_sec : 3.0f;
    entry->elapsed  = 0.0f;
    t->toast_count++;

    /* Ensure ANIMATING flag is set */
    t->widget.flags |= LUI_WIDGET_ANIMATING;
}
