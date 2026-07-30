/*
 * layout.c — Widget layout engine (flexbox-inspired)
 *
 * Two-pass algorithm:
 *   Pass 1 — Measure (bottom-up): compute _desired_w/_desired_h for every node.
 *   Pass 2 — Arrange (top-down): assign computed rect for every node.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/layout.h>
#include <lightui/widget_cache.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

static inline int lui__max(int a, int b) { return a > b ? a : b; }
static inline int lui__min(int a, int b) { return a < b ? a : b; }

static inline int lui__clamp_size(int v, int lo, int hi)
{
    if (v < lo) v = lo;
    if (hi > 0 && v > hi) v = hi;
    return v;
}

static inline int lui__pad_h(const lui_edges_t *p) { return p->left + p->right; }
static inline int lui__pad_v(const lui_edges_t *p) { return p->top + p->bottom; }

/* -------------------------------------------------------------------------
 * API — tree management
 * ------------------------------------------------------------------------- */

void lui_widget_init(lui_widget_t *w)
{
    memset(w, 0, sizeof(*w));
    w->direction   = LUI_LAYOUT_COLUMN;
    w->width       = lvg_size_hug(0);
    w->height      = lvg_size_hug(0);
    w->align_items = LUI_ALIGN_START;
    w->align_self  = LUI_ALIGN_START;
    w->justify     = LUI_JUSTIFY_START;
}

void lui_widget_add_child(lui_widget_t *parent, lui_widget_t *child)
{
    if (!parent || !child) return;

    /* Detach from old parent first */
    if (child->parent)
        lui_widget_remove(child);

    child->parent = parent;
    child->next_sibling = NULL;

    if (!parent->first_child) {
        parent->first_child = child;
    } else {
        lui_widget_t *last = parent->first_child;
        while (last->next_sibling)
            last = last->next_sibling;
        last->next_sibling = child;
    }
}

void lui_widget_remove(lui_widget_t *child)
{
    if (!child || !child->parent) return;

    lui_widget_t *parent = child->parent;

    if (parent->first_child == child) {
        parent->first_child = child->next_sibling;
    } else {
        lui_widget_t *prev = parent->first_child;
        while (prev && prev->next_sibling != child)
            prev = prev->next_sibling;
        if (prev)
            prev->next_sibling = child->next_sibling;
    }

    child->parent = NULL;
    child->next_sibling = NULL;
}

int lui_widget_child_count(const lui_widget_t *w)
{
    int count = 0;
    if (!w) return 0;
    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        count++;
    return count;
}

lui_widget_t *lui_widget_child_at(const lui_widget_t *w, int index)
{
    if (!w || index < 0) return NULL;
    lui_widget_t *c = w->first_child;
    for (int i = 0; c && i < index; i++)
        c = c->next_sibling;
    return c;
}

/* -------------------------------------------------------------------------
 * Pass 1 — Measure (bottom-up)
 *
 * Computes _desired_w and _desired_h for each widget.
 * For HUG: desired = sum/max of children desired sizes + padding + spacing.
 * For FIXED: desired = value.
 * For FILL: desired = value (minimum), resolved later during arrange.
 * ------------------------------------------------------------------------- */

