/*
 * lightui/table.h — Table / spreadsheet widget
 *
 * A scrollable table with sortable columns, row selection, column resize,
 * and multiple cell types (text, number, checkbox, color swatch, progress).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TABLE_H
#define LIGHTUI_TABLE_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Per-instance limits — these are the defaults used by lui_table_init().
 * Use lui_table_init_ex() to pick smaller (or larger) capacities. */
#define LUI_TABLE_MAX_COLUMNS   32
#define LUI_TABLE_MAX_ROWS     512
#define LUI_TABLE_MAX_TEXT_LEN  63  /* excl. NUL */

/* ---- Cell types --------------------------------------------------------- */

typedef enum {
    LUI_CELL_TEXT = 0,
    LUI_CELL_NUMBER,
    LUI_CELL_CHECKBOX,
    LUI_CELL_COLOR_SWATCH,
    LUI_CELL_PROGRESS,
} lui_cell_type_t;

/* ---- Cell data ---------------------------------------------------------- */

typedef struct {
    lui_cell_type_t type;
    union {
        char        text[LUI_TABLE_MAX_TEXT_LEN + 1];
        double      number;
        bool        checked;
        lvg_color_t color;
        float       progress;   /* 0.0 .. 1.0 */
    } data;
} lui_table_cell_t;

/* ---- Column definition -------------------------------------------------- */

typedef struct {
    char            label[LUI_TABLE_MAX_TEXT_LEN + 1];
    int             width;       /* current width in pixels               */
    int             min_width;   /* minimum resize width                  */
    bool            resizable;   /* allow drag-resize                     */
    lui_cell_type_t cell_type;   /* default cell type for this column     */
} lui_table_column_t;

/* ---- Events ------------------------------------------------------------- */

typedef enum {
    LUI_TABLE_EVENT_NONE = 0,
    LUI_TABLE_EVENT_ROW_SELECTED,   /* row selection changed               */
    LUI_TABLE_EVENT_HEADER_CLICK,   /* header cell clicked                 */
} lui_table_event_type_t;

typedef struct {
    lui_table_event_type_t type;
    int                    row;    /* row index (ROW_SELECTED)             */
    int                    col;    /* column index (HEADER_CLICK)          */
} lui_table_event_t;

typedef void (*lui_table_event_fn)(const lui_table_event_t *event, void *user);

/* ---- Table widget ------------------------------------------------------- */

typedef struct {
    lui_widget_t       widget;

    /* Column definitions */
    lui_table_column_t columns[LUI_TABLE_MAX_COLUMNS];
    int                col_count;

    /* Cell data (row-major). Heap-allocated; capacity = max_rows*max_cols. */
    lui_table_cell_t  *cells;
    int                row_count;
    int                max_rows;
    int                max_cols;

    /* Allocator callbacks paired with the heap arrays — used by destroy. */
    lui_alloc_fn       alloc_fn;
    lui_free_fn        free_fn;
    void              *alloc_user;

    /* Sort state */
    int                sort_col;       /* column index being sorted (-1=none) */
    bool               sort_ascending;
    int               *sort_order;     /* row index mapping (capacity max_rows)*/

    /* Interaction state */
    int                selected_row;   /* selected row index (-1=none)        */
    int                hovered_row;    /* hovered row index (-1=none)         */
    int                scroll_x;       /* horizontal scroll offset in pixels  */
    int                scroll_y;       /* vertical scroll offset in pixels    */
    int                content_width;  /* total computed content width         */
    int                content_height; /* total computed content height        */

    /* Column resize state */
    int                resize_col;     /* column being resized (-1=none)      */
    int                resize_start_x; /* mouse x at drag start               */
    int                resize_start_w; /* column width at drag start          */

    /* Appearance */
    int                header_height;  /* height of header row (24)           */
    int                row_height;     /* height of each data row (22)        */

    /* Colors */
    lvg_color_t        bg;             /* background color                    */
    lvg_color_t        header_bg;      /* header background                   */
    lvg_color_t        header_text;    /* header text color                   */
    lvg_color_t        text_color;     /* cell text / number color            */
    lvg_color_t        text_selected;  /* selected row text color             */
    lvg_color_t        row_bg_even;    /* even row background                 */
    lvg_color_t        row_bg_odd;     /* odd row background                  */
    lvg_color_t        hover_bg;       /* hovered row background              */
    lvg_color_t        selected_bg;    /* selected row background             */
    lvg_color_t        grid_color;     /* grid line color                     */
    lvg_color_t        sort_arrow;     /* sort indicator arrow color          */
    lvg_color_t        scrollbar_color;/* scrollbar thumb color               */
    lvg_color_t        progress_bg;    /* progress bar background             */
    lvg_color_t        progress_fill;  /* progress bar fill                   */
    lvg_color_t        check_color;    /* checkbox check mark color           */
    int                scrollbar_width;/* scrollbar thickness (6)             */

    /* Callback */
    lui_table_event_fn on_event;
    void              *on_event_user;
} lui_table_t;

/* ---- API ---------------------------------------------------------------- */

/**
 * Initialise a table with default capacity (LUI_TABLE_MAX_ROWS rows,
 * LUI_TABLE_MAX_COLUMNS cols) using malloc/free for the heap arrays.
 * Returns true on success, false if the cell/sort_order allocation fails.
 * Pair every successful init with lui_table_destroy(); a failed init does
 * not require destroy.
 */
bool lui_table_init(lui_table_t *table);

/**
 * Initialise a table with caller-supplied capacity and allocator.
 * `alloc_fn` and `free_fn` may both be NULL to use stdlib malloc/free.
 * If non-NULL, both must be supplied (and must form a matched pair).
 */
bool lui_table_init_ex(lui_table_t *table,
                        int max_rows, int max_cols,
                        lui_alloc_fn alloc_fn,
                        lui_free_fn  free_fn,
                        void        *alloc_user);

/**
 * Free the heap arrays owned by `table`. Safe to call on a zero-initialised
 * struct (no-op). After destroy the table must not be used until re-init.
 */
void lui_table_destroy(lui_table_t *table);

/**
 * Add a column.  Returns column index (>= 0) or -1 on failure.
 * @label      Column header text (copied internally).
 * @width      Initial column width in pixels.
 * @cell_type  Default cell type for this column.
 */
int lui_table_add_column(lui_table_t *table, const char *label,
                         int width, lui_cell_type_t cell_type);

/** Set the number of data rows (0 .. LUI_TABLE_MAX_ROWS). */
void lui_table_set_row_count(lui_table_t *table, int count);

/** Set cell text (for TEXT cells). */
void lui_table_set_cell_text(lui_table_t *table, int row, int col,
                             const char *text);

/** Set cell numeric value (for NUMBER cells). */
void lui_table_set_cell_number(lui_table_t *table, int row, int col,
                               double value);

/** Set cell checked state (for CHECKBOX cells). */
void lui_table_set_cell_checked(lui_table_t *table, int row, int col,
                                bool checked);

/** Set cell color (for COLOR_SWATCH cells). */
void lui_table_set_cell_color(lui_table_t *table, int row, int col,
                              lvg_color_t color);

/** Set cell progress value 0.0 .. 1.0 (for PROGRESS cells). */
void lui_table_set_cell_progress(lui_table_t *table, int row, int col,
                                 float value);

/** Get the currently selected row index (-1 if none). */
int lui_table_get_selected_row(const lui_table_t *table);

/** Sort the table by the given column. */
void lui_table_sort(lui_table_t *table, int col, bool ascending);

/** Get the widget node. */
static inline lui_widget_t *lui_table_widget(lui_table_t *table) {
    return &table->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TABLE_H */
