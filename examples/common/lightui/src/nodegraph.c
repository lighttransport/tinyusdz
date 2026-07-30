/*
 * nodegraph.c — Node graph editor widget
 *
 * Renders a pan/zoomable graph of connected nodes with input/output pins.
 * Supports drag-to-connect, node selection, and auto-layout.
 *
 * Built-in auto-layout uses a Sugiyama-style layered algorithm:
 *   1. Topological sort (Kahn's algorithm)
 *   2. Layer assignment (longest-path from roots)
 *   3. Crossing reduction (barycenter heuristic)
 *   4. X-position assignment (averaging connected neighbours)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/nodegraph.h>
#include <string.h>
#include <math.h>
#include <stddef.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Helpers ------------------------------------------------------------ */

static inline float ng_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float ng_minf(float a, float b) { return a < b ? a : b; }
static inline float ng_maxf(float a, float b) { return a > b ? a : b; }
static inline int ng_maxi(int a, int b) { return a > b ? a : b; }

/* ---- Index lookup ------------------------------------------------------- */

static int find_node_idx(const lui_nodegraph_t *ng, int node_id)
{
    for (int i = 0; i < ng->node_count; i++)
        if (ng->nodes[i].id == node_id) return i;
    return -1;
}

static int find_link_idx(const lui_nodegraph_t *ng, int link_id)
{
    for (int i = 0; i < ng->link_count; i++)
        if (ng->links[i].id == link_id) return i;
    return -1;
}

/* Find which node owns a pin (by pin ID).  Returns node index and
 * writes the pin array index + direction. */
static int find_pin_owner(const lui_nodegraph_t *ng, int pin_id,
                            int *pin_idx, lui_pin_dir_t *dir)
{
    for (int i = 0; i < ng->node_count; i++) {
        const lui_ng_node_t *n = &ng->nodes[i];
        for (int j = 0; j < n->input_count; j++) {
            if (n->inputs[j].id == pin_id) {
                if (pin_idx) *pin_idx = j;
                if (dir) *dir = LUI_PIN_INPUT;
                return i;
            }
        }
        for (int j = 0; j < n->output_count; j++) {
            if (n->outputs[j].id == pin_id) {
                if (pin_idx) *pin_idx = j;
                if (dir) *dir = LUI_PIN_OUTPUT;
                return i;
            }
        }
    }
    return -1;
}

/* ---- Geometry helpers --------------------------------------------------- */

int lui_ng_node_height(const lui_nodegraph_t *ng, const lui_ng_node_t *node)
{
    if (node->collapsed)
        return ng->node_header_h;
    int pins = node->input_count > node->output_count
             ? node->input_count : node->output_count;
    if (pins < 1) pins = 1;
    return ng->node_header_h + pins * ng->node_pin_spacing + ng->node_padding;
}

/* Get screen-space rect for a node */
static lvg_rect_t node_screen_rect(const lui_nodegraph_t *ng,
                                     const lui_ng_node_t *node,
                                     const lvg_rect_t *widget_rect)
{
    int sx = widget_rect->x + (int)((node->x - ng->view_x) * ng->zoom);
    int sy = widget_rect->y + (int)((node->y - ng->view_y) * ng->zoom);
    int sw = (int)(node->width * ng->zoom);
    int sh = (int)(lui_ng_node_height(ng, node) * ng->zoom);
    lvg_rect_t r = { sx, sy, sw, sh };
    return r;
}

/* Get screen-space position of a pin */
static void pin_screen_pos(const lui_nodegraph_t *ng,
                             const lui_ng_node_t *node,
                             lui_pin_dir_t dir, int pin_index,
                             const lvg_rect_t *widget_rect,
                             int *out_x, int *out_y)
{
    lvg_rect_t nr = node_screen_rect(ng, node, widget_rect);
    int py = nr.y + (int)(ng->node_header_h * ng->zoom)
           + (int)((pin_index * ng->node_pin_spacing + ng->node_pin_spacing / 2)
                   * ng->zoom);
    if (dir == LUI_PIN_INPUT) {
        *out_x = nr.x;
    } else {
        *out_x = nr.x + nr.width;
    }
    *out_y = py;
}

/* ---- Coordinate conversion ---------------------------------------------- */

void lui_nodegraph_screen_to_graph(const lui_nodegraph_t *ng,
                                    int sx, int sy,
                                    float *gx, float *gy)
{
    if (ng->zoom <= 0.0f) { *gx = 0; *gy = 0; return; }
    *gx = (float)sx / ng->zoom + ng->view_x;
    *gy = (float)sy / ng->zoom + ng->view_y;
}

void lui_nodegraph_graph_to_screen(const lui_nodegraph_t *ng,
                                    float gx, float gy,
                                    int *sx, int *sy)
{
    *sx = (int)((gx - ng->view_x) * ng->zoom);
    *sy = (int)((gy - ng->view_y) * ng->zoom);
}

/* ---- Drawing ------------------------------------------------------------ */

static void draw_grid(const lui_nodegraph_t *ng, lvg_canvas_t *canvas,
                        const lvg_rect_t *r)
{
    if (ng->grid_size <= 0) return;
    float gs = ng->grid_size * ng->zoom;
    if (gs < 4.0f) return;  /* too dense to draw */

    /* First grid line in view */
    float ox = fmodf(-(ng->view_x * ng->zoom), gs);
    if (ox < 0) ox += gs;
    float oy = fmodf(-(ng->view_y * ng->zoom), gs);
    if (oy < 0) oy += gs;

    for (float x = ox; x < r->width; x += gs) {
        int sx = r->x + (int)x;
        /* Every 5th line is "major" */
        float gx = (float)sx / ng->zoom + ng->view_x;
        int gi = (int)roundf(gx / ng->grid_size);
        lvg_color_t col = (gi % 5 == 0) ? ng->grid_major_color : ng->grid_color;
        lvg_canvas_draw_line(canvas, sx, r->y, sx, r->y + r->height, col, 1);
    }
    for (float y = oy; y < r->height; y += gs) {
        int sy = r->y + (int)y;
        float gy = (float)sy / ng->zoom + ng->view_y;
        int gi = (int)roundf(gy / ng->grid_size);
        lvg_color_t col = (gi % 5 == 0) ? ng->grid_major_color : ng->grid_color;
        lvg_canvas_draw_line(canvas, r->x, sy, r->x + r->width, sy, col, 1);
    }
}

