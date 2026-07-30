/*
 * heatmap.c — Heatmap / matrix display widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/heatmap.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Color mapping
 * ------------------------------------------------------------------------- */

static float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static lvg_color_t colormap_sequential(float t)
{
    /* Blue (cold) -> Red (hot) via white midpoint */
    t = clampf(t, 0.0f, 1.0f);
    int r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = (int)(s * 255);
        g = (int)(s * 255);
        b = 255;
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = 255;
        g = (int)((1.0f - s) * 255);
        b = (int)((1.0f - s) * 255);
    }
    return LVG_COLOR_RGB(r, g, b);
}

static lvg_color_t colormap_diverging(float t)
{
    /* Blue -> White -> Red */
    t = clampf(t, 0.0f, 1.0f);
    int r, g, b;
    if (t < 0.5f) {
        float s = t * 2.0f;
        r = (int)(s * 255);
        g = (int)(s * 255);
        b = 255;
    } else {
        float s = (t - 0.5f) * 2.0f;
        r = 255;
        g = (int)((1.0f - s) * 255);
        b = (int)((1.0f - s) * 255);
    }
    return LVG_COLOR_RGB(r, g, b);
}

static lvg_color_t colormap_viridis(float t)
{
    /* Simplified viridis approximation: purple -> teal -> yellow */
    t = clampf(t, 0.0f, 1.0f);
    int r, g, b;
    if (t < 0.33f) {
        float s = t * 3.0f;
        r = (int)(68 + s * (49 - 68));
        g = (int)(1 + s * (104 - 1));
        b = (int)(84 + s * (142 - 84));
    } else if (t < 0.66f) {
        float s = (t - 0.33f) * 3.0f;
        r = (int)(49 + s * (53 - 49));
        g = (int)(104 + s * (183 - 104));
        b = (int)(142 + s * (121 - 142));
    } else {
        float s = (t - 0.66f) * 3.0f;
        r = (int)(53 + s * (253 - 53));
        g = (int)(183 + s * (231 - 183));
        b = (int)(121 + s * (37 - 121));
    }
    return LVG_COLOR_RGB((int)clampf((float)r, 0, 255),
                          (int)clampf((float)g, 0, 255),
                          (int)clampf((float)b, 0, 255));
}

static lvg_color_t apply_colormap(const lui_heatmap_t *hm, float value)
{
    float range = hm->data_max - hm->data_min;
    float t = range > 0 ? (value - hm->data_min) / range : 0.0f;

    switch (hm->colormap) {
    case LUI_HEATMAP_DIVERGING:  return colormap_diverging(t);
    case LUI_HEATMAP_VIRIDIS:    return colormap_viridis(t);
    default:                      return colormap_sequential(t);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int hm_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    const lui_heatmap_t *hm = (const lui_heatmap_t *)w;
    (void)user;
    int label_margin = hm->show_labels ? 50 : 0;
    *out_w = hm->cols * (hm->cell_size + hm->cell_gap) + label_margin;
    *out_h = hm->rows * (hm->cell_size + hm->cell_gap) + label_margin;
    return 0;
}

static void hm_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_heatmap_t *hm = (lui_heatmap_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, hm->bg);

    int label_margin = hm->show_labels ? 50 : 0;
    int grid_x = r.x + label_margin;
    int grid_y = r.y + label_margin;
    int step = hm->cell_size + hm->cell_gap;

    /* Draw cells */
    for (int row = 0; row < hm->rows; row++) {
        for (int col = 0; col < hm->cols; col++) {
            int cx = grid_x + col * step;
            int cy = grid_y + row * step;
            lvg_color_t cc = apply_colormap(hm, hm->data[row][col]);
            lvg_canvas_fill_rect(canvas, cx, cy, hm->cell_size,
                                  hm->cell_size, cc);

            /* Hover border */
            if (row == hm->hover_row && col == hm->hover_col) {
                lvg_canvas_stroke_rect(canvas, cx - 1, cy - 1,
                                        hm->cell_size + 2,
                                        hm->cell_size + 2,
                                        hm->hover_border, 1);
            }
        }
    }

    /* Row labels */
    if (hm->show_labels) {
        for (int row = 0; row < hm->rows; row++) {
            int ly = grid_y + row * step + (hm->cell_size - 10) / 2;
            int lx = r.x + 2;
            int len = (int)strlen(hm->row_labels[row]);
            for (int c = 0; c < len && c < 6; c++)
                lvg_canvas_fill_rect(canvas, lx + c * 7, ly, 5, 10,
                                      hm->text_color);
        }

        /* Column labels (vertical text approximation) */
        for (int col = 0; col < hm->cols; col++) {
            int lx = grid_x + col * step + (hm->cell_size - 5) / 2;
            int ly = r.y + 2;
            int len = (int)strlen(hm->col_labels[col]);
            for (int c = 0; c < len && c < 6; c++)
                lvg_canvas_fill_rect(canvas, lx, ly + c * 7, 5, 5,
                                      hm->text_color);
        }
    }

    /* Border */
    lvg_canvas_stroke_rect(canvas, r.x, r.y, r.width, r.height,
                            hm->border_color, 1);
}

