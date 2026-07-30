/*
 * lightui/treeview.h — Tree view widget
 *
 * A hierarchical list with expandable/collapsible nodes.
 * Each node has a label, optional icon color indicator,
 * and can contain child nodes.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TREEVIEW_H
#define LIGHTUI_TREEVIEW_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default node-pool capacity used by lui_treeview_init().
 * Use lui_treeview_init_ex() to pick a different size. */
#define LUI_TV_MAX_NODES        512
#define LUI_TV_MAX_LABEL_LEN     63  /* excl. NUL */

/* ---- Tree node ---------------------------------------------------------- */

typedef struct {
    int           id;              /* unique node ID                       */
    int           parent_id;       /* parent node ID (-1 = root level)     */
    char          label[LUI_TV_MAX_LABEL_LEN + 1];
    lvg_color_t   icon_color;      /* icon/indicator color (0 = none)      */
    bool          expanded;        /* true if children are visible          */
    bool          selected;        /* true if this node is selected         */
    bool          leaf;            /* true if cannot have children          */
} lui_tv_node_t;

/* ---- Events ------------------------------------------------------------- */

typedef enum {
    LUI_TV_EVENT_NONE = 0,
    LUI_TV_EVENT_SELECTED,         /* node selection changed               */
    LUI_TV_EVENT_EXPANDED,         /* node expanded                        */
    LUI_TV_EVENT_COLLAPSED,        /* node collapsed                       */
    LUI_TV_EVENT_DOUBLE_CLICK,     /* double-click on a node               */
} lui_tv_event_type_t;

typedef struct {
    lui_tv_event_type_t type;
    int                  node_id;
} lui_tv_event_t;

typedef void (*lui_tv_event_fn)(const lui_tv_event_t *event, void *user);

/* ---- Tree view widget --------------------------------------------------- */

typedef struct {
    lui_widget_t   widget;

    /* Nodes — flat heap array (tree structure via parent_id), capacity = max_nodes. */
    lui_tv_node_t *nodes;
    int            node_count;
    int            max_nodes;
    int            next_id;

    /* Allocator paired with `nodes` — used by destroy. */
    lui_alloc_fn   alloc_fn;
    lui_free_fn    free_fn;
    void          *alloc_user;

    /* Interaction state */
    int            selected_id;     /* currently selected node (-1 = none) */
    int            hovered_id;      /* currently hovered node (-1 = none)  */
    int            scroll_y;        /* vertical scroll offset in pixels    */
    int            content_height;  /* total computed content height        */

    /* Appearance */
    int            row_height;      /* height of each row (22)             */
    int            indent;          /* indentation per depth level (18)    */
    int            icon_size;       /* icon indicator size (10)            */
    int            arrow_size;      /* expand/collapse arrow size (8)      */
    int            corner_radius;   /* selection highlight radius (3)      */

    /* Colors */
    lvg_color_t    bg;              /* background color                    */
    lvg_color_t    text_color;      /* label text color                    */
    lvg_color_t    text_selected;   /* selected label color                */
    lvg_color_t    hover_bg;        /* hovered row background              */
    lvg_color_t    selected_bg;     /* selected row background             */
    lvg_color_t    arrow_color;     /* expand/collapse arrow color         */
    lvg_color_t    guide_color;     /* indentation guide line color        */
    lvg_color_t    scrollbar_color; /* scrollbar thumb color               */
    int            scrollbar_width; /* scrollbar width (6)                 */

    /* Callback */
    lui_tv_event_fn on_event;
    void           *on_event_user;
} lui_treeview_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise with default capacity using malloc/free. Pair with destroy. */
bool lui_treeview_init(lui_treeview_t *tv);

/** Initialise with caller-supplied capacity / allocator (NULL/NULL = default). */
bool lui_treeview_init_ex(lui_treeview_t *tv, int max_nodes,
                           lui_alloc_fn alloc_fn,
                           lui_free_fn  free_fn,
                           void        *alloc_user);

/** Free heap arrays owned by `tv`. */
void lui_treeview_destroy(lui_treeview_t *tv);

/**
 * Add a node to the tree.  Returns the node ID (> 0) or -1 on failure.
 * @parent_id  Parent node ID, or -1 for root level.
 * @label      Node label (copied internally, max LUI_TV_MAX_LABEL_LEN bytes).
 * @leaf       If true, node cannot have children (no expand arrow).
 */
int lui_treeview_add_node(lui_treeview_t *tv, int parent_id,
                           const char *label, bool leaf);

/** Remove a node and all its descendants. */
void lui_treeview_remove_node(lui_treeview_t *tv, int node_id);

/** Get a node by ID. Returns NULL if not found. */
lui_tv_node_t *lui_treeview_get_node(lui_treeview_t *tv, int node_id);

/** Set the selected node by ID (-1 to deselect). */
void lui_treeview_select(lui_treeview_t *tv, int node_id);

/** Expand a node (show its children). */
void lui_treeview_expand(lui_treeview_t *tv, int node_id);

/** Collapse a node (hide its children). */
void lui_treeview_collapse(lui_treeview_t *tv, int node_id);

/** Expand all nodes in the tree. */
void lui_treeview_expand_all(lui_treeview_t *tv);

/** Collapse all nodes in the tree. */
void lui_treeview_collapse_all(lui_treeview_t *tv);

/** Get the number of children of a node (direct children only). */
int lui_treeview_child_count(const lui_treeview_t *tv, int node_id);

/** Get the depth of a node (root level = 0). */
int lui_treeview_depth(const lui_treeview_t *tv, int node_id);

/** Get the widget node. */
static inline lui_widget_t *lui_treeview_widget(lui_treeview_t *tv) {
    return &tv->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TREEVIEW_H */