/* Draw a cubic Bezier link between two screen points */
static void draw_bezier_link(lvg_canvas_t *canvas,
                               int x0, int y0, int x1, int y1,
                               lvg_color_t color, int width,
                               bool vertical)
{
    /* Flatten the Bezier into a polyline */
    #define BEZ_SEGMENTS 24
    lvg_point_t pts[BEZ_SEGMENTS + 1];

    float cx0, cy0, cx1, cy1;
    if (vertical) {
        float dy = (float)(y1 - y0) * 0.5f;
        if (dy < 30.0f && dy > -30.0f) dy = (y1 > y0) ? 30.0f : -30.0f;
        cx0 = (float)x0;
        cy0 = y0 + dy;
        cx1 = (float)x1;
        cy1 = y1 - dy;
    } else {
        /* Control points: horizontal tangent leaving output, entering input */
        float dx = (float)(x1 - x0) * 0.5f;
        if (dx < 30.0f && dx > -30.0f) dx = (x1 > x0) ? 30.0f : -30.0f;
        cx0 = x0 + dx;
        cy0 = (float)y0;
        cx1 = x1 - dx;
        cy1 = (float)y1;
    }

    for (int i = 0; i <= BEZ_SEGMENTS; i++) {
        float t = (float)i / BEZ_SEGMENTS;
        float u = 1.0f - t;
        float b0 = u * u * u;
        float b1 = 3.0f * u * u * t;
        float b2 = 3.0f * u * t * t;
        float b3 = t * t * t;
        pts[i].x = (int)(b0 * x0 + b1 * cx0 + b2 * cx1 + b3 * x1);
        pts[i].y = (int)(b0 * y0 + b1 * cy0 + b2 * cy1 + b3 * y1);
    }
    #undef BEZ_SEGMENTS

    lvg_canvas_draw_polyline(canvas, pts, 25, color, width);
}

static void draw_orthogonal_link(lvg_canvas_t *canvas,
                                 int x0, int y0, int x1, int y1,
                                 lvg_color_t color, int width,
                                 bool vertical)
{
    lvg_point_t pts[4];
    pts[0].x = x0;
    pts[0].y = y0;

    if (vertical) {
        int mid_y = y0 + (y1 - y0) / 2;
        pts[1].x = x0;
        pts[1].y = mid_y;
        pts[2].x = x1;
        pts[2].y = mid_y;
    } else {
        int mid_x = x0 + (x1 - x0) / 2;
        pts[1].x = mid_x;
        pts[1].y = y0;
        pts[2].x = mid_x;
        pts[2].y = y1;
    }

    pts[3].x = x1;
    pts[3].y = y1;

    lvg_canvas_draw_polyline(canvas, pts, 4, color, width);
}

static void draw_node(const lui_nodegraph_t *ng, lvg_canvas_t *canvas,
                        const lui_ng_node_t *node, const lvg_rect_t *wr)
{
    lvg_rect_t nr = node_screen_rect(ng, node, wr);
    if (nr.width < 2 || nr.height < 2) return;

    int cr = (int)(ng->node_corner_radius * ng->zoom);

    /* Body */
    if (cr <= 0) {
        lvg_canvas_fill_rect(canvas, nr.x, nr.y, nr.width, nr.height,
                             node->body_color);
    } else {
        lvg_canvas_fill_rounded_rect(canvas, nr.x, nr.y, nr.width, nr.height,
                                     cr, node->body_color);
    }

    /* Header */
    int hh = (int)(ng->node_header_h * ng->zoom);
    if (cr <= 0) {
        lvg_canvas_fill_rect(canvas, nr.x, nr.y, nr.width, hh,
                             node->header_color);
    } else {
        lvg_canvas_fill_rounded_rect(canvas, nr.x, nr.y, nr.width, hh,
                                     cr, node->header_color);
    }
    /* Square off the bottom corners of the header (if not collapsed) */
    if (cr > 0 && !node->collapsed && hh < nr.height)
        lvg_canvas_fill_rect(canvas, nr.x, nr.y + hh - cr,
                              nr.width, cr, node->header_color);

    /* Border */
    lvg_color_t border = node->selected
        ? ng->node_selected_border
        : ng->node_border;
    if (cr <= 0) {
        lvg_canvas_stroke_rect(canvas, nr.x, nr.y, nr.width, nr.height,
                               border, node->selected ? 2 : 1);
    } else {
        lvg_canvas_stroke_rounded_rect(canvas, nr.x, nr.y, nr.width, nr.height,
                                       cr, border, node->selected ? 2 : 1);
    }

    /* Pins */
    if (!node->collapsed) {
        int pr = (int)(ng->node_pin_radius * ng->zoom);
        if (pr < 2) pr = 2;

        for (int i = 0; i < node->input_count; i++) {
            int px, py;
            pin_screen_pos(ng, node, LUI_PIN_INPUT, i, wr, &px, &py);
            if (ng->square_pins) {
                lvg_canvas_fill_rect(canvas, px - pr, py - pr, pr * 2, pr * 2,
                                     node->inputs[i].color);
                lvg_canvas_stroke_rect(canvas, px - pr, py - pr, pr * 2, pr * 2,
                                       ng->node_border, 1);
            } else {
                lvg_canvas_fill_circle(canvas, px, py, pr, node->inputs[i].color);
                lvg_canvas_stroke_circle(canvas, px, py, pr, ng->node_border, 1);
            }
        }
        for (int i = 0; i < node->output_count; i++) {
            int px, py;
            pin_screen_pos(ng, node, LUI_PIN_OUTPUT, i, wr, &px, &py);
            if (ng->square_pins) {
                lvg_canvas_fill_rect(canvas, px - pr, py - pr, pr * 2, pr * 2,
                                     node->outputs[i].color);
                lvg_canvas_stroke_rect(canvas, px - pr, py - pr, pr * 2, pr * 2,
                                       ng->node_border, 1);
            } else {
                lvg_canvas_fill_circle(canvas, px, py, pr, node->outputs[i].color);
                lvg_canvas_stroke_circle(canvas, px, py, pr, ng->node_border, 1);
            }
        }
    }
}

