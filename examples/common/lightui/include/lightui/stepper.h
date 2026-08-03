/*
 * lightui/stepper.h — Multi-step workflow indicator (wizard)
 *
 * Displays a sequence of numbered steps connected by lines, indicating
 * progress through a multi-step workflow (checkout, setup wizard, etc.).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_STEPPER_H
#define LIGHTUI_STEPPER_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_STEPPER_MAX 8

/* ---- Step status -------------------------------------------------------- */

typedef enum {
    LUI_STEP_PENDING  = 0,
    LUI_STEP_ACTIVE   = 1,
    LUI_STEP_COMPLETE = 2,
    LUI_STEP_ERROR    = 3,
} lui_step_status_t;

/* ---- Step orientation --------------------------------------------------- */

typedef enum {
    LUI_STEPPER_HORIZONTAL = 0,
    LUI_STEPPER_VERTICAL   = 1,
} lui_stepper_orientation_t;

/* ---- Step entry --------------------------------------------------------- */

typedef struct {
    char               label[48];
    lui_step_status_t  status;
} lui_step_t;

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_stepper_click_fn)(int step, void *user);

/* ---- Stepper widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t               widget;

    lui_step_t                 steps[LUI_STEPPER_MAX];
    int                        step_count;
    int                        current_step;
    lui_stepper_orientation_t  orientation;

    /* Dimensions */
    int                        circle_radius;    /* default 12 */
    int                        connector_width;  /* default 2  */

    /* Colours */
    lvg_color_t                pending_color;
    lvg_color_t                active_color;
    lvg_color_t                complete_color;
    lvg_color_t                error_color;
    lvg_color_t                text_color;
    lvg_color_t                connector_color;
    lvg_color_t                bg_color;

    /* Click callback */
    lui_stepper_click_fn       on_click;
    void                      *on_click_user;
} lui_stepper_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a stepper widget. */
void lui_stepper_init(lui_stepper_t *s);

/** Add a step with a label. Returns the step index, or -1 if full. */
int lui_stepper_add_step(lui_stepper_t *s, const char *label);

/** Set the current active step (updates statuses). */
void lui_stepper_set_current(lui_stepper_t *s, int index);

/** Override the status of an individual step. */
void lui_stepper_set_status(lui_stepper_t *s, int index, lui_step_status_t status);

/** Get the widget node. */
static inline lui_widget_t *lui_stepper_widget(lui_stepper_t *s) {
    return &s->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_STEPPER_H */
