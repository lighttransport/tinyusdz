/*
 * src/button.c — Minimal button primitive
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/button.h>

static bool lui_button_visual_active(const lui_button_t *button)
{
    return button->pressed || button->highlighted;
}

void lui_button_init(lui_button_t *button, int x, int y, int width, int height)
{
    if (!button) return;
    button->rect = lvg_rect_make(x, y, width, height);
    button->armed = false;
    button->pressed = false;
    button->highlighted = false;
}

void lui_button_set_rect(lui_button_t *button, int x, int y, int width, int height)
{
    if (!button) return;
    button->rect = lvg_rect_make(x, y, width, height);
}

bool lui_button_contains(const lui_button_t *button, int x, int y)
{
    if (!button) return false;
    return lvg_rect_contains_point(&button->rect, x, y);
}

bool lui_button_is_highlighted(const lui_button_t *button)
{
    if (!button) return false;
    return lui_button_visual_active(button);
}

lui_button_result_t lui_button_handle_event(lui_button_t *button,
                                            const lui_event_t *event)
{
    if (!button || !event) return LUI_BUTTON_RESULT_NONE;

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN:
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT)
            return LUI_BUTTON_RESULT_NONE;
        if (lui_button_contains(button,
                                event->data.mouse_button.x,
                                event->data.mouse_button.y)) {
            button->armed = true;
            button->pressed = true;
            return LUI_BUTTON_RESULT_REDRAW;
        }
        return LUI_BUTTON_RESULT_NONE;

    case LUI_EVENT_MOUSE_MOVE:
        if (button->armed) {
            bool inside = lui_button_contains(button,
                                              event->data.mouse_move.x,
                                              event->data.mouse_move.y);
            if (button->pressed != inside) {
                button->pressed = inside;
                return LUI_BUTTON_RESULT_REDRAW;
            }
        }
        return LUI_BUTTON_RESULT_NONE;

    case LUI_EVENT_MOUSE_UP:
        if (event->data.mouse_button.button != LUI_MOUSE_LEFT)
            return LUI_BUTTON_RESULT_NONE;
        if (button->armed) {
            bool inside = lui_button_contains(button,
                                              event->data.mouse_button.x,
                                              event->data.mouse_button.y);
            button->armed = false;
            button->pressed = false;
            return inside
                ? (LUI_BUTTON_RESULT_REDRAW | LUI_BUTTON_RESULT_CLICKED)
                : LUI_BUTTON_RESULT_REDRAW;
        }
        return LUI_BUTTON_RESULT_NONE;

    case LUI_EVENT_WINDOW_FOCUS_OUT:
        if (button->armed || button->pressed) {
            button->armed = false;
            button->pressed = false;
            return LUI_BUTTON_RESULT_REDRAW;
        }
        return LUI_BUTTON_RESULT_NONE;

    default:
        return LUI_BUTTON_RESULT_NONE;
    }
}

void lui_button_style_softimage(lui_button_style_t *style)
{
    if (!style) return;
    style->face           = LVG_COLOR_RGB(0x65, 0x6A, 0x71);
    style->face_pressed   = LVG_COLOR_RGB(0x57, 0x6A, 0x7D);
    style->face_highlight = LVG_COLOR_RGB(0x74, 0x82, 0x92);
    style->border         = LVG_COLOR_RGB(0x22, 0x25, 0x29);
    style->edge_light     = LVG_COLOR_RGB(0x91, 0x98, 0xA1);
    style->edge_shadow    = LVG_COLOR_RGB(0x3B, 0x40, 0x47);
    style->accent         = LVG_COLOR_RGB(0x88, 0xB2, 0xDA);
}

void lui_button_draw(lvg_canvas_t *canvas,
                     const lui_button_t *button,
                     const lui_button_style_t *style)
{
    lui_button_style_t default_style;
    lvg_color_t fill;
    lvg_color_t top_left;
    lvg_color_t bottom_right;

    if (!canvas || !button || lvg_rect_is_empty(&button->rect))
        return;

    if (!style) {
        lui_button_style_softimage(&default_style);
        style = &default_style;
    }

    if (button->pressed) {
        fill = style->face_pressed;
        top_left = style->edge_shadow;
        bottom_right = style->edge_light;
    } else if (button->highlighted) {
        fill = style->face_highlight;
        top_left = style->edge_light;
        bottom_right = style->edge_shadow;
    } else {
        fill = style->face;
        top_left = style->edge_light;
        bottom_right = style->edge_shadow;
    }

    lvg_canvas_fill_rect(canvas,
                         button->rect.x, button->rect.y,
                         button->rect.width, button->rect.height,
                         style->border);

    if (button->rect.width <= 2 || button->rect.height <= 2)
        return;

    lvg_canvas_fill_rect(canvas,
                         button->rect.x + 1, button->rect.y + 1,
                         button->rect.width - 2, button->rect.height - 2,
                         fill);

    lvg_canvas_fill_rect(canvas,
                         button->rect.x + 1, button->rect.y + 1,
                         button->rect.width - 2, 1,
                         top_left);
    lvg_canvas_fill_rect(canvas,
                         button->rect.x + 1, button->rect.y + 1,
                         1, button->rect.height - 2,
                         top_left);
    lvg_canvas_fill_rect(canvas,
                         button->rect.x + 1, button->rect.y + button->rect.height - 2,
                         button->rect.width - 2, 1,
                         bottom_right);
    lvg_canvas_fill_rect(canvas,
                         button->rect.x + button->rect.width - 2, button->rect.y + 1,
                         1, button->rect.height - 2,
                         bottom_right);

    if (button->highlighted && button->rect.width > 6 && button->rect.height > 6) {
        lvg_canvas_fill_rect(canvas,
                             button->rect.x + 3, button->rect.y + 3,
                             button->rect.width - 6, 2,
                             style->accent);
    }
}