static void nodegraph_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_nodegraph_t *ng = (lui_nodegraph_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Save clip and restrict to widget */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t new_clip = lvg_rect_intersect(&r, &old_clip);
    lvg_canvas_set_clip(canvas, &new_clip);

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, ng->bg);

    /* Grid */
    draw_grid(ng, canvas, &r);

    /* Links */
    for (int i = 0; i < ng->link_count; i++) {
        const lui_ng_link_t *lk = &ng->links[i];
        int sni = find_node_idx(ng, lk->src_node);
        int dni = find_node_idx(ng, lk->dst_node);
        if (sni < 0 || dni < 0) continue;

        /* Find pin indices */
        int spi = -1, dpi = -1;
        for (int j = 0; j < ng->nodes[sni].output_count; j++)
            if (ng->nodes[sni].outputs[j].id == lk->src_pin) { spi = j; break; }
        for (int j = 0; j < ng->nodes[dni].input_count; j++)
            if (ng->nodes[dni].inputs[j].id == lk->dst_pin) { dpi = j; break; }
        if (spi < 0 || dpi < 0) continue;

        int x0, y0, x1, y1;
        pin_screen_pos(ng, &ng->nodes[sni], LUI_PIN_OUTPUT, spi, &r, &x0, &y0);
        pin_screen_pos(ng, &ng->nodes[dni], LUI_PIN_INPUT, dpi, &r, &x1, &y1);

        lvg_color_t col = lk->color ? lk->color : ng->link_default_color;
        int lw = ng_maxi(1, (int)(ng->link_width * ng->zoom));
        if (ng->orthogonal_links) {
            draw_orthogonal_link(canvas, x0, y0, x1, y1, col, lw,
                                 ng->vertical_links);
        } else {
            draw_bezier_link(canvas, x0, y0, x1, y1, col, lw,
                             ng->vertical_links);
        }
    }

    /* In-progress link being dragged */
    if (ng->drag_mode == 2) {
        int sni = ng->link_src_node;
        if (sni >= 0 && sni < ng->node_count) {
            int x0, y0;
            pin_screen_pos(ng, &ng->nodes[sni], LUI_PIN_OUTPUT,
                            ng->link_src_pin, &r, &x0, &y0);
            int x1 = r.x + (int)((ng->link_drag_x - ng->view_x) * ng->zoom);
            int y1 = r.y + (int)((ng->link_drag_y - ng->view_y) * ng->zoom);
            if (ng->orthogonal_links) {
                draw_orthogonal_link(canvas, x0, y0, x1, y1,
                                     ng->link_default_color, ng->link_width,
                                     ng->vertical_links);
            } else {
                draw_bezier_link(canvas, x0, y0, x1, y1,
                                 ng->link_default_color, ng->link_width,
                                 ng->vertical_links);
            }
        }
    }

    /* Nodes (back to front — lower index drawn first) */
    for (int i = 0; i < ng->node_count; i++)
        draw_node(ng, canvas, &ng->nodes[i], &r);

    /* Box selection overlay */
    if (ng->drag_mode == 4) {
        float x0 = ng_minf(ng->box_sel_x0, ng->box_sel_x1);
        float y0 = ng_minf(ng->box_sel_y0, ng->box_sel_y1);
        float x1 = ng_maxf(ng->box_sel_x0, ng->box_sel_x1);
        float y1 = ng_maxf(ng->box_sel_y0, ng->box_sel_y1);
        int sx = r.x + (int)((x0 - ng->view_x) * ng->zoom);
        int sy = r.y + (int)((y0 - ng->view_y) * ng->zoom);
        int sw = (int)((x1 - x0) * ng->zoom);
        int sh = (int)((y1 - y0) * ng->zoom);
        lvg_canvas_fill_rect(canvas, sx, sy, sw, sh, ng->box_select_fill);
        lvg_canvas_stroke_rect(canvas, sx, sy, sw, sh,
                                ng->box_select_border, 1);
    }

    lvg_canvas_set_clip(canvas, &old_clip);
}

/* ---- Hit testing -------------------------------------------------------- */

/* Hit-test a pin: returns pin ID if (sx, sy) is within radius of a pin,
 * otherwise -1.  Also writes node index and direction. */
static int hit_test_pin(const lui_nodegraph_t *ng, int sx, int sy,
                          const lvg_rect_t *wr,
                          int *out_node_idx, lui_pin_dir_t *out_dir,
                          int *out_pin_array_idx)
{
    int hit_r = (int)(ng->node_pin_radius * ng->zoom) + 4;
    int hit_r2 = hit_r * hit_r;

    /* Check back-to-front (top node gets priority) */
    for (int i = ng->node_count - 1; i >= 0; i--) {
        const lui_ng_node_t *n = &ng->nodes[i];
        if (n->collapsed) continue;

        for (int j = 0; j < n->output_count; j++) {
            int px, py;
            pin_screen_pos(ng, n, LUI_PIN_OUTPUT, j, wr, &px, &py);
            int dx = sx - px, dy = sy - py;
            if (dx * dx + dy * dy <= hit_r2) {
                *out_node_idx = i;
                *out_dir = LUI_PIN_OUTPUT;
                *out_pin_array_idx = j;
                return n->outputs[j].id;
            }
        }
        for (int j = 0; j < n->input_count; j++) {
            int px, py;
            pin_screen_pos(ng, n, LUI_PIN_INPUT, j, wr, &px, &py);
            int dx = sx - px, dy = sy - py;
            if (dx * dx + dy * dy <= hit_r2) {
                *out_node_idx = i;
                *out_dir = LUI_PIN_INPUT;
                *out_pin_array_idx = j;
                return n->inputs[j].id;
            }
        }
    }
    return -1;
}

/* Hit-test nodes: returns node index or -1 */
static int hit_test_node(const lui_nodegraph_t *ng, int sx, int sy,
                           const lvg_rect_t *wr)
{
    /* Back-to-front for correct overlap priority */
    for (int i = ng->node_count - 1; i >= 0; i--) {
        lvg_rect_t nr = node_screen_rect(ng, &ng->nodes[i], wr);
        if (lvg_rect_contains_point(&nr, sx, sy))
            return i;
    }
    return -1;
}

/* ---- Event handling ----------------------------------------------------- */

static void emit_event(lui_nodegraph_t *ng, lui_ng_event_type_t type,
                         int node_id, int link_id, int pin_id)
{
    if (!ng->on_event) return;
    lui_ng_event_t ev;
    ev.type = type;
    ev.node_id = node_id;
    ev.link_id = link_id;
    ev.pin_id = pin_id;
    ng->on_event(&ev, ng->on_event_user);
}

