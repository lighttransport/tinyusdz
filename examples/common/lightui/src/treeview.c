/*
 * treeview.c — Tree view widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/treeview.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static int find_node_idx(const lui_treeview_t *tv, int node_id)
{
    for (int i = 0; i < tv->node_count; i++)
        if (tv->nodes[i].id == node_id) return i;
    return -1;
}

/* Compute the depth of a node by walking up parent_id chain. */
static int node_depth(const lui_treeview_t *tv, int node_id)
{
    int depth = 0;
    int cur = node_id;
    while (cur >= 0) {
        int idx = find_node_idx(tv, cur);
        if (idx < 0) break;
        cur = tv->nodes[idx].parent_id;
        if (cur >= 0) depth++;
    }
    return depth;
}

/* Check if a node has any children. */
static bool has_children(const lui_treeview_t *tv, int node_id)
{
    for (int i = 0; i < tv->node_count; i++)
        if (tv->nodes[i].parent_id == node_id) return true;
    return false;
}

/* Collect visible node indices in display order (DFS). */
static int collect_visible(const lui_treeview_t *tv, int *out, int max_out)
{
    /* Build display order: root-level nodes in array order,
       then recursively their children if expanded. */
    int count = 0;

    /* Stack-based iterative DFS */
    int stack[LUI_TV_MAX_NODES];
    int sp = 0;

    /* Push root-level nodes in reverse order so first appears first */
    for (int i = tv->node_count - 1; i >= 0; i--) {
        if (tv->nodes[i].parent_id < 0)
            stack[sp++] = i;
    }

    while (sp > 0 && count < max_out) {
        int idx = stack[--sp];
        out[count++] = idx;

        /* If expanded, push children in reverse order */
        if (tv->nodes[idx].expanded && !tv->nodes[idx].leaf) {
            int nid = tv->nodes[idx].id;
            for (int i = tv->node_count - 1; i >= 0; i--) {
                if (tv->nodes[i].parent_id == nid)
                    stack[sp++] = i;
            }
        }
    }

    return count;
}

