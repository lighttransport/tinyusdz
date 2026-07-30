/*
 * lightui/imagecrop.h — Crop region selector overlay for image editing
 *
 * Draws a resizable crop rectangle over an image area with darkened
 * overlay, resize handles at corners and edge midpoints, and optional
 * grid lines (rule-of-thirds or crosshair).  Supports aspect-ratio
 * locking and drag-to-move/resize interaction.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_IMAGECROP_H
#define LIGHTUI_IMAGECROP_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Drag mode ---------------------------------------------------------- */

typedef enum {
    LUI_CROP_NONE       = 0,
    LUI_CROP_MOVE       = 1,
    LUI_CROP_RESIZE_TL  = 2,
    LUI_CROP_RESIZE_TR  = 3,
    LUI_CROP_RESIZE_BL  = 4,
    LUI_CROP_RESIZE_BR  = 5,
    LUI_CROP_RESIZE_TOP    = 6,
    LUI_CROP_RESIZE_BOTTOM = 7,
    LUI_CROP_RESIZE_LEFT   = 8,
    LUI_CROP_RESIZE_RIGHT  = 9,
} lui_crop_drag_mode_t;

/* ---- Grid type ---------------------------------------------------------- */

typedef enum {
    LUI_GRID_THIRDS = 0,   /* rule-of-thirds lines */
    LUI_GRID_CENTER = 1,   /* crosshair            */
} lui_crop_grid_type_t;

/* ---- Callback ----------------------------------------------------------- */

typedef void (*lui_imagecrop_change_fn)(int x, int y, int w, int h, void *user);

/* ---- Saved crop rect for drag start ------------------------------------- */

typedef struct {
    int x, y, w, h;
} lui_crop_rect_t;

/* ---- Image crop widget -------------------------------------------------- */

typedef struct {
    lui_widget_t             widget;

    /* Crop rectangle (image coordinates) */
    int                      crop_x, crop_y, crop_w, crop_h;

    /* Full image dimensions */
    int                      image_w, image_h;

    /* Drag state */
    lui_crop_drag_mode_t     drag_mode;
    int                      drag_start_x, drag_start_y;
    lui_crop_rect_t          drag_start_crop;

    /* Aspect ratio */
    bool                     lock_aspect;
    float                    aspect_ratio;   /* w/h, 0 = free */

    /* Handle dimensions */
    int                      handle_size;    /* default 8 */

    /* Grid overlay */
    bool                     show_grid;
    lui_crop_grid_type_t     grid_type;

    /* Colours */
    lvg_color_t              overlay_color;  /* semi-transparent dark overlay */
    lvg_color_t              border_color;
    lvg_color_t              handle_color;
    lvg_color_t              grid_color;

    /* Callback */
    lui_imagecrop_change_fn  on_change;
    void                    *on_change_user;
} lui_imagecrop_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise an image crop widget with the image dimensions. */
void lui_imagecrop_init(lui_imagecrop_t *c, int image_w, int image_h);

/** Set the crop rectangle. */
void lui_imagecrop_set_crop(lui_imagecrop_t *c, int x, int y, int w, int h);

/** Set the aspect ratio (w/h). Pass 0 for free aspect. */
void lui_imagecrop_set_aspect(lui_imagecrop_t *c, float ratio);

/** Read back the current crop rectangle. */
void lui_imagecrop_get_crop(const lui_imagecrop_t *c,
                             int *out_x, int *out_y,
                             int *out_w, int *out_h);

/** Get the widget node. */
static inline lui_widget_t *lui_imagecrop_widget(lui_imagecrop_t *c) {
    return &c->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_IMAGECROP_H */