static int nodegraph_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_nodegraph_t *ng = (lui_nodegraph_t *)w;
    lvg_rect_t wr = lui_widget_absolute_rect(w);

    switch (event->type) {
    case LUI_EVENT_MOUSE_DOWN: {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;
        if (!lvg_rect_contains_point(&wr, mx, my)) return 0;

        int lx = mx - wr.x;
        int ly = my - wr.y;

        /* Middle button: start pan */
        if (event->data.mouse_button.button == LUI_MOUSE_MIDDLE) {
            ng->drag_mode = 3;
            ng->drag_offset_x = (float)lx;
            ng->drag_offset_y = (float)ly;
            return 1;
        }

        if (event->data.mouse_button.button != LUI_MOUSE_LEFT) break;

        /* Double-click on node */
        if (event->data.mouse_button.clicks >= 2) {
            int ni = hit_test_node(ng, mx, my, &wr);
            if (ni >= 0) {
                emit_event(ng, LUI_NG_EVENT_NODE_DOUBLE_CLICK,
                            ng->nodes[ni].id, -1, -1);
                return 1;
            }
        }

        /* Check pins first */
        int pin_node_idx;
        lui_pin_dir_t pin_dir;
        int pin_arr_idx;
        int pin_id = hit_test_pin(ng, mx, my, &wr,
                                    &pin_node_idx, &pin_dir, &pin_arr_idx);
        if (pin_id >= 0 && pin_dir == LUI_PIN_OUTPUT) {
            /* Start link drag from output pin */
            ng->drag_mode = 2;
            ng->link_src_node = pin_node_idx;
            ng->link_src_pin = pin_arr_idx;
            float gx, gy;
            lui_nodegraph_screen_to_graph(ng, lx, ly, &gx, &gy);
            ng->link_drag_x = gx;
            ng->link_drag_y = gy;
            return 1;
        }

        /* Check nodes */
        int ni = hit_test_node(ng, mx, my, &wr);
        if (ni >= 0) {
            /* Select / deselect */
            if (!(event->data.mouse_button.clicks >= 2)) {
                if (!ng->nodes[ni].selected) {
                    /* If shift not held, deselect others */
                    for (int j = 0; j < ng->node_count; j++)
                        ng->nodes[j].selected = false;
                    ng->nodes[ni].selected = true;
                    emit_event(ng, LUI_NG_EVENT_NODE_SELECTED,
                                ng->nodes[ni].id, -1, -1);
                }
            }

            /* Start node drag */
            ng->drag_mode = 1;
            ng->drag_node = ni;
            lvg_rect_t nr = node_screen_rect(ng, &ng->nodes[ni], &wr);
            ng->drag_offset_x = (float)(mx - nr.x) / ng->zoom;
            ng->drag_offset_y = (float)(my - nr.y) / ng->zoom;

            /* Freeze physics and init velocity tracking */
            ng->nodes[ni].vx = 0;
            ng->nodes[ni].vy = 0;
            ng->drag_hist_count = 0;
            ng->shake_accum = 0;
            ng->shake_last_dx = 0;
            ng->shake_last_dy = 0;
            ng->shake_timer = 0;
            return 1;
        }

        /* Click on empty space: deselect all, start box select */
        for (int j = 0; j < ng->node_count; j++)
            ng->nodes[j].selected = false;
        ng->drag_mode = 4;
        float gx, gy;
        lui_nodegraph_screen_to_graph(ng, lx, ly, &gx, &gy);
        ng->box_sel_x0 = ng->box_sel_x1 = gx;
        ng->box_sel_y0 = ng->box_sel_y1 = gy;
        return 1;
    }

    case LUI_EVENT_MOUSE_MOVE: {
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;
        int lx = mx - wr.x;
        int ly = my - wr.y;

        if (ng->drag_mode == 1) {
            /* Drag node(s) */
            float gx, gy;
            lui_nodegraph_screen_to_graph(ng, lx, ly, &gx, &gy);
            float new_x = gx - ng->drag_offset_x;
            float new_y = gy - ng->drag_offset_y;
            float ddx = new_x - ng->nodes[ng->drag_node].x;
            float ddy = new_y - ng->nodes[ng->drag_node].y;

            /* Move all selected nodes by the same delta */
            for (int i = 0; i < ng->node_count; i++) {
                if (ng->nodes[i].selected) {
                    ng->nodes[i].x += ddx;
                    ng->nodes[i].y += ddy;
                    if (ng->grid_snap && ng->grid_size > 0) {
                        ng->nodes[i].x = roundf(ng->nodes[i].x / ng->grid_size)
                                         * ng->grid_size;
                        ng->nodes[i].y = roundf(ng->nodes[i].y / ng->grid_size)
                                         * ng->grid_size;
                    }
                }
            }

            /* Record position for velocity estimation */
            if (ng->drag_hist_count < LUI_NG_VEL_HISTORY) {
                int h = ng->drag_hist_count++;
                ng->drag_hist_x[h] = new_x;
                ng->drag_hist_y[h] = new_y;
                ng->drag_hist_t[h] = ng->drag_time;
            } else {
                /* Shift history */
                for (int i = 0; i < LUI_NG_VEL_HISTORY - 1; i++) {
                    ng->drag_hist_x[i] = ng->drag_hist_x[i + 1];
                    ng->drag_hist_y[i] = ng->drag_hist_y[i + 1];
                    ng->drag_hist_t[i] = ng->drag_hist_t[i + 1];
                }
                int h = LUI_NG_VEL_HISTORY - 1;
                ng->drag_hist_x[h] = new_x;
                ng->drag_hist_y[h] = new_y;
                ng->drag_hist_t[h] = ng->drag_time;
            }

            /* Shake detection: check for direction reversals */
            if (ng->physics_enabled) {
                /* Reset shake if window expired */
                ng->shake_timer += (ng->drag_hist_count > 1)
                    ? (ng->drag_hist_t[ng->drag_hist_count - 1]
                       - ng->drag_hist_t[ng_maxi(0, ng->drag_hist_count - 2)])
                    : 0.0f;
                if (ng->shake_timer > ng->shake_window) {
                    ng->shake_accum *= 0.5f; /* decay, don't fully reset */
                    ng->shake_timer = 0;
                }

                /* Check direction reversal */
                if (fabsf(ddx) > 0.5f || fabsf(ddy) > 0.5f) {
                    float dot = ddx * ng->shake_last_dx
                              + ddy * ng->shake_last_dy;
                    if (dot < 0) {
                        /* Direction reversed — accumulate magnitude */
                        ng->shake_accum += sqrtf(ddx * ddx + ddy * ddy);
                    }
                    ng->shake_last_dx = ddx;
                    ng->shake_last_dy = ddy;
                }

                /* Trigger shake disconnect */
                if (ng->shake_accum >= ng->shake_threshold) {
                    ng->shake_accum = 0;
                    int nid = ng->nodes[ng->drag_node].id;
                    /* Remove all links connected to this node */
                    for (int i = ng->link_count - 1; i >= 0; i--) {
                        if (ng->links[i].src_node == nid ||
                            ng->links[i].dst_node == nid) {
                            int lid = ng->links[i].id;
                            for (int j = i; j < ng->link_count - 1; j++)
                                ng->links[j] = ng->links[j + 1];
                            ng->link_count--;
                            emit_event(ng, LUI_NG_EVENT_LINK_SHAKEN_OFF,
                                        nid, lid, -1);
                        }
                    }
                }
            }

            emit_event(ng, LUI_NG_EVENT_NODE_MOVED,
                        ng->nodes[ng->drag_node].id, -1, -1);
            return 1;
        }

        if (ng->drag_mode == 2) {
            /* Link drag preview */
            float gx, gy;
            lui_nodegraph_screen_to_graph(ng, lx, ly, &gx, &gy);
            ng->link_drag_x = gx;
            ng->link_drag_y = gy;
            return 1;
        }

        if (ng->drag_mode == 3) {
            /* Pan */
            float dx = ((float)lx - ng->drag_offset_x) / ng->zoom;
            float dy = ((float)ly - ng->drag_offset_y) / ng->zoom;
            ng->view_x -= dx;
            ng->view_y -= dy;
            ng->drag_offset_x = (float)lx;
            ng->drag_offset_y = (float)ly;
            emit_event(ng, LUI_NG_EVENT_VIEW_CHANGED, -1, -1, -1);
            return 1;
        }

        if (ng->drag_mode == 4) {
            /* Box select update */
            float gx, gy;
            lui_nodegraph_screen_to_graph(ng, lx, ly, &gx, &gy);
            ng->box_sel_x1 = gx;
            ng->box_sel_y1 = gy;

            /* Select nodes within box */
            float bx0 = ng_minf(ng->box_sel_x0, ng->box_sel_x1);
            float by0 = ng_minf(ng->box_sel_y0, ng->box_sel_y1);
            float bx1 = ng_maxf(ng->box_sel_x0, ng->box_sel_x1);
            float by1 = ng_maxf(ng->box_sel_y0, ng->box_sel_y1);

            for (int i = 0; i < ng->node_count; i++) {
                lui_ng_node_t *n = &ng->nodes[i];
                int nh = lui_ng_node_height(ng, n);
                bool overlap = n->x + n->width > bx0
                            && n->x < bx1
                            && n->y + nh > by0
                            && n->y < by1;
                n->selected = overlap;
            }
            return 1;
        }
        break;
    }

    case LUI_EVENT_MOUSE_UP: {
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (ng->drag_mode == 2 &&
            event->data.mouse_button.button == LUI_MOUSE_LEFT) {
            /* Complete link: check if cursor is over an input pin */
            int pin_node_idx;
            lui_pin_dir_t pin_dir;
            int pin_arr_idx;
            int pin_id = hit_test_pin(ng, mx, my, &wr,
                                        &pin_node_idx, &pin_dir, &pin_arr_idx);
            if (pin_id >= 0 && pin_dir == LUI_PIN_INPUT &&
                pin_node_idx != ng->link_src_node) {
                int src_pin_id = ng->nodes[ng->link_src_node]
                                    .outputs[ng->link_src_pin].id;
                int lid = lui_nodegraph_add_link(ng, src_pin_id, pin_id);
                if (lid >= 0)
                    emit_event(ng, LUI_NG_EVENT_LINK_CREATED, -1, lid, -1);
            }
            ng->drag_mode = 0;
            return 1;
        }

        if (ng->drag_mode == 1) {
            /* Node drag release: compute release velocity from history */
            if (ng->physics_enabled && ng->drag_hist_count >= 2) {
                int last = ng->drag_hist_count - 1;
                int first = ng_maxi(0, last - 3);
                float dt_hist = ng->drag_hist_t[last] - ng->drag_hist_t[first];
                if (dt_hist > 0.001f) {
                    float release_vx = (ng->drag_hist_x[last]
                                      - ng->drag_hist_x[first]) / dt_hist;
                    float release_vy = (ng->drag_hist_y[last]
                                      - ng->drag_hist_y[first]) / dt_hist;
                    /* Apply to all selected nodes */
                    for (int i = 0; i < ng->node_count; i++) {
                        if (ng->nodes[i].selected) {
                            ng->nodes[i].vx = release_vx;
                            ng->nodes[i].vy = release_vy;
                        }
                    }
                }
            }
            ng->drag_mode = 0;
            return 1;
        }

        if (ng->drag_mode != 0) {
            ng->drag_mode = 0;
            return 1;
        }
        break;
    }

    case LUI_EVENT_SCROLL: {
        int mx = event->data.scroll.x;
        int my = event->data.scroll.y;
        if (!lvg_rect_contains_point(&wr, mx, my)) return 0;

        float factor = (event->data.scroll.delta_y > 0) ? 0.9f : 1.1f;
        /* Zoom centred on cursor */
        float gx = (float)(mx - wr.x) / ng->zoom + ng->view_x;
        float gy = (float)(my - wr.y) / ng->zoom + ng->view_y;
        lui_nodegraph_zoom(ng, factor, gx, gy);
        emit_event(ng, LUI_NG_EVENT_VIEW_CHANGED, -1, -1, -1);
        return 1;
    }

    default:
        break;
    }

    return 0;
}

