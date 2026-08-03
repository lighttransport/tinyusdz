/*
 * table.c — Table / spreadsheet widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/table.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static lui_table_cell_t *cell_at(lui_table_t *t, int row, int col)
{
    if (row < 0 || row >= t->row_count) return NULL;
    if (col < 0 || col >= t->col_count) return NULL;
    return &t->cells[row * t->max_cols + col];
}

static const lui_table_cell_t *cell_at_const(const lui_table_t *t,
                                             int row, int col)
{
    if (row < 0 || row >= t->row_count) return NULL;
    if (col < 0 || col >= t->col_count) return NULL;
    return &t->cells[row * t->max_cols + col];
}

/* Map display row index to data row index through sort_order. */
static int display_to_data(const lui_table_t *t, int display_row)
{
    if (display_row < 0 || display_row >= t->row_count) return -1;
    return t->sort_order[display_row];
}

/* Compute total content width from column widths. */
static int compute_content_width(const lui_table_t *t)
{
    int w = 0;
    for (int i = 0; i < t->col_count; i++)
        w += t->columns[i].width;
    return w;
}

/* Column x offset (before scroll). */
static int column_x_offset(const lui_table_t *t, int col)
{
    int x = 0;
    for (int i = 0; i < col && i < t->col_count; i++)
        x += t->columns[i].width;
    return x;
}

/* Draw text as character rectangles: 5x10 px glyphs, 7 px advance. */
static void draw_text_rects(lvg_canvas_t *canvas, int x, int y,
                            const char *text, int max_width,
                            lvg_color_t color)
{
    int tx = x;
    for (int i = 0; text[i] != '\0'; i++) {
        if (tx + 5 > x + max_width) break;
        if (text[i] != ' ')
            lvg_canvas_fill_rect(canvas, tx, y, 5, 10, color);
        tx += 7;
    }
}

/* Draw a small sort arrow (up or down). */
static void draw_sort_arrow(lvg_canvas_t *canvas, int cx, int cy,
                            bool ascending, lvg_color_t color)
{
    if (ascending) {
        /* Up-pointing triangle */
        lvg_canvas_fill_triangle(canvas,
                                 cx, cy - 3,
                                 cx - 3, cy + 2,
                                 cx + 3, cy + 2,
                                 color);
    } else {
        /* Down-pointing triangle */
        lvg_canvas_fill_triangle(canvas,
                                 cx - 3, cy - 2,
                                 cx + 3, cy - 2,
                                 cx, cy + 3,
                                 color);
    }
}

/* -------------------------------------------------------------------------
 * Sort comparator
 * ------------------------------------------------------------------------- */

/* Global pointers for qsort comparator (C99 has no qsort_r portably). */
static const lui_table_t *s_sort_table;
static int                s_sort_col;
static bool               s_sort_asc;

