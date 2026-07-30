/*
 * lightui/rating.h — Star rating input widget
 *
 * An interactive star rating control that lets users set a value by
 * clicking.  Stars are drawn as overlapping triangle pairs (Star of
 * David shape).  Supports hover preview.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_RATING_H
#define LIGHTUI_RATING_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_rating_change_fn)(int value, void *user);

/* ---- Rating widget ------------------------------------------------------ */

typedef struct {
    lui_widget_t          widget;

    int                   value;          /* current rating (0 .. max_stars)    */
    int                   max_stars;      /* total star count (default 5)       */
    int                   hovered_star;   /* star under cursor (-1 = none)      */
    bool                  allow_half;     /* reserved: half-star precision      */
    float                 half_value;     /* reserved: fractional value         */

    /* Dimensions */
    int                   star_size;      /* bounding size per star (default 20)*/
    int                   spacing;        /* gap between stars (default 4)      */

    /* Colours */
    lvg_color_t           filled_color;   /* filled star colour (gold)          */
    lvg_color_t           empty_color;    /* empty star outline (gray)          */
    lvg_color_t           hover_color;    /* hover preview colour               */
    lvg_color_t           border_color;   /* star outline colour                */

    /* Callback */
    lui_rating_change_fn  on_change;
    void                 *on_change_user;
} lui_rating_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a rating widget with a given maximum number of stars. */
void lui_rating_init(lui_rating_t *r, int max_stars);

/** Set the current rating value (clamped to [0, max_stars]). */
void lui_rating_set_value(lui_rating_t *r, int value);

/** Get the current rating value. */
int lui_rating_get_value(const lui_rating_t *r);

/** Get the widget node. */
static inline lui_widget_t *lui_rating_widget(lui_rating_t *r) {
    return &r->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_RATING_H */