static int hm_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_heatmap_t *hm = (lui_heatmap_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int label_margin = hm->show_labels ? 50 : 0;
        int grid_x = r.x + label_margin;
        int grid_y = r.y + label_margin;
        int step = hm->cell_size + hm->cell_gap;

        int col = (mx - grid_x) / step;
        int row = (my - grid_y) / step;

        if (col >= 0 && col < hm->cols && row >= 0 && row < hm->rows) {
            hm->hover_row = row;
            hm->hover_col = col;
        } else {
            hm->hover_row = -1;
            hm->hover_col = -1;
        }
        return 1;
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *hm_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  hm_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_heatmap_init_ex(lui_heatmap_t *hm,
                          lui_alloc_fn alloc_fn,
                          lui_free_fn  free_fn,
                          void        *alloc_user)
{
    if (!hm) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = hm_default_alloc;
        free_fn    = hm_default_free;
        alloc_user = NULL;
    }

    size_t data_bytes = (size_t)LUI_HEATMAP_MAX_SIZE *
                        sizeof(float[LUI_HEATMAP_MAX_SIZE]);
    size_t lbl_bytes  = (size_t)LUI_HEATMAP_MAX_SIZE *
                        sizeof(char[LUI_HEATMAP_MAX_LABEL + 1]);

    float (*data)[LUI_HEATMAP_MAX_SIZE] =
        (float (*)[LUI_HEATMAP_MAX_SIZE])alloc_fn(alloc_user, data_bytes);
    char  (*rlbl)[LUI_HEATMAP_MAX_LABEL + 1] = data ?
        (char (*)[LUI_HEATMAP_MAX_LABEL + 1])alloc_fn(alloc_user, lbl_bytes) : NULL;
    char  (*clbl)[LUI_HEATMAP_MAX_LABEL + 1] = rlbl ?
        (char (*)[LUI_HEATMAP_MAX_LABEL + 1])alloc_fn(alloc_user, lbl_bytes) : NULL;
    if (!data || !rlbl || !clbl) {
        if (clbl) free_fn(alloc_user, clbl);
        if (rlbl) free_fn(alloc_user, rlbl);
        if (data) free_fn(alloc_user, data);
        return false;
    }
    memset(data, 0, data_bytes);
    memset(rlbl, 0, lbl_bytes);
    memset(clbl, 0, lbl_bytes);

    memset(hm, 0, sizeof(*hm));
    hm->data       = data;
    hm->row_labels = rlbl;
    hm->col_labels = clbl;
    hm->alloc_fn   = alloc_fn;
    hm->free_fn    = free_fn;
    hm->alloc_user = alloc_user;

    lui_widget_init(&hm->widget);
    hm->widget.width   = lvg_size_hug(0);
    hm->widget.height  = lvg_size_hug(0);
    hm->widget.measure = hm_measure;
    hm->widget.draw    = hm_draw;
    hm->widget.on_event = hm_event;
    hm->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    hm->show_labels = true;
    hm->colormap  = LUI_HEATMAP_SEQUENTIAL;
    hm->cell_size = 20;
    hm->cell_gap  = 1;
    hm->hover_row = -1;
    hm->hover_col = -1;

    hm->bg           = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    hm->text_color   = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    hm->border_color = LVG_COLOR_RGB(0x40, 0x44, 0x4C);
    hm->hover_border = LVG_COLOR_RGB(0xFF, 0xFF, 0x00);

    return true;
}

