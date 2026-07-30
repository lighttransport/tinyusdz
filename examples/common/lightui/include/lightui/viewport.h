/*
 * lightui/viewport.h — Image viewport with pan/zoom
 *
 * Displays a lvg_surface_t with smooth pan and zoom,
 * pixel grid at high magnification, and overlay support.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_VIEWPORT_H
#define LIGHTUI_VIEWPORT_H

#include "layout.h"
#include <lightvg/surface.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Overlay callback --------------------------------------------------- */

/**
 * User overlay callback — draw on top of the image.
 * Coordinates are in widget space (after pan/zoom transform).
 */
typedef void (*lui_viewport_overlay_fn)(lvg_canvas_t *canvas,
                                         float zoom, float pan_x, float pan_y,
                                         void *user);

/* ---- Viewport widget ---------------------------------------------------- */

typedef struct {
    lui_widget_t          widget;

    /* Image source */
    const lvg_surface_t  *image;        /* image to display (not owned)    */

    /* View transform */
    float                 pan_x, pan_y; /* pan offset in image pixels      */
    float                 zoom;         /* zoom level (1.0 = 1:1)          */
    float                 zoom_min;     /* minimum zoom (default 0.1)      */
    float                 zoom_max;     /* maximum zoom (default 32.0)     */

    /* Interaction */
    bool                  panning;      /* middle-drag or space+drag       */
    int                   pan_start_mx; /* mouse position at drag start    */
    int                   pan_start_my;
    float                 pan_start_px; /* pan_x at drag start             */
    float                 pan_start_py;

    /* Display options */
    bool                  show_grid;    /* pixel grid at zoom >= threshold */
    float                 grid_threshold; /* zoom level for pixel grid (8) */
    bool                  show_checker; /* transparent checker pattern      */
    bool                  show_crosshair; /* crosshair cursor              */
    int                   crosshair_x;  /* cursor position in image pixels */
    int                   crosshair_y;

    /* Appearance */
    lvg_color_t           bg;
    lvg_color_t           grid_color;
    lvg_color_t           checker_a;    /* checker pattern colors           */
    lvg_color_t           checker_b;
    lvg_color_t           crosshair_color;
    lvg_color_t           border_color;

    /* Overlays */
    lui_viewport_overlay_fn overlay;
    void                   *overlay_user;
} lui_viewport_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a viewport widget. */
void lui_viewport_init(lui_viewport_t *vp);

/** Set the image to display (not owned, caller must keep alive). */
void lui_viewport_set_image(lui_viewport_t *vp, const lvg_surface_t *image);

/** Set the zoom level centred on a widget-space point. */
void lui_viewport_set_zoom(lui_viewport_t *vp, float zoom,
                             int center_x, int center_y);

/** Fit the image to the viewport. */
void lui_viewport_fit(lui_viewport_t *vp);

/** Reset to 1:1 zoom centred. */
void lui_viewport_reset(lui_viewport_t *vp);

/** Convert widget-space point to image-space. */
void lui_viewport_widget_to_image(const lui_viewport_t *vp,
                                    int wx, int wy,
                                    float *ix, float *iy);

/** Convert image-space point to widget-space. */
void lui_viewport_image_to_widget(const lui_viewport_t *vp,
                                    float ix, float iy,
                                    int *wx, int *wy);

/** Get the widget node. */
static inline lui_widget_t *lui_viewport_widget(lui_viewport_t *vp) {
    return &vp->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_VIEWPORT_H */