static int sort_compare(const void *a, const void *b)
{
    int ra = *(const int *)a;
    int rb = *(const int *)b;
    const lui_table_cell_t *ca = cell_at_const(s_sort_table, ra, s_sort_col);
    const lui_table_cell_t *cb = cell_at_const(s_sort_table, rb, s_sort_col);
    if (!ca || !cb) return 0;

    int cmp = 0;
    switch (ca->type) {
    case LUI_CELL_TEXT:
        cmp = strcmp(ca->data.text, cb->data.text);
        break;
    case LUI_CELL_NUMBER:
        if (ca->data.number < cb->data.number) cmp = -1;
        else if (ca->data.number > cb->data.number) cmp = 1;
        break;
    case LUI_CELL_CHECKBOX:
        cmp = (int)ca->data.checked - (int)cb->data.checked;
        break;
    case LUI_CELL_PROGRESS:
        if (ca->data.progress < cb->data.progress) cmp = -1;
        else if (ca->data.progress > cb->data.progress) cmp = 1;
        break;
    case LUI_CELL_COLOR_SWATCH:
        if (ca->data.color < cb->data.color) cmp = -1;
        else if (ca->data.color > cb->data.color) cmp = 1;
        break;
    }
    return s_sort_asc ? cmp : -cmp;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int table_measure(const lui_widget_t *w, int *out_w, int *out_h,
                         void *user)
{
    (void)w; (void)user;
    *out_w = 400;
    *out_h = 300;
    return 0;
}

static void table_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_table_t *t = (lui_table_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, t->bg);

    /* Clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    t->content_width = compute_content_width(t);
    t->content_height = t->row_count * t->row_height;

    int body_h = r.height - t->header_height;
    int body_w = r.width - t->scrollbar_width;

    /* Clamp scroll */
    int max_scroll_y = t->content_height - body_h;
    if (max_scroll_y < 0) max_scroll_y = 0;
    if (t->scroll_y < 0) t->scroll_y = 0;
    if (t->scroll_y > max_scroll_y) t->scroll_y = max_scroll_y;

    int max_scroll_x = t->content_width - body_w;
    if (max_scroll_x < 0) max_scroll_x = 0;
    if (t->scroll_x < 0) t->scroll_x = 0;
    if (t->scroll_x > max_scroll_x) t->scroll_x = max_scroll_x;

    /* ---- Draw header ---- */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, t->header_height,
                         t->header_bg);

    for (int c = 0; c < t->col_count; c++) {
        int cx = r.x + column_x_offset(t, c) - t->scroll_x;
        int cw = t->columns[c].width;

        /* Header text */
        int text_x = cx + 4;
        int text_y = r.y + (t->header_height - 10) / 2;
        draw_text_rects(canvas, text_x, text_y, t->columns[c].label,
                        cw - 8 - (t->sort_col == c ? 10 : 0),
                        t->header_text);

        /* Sort indicator */
        if (t->sort_col == c) {
            int arrow_x = cx + cw - 10;
            int arrow_y = r.y + t->header_height / 2;
            draw_sort_arrow(canvas, arrow_x, arrow_y,
                            t->sort_ascending, t->sort_arrow);
        }

        /* Column border */
        lvg_canvas_fill_rect(canvas, cx + cw - 1, r.y, 1,
                             t->header_height, t->grid_color);
    }

    /* Header bottom border */
    lvg_canvas_fill_rect(canvas, r.x, r.y + t->header_height - 1,
                         r.width, 1, t->grid_color);

    /* ---- Draw rows ---- */
    int body_y = r.y + t->header_height;

    for (int di = 0; di < t->row_count; di++) {
        int row_y = body_y + di * t->row_height - t->scroll_y;

        /* Skip rows outside visible area */
        if (row_y + t->row_height <= body_y) continue;
        if (row_y >= r.y + r.height) break;

        int data_row = display_to_data(t, di);
        if (data_row < 0) continue;

        /* Row background */
        lvg_color_t row_bg;
        if (data_row == t->selected_row) {
            row_bg = t->selected_bg;
        } else if (data_row == t->hovered_row) {
            row_bg = t->hover_bg;
        } else {
            row_bg = (di & 1) ? t->row_bg_odd : t->row_bg_even;
        }
        lvg_canvas_fill_rect(canvas, r.x, row_y, r.width, t->row_height,
                             row_bg);

        /* Draw cells */
        for (int c = 0; c < t->col_count; c++) {
            int cx = r.x + column_x_offset(t, c) - t->scroll_x;
            int cw = t->columns[c].width;
            const lui_table_cell_t *cell = cell_at_const(t, data_row, c);
            if (!cell) continue;

            lvg_color_t tc = (data_row == t->selected_row)
                           ? t->text_selected : t->text_color;

            int cell_text_y = row_y + (t->row_height - 10) / 2;

            switch (cell->type) {
            case LUI_CELL_TEXT:
                draw_text_rects(canvas, cx + 4, cell_text_y,
                                cell->data.text, cw - 8, tc);
                break;

            case LUI_CELL_NUMBER: {
                char buf[32];
                snprintf(buf, sizeof(buf), "%.2f", cell->data.number);
                /* Right-align: compute text width */
                int len = (int)strlen(buf);
                int tw = len * 7;
                int tx = cx + cw - 4 - tw;
                if (tx < cx + 4) tx = cx + 4;
                draw_text_rects(canvas, tx, cell_text_y, buf, cw - 8, tc);
                break;
            }

            case LUI_CELL_CHECKBOX: {
                int box_sz = 12;
                int bx = cx + (cw - box_sz) / 2;
                int by = row_y + (t->row_height - box_sz) / 2;
                lvg_canvas_stroke_rect(canvas, bx, by, box_sz, box_sz,
                                       t->grid_color, 1);
                if (cell->data.checked) {
                    /* Checkmark lines */
                    int mx = bx + box_sz / 2;
                    int my = by + box_sz / 2;
                    lvg_canvas_draw_line(canvas,
                                         mx - box_sz / 4, my,
                                         mx - box_sz / 8, my + box_sz / 4,
                                         t->check_color, 2);
                    lvg_canvas_draw_line(canvas,
                                         mx - box_sz / 8, my + box_sz / 4,
                                         mx + box_sz / 4, my - box_sz / 4,
                                         t->check_color, 2);
                }
                break;
            }

            case LUI_CELL_COLOR_SWATCH: {
                int sw_sz = 14;
                int sx = cx + (cw - sw_sz) / 2;
                int sy = row_y + (t->row_height - sw_sz) / 2;
                lvg_canvas_fill_rect(canvas, sx, sy, sw_sz, sw_sz,
                                     cell->data.color);
                lvg_canvas_stroke_rect(canvas, sx, sy, sw_sz, sw_sz,
                                       t->grid_color, 1);
                break;
            }

            case LUI_CELL_PROGRESS: {
                int bar_h = 8;
                int bar_x = cx + 4;
                int bar_w = cw - 8;
                int bar_y = row_y + (t->row_height - bar_h) / 2;
                if (bar_w < 4) bar_w = 4;
                lvg_canvas_fill_rect(canvas, bar_x, bar_y, bar_w, bar_h,
                                     t->progress_bg);
                float pv = cell->data.progress;
                if (pv < 0.0f) pv = 0.0f;
                if (pv > 1.0f) pv = 1.0f;
                int fill_w = (int)(pv * (float)bar_w);
                if (fill_w > 0) {
                    lvg_canvas_fill_rect(canvas, bar_x, bar_y,
                                         fill_w, bar_h, t->progress_fill);
                }
                break;
            }
            } /* switch */

            /* Column grid line */
            lvg_canvas_fill_rect(canvas, cx + cw - 1, row_y, 1,
                                 t->row_height, t->grid_color);
        }
    }

    /* ---- Vertical scrollbar ---- */
    if (t->content_height > body_h && body_h > 0) {
        int sb_x = r.x + r.width - t->scrollbar_width;
        int thumb_h = (body_h * body_h) / t->content_height;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = body_y;
        if (max_scroll_y > 0)
            thumb_y += (t->scroll_y * (body_h - thumb_h)) / max_scroll_y;
        lvg_canvas_fill_rounded_rect(canvas, sb_x, thumb_y,
                                     t->scrollbar_width, thumb_h,
                                     t->scrollbar_width / 2,
                                     t->scrollbar_color);
    }

    /* ---- Horizontal scrollbar ---- */
    if (t->content_width > body_w && body_w > 0) {
        int sb_y = r.y + r.height - t->scrollbar_width;
        int thumb_w = (body_w * body_w) / t->content_width;
        if (thumb_w < 20) thumb_w = 20;
        int thumb_x = r.x;
        if (max_scroll_x > 0)
            thumb_x += (t->scroll_x * (body_w - thumb_w)) / max_scroll_x;
        lvg_canvas_fill_rounded_rect(canvas, thumb_x, sb_y,
                                     thumb_w, t->scrollbar_width,
                                     t->scrollbar_width / 2,
                                     t->scrollbar_color);
    }

    canvas->_clip = old_clip;
}