/* ---- Measure ------------------------------------------------------------ */

static int nodegraph_measure(const lui_widget_t *w, int *out_w, int *out_h,
                               void *user)
{
    (void)w; (void)user;
    *out_w = 640;
    *out_h = 480;
    return 0;
}

/* ---- Public API --------------------------------------------------------- */

void lui_nodegraph_init(lui_nodegraph_t *ng)
{
    if (!ng) return;
    memset(ng, 0, sizeof(*ng));

    lui_widget_init(&ng->widget);
    ng->widget.width    = lvg_size_hug(640);
    ng->widget.height   = lvg_size_hug(480);
    ng->widget.measure  = nodegraph_measure;
    ng->widget.draw     = nodegraph_draw;
    ng->widget.on_event = nodegraph_event;
    ng->widget.flags    = LUI_WIDGET_DRAWS_CHILDREN;

    ng->view_x = 0.0f;
    ng->view_y = 0.0f;
    ng->zoom   = 1.0f;
    ng->next_id = 1;

    ng->node_header_h     = 24;
    ng->node_pin_spacing  = 20;
    ng->node_pin_radius   = 5;
    ng->node_padding      = 8;
    ng->node_corner_radius = 6;
    ng->node_default_width = 140;

    ng->grid_size = 20;
    ng->grid_snap = false;

    /* Physics defaults */
    ng->physics_enabled      = true;
    ng->phys_damping         = 0.92f;
    ng->phys_repulsion       = 200.0f;
    ng->phys_spring          = 0.3f;
    ng->phys_spring_len      = 180.0f;
    ng->phys_min_vel         = 0.5f;
    ng->throw_speed_threshold = 800.0f;
    ng->throw_remove_dist    = 2000.0f;
    ng->shake_threshold      = 60.0f;
    ng->shake_window         = 0.4f;

    ng->bg                  = LVG_COLOR_RGB(0x2A, 0x2A, 0x2A);
    ng->grid_color          = LVG_COLOR_RGB(0x33, 0x33, 0x33);
    ng->grid_major_color    = LVG_COLOR_RGB(0x3D, 0x3D, 0x3D);
    ng->node_border         = LVG_COLOR_RGB(0x1A, 0x1A, 0x1A);
    ng->node_selected_border = LVG_COLOR_RGB(0xFF, 0xA0, 0x20);
    ng->link_default_color  = LVG_COLOR_RGB(0xA0, 0xA0, 0xA0);
    ng->box_select_fill     = LVG_COLOR_ARGB(0x30, 0x60, 0x90, 0xFF);
    ng->box_select_border   = LVG_COLOR_RGB(0x60, 0x90, 0xFF);
    ng->link_width          = 2;
    ng->square_pins         = false;
    ng->orthogonal_links    = false;
    ng->vertical_links      = false;
}

int lui_nodegraph_add_node(lui_nodegraph_t *ng,
                            const char *title, float x, float y)
{
    if (!ng || ng->node_count >= LUI_NG_MAX_NODES) return -1;
    int idx = ng->node_count;
    lui_ng_node_t *n = &ng->nodes[idx];
    memset(n, 0, sizeof(*n));
    n->id           = ng->next_id++;
    n->title        = title;
    n->x            = x;
    n->y            = y;
    n->width        = ng->node_default_width;
    n->header_color = LVG_COLOR_RGB(0x40, 0x60, 0x80);
    n->body_color   = LVG_COLOR_RGB(0x38, 0x38, 0x38);
    n->selected     = false;
    n->collapsed    = false;
    ng->node_count++;
    return n->id;
}

