/*
 * lightui/dialog.h — Modal dialog box overlay widget
 *
 * Centered dialog with title bar, message, and up to 3 action buttons.
 * Optionally draws a semi-transparent backdrop and consumes all clicks
 * when modal.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_DIALOG_H
#define LIGHTUI_DIALOG_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_DIALOG_MAX_BUTTONS  3

typedef void (*lui_dialog_action_fn)(int button_index, void *user);

typedef struct {
    lui_widget_t  widget;

    /* Content */
    char          title[128];
    char          message[256];
    bool          visible;
    bool          modal;

    /* Buttons */
    char          button_labels[LUI_DIALOG_MAX_BUTTONS][32];
    int           button_count;

    /* Dimensions */
    int           dialog_width;
    int           dialog_height;
    int           title_height;
    int           button_height;

    /* Colors */
    lvg_color_t   bg_color;
    lvg_color_t   title_bg;
    lvg_color_t   title_color;
    lvg_color_t   text_color;
    lvg_color_t   border_color;
    lvg_color_t   overlay_color;    /* semi-transparent backdrop           */
    lvg_color_t   button_bg;
    lvg_color_t   button_text;

    /* Callback */
    lui_dialog_action_fn on_action;
    void                *on_action_user;
} lui_dialog_t;

/* ---- API ---------------------------------------------------------------- */

void lui_dialog_init(lui_dialog_t *dlg);
void lui_dialog_show(lui_dialog_t *dlg, const char *title, const char *message);
void lui_dialog_add_button(lui_dialog_t *dlg, const char *label);
void lui_dialog_hide(lui_dialog_t *dlg);

static inline bool lui_dialog_is_visible(const lui_dialog_t *dlg) {
    return dlg ? dlg->visible : false;
}

static inline lui_widget_t *lui_dialog_widget(lui_dialog_t *dlg) {
    return &dlg->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_DIALOG_H */