static void lui__measure(lui_widget_t *w)
{
    /* Measure children first (bottom-up) */
    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        lui__measure(c);

    int content_w = 0, content_h = 0;

    /* If the widget has an intrinsic size callback, use it */
    if (w->measure) {
        w->measure(w, &content_w, &content_h, w->measure_user);
    }

    /* Accumulate children sizes */
    int child_count = 0;
    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling) {
        int cw = c->_desired_w + c->margin.left + c->margin.right;
        int ch = c->_desired_h + c->margin.top + c->margin.bottom;

        if (w->direction == LUI_LAYOUT_ROW) {
            content_w += cw;
            content_h = lui__max(content_h, ch);
        } else if (w->direction == LUI_LAYOUT_COLUMN) {
            content_w = lui__max(content_w, cw);
            content_h += ch;
        } else { /* STACK */
            content_w = lui__max(content_w, cw);
            content_h = lui__max(content_h, ch);
        }
        child_count++;
    }

    /* Add spacing between children */
    if (child_count > 1 && w->direction != LUI_LAYOUT_STACK) {
        int total_spacing = w->spacing * (child_count - 1);
        if (w->direction == LUI_LAYOUT_ROW)
            content_w += total_spacing;
        else
            content_h += total_spacing;
    }

    /* Resolve desired width */
    switch (w->width.mode) {
    case LUI_SIZE_FIXED:
        w->_desired_w = w->width.value;
        break;
    case LUI_SIZE_FILL:
        w->_desired_w = lui__max(w->width.value, content_w + lui__pad_h(&w->padding));
        break;
    case LUI_SIZE_HUG:
    default:
        w->_desired_w = lui__max(w->width.value, content_w + lui__pad_h(&w->padding));
        break;
    }

    /* Resolve desired height */
    switch (w->height.mode) {
    case LUI_SIZE_FIXED:
        w->_desired_h = w->height.value;
        break;
    case LUI_SIZE_FILL:
        w->_desired_h = lui__max(w->height.value, content_h + lui__pad_v(&w->padding));
        break;
    case LUI_SIZE_HUG:
    default:
        w->_desired_h = lui__max(w->height.value, content_h + lui__pad_v(&w->padding));
        break;
    }

    /* Apply min/max constraints */
    w->_desired_w = lui__clamp_size(w->_desired_w, w->min_width, w->max_width);
    w->_desired_h = lui__clamp_size(w->_desired_h, w->min_height, w->max_height);
}

/* -------------------------------------------------------------------------
 * Pass 2 — Arrange (top-down)
 *
 * Given that w->computed is already set (width/height), position children.
 * ------------------------------------------------------------------------- */