void lui_nodegraph_remove_node(lui_nodegraph_t *ng, int node_id)
{
    if (!ng) return;
    int idx = find_node_idx(ng, node_id);
    if (idx < 0) return;

    /* Remove all links connected to this node's pins */
    for (int i = ng->link_count - 1; i >= 0; i--) {
        if (ng->links[i].src_node == node_id ||
            ng->links[i].dst_node == node_id) {
            for (int j = i; j < ng->link_count - 1; j++)
                ng->links[j] = ng->links[j + 1];
            ng->link_count--;
        }
    }

    /* Remove node */
    for (int i = idx; i < ng->node_count - 1; i++)
        ng->nodes[i] = ng->nodes[i + 1];
    ng->node_count--;
}

int lui_nodegraph_add_pin(lui_nodegraph_t *ng, int node_id,
                           lui_pin_dir_t dir, const char *label,
                           lvg_color_t color)
{
    if (!ng) return -1;
    int idx = find_node_idx(ng, node_id);
    if (idx < 0) return -1;
    lui_ng_node_t *n = &ng->nodes[idx];

    lui_ng_pin_t pin;
    pin.id    = ng->next_id++;
    pin.dir   = dir;
    pin.label = label;
    pin.color = color;

    if (dir == LUI_PIN_INPUT) {
        if (n->input_count >= LUI_NG_MAX_PINS) return -1;
        n->inputs[n->input_count++] = pin;
    } else {
        if (n->output_count >= LUI_NG_MAX_PINS) return -1;
        n->outputs[n->output_count++] = pin;
    }
    return pin.id;
}

int lui_nodegraph_add_link(lui_nodegraph_t *ng,
                            int src_pin, int dst_pin)
{
    if (!ng || ng->link_count >= LUI_NG_MAX_LINKS) return -1;

    /* Validate: src must be output, dst must be input */
    int sni, spi, dni, dpi;
    lui_pin_dir_t sdir, ddir;
    sni = find_pin_owner(ng, src_pin, &spi, &sdir);
    dni = find_pin_owner(ng, dst_pin, &dpi, &ddir);
    if (sni < 0 || dni < 0) return -1;
    if (sdir != LUI_PIN_OUTPUT || ddir != LUI_PIN_INPUT) return -1;

    /* Prevent duplicate connections to the same input pin */
    for (int i = 0; i < ng->link_count; i++)
        if (ng->links[i].dst_pin == dst_pin) return -1;

    int idx = ng->link_count;
    lui_ng_link_t *lk = &ng->links[idx];
    lk->id       = ng->next_id++;
    lk->src_node = ng->nodes[sni].id;
    lk->src_pin  = src_pin;
    lk->dst_node = ng->nodes[dni].id;
    lk->dst_pin  = dst_pin;
    lk->color    = 0; /* auto */
    ng->link_count++;
    return lk->id;
}

void lui_nodegraph_remove_link(lui_nodegraph_t *ng, int link_id)
{
    if (!ng) return;
    int idx = find_link_idx(ng, link_id);
    if (idx < 0) return;
    for (int i = idx; i < ng->link_count - 1; i++)
        ng->links[i] = ng->links[i + 1];
    ng->link_count--;
}

lui_ng_node_t *lui_nodegraph_get_node(lui_nodegraph_t *ng, int node_id)
{
    if (!ng) return NULL;
    int idx = find_node_idx(ng, node_id);
    return (idx >= 0) ? &ng->nodes[idx] : NULL;
}

lui_ng_link_t *lui_nodegraph_get_link(lui_nodegraph_t *ng, int link_id)
{
    if (!ng) return NULL;
    int idx = find_link_idx(ng, link_id);
    return (idx >= 0) ? &ng->links[idx] : NULL;
}

/* ---- View control ------------------------------------------------------- */

void lui_nodegraph_set_view(lui_nodegraph_t *ng, float x, float y, float zoom)
{
    if (!ng) return;
    ng->view_x = x;
    ng->view_y = y;
    ng->zoom   = ng_clampf(zoom, 0.1f, 5.0f);
}

void lui_nodegraph_zoom(lui_nodegraph_t *ng, float factor,
                         float center_x, float center_y)
{
    if (!ng) return;
    float old_zoom = ng->zoom;
    ng->zoom = ng_clampf(ng->zoom * factor, 0.1f, 5.0f);
    /* Adjust view so center_x, center_y stays at the same screen position */
    ng->view_x = center_x - (center_x - ng->view_x) * old_zoom / ng->zoom;
    ng->view_y = center_y - (center_y - ng->view_y) * old_zoom / ng->zoom;
}

void lui_nodegraph_fit_all(lui_nodegraph_t *ng)
{
    if (!ng || ng->node_count == 0) return;

    /* Compute bounding box in graph space */
    float min_x = ng->nodes[0].x;
    float min_y = ng->nodes[0].y;
    float max_x = ng->nodes[0].x + ng->nodes[0].width;
    float max_y = ng->nodes[0].y + lui_ng_node_height(ng, &ng->nodes[0]);

    for (int i = 1; i < ng->node_count; i++) {
        const lui_ng_node_t *n = &ng->nodes[i];
        int nh = lui_ng_node_height(ng, n);
        if (n->x < min_x) min_x = n->x;
        if (n->y < min_y) min_y = n->y;
        if (n->x + n->width > max_x) max_x = n->x + n->width;
        if (n->y + nh > max_y) max_y = n->y + nh;
    }

    float margin = 40.0f;
    float gw = max_x - min_x + margin * 2;
    float gh = max_y - min_y + margin * 2;
    if (gw < 1) gw = 1;
    if (gh < 1) gh = 1;

    lvg_rect_t wr = lui_widget_absolute_rect(&ng->widget);
    float zx = (float)wr.width / gw;
    float zy = (float)wr.height / gh;
    ng->zoom = ng_clampf(zx < zy ? zx : zy, 0.1f, 5.0f);
    ng->view_x = min_x - margin;
    ng->view_y = min_y - margin;
}

void lui_nodegraph_select_node(lui_nodegraph_t *ng, int node_id, bool selected)
{
    if (!ng) return;
    int idx = find_node_idx(ng, node_id);
    if (idx >= 0) ng->nodes[idx].selected = selected;
}

void lui_nodegraph_deselect_all(lui_nodegraph_t *ng)
{
    if (!ng) return;
    for (int i = 0; i < ng->node_count; i++)
        ng->nodes[i].selected = false;
}

/* ---- Auto-layout (Sugiyama-style layered graph drawing) ----------------- */

/*
 * Built-in algorithm overview:
 *   1. Build adjacency (src → dst edges from links)
 *   2. Topological sort (Kahn's algorithm) — detect and handle cycles
 *   3. Layer assignment (longest path from sources)
 *   4. Order nodes within each layer to minimise crossings
 *      (barycenter heuristic, multiple passes)
 *   5. Assign X,Y coordinates from layer/position
 */

