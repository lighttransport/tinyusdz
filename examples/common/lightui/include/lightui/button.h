/*
 * lightui/button.h — Minimal button primitive
 *
 * A button owns only geometry and interaction state. Applications can draw it
 * into any canvas and feed it pointer events from any window backend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_BUTTON_H
#define LIGHTUI_BUTTON_H

#include <lightvg/canvas.h>
#include "event.h"
#include <lightvg/types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    lvg_rect_t rect;        /* logical bounds in canvas coordinates     */
    bool       armed;       /* left button pressed began inside button   */
    bool       pressed;     /* currently visually pressed               */
    bool       highlighted; /* latched app-driven highlight state       */
} lui_button_t;

typedef struct {
    lvg_color_t face;
    lvg_color_t face_pressed;
    lvg_color_t face_highlight;
    lvg_color_t border;
    lvg_color_t edge_light;
    lvg_color_t edge_shadow;
    lvg_color_t accent;
} lui_button_style_t;

typedef enum {
    LUI_BUTTON_RESULT_NONE    = 0,
    LUI_BUTTON_RESULT_REDRAW  = 1 << 0,
    LUI_BUTTON_RESULT_CLICKED = 1 << 1,
} lui_button_result_t;

void lui_button_init(lui_button_t *button, int x, int y, int width, int height);
void lui_button_set_rect(lui_button_t *button, int x, int y, int width, int height);
bool lui_button_contains(const lui_button_t *button, int x, int y);
bool lui_button_is_highlighted(const lui_button_t *button);

/*
 * Feed one input event into the button.
 *
 * Returns a bitmask of lui_button_result_t values. Applications should
 * invalidate/redraw when LUI_BUTTON_RESULT_REDRAW is returned and can latch
 * their own action state when LUI_BUTTON_RESULT_CLICKED is returned.
 */
lui_button_result_t lui_button_handle_event(lui_button_t *button,
                                            const lui_event_t *event);

/* Initialise a flat dark-grey style inspired by legacy DCC tools. */
void lui_button_style_softimage(lui_button_style_t *style);

/*
 * Draw the button using the provided style. Pass style == NULL to use the
 * default Softimage-inspired style.
 */
void lui_button_draw(lvg_canvas_t *canvas,
                     const lui_button_t *button,
                     const lui_button_style_t *style);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_BUTTON_H */