static void lui__arrange(lui_widget_t *w)
{
    int px_left   = w->padding.left;
    int px_top    = w->padding.top;
    int content_w = w->computed.width  - lui__pad_h(&w->padding);
    int content_h = w->computed.height - lui__pad_v(&w->padding);

    if (content_w < 0) content_w = 0;
    if (content_h < 0) content_h = 0;

    if (!w->first_child) return;

    /* Count children, total desired main-axis size, and total fill weight */
    int child_count = 0;
    int total_desired_main = 0;
    int total_fill_weight = 0;

    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling) {
        child_count++;
        int m_margin, c_margin;
        if (w->direction == LUI_LAYOUT_ROW) {
            m_margin = c->margin.left + c->margin.right;
            c_margin = c->margin.top + c->margin.bottom;
            if (c->width.mode == LUI_SIZE_FILL) {
                total_fill_weight += c->width.value;
                total_desired_main += c->width.value + m_margin; /* minimum */
            } else {
                total_desired_main += c->_desired_w + m_margin;
            }
            (void)c_margin;
        } else if (w->direction == LUI_LAYOUT_COLUMN) {
            m_margin = c->margin.top + c->margin.bottom;
            c_margin = c->margin.left + c->margin.right;
            if (c->height.mode == LUI_SIZE_FILL) {
                total_fill_weight += c->height.value;
                total_desired_main += c->height.value + m_margin;
            } else {
                total_desired_main += c->_desired_h + m_margin;
            }
            (void)c_margin;
        } else {
            /* STACK: no main-axis accumulation */
        }
    }

    int total_spacing = 0;
    if (child_count > 1 && w->direction != LUI_LAYOUT_STACK)
        total_spacing = w->spacing * (child_count - 1);

    int main_avail = (w->direction == LUI_LAYOUT_ROW ? content_w : content_h);
    int remaining = main_avail - total_desired_main - total_spacing;
    if (remaining < 0) remaining = 0;

    /* Compute justify offsets */
    int justify_start = 0;
    int justify_gap = 0;

    if (w->direction != LUI_LAYOUT_STACK && total_fill_weight == 0) {
        int used = total_desired_main + total_spacing;
        int slack = main_avail - used;
        if (slack < 0) slack = 0;

        switch (w->justify) {
        case LUI_JUSTIFY_CENTER:
            justify_start = slack / 2;
            break;
        case LUI_JUSTIFY_END:
            justify_start = slack;
            break;
        case LUI_JUSTIFY_BETWEEN:
            if (child_count > 1)
                justify_gap = slack / (child_count - 1);
            break;
        case LUI_JUSTIFY_EVENLY:
            if (child_count > 0) {
                int gap = slack / (child_count + 1);
                justify_start = gap;
                justify_gap = gap;
            }
            break;
        case LUI_JUSTIFY_START:
        default:
            break;
        }
    }

    /* Position each child */
    int cursor = (w->direction == LUI_LAYOUT_ROW ? px_left : px_top) + justify_start;

    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling) {
        int cw, ch;

        /* Resolve child width */
        if (c->width.mode == LUI_SIZE_FILL && w->direction == LUI_LAYOUT_ROW) {
            int share = total_fill_weight > 0
                ? (remaining * c->width.value) / total_fill_weight : 0;
            cw = c->width.value + share; /* minimum + share of remaining */
        } else if (c->width.mode == LUI_SIZE_FILL && w->direction != LUI_LAYOUT_ROW) {
            cw = content_w - c->margin.left - c->margin.right;
        } else {
            cw = c->_desired_w;
        }

        /* Resolve child height */
        if (c->height.mode == LUI_SIZE_FILL && w->direction == LUI_LAYOUT_COLUMN) {
            int share = total_fill_weight > 0
                ? (remaining * c->height.value) / total_fill_weight : 0;
            ch = c->height.value + share;
        } else if (c->height.mode == LUI_SIZE_FILL && w->direction != LUI_LAYOUT_COLUMN) {
            ch = content_h - c->margin.top - c->margin.bottom;
        } else {
            ch = c->_desired_h;
        }

        /* Apply min/max */
        cw = lui__clamp_size(cw, c->min_width, c->max_width);
        ch = lui__clamp_size(ch, c->min_height, c->max_height);

        /* Determine cross-axis alignment */
        lui_align_t cross_align = c->align_self != LUI_ALIGN_START
            ? c->align_self : w->align_items;

        /* Handle STRETCH on cross axis */
        if (cross_align == LUI_ALIGN_STRETCH) {
            if (w->direction == LUI_LAYOUT_ROW) {
                ch = content_h - c->margin.top - c->margin.bottom;
                ch = lui__clamp_size(ch, c->min_height, c->max_height);
            } else if (w->direction == LUI_LAYOUT_COLUMN) {
                cw = content_w - c->margin.left - c->margin.right;
                cw = lui__clamp_size(cw, c->min_width, c->max_width);
            }
        }

        if (cw < 0) cw = 0;
        if (ch < 0) ch = 0;

        /* Position */
        int cx, cy;

        if (w->direction == LUI_LAYOUT_ROW) {
            cx = cursor + c->margin.left;
            int cross_space = content_h - ch - c->margin.top - c->margin.bottom;
            if (cross_space < 0) cross_space = 0;
            switch (cross_align) {
            case LUI_ALIGN_CENTER:
                cy = px_top + c->margin.top + cross_space / 2;
                break;
            case LUI_ALIGN_END:
                cy = px_top + c->margin.top + cross_space;
                break;
            case LUI_ALIGN_STRETCH:
            case LUI_ALIGN_START:
            default:
                cy = px_top + c->margin.top;
                break;
            }
            cursor += c->margin.left + cw + c->margin.right + w->spacing + justify_gap;
        } else if (w->direction == LUI_LAYOUT_COLUMN) {
            cy = cursor + c->margin.top;
            int cross_space = content_w - cw - c->margin.left - c->margin.right;
            if (cross_space < 0) cross_space = 0;
            switch (cross_align) {
            case LUI_ALIGN_CENTER:
                cx = px_left + c->margin.left + cross_space / 2;
                break;
            case LUI_ALIGN_END:
                cx = px_left + c->margin.left + cross_space;
                break;
            case LUI_ALIGN_STRETCH:
            case LUI_ALIGN_START:
            default:
                cx = px_left + c->margin.left;
                break;
            }
            cursor += c->margin.top + ch + c->margin.bottom + w->spacing + justify_gap;
        } else {
            /* STACK: all children at padding origin */
            cx = px_left + c->margin.left;
            cy = px_top + c->margin.top;
        }

        c->computed = lvg_rect_make(cx, cy, cw, ch);

        /* Recurse */
        lui__arrange(c);
    }
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

