/*
 * label.c — Text label widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/label.h>

#ifdef LUI_HAVE_FONTS

static int lui_label_measure(const lui_widget_t *widget,
                              int *out_w, int *out_h,
                              void *user)
{
    (void)user;
    const lui_label_t *label = (const lui_label_t *)widget;

    if (!label->font || !label->text) {
        *out_w = 0;
        *out_h = 0;
        return 0;
    }

    *out_w = lui_font_measure_text(label->font, label->text, -1);
    *out_h = lui_font_line_height(label->font);
    return 0;
}

static void lui_label_draw(lui_widget_t *widget, lvg_canvas_t *canvas)
{
    lui_label_t *label = (lui_label_t *)widget;
    lvg_rect_t r = lui_widget_absolute_rect(widget);

    if (!label->font || !label->text || lvg_rect_is_empty(&r))
        return;

    /* Draw background if not transparent */
    if (LVG_COLOR_A(label->bg) > 0) {
        lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, label->bg);
    }

    /* Draw text at baseline (ascent below top edge) */
    int baseline_y = r.y + lui_font_ascent(label->font);
    lui_canvas_draw_text(canvas, r.x, baseline_y,
                          label->text, -1,
                          label->font, label->color);
}

void lui_label_init(lui_label_t *label,
                    const char *text,
                    lui_font_t *font,
                    lvg_color_t color)
{
    if (!label) return;
    lui_widget_init(&label->widget);

    label->text  = text;
    label->font  = font;
    label->color = color;
    label->bg    = LVG_COLOR_TRANSPARENT;

    /* Wire up callbacks */
    label->widget.width   = lvg_size_hug(0);
    label->widget.height  = lvg_size_hug(0);
    label->widget.measure = lui_label_measure;
    label->widget.draw    = lui_label_draw;
}

void lui_label_set_text(lui_label_t *label, const char *text)
{
    if (!label) return;
    if (label->text != text) {
        label->text = text;
        lui_widget_invalidate(&label->widget);
    }
}

void lui_label_set_font(lui_label_t *label, lui_font_t *font)
{
    if (!label) return;
    label->font = font;
}

#else /* !LUI_HAVE_FONTS */

void lui_label_init(lui_label_t *label,
                    const char *text,
                    lui_font_t *font,
                    lvg_color_t color)
{
    (void)label; (void)text; (void)font; (void)color;
}

void lui_label_set_text(lui_label_t *label, const char *text)
{
    (void)label; (void)text;
}

void lui_label_set_font(lui_label_t *label, lui_font_t *font)
{
    (void)label; (void)font;
}

#endif /* LUI_HAVE_FONTS */
