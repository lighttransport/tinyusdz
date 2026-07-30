/*
 * lightui/layout.h — Widget layout framework
 *
 * A flexbox-inspired layout system for arranging rectangular widgets in rows
 * and columns.  Widgets form a tree (first-child / next-sibling).  Layout is
 * computed in two passes:
 *
 *   1. Measure (bottom-up) — compute desired sizes from leaves to root.
 *   2. Arrange (top-down)  — assign final bounds from root to leaves.
 *
 * All widget structs can live on the stack; the framework performs no heap
 * allocation.  Call lui_layout_compute() after building the tree.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_LAYOUT_H
#define LIGHTUI_LAYOUT_H

#include <lightvg/types.h>
#include <lightvg/canvas.h>
#include <lightvg/dirty.h>
#include "event.h"
#include "frame_clock.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opt-in per-widget pixel cache (see lightui/widget_cache.h). Forward-declared
 * here so the widget struct can hold a pointer without a circular include. */
typedef struct lui_widget_cache lui_widget_cache_t;

/* ---- Size specification ------------------------------------------------- */

typedef enum {
    LUI_SIZE_FIXED  = 0,  /* exact pixel value                             */
    LUI_SIZE_HUG    = 1,  /* shrink-wrap to content (+ optional minimum)   */
    LUI_SIZE_FILL   = 2,  /* expand into remaining space (weighted)        */
} lvg_size_mode_t;

typedef struct {
    lvg_size_mode_t mode;
    int             value;  /* pixels (FIXED), min pixels (HUG), weight (FILL) */
} lvg_size_spec_t;

/* Convenience constructors */
static inline lvg_size_spec_t lvg_size_fixed(int px) {
    lvg_size_spec_t s; s.mode = LUI_SIZE_FIXED; s.value = px; return s;
}
static inline lvg_size_spec_t lvg_size_hug(int min_px) {
    lvg_size_spec_t s; s.mode = LUI_SIZE_HUG; s.value = min_px; return s;
}
static inline lvg_size_spec_t lvg_size_fill(int weight) {
    lvg_size_spec_t s; s.mode = LUI_SIZE_FILL; s.value = weight > 0 ? weight : 1; return s;
}

/* ---- Layout direction & alignment --------------------------------------- */

typedef enum {
    LUI_LAYOUT_ROW    = 0,  /* children flow left-to-right  */
    LUI_LAYOUT_COLUMN = 1,  /* children flow top-to-bottom  */
    LUI_LAYOUT_STACK  = 2,  /* children overlap (z-stack)   */
} lui_layout_dir_t;

typedef enum {
    LUI_ALIGN_START   = 0,
    LUI_ALIGN_CENTER  = 1,
    LUI_ALIGN_END     = 2,
    LUI_ALIGN_STRETCH = 3,
} lui_align_t;

typedef enum {
    LUI_JUSTIFY_START   = 0,
    LUI_JUSTIFY_CENTER  = 1,
    LUI_JUSTIFY_END     = 2,
    LUI_JUSTIFY_BETWEEN = 3,  /* space-between                   */
    LUI_JUSTIFY_EVENLY  = 4,  /* space-evenly                    */
} lui_justify_t;

/* ---- Edges (padding / margin) ------------------------------------------- */

typedef struct {
    int top, right, bottom, left;
} lui_edges_t;

static inline lui_edges_t lui_edges_uniform(int v) {
    lui_edges_t e; e.top = v; e.right = v; e.bottom = v; e.left = v; return e;
}
static inline lui_edges_t lui_edges_xy(int x, int y) {
    lui_edges_t e; e.top = y; e.right = x; e.bottom = y; e.left = x; return e;
}
static inline lui_edges_t lui_edges(int top, int right, int bottom, int left) {
    lui_edges_t e; e.top = top; e.right = right; e.bottom = bottom; e.left = left; return e;
}

/* ---- Widget ------------------------------------------------------------- */