void lui_layout_compute(lui_widget_t *root, int avail_w, int avail_h)
{
    if (!root) return;

    /* Pass 1: measure */
    lui__measure(root);

    /* Resolve root size */
    int rw, rh;

    switch (root->width.mode) {
    case LUI_SIZE_FILL:   rw = avail_w; break;
    case LUI_SIZE_FIXED:  rw = root->width.value; break;
    case LUI_SIZE_HUG:
    default:              rw = lui__min(root->_desired_w, avail_w); break;
    }

    switch (root->height.mode) {
    case LUI_SIZE_FILL:   rh = avail_h; break;
    case LUI_SIZE_FIXED:  rh = root->height.value; break;
    case LUI_SIZE_HUG:
    default:              rh = lui__min(root->_desired_h, avail_h); break;
    }

    rw = lui__clamp_size(rw, root->min_width, root->max_width);
    rh = lui__clamp_size(rh, root->min_height, root->max_height);

    root->computed = lvg_rect_make(0, 0, rw, rh);

    /* Pass 2: arrange */
    lui__arrange(root);
}

lvg_rect_t lui_widget_absolute_rect(const lui_widget_t *w)
{
    if (!w) return lvg_rect_make(0, 0, 0, 0);

    int abs_x = w->computed.x;
    int abs_y = w->computed.y;

    /* Walk up the tree.  Each child's computed coords already include
     * the parent's padding offset, so we only add the parent's origin. */
    const lui_widget_t *p = w->parent;
    while (p) {
        abs_x += p->computed.x;
        abs_y += p->computed.y;
        p = p->parent;
    }

    return lvg_rect_make(abs_x, abs_y, w->computed.width, w->computed.height);
}

/* -------------------------------------------------------------------------
 * Animation
 * ------------------------------------------------------------------------- */

int lui_widget_animate_tree(lui_widget_t *root, float dt)
{
    if (!root) return 0;
    int changed = 0;

    if ((root->flags & LUI_WIDGET_ANIMATING) && root->animate) {
        if (root->animate(root, dt)) {
            lui_widget_invalidate(root);
            changed++;
        }
    }

    for (lui_widget_t *c = root->first_child; c; c = c->next_sibling)
        changed += lui_widget_animate_tree(c, dt);

    return changed;
}

bool lui_widget_has_animations(const lui_widget_t *root)
{
    if (!root) return false;
    if (root->flags & LUI_WIDGET_ANIMATING) return true;
    for (const lui_widget_t *c = root->first_child; c; c = c->next_sibling)
        if (lui_widget_has_animations(c)) return true;
    return false;
}

/* -------------------------------------------------------------------------
 * Drawing
 * ------------------------------------------------------------------------- */

void lui_widget_draw_tree(lui_widget_t *root, lvg_canvas_t *canvas)
{
    if (!root) return;

    /* Draw self first (painter's order: parent behind children) */
    if (root->draw)
        root->draw(root, canvas);

    /* If the widget handles its own children (e.g. scroll container), stop */
    if (root->flags & LUI_WIDGET_DRAWS_CHILDREN)
        return;

    /* Draw children */
    for (lui_widget_t *c = root->first_child; c; c = c->next_sibling)
        lui_widget_draw_tree(c, canvas);
}

/* Recursive helper for timed drawing. Returns false if deadline expired. */
static bool lui__draw_tree_timed(lui_widget_t *root, lvg_canvas_t *canvas,
                                  lui_frame_clock_t *clk,
                                  lui_draw_stats_t *stats)
{
    if (!root) return true;

    /* Check deadline before drawing this widget */
    if (clk && lui_frame_clock_expired(clk)) {
        stats->skipped++;
        stats->timed_out = true;
        return false;
    }

    if (root->draw) {
        root->draw(root, canvas);
        stats->drawn++;
    }

    if (root->flags & LUI_WIDGET_DRAWS_CHILDREN)
        return true;

    for (lui_widget_t *c = root->first_child; c; c = c->next_sibling) {
        if (!lui__draw_tree_timed(c, canvas, clk, stats))
            return false;
    }
    return true;
}

void lui_widget_draw_tree_timed(lui_widget_t *root, lvg_canvas_t *canvas,
                                  lui_frame_clock_t *clk,
                                  lui_draw_stats_t *stats)
{
    lui_draw_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof(*stats));

    /* NULL clock = no deadline, same as regular draw_tree */
    if (!clk) {
        lui_widget_draw_tree(root, canvas);
        return;
    }

    lui__draw_tree_timed(root, canvas, clk, stats);
}