/* Draw a small triangle arrow (right or down). */
static void draw_expand_arrow(lvg_canvas_t *canvas, int cx, int cy,
                                int size, bool expanded, lvg_color_t color)
{
    int hs = size / 2;
    if (expanded) {
        /* Down-pointing triangle */
        lvg_canvas_fill_triangle(canvas,
                                  cx - hs, cy - hs / 2,
                                  cx + hs, cy - hs / 2,
                                  cx,      cy + hs / 2,
                                  color);
    } else {
        /* Right-pointing triangle */
        lvg_canvas_fill_triangle(canvas,
                                  cx - hs / 2, cy - hs,
                                  cx + hs / 2, cy,
                                  cx - hs / 2, cy + hs,
                                  color);
    }
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int tv_measure(const lui_widget_t *w, int *out_w, int *out_h,
                       void *user)
{
    (void)w; (void)user;
    *out_w = 200;
    *out_h = 300;
    return 0;
}

static void tv_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_treeview_t *tv = (lui_treeview_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, tv->bg);

    /* Clip */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    /* Collect visible nodes */
    int visible[LUI_TV_MAX_NODES];
    int vis_count = collect_visible(tv, visible, LUI_TV_MAX_NODES);

    tv->content_height = vis_count * tv->row_height;

    /* Clamp scroll */
    int max_scroll = tv->content_height - r.height;
    if (max_scroll < 0) max_scroll = 0;
    if (tv->scroll_y < 0) tv->scroll_y = 0;
    if (tv->scroll_y > max_scroll) tv->scroll_y = max_scroll;

    /* Draw visible rows */
    for (int i = 0; i < vis_count; i++) {
        int idx = visible[i];
        const lui_tv_node_t *node = &tv->nodes[idx];
        int depth = node_depth(tv, node->id);

        int row_y = r.y + i * tv->row_height - tv->scroll_y;

        /* Skip if outside visible area */
        if (row_y + tv->row_height <= r.y || row_y >= r.y + r.height)
            continue;

        int row_x = r.x + depth * tv->indent;

        /* Selection / hover highlight */
        if (node->id == tv->selected_id) {
            lvg_canvas_fill_rect(canvas, r.x, row_y, r.width, tv->row_height,
                                  tv->selected_bg);
        } else if (node->id == tv->hovered_id) {
            lvg_canvas_fill_rect(canvas, r.x, row_y, r.width, tv->row_height,
                                  tv->hover_bg);
        }

        /* Indentation guide lines */
        for (int d = 1; d <= depth; d++) {
            int gx = r.x + d * tv->indent - tv->indent / 2;
            lvg_canvas_fill_rect(canvas, gx, row_y, 1, tv->row_height,
                                  tv->guide_color);
        }

        int cx = row_x + tv->arrow_size;
        int cy = row_y + tv->row_height / 2;

        /* Expand/collapse arrow (only for non-leaf nodes with children) */
        if (!node->leaf && has_children(tv, node->id)) {
            draw_expand_arrow(canvas, row_x + tv->arrow_size / 2 + 2, cy,
                               tv->arrow_size, node->expanded, tv->arrow_color);
        }

        /* Icon color indicator */
        if (LVG_COLOR_A(node->icon_color) > 0) {
            int icon_x = cx + 4;
            int icon_y = cy - tv->icon_size / 2;
            lvg_canvas_fill_rect(canvas, icon_x, icon_y,
                                  tv->icon_size, tv->icon_size,
                                  node->icon_color);
            cx = icon_x + tv->icon_size + 4;
        } else {
            cx += 6;
        }

        /* Label (character rectangles) */
        lvg_color_t tc = (node->id == tv->selected_id)
                       ? tv->text_selected : tv->text_color;
        int label_len = (int)strlen(node->label);
        int tx = cx;
        int ty = cy - 5;
        for (int c = 0; c < label_len && tx < r.x + r.width - tv->scrollbar_width - 4; c++) {
            lvg_canvas_fill_rect(canvas, tx, ty, 5, 10, tc);
            tx += 7;
        }
    }

    /* Scrollbar */
    if (tv->content_height > r.height) {
        int sb_x = r.x + r.width - tv->scrollbar_width;
        int track_h = r.height;
        int thumb_h = (r.height * r.height) / tv->content_height;
        if (thumb_h < 20) thumb_h = 20;
        int thumb_y = r.y;
        if (max_scroll > 0)
            thumb_y += (tv->scroll_y * (track_h - thumb_h)) / max_scroll;
        lvg_canvas_fill_rounded_rect(canvas, sb_x, thumb_y,
                                      tv->scrollbar_width, thumb_h,
                                      tv->scrollbar_width / 2,
                                      tv->scrollbar_color);
    }

    canvas->_clip = old_clip;
}