#ifndef LUI_WIDGET_T_DEFINED
#define LUI_WIDGET_T_DEFINED
typedef struct lui_widget lui_widget_t;
#endif

/**
 * Optional callback to supply an intrinsic (content) size for a leaf widget.
 *
 * The framework calls this during the measure pass for widgets with
 * LUI_SIZE_HUG on one or both axes.  The callback writes the desired
 * content size into *out_w and *out_h.  The values are used as the
 * widget's natural size before padding is added.
 *
 * Return value is unused (reserved); always return 0.
 */
typedef int (*lui_measure_fn)(const lui_widget_t *widget,
                              int *out_w, int *out_h,
                              void *user);

/**
 * Draw callback.  Invoked by lui_widget_draw_tree().
 * The widget should draw itself into @canvas at its absolute rect.
 * Call lui_widget_absolute_rect(widget) to get the position.
 */
typedef void (*lui_widget_draw_fn)(lui_widget_t *widget, lvg_canvas_t *canvas);

/**
 * Event callback.  Invoked by lui_widget_dispatch_event().
 * Return non-zero if the event was consumed (stops bubbling).
 */
typedef int (*lui_widget_event_fn)(lui_widget_t *widget, const lui_event_t *event);

/**
 * Animation callback.  Invoked by lui_widget_animate_tree() each frame.
 * @dt  Delta time in seconds since the last frame.
 * Return true if the widget's visual state changed (needs redraw).
 */
typedef bool (*lui_widget_animate_fn)(lui_widget_t *widget, float dt);

struct lui_widget {
    /* ---- Tree pointers (set by lui_widget_add_child) ---- */
    lui_widget_t *parent;
    lui_widget_t *first_child;
    lui_widget_t *next_sibling;

    /* ---- Layout input (set by the application) ---- */
    lui_layout_dir_t direction;      /* child flow direction           */
    lvg_size_spec_t  width;          /* width specification            */
    lvg_size_spec_t  height;         /* height specification           */
    lui_align_t      align_items;    /* cross-axis alignment of children */
    lui_align_t      align_self;     /* override parent's align_items  */
    lui_justify_t    justify;        /* main-axis distribution         */
    lui_edges_t      padding;        /* inner padding                  */
    lui_edges_t      margin;         /* outer margin                   */
    int              spacing;        /* gap between children (main axis) */
    int              min_width;      /* floor on computed width        */
    int              min_height;     /* floor on computed height       */
    int              max_width;      /* ceiling on computed width (0=none) */
    int              max_height;     /* ceiling on computed height (0=none) */

    /* ---- Callbacks (optional) ---- */
    lui_measure_fn   measure;
    void            *measure_user;
    lui_widget_draw_fn    draw;       /* self-draw callback              */
    lui_widget_event_fn   on_event;   /* event handler callback          */
    lui_widget_animate_fn animate;    /* per-frame animation callback    */

    /* ---- Layout output (written by lui_layout_compute) ---- */
    lvg_rect_t       computed;       /* bounds in parent's content area */
    int              _desired_w;     /* internal: measured desired width  */
    int              _desired_h;     /* internal: measured desired height */

    /* ---- Application data ---- */
    void            *user_data;
    int              id;             /* application-assigned id         */
    unsigned         flags;          /* LUI_WIDGET_* bitmask            */

    /* ---- Dirty-rect rendering / caching ---- */
    lui_widget_cache_t *cache;       /* NULL unless caching is enabled  */
};

/* Widget flags */
#define LUI_WIDGET_DRAWS_CHILDREN  (1u << 0)  /* draw callback handles children */
#define LUI_WIDGET_FOCUSABLE       (1u << 1)  /* can receive keyboard focus     */
#define LUI_WIDGET_ANIMATING       (1u << 2)  /* widget has active animation    */
#define LUI_WIDGET_DIRTY           (1u << 3)  /* needs redraw this frame        */
#define LUI_WIDGET_CACHE           (1u << 4)  /* opt-in pixel cache enabled     */
#define LUI_WIDGET_CACHE_VALID     (1u << 5)  /* cache surface holds current px */