/* -------------------------------------------------------------------------
 * Dirty-rect rendering
 * ------------------------------------------------------------------------- */

void lui_widget_invalidate(lui_widget_t *w)
{
    if (!w) return;
    w->flags |=  LUI_WIDGET_DIRTY;
    w->flags &= ~LUI_WIDGET_CACHE_VALID;   /* force cache rebuild on next draw */
}

bool lui_widget_is_dirty(const lui_widget_t *w)
{
    return w && (w->flags & LUI_WIDGET_DIRTY) != 0;
}

/* Recursive collector: threads an accumulated absolute origin so each node's
 * absolute rect is (origin + computed) in O(1), avoiding per-node ancestor
 * walks. */
static void lui__collect_dirty(const lui_widget_t *w, int ox, int oy,
                               lvg_dirty_t *out)
{
    int ax = ox + w->computed.x;
    int ay = oy + w->computed.y;

    if (w->flags & LUI_WIDGET_DIRTY) {
        lvg_rect_t r = lvg_rect_make(ax, ay, w->computed.width, w->computed.height);
        lvg_dirty_add(out, &r);
    }

    if (w->flags & LUI_WIDGET_DRAWS_CHILDREN)
        return;

    for (const lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        lui__collect_dirty(c, ax, ay, out);
}

void lui_widget_collect_dirty(lui_widget_t *root, lvg_dirty_t *out)
{
    if (!root || !out) return;
    lui__collect_dirty(root, 0, 0, out);
}

/* Clear LUI_WIDGET_DIRTY across a subtree — used after a cached widget is
 * (re)captured so its descendants don't re-trigger collection next frame. */
static void lui__clear_dirty_subtree(lui_widget_t *w)
{
    w->flags &= ~LUI_WIDGET_DIRTY;
    if (w->flags & LUI_WIDGET_DRAWS_CHILDREN)
        return;
    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        lui__clear_dirty_subtree(c);
}

/* Replay or rebuild a cached widget that overlaps the active dirty clip. */
static void lui__draw_cached(lui_widget_t *w, int ax, int ay,
                             lvg_canvas_t *canvas, const lvg_rect_t *abs,
                             const lvg_rect_t *clip_bounds,
                             lui_draw_stats_t *stats)
{
    lvg_rect_t clip = lvg_rect_intersect(abs, clip_bounds);

    bool valid = (w->flags & LUI_WIDGET_CACHE_VALID) && w->cache &&
                 w->cache->surface && w->cache->w == abs->width &&
                 w->cache->h == abs->height && !(w->flags & LUI_WIDGET_DIRTY);

    if (valid) {
        /* HIT: copy cached pixels, clipped to the dirty region. */
        lvg_canvas_set_clip(canvas, &clip);
        lvg_canvas_blit(canvas, ax, ay, w->cache->surface, NULL);
        lui_widget_cache_touch(w);
        stats->cache_hits++;
        return;
    }

    /* MISS: render the full subtree live, then capture it into the cache. */
    if (lui_widget_cache_ensure(w, abs->width, abs->height)) {
        lvg_canvas_set_clip(canvas, abs);          /* render whole widget */
        lui_widget_draw_tree(w, canvas);
        lvg_surface_t *main_surf = lvg_canvas_get_surface(canvas);
        lvg_canvas_t cache_canvas;
        lvg_canvas_init(&cache_canvas, w->cache->surface);
        lvg_canvas_blit(&cache_canvas, 0, 0, main_surf, abs);
        w->flags |= LUI_WIDGET_CACHE_VALID;
        lui_widget_cache_touch(w);
    } else {
        /* Could not allocate a cache surface: draw live, clipped to dirty. */
        lvg_canvas_set_clip(canvas, &clip);
        lui_widget_draw_tree(w, canvas);
    }
    lui__clear_dirty_subtree(w);
    stats->cache_misses++;
}

/* Recursive clipped draw for one dirty rectangle. Parent-first = painter's
 * order. Any widget that overlaps the active clip must redraw, because that
 * exact clip was cleared before this pass. */
static void lui__draw_tree_clip(lui_widget_t *w, int ox, int oy,
                                lvg_canvas_t *canvas,
                                const lvg_rect_t *clip_bounds,
                                lui_draw_stats_t *stats)
{
    int ax = ox + w->computed.x;
    int ay = oy + w->computed.y;
    lvg_rect_t abs = lvg_rect_make(ax, ay, w->computed.width, w->computed.height);
    bool overlaps = lvg_rect_overlaps(&abs, clip_bounds);

    /* Cached widgets are handled as a unit: their whole subtree is either
     * replayed from the cache or re-rendered and re-captured. */
    if (w->flags & LUI_WIDGET_CACHE) {
        if (overlaps)
            lui__draw_cached(w, ax, ay, canvas, &abs, clip_bounds, stats);
        else
            stats->skipped++;
        return;
    }

    if (w->draw) {
        if (overlaps) {
            lvg_rect_t clip = lvg_rect_intersect(&abs, clip_bounds);
            lvg_canvas_set_clip(canvas, &clip);
            w->draw(w, canvas);
            stats->drawn++;
            w->flags &= ~LUI_WIDGET_DIRTY;   /* consumed */
        } else {
            stats->skipped++;
        }
    }

    if (w->flags & LUI_WIDGET_DRAWS_CHILDREN)
        return;

    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling)
        lui__draw_tree_clip(c, ax, ay, canvas, clip_bounds, stats);
}