void lui_nodegraph_auto_layout(lui_nodegraph_t *ng)
{
    if (!ng || ng->node_count == 0) return;

    /* User callback override */
    if (ng->layout) {
        ng->layout(ng->nodes, ng->node_count,
                    ng->links, ng->link_count,
                    ng->layout_user);
        return;
    }

    int n = ng->node_count;

    /* Build adjacency lists (outgoing edges) and in-degree */
    int adj[LUI_NG_MAX_NODES][LUI_NG_MAX_NODES];
    int adj_count[LUI_NG_MAX_NODES];
    int in_degree[LUI_NG_MAX_NODES];
    memset(adj_count, 0, sizeof(int) * (size_t)n);
    memset(in_degree, 0, sizeof(int) * (size_t)n);

    for (int i = 0; i < ng->link_count; i++) {
        int si = find_node_idx(ng, ng->links[i].src_node);
        int di = find_node_idx(ng, ng->links[i].dst_node);
        if (si < 0 || di < 0 || si == di) continue;
        /* Avoid duplicate edges */
        bool dup = false;
        for (int k = 0; k < adj_count[si]; k++)
            if (adj[si][k] == di) { dup = true; break; }
        if (!dup && adj_count[si] < LUI_NG_MAX_NODES) {
            adj[si][adj_count[si]++] = di;
            in_degree[di]++;
        }
    }

    /* Step 1: Topological sort (Kahn's algorithm) */
    int topo[LUI_NG_MAX_NODES];
    int topo_count = 0;
    int queue[LUI_NG_MAX_NODES];
    int qhead = 0, qtail = 0;
    int deg[LUI_NG_MAX_NODES];
    memcpy(deg, in_degree, sizeof(int) * (size_t)n);

    for (int i = 0; i < n; i++)
        if (deg[i] == 0) queue[qtail++] = i;

    while (qhead < qtail) {
        int u = queue[qhead++];
        topo[topo_count++] = u;
        for (int k = 0; k < adj_count[u]; k++) {
            int v = adj[u][k];
            if (--deg[v] == 0)
                queue[qtail++] = v;
        }
    }

    /* If not all nodes are in topo (cycle), append remaining */
    if (topo_count < n) {
        bool in_topo[LUI_NG_MAX_NODES];
        memset(in_topo, 0, sizeof(in_topo));
        for (int i = 0; i < topo_count; i++)
            in_topo[topo[i]] = true;
        for (int i = 0; i < n; i++)
            if (!in_topo[i]) topo[topo_count++] = i;
    }

    /* Step 2: Layer assignment (longest path from sources) */
    int layer[LUI_NG_MAX_NODES];
    memset(layer, 0, sizeof(layer));

    for (int ti = 0; ti < topo_count; ti++) {
        int u = topo[ti];
        for (int k = 0; k < adj_count[u]; k++) {
            int v = adj[u][k];
            if (layer[u] + 1 > layer[v])
                layer[v] = layer[u] + 1;
        }
    }

    int max_layer = 0;
    for (int i = 0; i < n; i++)
        if (layer[i] > max_layer) max_layer = layer[i];
    int num_layers = max_layer + 1;

    /* Step 3: Build per-layer node lists */
    int layer_nodes[LUI_NG_MAX_NODES][LUI_NG_MAX_NODES];
    int layer_count[LUI_NG_MAX_NODES];
    memset(layer_count, 0, sizeof(layer_count));

    for (int i = 0; i < n; i++) {
        int L = layer[i];
        layer_nodes[L][layer_count[L]++] = i;
    }

    /* Step 4: Crossing minimisation — barycenter heuristic (4 passes) */
    float pos[LUI_NG_MAX_NODES];  /* position within layer */
    for (int i = 0; i < n; i++)
        pos[i] = 0.0f;

    /* Initial position: order of insertion */
    for (int L = 0; L < num_layers; L++)
        for (int j = 0; j < layer_count[L]; j++)
            pos[layer_nodes[L][j]] = (float)j;

    /* Build reverse adjacency for backward sweep */
    int radj[LUI_NG_MAX_NODES][LUI_NG_MAX_NODES];
    int radj_count[LUI_NG_MAX_NODES];
    memset(radj_count, 0, sizeof(int) * (size_t)n);
    for (int i = 0; i < n; i++)
        for (int k = 0; k < adj_count[i]; k++) {
            int v = adj[i][k];
            if (radj_count[v] < LUI_NG_MAX_NODES)
                radj[v][radj_count[v]++] = i;
        }

    for (int pass = 0; pass < 4; pass++) {
        if (pass % 2 == 0) {
            /* Forward sweep: layers 1..max */
            for (int L = 1; L < num_layers; L++) {
                for (int j = 0; j < layer_count[L]; j++) {
                    int u = layer_nodes[L][j];
                    if (radj_count[u] == 0) continue;
                    float sum = 0;
                    for (int k = 0; k < radj_count[u]; k++)
                        sum += pos[radj[u][k]];
                    pos[u] = sum / (float)radj_count[u];
                }
                /* Sort layer by position (insertion sort) */
                for (int a = 1; a < layer_count[L]; a++) {
                    int key = layer_nodes[L][a];
                    float kp = pos[key];
                    int b = a - 1;
                    while (b >= 0 && pos[layer_nodes[L][b]] > kp) {
                        layer_nodes[L][b + 1] = layer_nodes[L][b];
                        b--;
                    }
                    layer_nodes[L][b + 1] = key;
                }
                /* Reassign integer positions after sort */
                for (int j = 0; j < layer_count[L]; j++)
                    pos[layer_nodes[L][j]] = (float)j;
            }
        } else {
            /* Backward sweep: layers max-1..0 */
            for (int L = num_layers - 2; L >= 0; L--) {
                for (int j = 0; j < layer_count[L]; j++) {
                    int u = layer_nodes[L][j];
                    if (adj_count[u] == 0) continue;
                    float sum = 0;
                    for (int k = 0; k < adj_count[u]; k++)
                        sum += pos[adj[u][k]];
                    pos[u] = sum / (float)adj_count[u];
                }
                for (int a = 1; a < layer_count[L]; a++) {
                    int key = layer_nodes[L][a];
                    float kp = pos[key];
                    int b = a - 1;
                    while (b >= 0 && pos[layer_nodes[L][b]] > kp) {
                        layer_nodes[L][b + 1] = layer_nodes[L][b];
                        b--;
                    }
                    layer_nodes[L][b + 1] = key;
                }
                for (int j = 0; j < layer_count[L]; j++)
                    pos[layer_nodes[L][j]] = (float)j;
            }
        }
    }

    /* Step 5: Assign coordinates */
    float layer_spacing = 200.0f; /* horizontal gap between layers */
    float node_spacing  = 30.0f;  /* vertical gap between nodes in a layer */

    for (int L = 0; L < num_layers; L++) {
        /* Compute total height of this layer */
        float total_h = 0;
        for (int j = 0; j < layer_count[L]; j++) {
            int ni = layer_nodes[L][j];
            total_h += (float)lui_ng_node_height(ng, &ng->nodes[ni]);
            if (j > 0) total_h += node_spacing;
        }

        float start_y = -total_h / 2.0f;
        float cur_y = start_y;
        for (int j = 0; j < layer_count[L]; j++) {
            int ni = layer_nodes[L][j];
            ng->nodes[ni].x = (float)L * layer_spacing;
            ng->nodes[ni].y = cur_y;
            cur_y += (float)lui_ng_node_height(ng, &ng->nodes[ni])
                   + node_spacing;
        }
    }
}