/* ---- API ---------------------------------------------------------------- */

/**
 * Initialise a widget to safe defaults.
 * Direction=COLUMN, size=HUG(0), align=START, no padding/margin/spacing.
 */
void lui_widget_init(lui_widget_t *w);

/**
 * Append @child as the last child of @parent.
 * A widget can only have one parent; re-parenting detaches from the old parent.
 */
void lui_widget_add_child(lui_widget_t *parent, lui_widget_t *child);

/**
 * Remove @child from its parent's child list.
 * Does nothing if @child has no parent.
 */
void lui_widget_remove(lui_widget_t *child);

/** Count direct children. */
int lui_widget_child_count(const lui_widget_t *w);

/** Get Nth direct child (0-based), or NULL if out of range. */
lui_widget_t *lui_widget_child_at(const lui_widget_t *w, int index);

/**
 * Compute layout for the widget tree rooted at @root.
 *
 * @root's computed rect origin is set to (0, 0); all descendant rects
 * are relative to their parent's content area (padding inset).
 *
 * @avail_w / @avail_h  Available size for the root widget.  Used to
 *   resolve FILL specs on the root itself.  For HUG/FIXED roots these
 *   values act as upper bounds.
 */
void lui_layout_compute(lui_widget_t *root, int avail_w, int avail_h);

/**
 * Convert a widget's computed rect from parent-relative to absolute
 * (root-relative) coordinates by walking up the tree.
 */
lvg_rect_t lui_widget_absolute_rect(const lui_widget_t *w);

/* ---- Drawing ------------------------------------------------------------ */

/**
 * Draw the entire widget tree rooted at @root.
 *
 * Walks the tree depth-first (parent before children = painter's order).
 * For each widget with a non-NULL draw callback, the callback is invoked
 * with the canvas.
 */
void lui_widget_draw_tree(lui_widget_t *root, lvg_canvas_t *canvas);

/**
 * Draw stats returned by timed drawing/animation functions.
 */
typedef struct {
    int  drawn;        /* widgets whose draw callback was invoked            */
    int  skipped;      /* widgets skipped (clean, off-screen, or timed out)  */
    int  cache_hits;   /* cached widgets replayed from their cache surface   */
    int  cache_misses; /* cached widgets (re)rendered into their cache       */
    bool timed_out;    /* true if the frame clock expired during traversal   */
} lui_draw_stats_t;

/**
 * Draw the widget tree with a frame-clock deadline.
 *
 * Same depth-first traversal as lui_widget_draw_tree(), but checks
 * @clk before each widget's draw callback.  If the deadline has expired,
 * remaining widgets are skipped and stats->timed_out is set.
 *
 * @clk    Frame clock (may be NULL to disable timeout — same as draw_tree).
 * @stats  Output stats (may be NULL if not needed).
 */
void lui_widget_draw_tree_timed(lui_widget_t *root, lvg_canvas_t *canvas,
                                  lui_frame_clock_t *clk,
                                  lui_draw_stats_t *stats);

/* ---- Dirty-rect rendering ----------------------------------------------- */

/**
 * Mark @w as needing a redraw on the next frame.  Also invalidates @w's pixel
 * cache (if any) so it is rebuilt.  Cheap: just sets flag bits.  Call this from
 * the application whenever a widget's visual state changes (value, text, hover,
 * focus, …).  No ancestor propagation — lui_widget_collect_dirty() rediscovers
 * the dirty set by walking the tree each frame.
 */
void lui_widget_invalidate(lui_widget_t *w);

/** True if @w is flagged dirty (LUI_WIDGET_DIRTY). */
bool lui_widget_is_dirty(const lui_widget_t *w);

