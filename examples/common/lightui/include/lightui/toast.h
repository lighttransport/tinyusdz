/*
 * lightui/toast.h — Non-blocking notification (toast) widget
 *
 * Auto-dismissing notifications stacked from the top of the widget.
 * Supports info, success, warning, and error types with distinct colors.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TOAST_H
#define LIGHTUI_TOAST_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_TOAST_MAX  8

typedef enum {
    LUI_TOAST_INFO    = 0,
    LUI_TOAST_SUCCESS = 1,
    LUI_TOAST_WARNING = 2,
    LUI_TOAST_ERROR   = 3,
} lui_toast_type_t;

typedef struct {
    char             message[128];
    int              message_len;
    lui_toast_type_t type;
    float            duration;    /* total duration in seconds             */
    float            elapsed;     /* time elapsed since shown              */
} lui_toast_entry_t;

typedef struct {
    lui_widget_t      widget;

    /* Toast queue */
    lui_toast_entry_t toasts[LUI_TOAST_MAX];
    int               toast_count;

    /* Colors per type */
    lvg_color_t       info_bg;
    lvg_color_t       success_bg;
    lvg_color_t       warning_bg;
    lvg_color_t       error_bg;
    lvg_color_t       text_color;

    /* Dimensions */
    int               toast_height;  /* height of each toast (32)          */
    int               spacing;       /* gap between toasts (4)             */
    int               margin;        /* horizontal margin (8 per side)     */
    int               corner_radius; /* (4)                                */
} lui_toast_t;

/* ---- API ---------------------------------------------------------------- */

void lui_toast_init(lui_toast_t *t);
void lui_toast_show(lui_toast_t *t, const char *message,
                    lui_toast_type_t type, float duration_sec);

static inline lui_widget_t *lui_toast_widget(lui_toast_t *t) {
    return &t->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TOAST_H */