static int tv_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_treeview_t *tv = (lui_treeview_t *)w;

    if (event->type == LUI_EVENT_MOUSE_DOWN &&
        event->data.mouse_button.button == LUI_MOUSE_LEFT) {

        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_button.x;
        int my = event->data.mouse_button.y;

        if (!lvg_rect_contains_point(&r, mx, my)) return 0;

        /* Find which row was clicked */
        int visible[LUI_TV_MAX_NODES];
        int vis_count = collect_visible(tv, visible, LUI_TV_MAX_NODES);

        int rel_y = my - r.y + tv->scroll_y;
        int row = rel_y / tv->row_height;
        if (row < 0 || row >= vis_count) return 1;

        int idx = visible[row];
        lui_tv_node_t *node = &tv->nodes[idx];
        int depth = node_depth(tv, node->id);
        int arrow_x = r.x + depth * tv->indent;
        int arrow_end = arrow_x + tv->arrow_size + 4;

        /* Check if click was on the arrow area */
        if (mx < arrow_end && !node->leaf && has_children(tv, node->id)) {
            node->expanded = !node->expanded;
            if (tv->on_event) {
                lui_tv_event_t ev;
                ev.type = node->expanded ? LUI_TV_EVENT_EXPANDED
                                         : LUI_TV_EVENT_COLLAPSED;
                ev.node_id = node->id;
                tv->on_event(&ev, tv->on_event_user);
            }
            return 1;
        }

        /* Select node */
        int old_sel = tv->selected_id;
        tv->selected_id = node->id;
        for (int i = 0; i < tv->node_count; i++)
            tv->nodes[i].selected = (tv->nodes[i].id == node->id);

        if (old_sel != node->id && tv->on_event) {
            lui_tv_event_t ev;
            ev.type = LUI_TV_EVENT_SELECTED;
            ev.node_id = node->id;
            tv->on_event(&ev, tv->on_event_user);
        }

        /* Double-click */
        if (event->data.mouse_button.clicks >= 2 && tv->on_event) {
            lui_tv_event_t ev;
            ev.type = LUI_TV_EVENT_DOUBLE_CLICK;
            ev.node_id = node->id;
            tv->on_event(&ev, tv->on_event_user);
        }

        return 1;
    }

    if (event->type == LUI_EVENT_MOUSE_MOVE) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        int mx = event->data.mouse_move.x;
        int my = event->data.mouse_move.y;

        if (!lvg_rect_contains_point(&r, mx, my)) {
            tv->hovered_id = -1;
            return 0;
        }

        int visible[LUI_TV_MAX_NODES];
        int vis_count = collect_visible(tv, visible, LUI_TV_MAX_NODES);

        int rel_y = my - r.y + tv->scroll_y;
        int row = rel_y / tv->row_height;
        if (row >= 0 && row < vis_count)
            tv->hovered_id = tv->nodes[visible[row]].id;
        else
            tv->hovered_id = -1;
        return 1;
    }

    if (event->type == LUI_EVENT_SCROLL) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.scroll.x, event->data.scroll.y)) {
            int delta = (int)(event->data.scroll.delta_y * -3.0f)
                      * tv->row_height;
            tv->scroll_y += delta;
            int max_scroll = tv->content_height - r.height;
            if (max_scroll < 0) max_scroll = 0;
            if (tv->scroll_y < 0) tv->scroll_y = 0;
            if (tv->scroll_y > max_scroll) tv->scroll_y = max_scroll;
            return 1;
        }
    }

    if (event->type == LUI_EVENT_KEY_DOWN) {
        switch (event->data.key.key) {
        case LUI_KEY_UP: {
            int visible[LUI_TV_MAX_NODES];
            int vis_count = collect_visible(tv, visible, LUI_TV_MAX_NODES);
            for (int i = 0; i < vis_count; i++) {
                if (tv->nodes[visible[i]].id == tv->selected_id && i > 0) {
                    lui_treeview_select(tv, tv->nodes[visible[i - 1]].id);
                    break;
                }
            }
            return 1;
        }
        case LUI_KEY_DOWN: {
            int visible[LUI_TV_MAX_NODES];
            int vis_count = collect_visible(tv, visible, LUI_TV_MAX_NODES);
            for (int i = 0; i < vis_count; i++) {
                if (tv->nodes[visible[i]].id == tv->selected_id
                    && i + 1 < vis_count) {
                    lui_treeview_select(tv, tv->nodes[visible[i + 1]].id);
                    break;
                }
            }
            return 1;
        }
        case LUI_KEY_LEFT: {
            int idx = find_node_idx(tv, tv->selected_id);
            if (idx >= 0 && tv->nodes[idx].expanded
                && has_children(tv, tv->nodes[idx].id)) {
                tv->nodes[idx].expanded = false;
                if (tv->on_event) {
                    lui_tv_event_t ev;
                    ev.type = LUI_TV_EVENT_COLLAPSED;
                    ev.node_id = tv->nodes[idx].id;
                    tv->on_event(&ev, tv->on_event_user);
                }
            } else if (idx >= 0 && tv->nodes[idx].parent_id >= 0) {
                lui_treeview_select(tv, tv->nodes[idx].parent_id);
            }
            return 1;
        }
        case LUI_KEY_RIGHT: {
            int idx = find_node_idx(tv, tv->selected_id);
            if (idx >= 0 && !tv->nodes[idx].leaf
                && has_children(tv, tv->nodes[idx].id)) {
                if (!tv->nodes[idx].expanded) {
                    tv->nodes[idx].expanded = true;
                    if (tv->on_event) {
                        lui_tv_event_t ev;
                        ev.type = LUI_TV_EVENT_EXPANDED;
                        ev.node_id = tv->nodes[idx].id;
                        tv->on_event(&ev, tv->on_event_user);
                    }
                }
            }
            return 1;
        }
        default:
            break;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *tv_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  tv_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_treeview_init_ex(lui_treeview_t *tv, int max_nodes,
                           lui_alloc_fn alloc_fn,
                           lui_free_fn  free_fn,
                           void        *alloc_user)
{
    if (!tv || max_nodes <= 0) return false;
    /* Helper functions in this file use stack arrays sized at LUI_TV_MAX_NODES;
     * cap user-supplied capacity at the compile-time max for safety. */
    if (max_nodes > LUI_TV_MAX_NODES) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = tv_default_alloc;
        free_fn    = tv_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_nodes * sizeof(lui_tv_node_t);
    if (bytes / sizeof(lui_tv_node_t) != (size_t)max_nodes) return false;
    lui_tv_node_t *nodes = (lui_tv_node_t *)alloc_fn(alloc_user, bytes);
    if (!nodes) return false;
    memset(nodes, 0, bytes);

    memset(tv, 0, sizeof(*tv));
    tv->nodes      = nodes;
    tv->max_nodes  = max_nodes;
    tv->alloc_fn   = alloc_fn;
    tv->free_fn    = free_fn;
    tv->alloc_user = alloc_user;

    lui_widget_init(&tv->widget);
    tv->widget.width   = lvg_size_fill(1);
    tv->widget.height  = lvg_size_fill(1);
    tv->widget.measure = tv_measure;
    tv->widget.draw    = tv_draw;
    tv->widget.on_event = tv_event;
    tv->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN | LUI_WIDGET_FOCUSABLE;

    tv->node_count  = 0;
    tv->next_id     = 1;
    tv->selected_id = -1;
    tv->hovered_id  = -1;
    tv->scroll_y    = 0;
    tv->content_height = 0;

    tv->row_height  = 22;
    tv->indent      = 18;
    tv->icon_size   = 10;
    tv->arrow_size  = 8;
    tv->corner_radius = 3;

    tv->bg              = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    tv->text_color      = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    tv->text_selected   = LVG_COLOR_WHITE;
    tv->hover_bg        = LVG_COLOR_RGB(0x30, 0x34, 0x3C);
    tv->selected_bg     = LVG_COLOR_RGB(0x44, 0x6E, 0x9E);
    tv->arrow_color     = LVG_COLOR_RGB(0xA0, 0xA4, 0xAA);
    tv->guide_color     = LVG_COLOR_RGB(0x38, 0x3C, 0x44);
    tv->scrollbar_color = LVG_COLOR_ARGB(0x80, 0x80, 0x84, 0x8A);
    tv->scrollbar_width = 6;

    tv->on_event      = NULL;
    tv->on_event_user = NULL;

    return true;
}

bool lui_treeview_init(lui_treeview_t *tv)
{
    return lui_treeview_init_ex(tv, LUI_TV_MAX_NODES, NULL, NULL, NULL);
}

void lui_treeview_destroy(lui_treeview_t *tv)
{
    if (!tv) return;
    if (tv->free_fn && tv->nodes)
        tv->free_fn(tv->alloc_user, tv->nodes);
    tv->nodes      = NULL;
    tv->node_count = 0;
    tv->max_nodes  = 0;
}

int lui_treeview_add_node(lui_treeview_t *tv, int parent_id,
                           const char *label, bool leaf)
{
    if (!tv || !label || tv->node_count >= tv->max_nodes)
        return -1;

    /* Verify parent exists (if not root level) */
    if (parent_id >= 0 && find_node_idx(tv, parent_id) < 0)
        return -1;

    lui_tv_node_t *node = &tv->nodes[tv->node_count];
    node->id = tv->next_id++;
    node->parent_id = parent_id;
    node->icon_color = 0;
    node->expanded = false;
    node->selected = false;
    node->leaf = leaf;

    int len = (int)strlen(label);
    if (len > LUI_TV_MAX_LABEL_LEN) len = LUI_TV_MAX_LABEL_LEN;
    memcpy(node->label, label, len);
    node->label[len] = '\0';

    tv->node_count++;
    return node->id;
}

void lui_treeview_remove_node(lui_treeview_t *tv, int node_id)
{
    if (!tv) return;

    /* Collect all descendants (BFS) */
    int to_remove[LUI_TV_MAX_NODES];
    int rm_count = 0;
    to_remove[rm_count++] = node_id;

    for (int i = 0; i < rm_count; i++) {
        int rid = to_remove[i];
        for (int j = 0; j < tv->node_count; j++) {
            if (tv->nodes[j].parent_id == rid) {
                /* Check not already in list */
                bool found = false;
                for (int k = 0; k < rm_count; k++)
                    if (to_remove[k] == tv->nodes[j].id) { found = true; break; }
                if (!found && rm_count < LUI_TV_MAX_NODES)
                    to_remove[rm_count++] = tv->nodes[j].id;
            }
        }
    }

    /* Remove all marked nodes */
    for (int r = 0; r < rm_count; r++) {
        int idx = find_node_idx(tv, to_remove[r]);
        if (idx < 0) continue;
        if (tv->selected_id == to_remove[r])
            tv->selected_id = -1;
        /* Shift array */
        for (int j = idx; j < tv->node_count - 1; j++)
            tv->nodes[j] = tv->nodes[j + 1];
        tv->node_count--;
    }
}

lui_tv_node_t *lui_treeview_get_node(lui_treeview_t *tv, int node_id)
{
    if (!tv) return NULL;
    int idx = find_node_idx(tv, node_id);
    return idx >= 0 ? &tv->nodes[idx] : NULL;
}

void lui_treeview_select(lui_treeview_t *tv, int node_id)
{
    if (!tv) return;
    tv->selected_id = node_id;
    for (int i = 0; i < tv->node_count; i++)
        tv->nodes[i].selected = (tv->nodes[i].id == node_id);

    if (tv->on_event && node_id >= 0) {
        lui_tv_event_t ev;
        ev.type = LUI_TV_EVENT_SELECTED;
        ev.node_id = node_id;
        tv->on_event(&ev, tv->on_event_user);
    }
}

void lui_treeview_expand(lui_treeview_t *tv, int node_id)
{
    if (!tv) return;
    int idx = find_node_idx(tv, node_id);
    if (idx >= 0) tv->nodes[idx].expanded = true;
}

void lui_treeview_collapse(lui_treeview_t *tv, int node_id)
{
    if (!tv) return;
    int idx = find_node_idx(tv, node_id);
    if (idx >= 0) tv->nodes[idx].expanded = false;
}

void lui_treeview_expand_all(lui_treeview_t *tv)
{
    if (!tv) return;
    for (int i = 0; i < tv->node_count; i++)
        if (!tv->nodes[i].leaf)
            tv->nodes[i].expanded = true;
}

void lui_treeview_collapse_all(lui_treeview_t *tv)
{
    if (!tv) return;
    for (int i = 0; i < tv->node_count; i++)
        tv->nodes[i].expanded = false;
}

int lui_treeview_child_count(const lui_treeview_t *tv, int node_id)
{
    if (!tv) return 0;
    int count = 0;
    for (int i = 0; i < tv->node_count; i++)
        if (tv->nodes[i].parent_id == node_id) count++;
    return count;
}

int lui_treeview_depth(const lui_treeview_t *tv, int node_id)
{
    return node_depth(tv, node_id);
}