typedef struct {
    lui_widget_t      *root;
    lvg_canvas_t      *canvas;
    lui_draw_stats_t  *stats;
} lui_dirty_draw_ctx_t;

static void lui__draw_dirty_clip_cb(const lvg_rect_t *clip, void *user)
{
    lui_dirty_draw_ctx_t *ctx = (lui_dirty_draw_ctx_t *)user;
    lui__draw_tree_clip(ctx->root, 0, 0, ctx->canvas, clip, ctx->stats);
}

void lui_widget_draw_tree_dirty(lui_widget_t *root, lvg_canvas_t *canvas,
                                const lvg_dirty_t *dirty,
                                lui_draw_stats_t *stats)
{
    lui_draw_stats_t local;
    if (!stats) stats = &local;
    memset(stats, 0, sizeof(*stats));

    if (!root || !canvas || !dirty || !lvg_dirty_any(dirty))
        return;

    lui_widget_cache_tick();   /* advance frame counter for cache LRU */

    lvg_rect_t root_abs = lvg_rect_make(0, 0, root->computed.width,
                                        root->computed.height);
    lui_dirty_draw_ctx_t ctx = { root, canvas, stats };
    lvg_dirty_foreach(dirty, &root_abs, lui__draw_dirty_clip_cb, &ctx);

    lvg_canvas_reset_clip(canvas);
}

/* -------------------------------------------------------------------------
 * Event dispatch
 * ------------------------------------------------------------------------- */

/* Hit test: depth-first, last child wins (front-most in painter's order) */
lui_widget_t *lui_widget_hit_test(lui_widget_t *root, int x, int y)
{
    if (!root) return NULL;

    lvg_rect_t r = lui_widget_absolute_rect(root);
    if (!lvg_rect_contains_point(&r, x, y))
        return NULL;

    /* Check children in reverse order (last child = front-most) */
    lui_widget_t *last_child = NULL;
    for (lui_widget_t *c = root->first_child; c; c = c->next_sibling)
        last_child = c;

    /* Walk backwards by iterating and checking from last */
    for (lui_widget_t *c = root->first_child; c; c = c->next_sibling) {
        lui_widget_t *hit = lui_widget_hit_test(c, x, y);
        if (hit)
            last_child = hit; /* keep updating; last one wins */
    }

    /* If a child was hit, return it; otherwise return root itself */
    if (last_child) {
        lvg_rect_t cr = lui_widget_absolute_rect(last_child);
        if (lvg_rect_contains_point(&cr, x, y))
            return last_child;
    }

    return root;
}

static int lui__is_pointer_event(lui_event_type_t type)
{
    return type == LUI_EVENT_MOUSE_MOVE ||
           type == LUI_EVENT_MOUSE_DOWN ||
           type == LUI_EVENT_MOUSE_UP   ||
           type == LUI_EVENT_SCROLL;
}