/**
 * Walk the tree rooted at @root and add the absolute (surface-space) bounds of
 * every dirty widget into @out.  @out is NOT reset first — call
 * lvg_dirty_reset(out) (or lvg_dirty_mark_all(out)) beforehand.  Stops
 * descending into subtrees whose parent has LUI_WIDGET_DRAWS_CHILDREN, matching
 * the draw traversal.
 */
void lui_widget_collect_dirty(lui_widget_t *root, lvg_dirty_t *out);

/**
 * Draw only the widgets that intersect @dirty, clipping each draw to the dirty
 * region.  Clean / off-screen widgets are skipped (counted in stats->skipped).
 * Widgets with an enabled cache (LUI_WIDGET_CACHE) are replayed from their
 * cache surface when clean, or re-rendered and re-captured when dirty.
 *
 * The caller is responsible for clearing the dirty regions of the surface
 * first. Clear each rect in @dirty, not only lvg_dirty_bounds(), otherwise
 * clean pixels between distant dirty rects can be erased without being redrawn.
 *
 * @dirty  Dirty set in absolute surface coords (must be non-NULL).
 * @stats  Output stats (may be NULL).
 */
void lui_widget_draw_tree_dirty(lui_widget_t *root, lvg_canvas_t *canvas,
                                const lvg_dirty_t *dirty,
                                lui_draw_stats_t *stats);

/* ---- Animation ---------------------------------------------------------- */

/**
 * Tick all animations in the widget tree.
 *
 * Walks the tree depth-first and calls the animate callback on every
 * widget that has LUI_WIDGET_ANIMATING set and a non-NULL animate callback.
 *
 * @dt  Delta time in seconds since the last frame.
 * @return  Number of widgets that reported a visual change.
 */
int lui_widget_animate_tree(lui_widget_t *root, float dt);

/**
 * Returns true if any widget in the tree has the ANIMATING flag set.
 * Use this to decide whether to keep the render loop running vs. sleeping.
 */
bool lui_widget_has_animations(const lui_widget_t *root);

/* ---- Event dispatch ----------------------------------------------------- */

/**
 * Find the deepest (front-most) widget whose absolute rect contains (x, y).
 * Returns NULL if no widget contains the point.
 */
lui_widget_t *lui_widget_hit_test(lui_widget_t *root, int x, int y);

/**
 * Dispatch an event through the widget tree.
 *
 * For pointer events (mouse move/down/up/scroll): performs a hit test,
 * then calls on_event on the hit widget and bubbles up to the root until
 * consumed.
 *
 * For other events: walks from @root down and invokes on_event on each
 * widget until consumed.
 *
 * Returns the widget that consumed the event, or NULL.
 */
lui_widget_t *lui_widget_dispatch_event(lui_widget_t *root,
                                         const lui_event_t *event);

/* ---- UI context (focus-aware dispatch) ---------------------------------- */

/**
 * UI context: tracks focus state for keyboard event routing.
 *
 * Usage:
 *   lui_ui_ctx_t ui;
 *   lui_ui_ctx_init(&ui, &root);
 *   // in event loop:
 *   lui_ui_ctx_dispatch(&ui, &event);
 */
typedef struct {
    lui_widget_t *root;
    lui_widget_t *focus;    /* currently focused widget (or NULL)  */
} lui_ui_ctx_t;

void lui_ui_ctx_init(lui_ui_ctx_t *ctx, lui_widget_t *root);

/**
 * Dispatch an event with focus awareness.
 *
 * Pointer events: hit-tested and bubbled as before.  If a mouse-down
 * lands on a FOCUSABLE widget, it receives focus.
 *
 * Keyboard / text events: routed to the focused widget (bubbled up).
 *
 * Returns the widget that consumed the event, or NULL.
 */
lui_widget_t *lui_ui_ctx_dispatch(lui_ui_ctx_t *ctx, const lui_event_t *event);

/** Explicitly set focus to @widget (may be NULL to clear). */
void lui_ui_ctx_set_focus(lui_ui_ctx_t *ctx, lui_widget_t *widget);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_LAYOUT_H */
