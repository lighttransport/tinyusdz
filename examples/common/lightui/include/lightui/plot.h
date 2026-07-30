/*
 * lightui/plot.h — Plot / chart widget
 *
 * Up to 8 data series with up to 1024 points each.
 * Supports line, scatter, bar, and area-fill plot types.
 * Features auto-ranging axes, grid lines, crosshair cursor,
 * pan (middle-drag), zoom (scroll wheel), and a toggleable legend.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_PLOT_H
#define LIGHTUI_PLOT_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default capacity used by lui_plot_init().
 * Use lui_plot_init_ex() to pick a different MAX_SERIES.
 * MAX_POINTS is per-series and stays compile-time (used inside the series). */
#define LUI_PLOT_MAX_SERIES     8
#define LUI_PLOT_MAX_POINTS  1024
#define LUI_PLOT_MAX_LABEL     63  /* excl. NUL */

/* ---- Plot types --------------------------------------------------------- */

typedef enum {
    LUI_PLOT_LINE      = 0,
    LUI_PLOT_SCATTER   = 1,
    LUI_PLOT_BAR       = 2,
    LUI_PLOT_AREA_FILL = 3,
} lui_plot_type_t;

/* ---- Data point --------------------------------------------------------- */

typedef struct {
    float x;
    float y;
} lui_plot_point_t;

/* ---- Series ------------------------------------------------------------- */

typedef struct {
    char             label[LUI_PLOT_MAX_LABEL + 1];
    lui_plot_type_t  type;
    lvg_color_t      color;
    lui_plot_point_t points[LUI_PLOT_MAX_POINTS];
    int              point_count;
    bool             visible;   /* toggled via legend click */
} lui_plot_series_t;

/* ---- Axis range --------------------------------------------------------- */

typedef struct {
    float min;
    float max;
    bool  auto_range;  /* true = compute from data */
} lui_plot_range_t;

/* ---- Plot widget -------------------------------------------------------- */

typedef struct {
    lui_widget_t       widget;

    /* Series data — heap-allocated, capacity = max_series. */
    lui_plot_series_t *series;
    int                series_count;
    int                max_series;

    /* Allocator paired with `series` — used by destroy. */
    lui_alloc_fn       alloc_fn;
    lui_free_fn        free_fn;
    void              *alloc_user;

    /* Axis ranges */
    lui_plot_range_t   x_range;
    lui_plot_range_t   y_range;

    /* Axis titles (not owned; caller keeps strings alive) */
    const char        *x_title;
    const char        *y_title;

    /* Interaction state */
    bool               show_crosshair;  /* true when cursor is in plot area */
    int                cursor_x;        /* mouse position in widget coords  */
    int                cursor_y;
    bool               panning;         /* true during middle-drag          */
    int                pan_start_x;     /* mouse pos at pan start           */
    int                pan_start_y;
    float              pan_origin_xmin;  /* axis range at pan start         */
    float              pan_origin_xmax;
    float              pan_origin_ymin;
    float              pan_origin_ymax;
    bool               show_legend;     /* show/hide legend                 */

    /* Appearance — margins around plot area (pixels) */
    int                margin_left;
    int                margin_right;
    int                margin_top;
    int                margin_bottom;

    /* Colors */
    lvg_color_t        bg;
    lvg_color_t        plot_bg;
    lvg_color_t        grid_major;
    lvg_color_t        grid_minor;
    lvg_color_t        axis_color;
    lvg_color_t        text_color;
    lvg_color_t        crosshair_color;
    lvg_color_t        legend_bg;
} lui_plot_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise with default capacity using malloc/free. Pair with destroy. */
bool lui_plot_init(lui_plot_t *plot);

/** Initialise with caller-supplied capacity / allocator (NULL/NULL = default). */
bool lui_plot_init_ex(lui_plot_t *plot, int max_series,
                       lui_alloc_fn alloc_fn,
                       lui_free_fn  free_fn,
                       void        *alloc_user);

/** Free heap arrays owned by `plot`. */
void lui_plot_destroy(lui_plot_t *plot);

/**
 * Add a data series.  Returns the series index (0..7) or -1 on failure.
 * @label  Series label (copied internally, max LUI_PLOT_MAX_LABEL bytes).
 * @type   Plot type for this series.
 * @color  Series color.
 */
int lui_plot_add_series(lui_plot_t *plot, const char *label,
                        lui_plot_type_t type, lvg_color_t color);

/**
 * Set (copy) data points for a series.
 * @series  Series index returned by lui_plot_add_series().
 * @points  Array of (x, y) points to copy.
 * @count   Number of points (clamped to LUI_PLOT_MAX_POINTS).
 */
void lui_plot_set_data(lui_plot_t *plot, int series,
                       const lui_plot_point_t *points, int count);

/** Clear all points in a series. */
void lui_plot_clear_series(lui_plot_t *plot, int series);

/**
 * Set X axis range.  Pass min == 0 && max == 0 to enable auto-ranging.
 */
void lui_plot_set_x_range(lui_plot_t *plot, float min, float max);

/**
 * Set Y axis range.  Pass min == 0 && max == 0 to enable auto-ranging.
 */
void lui_plot_set_y_range(lui_plot_t *plot, float min, float max);

/** Auto-range both axes to fit all visible data. */
void lui_plot_fit_data(lui_plot_t *plot);

/** Get the widget pointer. */
static inline lui_widget_t *lui_plot_widget(lui_plot_t *plot) {
    return &plot->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_PLOT_H */