/* ---- Physics simulation ------------------------------------------------- */

/* Centre of a node in graph space */
static void node_center(const lui_nodegraph_t *ng, const lui_ng_node_t *n,
                          float *cx, float *cy)
{
    *cx = n->x + (float)n->width * 0.5f;
    *cy = n->y + (float)lui_ng_node_height(ng, n) * 0.5f;
}

bool lui_nodegraph_step_physics(lui_nodegraph_t *ng, float dt)
{
    if (!ng || !ng->physics_enabled || ng->node_count == 0 || dt <= 0.0f)
        return false;

    /* Accumulate drag_time for velocity tracking */
    ng->drag_time += dt;

    /* Accumulators for forces */
    float fx[LUI_NG_MAX_NODES], fy[LUI_NG_MAX_NODES];
    memset(fx, 0, sizeof(float) * (size_t)ng->node_count);
    memset(fy, 0, sizeof(float) * (size_t)ng->node_count);

    /* Node-node repulsion (only when overlapping or very close) */
    for (int i = 0; i < ng->node_count; i++) {
        lui_ng_node_t *a = &ng->nodes[i];
        float aw = (float)a->width;
        float ah = (float)lui_ng_node_height(ng, a);
        float acx, acy;
        node_center(ng, a, &acx, &acy);

        for (int j = i + 1; j < ng->node_count; j++) {
            lui_ng_node_t *b = &ng->nodes[j];
            float bw = (float)b->width;
            float bh = (float)lui_ng_node_height(ng, b);
            float bcx, bcy;
            node_center(ng, b, &bcx, &bcy);

            float dx = bcx - acx;
            float dy = bcy - acy;

            /* Minimum separation before repulsion kicks in */
            float min_sep_x = (aw + bw) * 0.5f + 20.0f;
            float min_sep_y = (ah + bh) * 0.5f + 10.0f;

            float overlap_x = min_sep_x - fabsf(dx);
            float overlap_y = min_sep_y - fabsf(dy);

            if (overlap_x > 0 && overlap_y > 0) {
                /* Nodes overlap — push apart */
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < 1.0f) { dx = 1.0f; dy = 0.0f; dist = 1.0f; }
                float force = ng->phys_repulsion * (overlap_x + overlap_y)
                            / dist;
                float nx = dx / dist;
                float ny = dy / dist;
                fx[i] -= force * nx;
                fy[i] -= force * ny;
                fx[j] += force * nx;
                fy[j] += force * ny;
            }
        }
    }

    /* Link spring forces */
    for (int i = 0; i < ng->link_count; i++) {
        int si = find_node_idx(ng, ng->links[i].src_node);
        int di = find_node_idx(ng, ng->links[i].dst_node);
        if (si < 0 || di < 0) continue;

        float scx, scy, dcx, dcy;
        node_center(ng, &ng->nodes[si], &scx, &scy);
        node_center(ng, &ng->nodes[di], &dcx, &dcy);

        float dx = dcx - scx;
        float dy = dcy - scy;
        float dist = sqrtf(dx * dx + dy * dy);
        if (dist < 1.0f) dist = 1.0f;

        float displacement = dist - ng->phys_spring_len;
        float force = ng->phys_spring * displacement;
        float nx = dx / dist;
        float ny = dy / dist;

        fx[si] += force * nx;
        fy[si] += force * ny;
        fx[di] -= force * nx;
        fy[di] -= force * ny;
    }

    /* Integrate: apply forces, damp velocity, update position */
    bool any_moving = false;
    float damp = powf(ng->phys_damping, dt * 60.0f); /* frame-rate independent */

    for (int i = 0; i < ng->node_count; i++) {
        lui_ng_node_t *n = &ng->nodes[i];

        /* Skip pinned nodes and nodes being dragged */
        if (n->pinned) continue;
        if (ng->drag_mode == 1 && n->selected) continue;

        /* Apply forces as acceleration (unit mass) */
        n->vx += fx[i] * dt;
        n->vy += fy[i] * dt;

        /* Damping */
        n->vx *= damp;
        n->vy *= damp;

        /* Zero out tiny velocities */
        float speed = sqrtf(n->vx * n->vx + n->vy * n->vy);
        if (speed < ng->phys_min_vel) {
            n->vx = 0;
            n->vy = 0;
        } else {
            any_moving = true;
        }

        /* Update position */
        n->x += n->vx * dt;
        n->y += n->vy * dt;
    }

    /* Throw-remove: check if any node has gone far from the graph centre */
    if (ng->throw_remove_dist > 0) {
        /* Compute centre of mass of all non-thrown nodes */
        float sum_x = 0, sum_y = 0;
        int count = 0;
        for (int i = 0; i < ng->node_count; i++) {
            float speed = sqrtf(ng->nodes[i].vx * ng->nodes[i].vx
                              + ng->nodes[i].vy * ng->nodes[i].vy);
            if (speed < ng->throw_speed_threshold) {
                sum_x += ng->nodes[i].x;
                sum_y += ng->nodes[i].y;
                count++;
            }
        }
        if (count > 0) {
            float cmx = sum_x / (float)count;
            float cmy = sum_y / (float)count;

            for (int i = ng->node_count - 1; i >= 0; i--) {
                lui_ng_node_t *n = &ng->nodes[i];
                float speed = sqrtf(n->vx * n->vx + n->vy * n->vy);
                if (speed >= ng->throw_speed_threshold) {
                    float dx = n->x - cmx;
                    float dy = n->y - cmy;
                    float dist = sqrtf(dx * dx + dy * dy);
                    if (dist > ng->throw_remove_dist) {
                        int nid = n->id;
                        emit_event(ng, LUI_NG_EVENT_NODE_THROWN, nid, -1, -1);
                        lui_nodegraph_remove_node(ng, nid);
                    }
                }
            }
        }
    }

    return any_moving;
}

void lui_nodegraph_impulse(lui_nodegraph_t *ng, int node_id,
                            float dvx, float dvy)
{
    if (!ng) return;
    int idx = find_node_idx(ng, node_id);
    if (idx < 0) return;
    ng->nodes[idx].vx += dvx;
    ng->nodes[idx].vy += dvy;
}

void lui_nodegraph_stop_all(lui_nodegraph_t *ng)
{
    if (!ng) return;
    for (int i = 0; i < ng->node_count; i++) {
        ng->nodes[i].vx = 0;
        ng->nodes[i].vy = 0;
    }
}
