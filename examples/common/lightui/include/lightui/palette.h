/*
 * lightui/palette.h — Grid of color swatches for quick color picking
 *
 * A rectangular grid of selectable colour swatches.  Clicking a swatch
 * selects it and fires the on_change callback.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_PALETTE_H
#define LIGHTUI_PALETTE_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_PALETTE_MAX_COLORS  64

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_palette_change_fn)(int index, lvg_color_t color, void *user);

/* ---- Palette widget ----------------------------------------------------- */

typedef struct {
    lui_widget_t        widget;

    /* Colors */
    lvg_color_t         colors[LUI_PALETTE_MAX_COLORS];
    int                 color_count;
    int                 selected;         /* selected index (-1 = none)      */

    /* Grid layout */
    int                 columns;          /* columns in the grid (default 8) */
    int                 swatch_size;      /* swatch side length (default 24) */
    int                 spacing;          /* gap between swatches (default 2)*/

    /* Appearance */
    lvg_color_t         border_color;
    lvg_color_t         selected_border;
    lvg_color_t         bg_color;

    /* Callback */
    lui_palette_change_fn on_change;
    void                 *on_change_user;
} lui_palette_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a palette widget with default appearance. */
void lui_palette_init(lui_palette_t *pal);

/** Add a single colour.  Returns the index or -1 if full. */
int lui_palette_add_color(lui_palette_t *pal, lvg_color_t color);

/** Replace all colours from an array. */
void lui_palette_set_colors(lui_palette_t *pal,
                             const lvg_color_t *colors, int count);

/** Remove all colours. */
void lui_palette_clear(lui_palette_t *pal);

/** Set the selected colour index (-1 to deselect). */
void lui_palette_set_selected(lui_palette_t *pal, int index);

/** Add a standard 16-colour set. */
void lui_palette_add_default_colors(lui_palette_t *pal);

/** Get the widget node. */
static inline lui_widget_t *lui_palette_widget(lui_palette_t *pal) {
    return &pal->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_PALETTE_H */