/* Detect if mouse is near a column border for resize (within 4 px). */
static int hit_column_border(const lui_table_t *t, const lvg_rect_t *r,
                             int mx, int my)
{
    if (my < r->y || my >= r->y + t->header_height)
        return -1;
    for (int c = 0; c < t->col_count; c++) {
        int edge_x = r->x + column_x_offset(t, c) + t->columns[c].width
                   - t->scroll_x;
        if (mx >= edge_x - 4 && mx <= edge_x + 4 && t->columns[c].resizable)
            return c;
    }
    return -1;
}

/* Detect which column header was clicked. */
static int hit_column_header(const lui_table_t *t, const lvg_rect_t *r,
                             int mx)
{
    for (int c = 0; c < t->col_count; c++) {
        int cx = r->x + column_x_offset(t, c) - t->scroll_x;
        int cw = t->columns[c].width;
        if (mx >= cx && mx < cx + cw)
            return c;
    }
    return -1;
}

static int table_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_table_t *t = (lui_table_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (!lvg_rect_contains_point(&r, mx, my)) return 0;

        /* Check column resize */
        int resize = hit_column_border(t, &r, mx, my);
        if (resize >= 0) {
            t->resize_col = resize;
            t->resize_start_x = mx;
            t->resize_start_w = t->columns[resize].width;
            return 1;
        }

        /* Header click -> sort */
        if (my < r.y + t->header_height) {
            int col = hit_column_header(t, &r, mx);
            if (col >= 0) {
                bool asc = true;
                if (t->sort_col == col)
                    asc = !t->sort_ascending;
                lui_table_sort(t, col, asc);

                if (t->on_event) {
                    lui_table_event_t ev;
                    ev.type = LUI_TABLE_EVENT_HEADER_CLICK;
                    ev.row = -1;
                    ev.col = col;
                    t->on_event(&ev, t->on_event_user);
                }
            }
            return 1;
        }

        /* Row click -> select */
        int body_y = r.y + t->header_height;
        int rel_y = my - body_y + t->scroll_y;
        int display_row = rel_y / t->row_height;
        if (display_row >= 0 && display_row < t->row_count) {
            int data_row = display_to_data(t, display_row);
            int old_sel = t->selected_row;
            t->selected_row = data_row;

            /* Toggle checkbox if clicked on a checkbox cell */
            if (data_row >= 0) {
                int col = hit_column_header(t, &r, mx);
                if (col >= 0) {
                    lui_table_cell_t *cell = cell_at(t, data_row, col);
                    if (cell && cell->type == LUI_CELL_CHECKBOX)
                        cell->data.checked = !cell->data.checked;
                }
            }

            if (old_sel != data_row && t->on_event) {
                lui_table_event_t ev;
                ev.type = LUI_TABLE_EVENT_ROW_SELECTED;
                ev.row = data_row;
                ev.col = -1;
                t->on_event(&ev, t->on_event_user);
            }
        }
        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_UP) {
        if (t->resize_col >= 0) {
            t->resize_col = -1;
            return 1;
        }
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        /* Column resize drag */
        if (t->resize_col >= 0) {
            int delta = mx - t->resize_start_x;
            int new_w = t->resize_start_w + delta;
            int min_w = t->columns[t->resize_col].min_width;
            if (min_w < 20) min_w = 20;
            if (new_w < min_w) new_w = min_w;
            t->columns[t->resize_col].width = new_w;
            return 1;
        }

        if (!lvg_rect_contains_point(&r, mx, my)) {
            t->hovered_row = -1;
            return 0;
        }

        /* Row hover */
        int body_y = r.y + t->header_height;
        if (my >= body_y) {
            int rel_y = my - body_y + t->scroll_y;
            int display_row = rel_y / t->row_height;
            if (display_row >= 0 && display_row < t->row_count)
                t->hovered_row = display_to_data(t, display_row);
            else
                t->hovered_row = -1;
        } else {
            t->hovered_row = -1;
        }
        return 1;
    }

    if (event->type == LUI_EVENT_SCROLL) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.scroll.x, event->data.scroll.y)) {
            /* Vertical scroll */
            int delta_y = (int)(event->data.scroll.delta_y * -3.0f)
                        * t->row_height;
            t->scroll_y += delta_y;

            /* Horizontal scroll (shift+scroll or trackpad) */
            int delta_x = (int)(event->data.scroll.delta_x * -3.0f) * 20;
            t->scroll_x += delta_x;

            /* Clamp */
            int body_h = r.height - t->header_height;
            int max_sy = t->content_height - body_h;
            if (max_sy < 0) max_sy = 0;
            if (t->scroll_y < 0) t->scroll_y = 0;
            if (t->scroll_y > max_sy) t->scroll_y = max_sy;

            int body_w = r.width - t->scrollbar_width;
            int max_sx = t->content_width - body_w;
            if (max_sx < 0) max_sx = 0;
            if (t->scroll_x < 0) t->scroll_x = 0;
            if (t->scroll_x > max_sx) t->scroll_x = max_sx;

            return 1;
        }
    }

    if (event->type == LUI_EVENT_KEY_DOWN) {
        switch (event->data.key.key) {
        case LUI_KEY_UP: {
            /* Find display row of selected, move up */
            for (int di = 0; di < t->row_count; di++) {
                if (display_to_data(t, di) == t->selected_row && di > 0) {
                    int new_sel = display_to_data(t, di - 1);
                    t->selected_row = new_sel;
                    if (t->on_event) {
                        lui_table_event_t ev;
                        ev.type = LUI_TABLE_EVENT_ROW_SELECTED;
                        ev.row = new_sel;
                        ev.col = -1;
                        t->on_event(&ev, t->on_event_user);
                    }
                    break;
                }
            }
            return 1;
        }
        case LUI_KEY_DOWN: {
            for (int di = 0; di < t->row_count; di++) {
                if (display_to_data(t, di) == t->selected_row
                    && di + 1 < t->row_count) {
                    int new_sel = display_to_data(t, di + 1);
                    t->selected_row = new_sel;
                    if (t->on_event) {
                        lui_table_event_t ev;
                        ev.type = LUI_TABLE_EVENT_ROW_SELECTED;
                        ev.row = new_sel;
                        ev.col = -1;
                        t->on_event(&ev, t->on_event_user);
                    }
                    break;
                }
            }
            return 1;
        }
        case LUI_KEY_HOME:
            if (t->row_count > 0) {
                t->selected_row = display_to_data(t, 0);
                t->scroll_y = 0;
            }
            return 1;
        case LUI_KEY_END:
            if (t->row_count > 0) {
                t->selected_row = display_to_data(t, t->row_count - 1);
            }
            return 1;
        default:
            break;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

/* Default allocator — passes through to stdlib malloc/free.
 * Matches the lui_alloc_fn signature: userdata is ignored. */
static void *table_default_alloc(void *userdata, size_t size)
{
    (void)userdata;
    return malloc(size);
}
static void table_default_free(void *userdata, void *ptr)
{
    (void)userdata;
    free(ptr);
}

bool lui_table_init_ex(lui_table_t *table,
                        int max_rows, int max_cols,
                        lui_alloc_fn alloc_fn,
                        lui_free_fn  free_fn,
                        void        *alloc_user)
{
    if (!table) return false;
    if (max_rows <= 0 || max_cols <= 0) return false;
    if (max_cols > LUI_TABLE_MAX_COLUMNS) return false;

    /* alloc_fn / free_fn must be either both NULL (use default) or both set. */
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = table_default_alloc;
        free_fn    = table_default_free;
        alloc_user = NULL;
    }

    /* Compute and check sizes for overflow before calling alloc_fn. */
    size_t cell_count = (size_t)max_rows * (size_t)max_cols;
    if (cell_count / (size_t)max_cols != (size_t)max_rows) return false;
    size_t cells_bytes = cell_count * sizeof(lui_table_cell_t);
    if (cells_bytes / sizeof(lui_table_cell_t) != cell_count) return false;
    size_t order_bytes = (size_t)max_rows * sizeof(int);

    lui_table_cell_t *cells = (lui_table_cell_t *)alloc_fn(alloc_user, cells_bytes);
    if (!cells) return false;
    int *sort_order = (int *)alloc_fn(alloc_user, order_bytes);
    if (!sort_order) {
        free_fn(alloc_user, cells);
        return false;
    }
    memset(cells, 0, cells_bytes);
    memset(sort_order, 0, order_bytes);

    memset(table, 0, sizeof(*table));

    table->cells       = cells;
    table->sort_order  = sort_order;
    table->max_rows    = max_rows;
    table->max_cols    = max_cols;
    table->alloc_fn    = alloc_fn;
    table->free_fn     = free_fn;
    table->alloc_user  = alloc_user;

    lui_widget_init(&table->widget);
    table->widget.width    = lvg_size_fill(1);
    table->widget.height   = lvg_size_fill(1);
    table->widget.measure  = table_measure;
    table->widget.draw     = table_draw;
    table->widget.on_event = table_event;
    table->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN | LUI_WIDGET_FOCUSABLE;

    table->col_count     = 0;
    table->row_count     = 0;

    table->sort_col      = -1;
    table->sort_ascending = true;

    table->selected_row  = -1;
    table->hovered_row   = -1;
    table->scroll_x      = 0;
    table->scroll_y      = 0;
    table->content_width = 0;
    table->content_height = 0;

    table->resize_col    = -1;

    table->header_height = 24;
    table->row_height    = 22;

    /* Dark theme colors matching existing widgets */
    table->bg            = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    table->header_bg     = LVG_COLOR_RGB(0x28, 0x2A, 0x36);
    table->header_text   = LVG_COLOR_RGB(0xC0, 0xC4, 0xCC);
    table->text_color    = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    table->text_selected = LVG_COLOR_WHITE;
    table->row_bg_even   = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    table->row_bg_odd    = LVG_COLOR_RGB(0x24, 0x26, 0x34);
    table->hover_bg      = LVG_COLOR_RGB(0x30, 0x34, 0x3C);
    table->selected_bg   = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    table->grid_color    = LVG_COLOR_RGB(0x38, 0x3C, 0x44);
    table->sort_arrow    = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    table->scrollbar_color = LVG_COLOR_ARGB(0x80, 0x80, 0x84, 0x8A);
    table->progress_bg   = LVG_COLOR_RGB(0x38, 0x3C, 0x44);
    table->progress_fill = LVG_COLOR_RGB(0x58, 0x9C, 0xE0);
    table->check_color   = LVG_COLOR_WHITE;
    table->scrollbar_width = 6;

    table->on_event      = NULL;
    table->on_event_user = NULL;

    return true;
}

bool lui_table_init(lui_table_t *table)
{
    return lui_table_init_ex(table,
                              LUI_TABLE_MAX_ROWS, LUI_TABLE_MAX_COLUMNS,
                              NULL, NULL, NULL);
}

void lui_table_destroy(lui_table_t *table)
{
    if (!table) return;
    if (table->free_fn) {
        if (table->cells)      table->free_fn(table->alloc_user, table->cells);
        if (table->sort_order) table->free_fn(table->alloc_user, table->sort_order);
    }
    table->cells      = NULL;
    table->sort_order = NULL;
    table->max_rows   = 0;
    table->max_cols   = 0;
    table->row_count  = 0;
    table->col_count  = 0;
}

int lui_table_add_column(lui_table_t *table, const char *label,
                         int width, lui_cell_type_t cell_type)
{
    if (!table || !label || table->col_count >= table->max_cols)
        return -1;

    int idx = table->col_count;
    lui_table_column_t *col = &table->columns[idx];

    int len = (int)strlen(label);
    if (len > LUI_TABLE_MAX_TEXT_LEN) len = LUI_TABLE_MAX_TEXT_LEN;
    memcpy(col->label, label, (size_t)len);
    col->label[len] = '\0';

    col->width     = width > 0 ? width : 80;
    col->min_width = 40;
    col->resizable = true;
    col->cell_type = cell_type;

    table->col_count++;
    return idx;
}

void lui_table_set_row_count(lui_table_t *table, int count)
{
    if (!table) return;
    if (count < 0) count = 0;
    if (count > table->max_rows) count = table->max_rows;

    /* Clear new cells if growing */
    if (count > table->row_count) {
        for (int r = table->row_count; r < count; r++) {
            for (int c = 0; c < table->max_cols; c++) {
                lui_table_cell_t *cell =
                    &table->cells[r * table->max_cols + c];
                memset(cell, 0, sizeof(*cell));
                if (c < table->col_count)
                    cell->type = table->columns[c].cell_type;
            }
        }
    }

    table->row_count = count;

    /* Reset sort order to identity */
    for (int i = 0; i < count; i++)
        table->sort_order[i] = i;

    /* Re-apply sort if active */
    if (table->sort_col >= 0 && table->sort_col < table->col_count)
        lui_table_sort(table, table->sort_col, table->sort_ascending);

    /* Clamp selection */
    if (table->selected_row >= count)
        table->selected_row = -1;
}

void lui_table_set_cell_text(lui_table_t *table, int row, int col,
                             const char *text)
{
    if (!table || !text) return;
    lui_table_cell_t *cell = cell_at(table, row, col);
    if (!cell) return;
    cell->type = LUI_CELL_TEXT;
    int len = (int)strlen(text);
    if (len > LUI_TABLE_MAX_TEXT_LEN) len = LUI_TABLE_MAX_TEXT_LEN;
    memcpy(cell->data.text, text, (size_t)len);
    cell->data.text[len] = '\0';
}

void lui_table_set_cell_number(lui_table_t *table, int row, int col,
                               double value)
{
    if (!table) return;
    lui_table_cell_t *cell = cell_at(table, row, col);
    if (!cell) return;
    cell->type = LUI_CELL_NUMBER;
    cell->data.number = value;
}

void lui_table_set_cell_checked(lui_table_t *table, int row, int col,
                                bool checked)
{
    if (!table) return;
    lui_table_cell_t *cell = cell_at(table, row, col);
    if (!cell) return;
    cell->type = LUI_CELL_CHECKBOX;
    cell->data.checked = checked;
}

void lui_table_set_cell_color(lui_table_t *table, int row, int col,
                              lvg_color_t color)
{
    if (!table) return;
    lui_table_cell_t *cell = cell_at(table, row, col);
    if (!cell) return;
    cell->type = LUI_CELL_COLOR_SWATCH;
    cell->data.color = color;
}

void lui_table_set_cell_progress(lui_table_t *table, int row, int col,
                                 float value)
{
    if (!table) return;
    lui_table_cell_t *cell = cell_at(table, row, col);
    if (!cell) return;
    cell->type = LUI_CELL_PROGRESS;
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    cell->data.progress = value;
}

int lui_table_get_selected_row(const lui_table_t *table)
{
    if (!table) return -1;
    return table->selected_row;
}

void lui_table_sort(lui_table_t *table, int col, bool ascending)
{
    if (!table) return;
    if (col < 0 || col >= table->col_count) return;
    if (table->row_count <= 0) return;

    table->sort_col = col;
    table->sort_ascending = ascending;

    /* Reset sort order to identity before sorting */
    for (int i = 0; i < table->row_count; i++)
        table->sort_order[i] = i;

    /* Sort using qsort with static context */
    s_sort_table = table;
    s_sort_col = col;
    s_sort_asc = ascending;
    qsort(table->sort_order, (size_t)table->row_count,
          sizeof(int), sort_compare);
}
