/*
 * lightui/heatmap.h — Heatmap / matrix display widget
 *
 * 2D grid with color-mapped cells, row/column labels.
 * Suitable for confusion matrices, correlation matrices, attention maps.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_HEATMAP_H
#define LIGHTUI_HEATMAP_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_HEATMAP_MAX_SIZE   64  /* max rows or columns */
#define LUI_HEATMAP_MAX_LABEL  15  /* excl. NUL */

typedef enum {
    LUI_HEATMAP_SEQUENTIAL = 0,  /* blue-to-red                       */
    LUI_HEATMAP_DIVERGING  = 1,  /* blue-white-red (centered on 0)    */
    LUI_HEATMAP_VIRIDIS    = 2,  /* perceptually uniform              */
} lui_heatmap_colormap_t;

typedef struct {
    lui_widget_t           widget;

    /* Data — heap-allocated, capacity LUI_HEATMAP_MAX_SIZE × LUI_HEATMAP_MAX_SIZE.
     * Pointer-to-array preserves the natural data[r][c] indexing at call sites. */
    float                (*data)[LUI_HEATMAP_MAX_SIZE];
    int                    rows;
    int                    cols;
    float                  data_min;    /* manual range (0,0 = auto)       */
    float                  data_max;

    /* Labels — heap-allocated, each capacity LUI_HEATMAP_MAX_SIZE rows. */
    char                 (*row_labels)[LUI_HEATMAP_MAX_LABEL + 1];
    char                 (*col_labels)[LUI_HEATMAP_MAX_LABEL + 1];
    bool                   show_labels;

    /* Allocator paired with the heap arrays — used by destroy. */
    lui_alloc_fn           alloc_fn;
    lui_free_fn            free_fn;
    void                  *alloc_user;

    /* Display */
    lui_heatmap_colormap_t colormap;
    int                    cell_size;   /* cell width/height (default 20)  */
    int                    cell_gap;    /* gap between cells (1)           */
    bool                   show_values; /* show numeric values in cells    */

    /* Interaction */
    int                    hover_row;   /* -1 = none                       */
    int                    hover_col;

    /* Appearance */
    lvg_color_t            bg;
    lvg_color_t            text_color;
    lvg_color_t            border_color;
    lvg_color_t            hover_border;
} lui_heatmap_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise with default allocator (malloc/free). Pair with destroy. */
bool lui_heatmap_init(lui_heatmap_t *hm);

/** Initialise with caller-supplied allocator (NULL/NULL = default). */
bool lui_heatmap_init_ex(lui_heatmap_t *hm,
                          lui_alloc_fn alloc_fn,
                          lui_free_fn  free_fn,
                          void        *alloc_user);

/** Free heap arrays owned by `hm`. */
void lui_heatmap_destroy(lui_heatmap_t *hm);

void lui_heatmap_set_size(lui_heatmap_t *hm, int rows, int cols);
void lui_heatmap_set_cell(lui_heatmap_t *hm, int row, int col, float value);
void lui_heatmap_set_row_label(lui_heatmap_t *hm, int row, const char *label);
void lui_heatmap_set_col_label(lui_heatmap_t *hm, int col, const char *label);
void lui_heatmap_set_range(lui_heatmap_t *hm, float min_val, float max_val);
void lui_heatmap_auto_range(lui_heatmap_t *hm);
float lui_heatmap_get_cell(const lui_heatmap_t *hm, int row, int col);

static inline lui_widget_t *lui_heatmap_widget(lui_heatmap_t *hm) {
    return &hm->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_HEATMAP_H */