static int lui__event_x(const lui_event_t *e)
{
    switch (e->type) {
    case LUI_EVENT_MOUSE_MOVE: return e->data.mouse_move.x;
    case LUI_EVENT_MOUSE_DOWN:
    case LUI_EVENT_MOUSE_UP:   return e->data.mouse_button.x;
    case LUI_EVENT_SCROLL:     return e->data.scroll.x;
    default: return 0;
    }
}

static int lui__event_y(const lui_event_t *e)
{
    switch (e->type) {
    case LUI_EVENT_MOUSE_MOVE: return e->data.mouse_move.y;
    case LUI_EVENT_MOUSE_DOWN:
    case LUI_EVENT_MOUSE_UP:   return e->data.mouse_button.y;
    case LUI_EVENT_SCROLL:     return e->data.scroll.y;
    default: return 0;
    }
}

/* Walk from widget up to root, calling on_event until consumed */
static lui_widget_t *lui__bubble_event(lui_widget_t *w, const lui_event_t *e)
{
    while (w) {
        if (w->on_event && w->on_event(w, e))
            return w;
        w = w->parent;
    }
    return NULL;
}

/* Broadcast: depth-first, stop on first consumer */
static lui_widget_t *lui__broadcast_event(lui_widget_t *w, const lui_event_t *e)
{
    if (!w) return NULL;
    if (w->on_event && w->on_event(w, e))
        return w;
    for (lui_widget_t *c = w->first_child; c; c = c->next_sibling) {
        lui_widget_t *consumer = lui__broadcast_event(c, e);
        if (consumer) return consumer;
    }
    return NULL;
}

lui_widget_t *lui_widget_dispatch_event(lui_widget_t *root,
                                         const lui_event_t *event)
{
    if (!root || !event) return NULL;

    if (lui__is_pointer_event(event->type)) {
        int x = lui__event_x(event);
        int y = lui__event_y(event);
        lui_widget_t *hit = lui_widget_hit_test(root, x, y);
        if (hit)
            return lui__bubble_event(hit, event);
        return NULL;
    }

    /* Non-pointer events: broadcast depth-first */
    return lui__broadcast_event(root, event);
}

/* -------------------------------------------------------------------------
 * UI context (focus-aware dispatch)
 * ------------------------------------------------------------------------- */

void lui_ui_ctx_init(lui_ui_ctx_t *ctx, lui_widget_t *root)
{
    if (!ctx) return;
    ctx->root  = root;
    ctx->focus = NULL;
}

static int lui__is_keyboard_event(lui_event_type_t type)
{
    return type == LUI_EVENT_KEY_DOWN ||
           type == LUI_EVENT_KEY_UP  ||
           type == LUI_EVENT_TEXT_INPUT;
}

/* Walk up from widget to find the nearest focusable ancestor (inclusive) */
static lui_widget_t *lui__find_focusable(lui_widget_t *w)
{
    while (w) {
        if (w->flags & LUI_WIDGET_FOCUSABLE)
            return w;
        w = w->parent;
    }
    return NULL;
}

lui_widget_t *lui_ui_ctx_dispatch(lui_ui_ctx_t *ctx, const lui_event_t *event)
{
    if (!ctx || !ctx->root || !event) return NULL;

    if (lui__is_pointer_event(event->type)) {
        int x = lui__event_x(event);
        int y = lui__event_y(event);
        lui_widget_t *hit = lui_widget_hit_test(ctx->root, x, y);

        /* On mouse down, move focus to the hit widget (or its focusable ancestor) */
        if (event->type == LUI_EVENT_MOUSE_DOWN && hit) {
            lui_widget_t *focusable = lui__find_focusable(hit);
            ctx->focus = focusable; /* may be NULL (clicks on non-focusable clear focus) */
        }

        if (hit)
            return lui__bubble_event(hit, event);
        return NULL;
    }

    if (lui__is_keyboard_event(event->type)) {
        /* Route keyboard events to the focused widget, bubble up */
        if (ctx->focus)
            return lui__bubble_event(ctx->focus, event);
        return NULL;
    }

    /* Other events: broadcast */
    return lui__broadcast_event(ctx->root, event);
}

void lui_ui_ctx_set_focus(lui_ui_ctx_t *ctx, lui_widget_t *widget)
{
    if (!ctx) return;
    ctx->focus = widget;
}