bool lui_heatmap_init(lui_heatmap_t *hm)
{
    return lui_heatmap_init_ex(hm, NULL, NULL, NULL);
}

void lui_heatmap_destroy(lui_heatmap_t *hm)
{
    if (!hm) return;
    if (hm->free_fn) {
        if (hm->data)       hm->free_fn(hm->alloc_user, hm->data);
        if (hm->row_labels) hm->free_fn(hm->alloc_user, hm->row_labels);
        if (hm->col_labels) hm->free_fn(hm->alloc_user, hm->col_labels);
    }
    hm->data = NULL;
    hm->row_labels = NULL;
    hm->col_labels = NULL;
    hm->rows = 0;
    hm->cols = 0;
}

void lui_heatmap_set_size(lui_heatmap_t *hm, int rows, int cols)
{
    if (!hm) return;
    if (rows > LUI_HEATMAP_MAX_SIZE) rows = LUI_HEATMAP_MAX_SIZE;
    if (cols > LUI_HEATMAP_MAX_SIZE) cols = LUI_HEATMAP_MAX_SIZE;
    if (rows < 0) rows = 0;
    if (cols < 0) cols = 0;
    hm->rows = rows;
    hm->cols = cols;
}

void lui_heatmap_set_cell(lui_heatmap_t *hm, int row, int col, float value)
{
    if (!hm || row < 0 || row >= hm->rows || col < 0 || col >= hm->cols) return;
    hm->data[row][col] = value;
}

void lui_heatmap_set_row_label(lui_heatmap_t *hm, int row, const char *label)
{
    if (!hm || row < 0 || row >= hm->rows || !label) return;
    int len = (int)strlen(label);
    if (len > LUI_HEATMAP_MAX_LABEL) len = LUI_HEATMAP_MAX_LABEL;
    memcpy(hm->row_labels[row], label, len);
    hm->row_labels[row][len] = '\0';
}

void lui_heatmap_set_col_label(lui_heatmap_t *hm, int col, const char *label)
{
    if (!hm || col < 0 || col >= hm->cols || !label) return;
    int len = (int)strlen(label);
    if (len > LUI_HEATMAP_MAX_LABEL) len = LUI_HEATMAP_MAX_LABEL;
    memcpy(hm->col_labels[col], label, len);
    hm->col_labels[col][len] = '\0';
}

void lui_heatmap_set_range(lui_heatmap_t *hm, float min_val, float max_val)
{
    if (!hm) return;
    hm->data_min = min_val;
    hm->data_max = max_val;
}

void lui_heatmap_auto_range(lui_heatmap_t *hm)
{
    if (!hm || hm->rows <= 0 || hm->cols <= 0) return;
    float mn = hm->data[0][0], mx = hm->data[0][0];
    for (int r = 0; r < hm->rows; r++)
        for (int c = 0; c < hm->cols; c++) {
            if (hm->data[r][c] < mn) mn = hm->data[r][c];
            if (hm->data[r][c] > mx) mx = hm->data[r][c];
        }
    hm->data_min = mn;
    hm->data_max = mx;
}

float lui_heatmap_get_cell(const lui_heatmap_t *hm, int row, int col)
{
    if (!hm || row < 0 || row >= hm->rows || col < 0 || col >= hm->cols)
        return 0.0f;
    return hm->data[row][col];
}
