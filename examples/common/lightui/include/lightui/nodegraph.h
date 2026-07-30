/*
 * lightui/nodegraph.h — Node graph editor widget
 *
 * A visual node-based graph editor for shader networks, compositing trees,
 * procedural pipelines, and similar directed acyclic graph structures.
 *
 * Features:
 *   - Nodes with typed input/output pins (sockets)
 *   - Bezier curve links connecting pins
 *   - Pan and zoom with mouse wheel / middle-drag
 *   - Node dragging (single and multi-select)
 *   - Link creation by dragging from an output pin to an input pin
 *   - Built-in layered auto-layout (Sugiyama-style)
 *   - User-overridable layout callback for custom algorithms
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_NODEGRAPH_H
#define LIGHTUI_NODEGRAPH_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

#define LUI_NG_MAX_NODES       128
#define LUI_NG_MAX_PINS          8  /* per node, per direction */
#define LUI_NG_MAX_LINKS       256

/* ---- Pin (socket) ------------------------------------------------------- */

typedef enum {
    LUI_PIN_INPUT  = 0,
    LUI_PIN_OUTPUT = 1,
} lui_pin_dir_t;

typedef struct {
    int          id;          /* unique pin ID (globally unique)      */
    lui_pin_dir_t dir;        /* input or output                      */
    const char   *label;      /* display label (not owned)            */
    lvg_color_t   color;      /* pin circle color                     */
} lui_ng_pin_t;

/* ---- Node --------------------------------------------------------------- */

typedef struct {
    int            id;         /* unique node ID                       */
    const char    *title;      /* node title (not owned)               */
    float          x, y;       /* position in graph space              */
    int            width;      /* node width (default 140)             */
    lvg_color_t    header_color; /* header bar color                   */
    lvg_color_t    body_color;   /* body background color              */
    bool           selected;   /* selection state                      */
    bool           collapsed;  /* collapsed (show only header)         */

    /* Pins */
    lui_ng_pin_t   inputs[LUI_NG_MAX_PINS];
    int            input_count;
    lui_ng_pin_t   outputs[LUI_NG_MAX_PINS];
    int            output_count;

    /* Physics state */
    float          vx, vy;     /* velocity in graph-space units/sec    */
    bool           pinned;     /* if true, physics won't move this node*/
} lui_ng_node_t;

/* ---- Link (connection) -------------------------------------------------- */

typedef struct {
    int  id;          /* unique link ID                               */
    int  src_node;    /* source node ID                               */
    int  src_pin;     /* source pin ID (output)                       */
    int  dst_node;    /* destination node ID                          */
    int  dst_pin;     /* destination pin ID (input)                   */
    lvg_color_t color; /* link color (0 = auto from pin color)        */
} lui_ng_link_t;

/* ---- Events ------------------------------------------------------------- */

typedef enum {
    LUI_NG_EVENT_NONE = 0,
    LUI_NG_EVENT_NODE_SELECTED,     /* node selection changed              */
    LUI_NG_EVENT_NODE_MOVED,        /* node dragged to new position        */
    LUI_NG_EVENT_NODE_DOUBLE_CLICK, /* double-click on a node              */
    LUI_NG_EVENT_LINK_CREATED,      /* new link connected                  */
    LUI_NG_EVENT_LINK_DELETED,      /* link disconnected                   */
    LUI_NG_EVENT_VIEW_CHANGED,      /* pan or zoom changed                 */
    LUI_NG_EVENT_NODE_THROWN,       /* node flung off canvas (removed)     */
    LUI_NG_EVENT_LINK_SHAKEN_OFF,  /* link disconnected by shaking node   */
} lui_ng_event_type_t;

typedef struct {
    lui_ng_event_type_t type;
    int node_id;    /* relevant node, or -1 */
    int link_id;    /* relevant link, or -1 */
    int pin_id;     /* relevant pin, or -1  */
} lui_ng_event_t;

typedef void (*lui_ng_event_fn)(const lui_ng_event_t *event, void *user);

/* ---- Layout callback ---------------------------------------------------- */

/**
 * User-defined layout callback.
 *
 * Called by lui_nodegraph_auto_layout() when set (instead of the built-in
 * algorithm).  The callback receives the full graph state and must write
 * final (x, y) positions into each node.
 *
 * @param nodes       Array of all nodes (writable — set x, y).
 * @param node_count  Number of nodes.
 * @param links       Array of all links (read-only).
 * @param link_count  Number of links.
 * @param user        User pointer from lui_nodegraph_t.layout_user.
 *
 * The callback can use lui_ng_node_height() to query a node's visual height.
 */
typedef void (*lui_ng_layout_fn)(lui_ng_node_t *nodes, int node_count,
                                  const lui_ng_link_t *links, int link_count,
                                  void *user);

/* ---- Node graph widget -------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    /* Nodes and links */
    lui_ng_node_t  nodes[LUI_NG_MAX_NODES];
    int            node_count;
    lui_ng_link_t  links[LUI_NG_MAX_LINKS];
    int            link_count;
    int            next_id;   /* monotonic ID generator                */

    /* View (camera) */
    float view_x, view_y;    /* pan offset in graph space              */
    float zoom;               /* zoom level (1.0 = 100%)               */

    /* Interaction state */
    int   drag_mode;          /* 0=none, 1=node, 2=link, 3=pan,
                                 4=box-select                          */
    int   drag_node;          /* node index being dragged              */
    float drag_offset_x;      /* mouse offset from node origin         */
    float drag_offset_y;
    int   link_src_node;      /* in-progress link source node index    */
    int   link_src_pin;       /* in-progress link source pin index     */
    float link_drag_x;        /* cursor position for link preview      */
    float link_drag_y;
    float box_sel_x0, box_sel_y0; /* box select start in graph space   */
    float box_sel_x1, box_sel_y1;

    /* Node geometry constants */
    int   node_header_h;      /* header height (default 24)            */
    int   node_pin_spacing;   /* vertical gap between pins (default 20)*/
    int   node_pin_radius;    /* pin circle radius (default 5)         */
    int   node_padding;       /* internal padding (default 8)          */
    int   node_corner_radius; /* rounded corner radius (default 6)     */
    int   node_default_width; /* default node width (default 140)      */

    /* Grid */
    int   grid_size;          /* background grid spacing (default 20)  */
    bool  grid_snap;          /* snap nodes to grid                    */

    /* Appearance */
    lvg_color_t bg;
    lvg_color_t grid_color;
    lvg_color_t grid_major_color;  /* every 5th grid line              */
    lvg_color_t node_border;
    lvg_color_t node_selected_border;
    lvg_color_t link_default_color;
    lvg_color_t box_select_fill;
    lvg_color_t box_select_border;
    int         link_width;        /* Bezier curve width (default 2)   */
    bool        square_pins;       /* draw pins as squares             */
    bool        orthogonal_links;  /* draw links as right-angled paths */
    bool        vertical_links;    /* use vertical Bezier tangents     */

    /* Callbacks */
    lui_ng_event_fn on_event;
    void           *on_event_user;

    /* Layout callback (NULL = use built-in Sugiyama) */
    lui_ng_layout_fn layout;
    void            *layout_user;

    /* ---- Physics -------------------------------------------------------- */
    bool  physics_enabled;     /* enable force simulation (default true)  */
    float phys_damping;        /* velocity decay per second (default 0.92)*/
    float phys_repulsion;      /* node-node repulsion strength (200.0)    */
    float phys_spring;         /* link spring stiffness (0.3)             */
    float phys_spring_len;     /* ideal link rest length (180.0)          */
    float phys_min_vel;        /* velocity below this is zeroed (0.5)     */

    /* Throw gesture */
    float throw_speed_threshold; /* min speed to throw-remove (800.0)     */
    float throw_remove_dist;     /* graph-space dist before removal (2000)*/

    /* Shake-to-disconnect */
    float shake_threshold;     /* cumulative reversal dist to trigger (60)*/
    float shake_window;        /* time window in seconds (0.4)            */

    /* Drag velocity tracking (internal) */
    #define LUI_NG_VEL_HISTORY 6
    float drag_hist_x[LUI_NG_VEL_HISTORY]; /* graph-space positions      */
    float drag_hist_y[LUI_NG_VEL_HISTORY];
    float drag_hist_t[LUI_NG_VEL_HISTORY]; /* timestamps (seconds)       */
    int   drag_hist_count;
    float drag_time;           /* elapsed drag time (set by caller)       */

    /* Shake detection (internal) */
    float shake_accum;         /* accumulated reversal magnitude          */
    float shake_last_dx;       /* last drag delta x                       */
    float shake_last_dy;       /* last drag delta y                       */
    float shake_timer;         /* time since last shake reset              */
} lui_nodegraph_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a node graph widget. Default: 640x480, zoom=1, centred. */
void lui_nodegraph_init(lui_nodegraph_t *ng);

/**
 * Add a node.  Returns the node ID (> 0) or -1 on failure.
 * The node is placed at (x, y) in graph space.
 */
int lui_nodegraph_add_node(lui_nodegraph_t *ng,
                            const char *title, float x, float y);

/** Remove a node by ID.  Also removes all connected links. */
void lui_nodegraph_remove_node(lui_nodegraph_t *ng, int node_id);

/**
 * Add a pin to a node.  Returns the pin ID (> 0) or -1 on failure.
 * @dir  LUI_PIN_INPUT or LUI_PIN_OUTPUT.
 */
int lui_nodegraph_add_pin(lui_nodegraph_t *ng, int node_id,
                           lui_pin_dir_t dir, const char *label,
                           lvg_color_t color);

/**
 * Connect two pins.  Returns the link ID (> 0) or -1 on failure.
 * @src_pin  Must be an output pin.
 * @dst_pin  Must be an input pin.
 */
int lui_nodegraph_add_link(lui_nodegraph_t *ng,
                            int src_pin, int dst_pin);

/** Remove a link by ID. */
void lui_nodegraph_remove_link(lui_nodegraph_t *ng, int link_id);

/** Get a node by ID.  Returns NULL if not found. */
lui_ng_node_t *lui_nodegraph_get_node(lui_nodegraph_t *ng, int node_id);

/** Get a link by ID.  Returns NULL if not found. */
lui_ng_link_t *lui_nodegraph_get_link(lui_nodegraph_t *ng, int link_id);

/**
 * Compute the visual height of a node (depends on pin count + collapsed).
 */
int lui_ng_node_height(const lui_nodegraph_t *ng, const lui_ng_node_t *node);

/* ---- View control ------------------------------------------------------- */

/** Set the pan position. */
void lui_nodegraph_set_view(lui_nodegraph_t *ng, float x, float y, float zoom);

/** Zoom centred on a graph-space point. factor > 1 zooms in. */
void lui_nodegraph_zoom(lui_nodegraph_t *ng, float factor,
                         float center_x, float center_y);

/** Pan/zoom to fit all nodes in view with some margin. */
void lui_nodegraph_fit_all(lui_nodegraph_t *ng);

/* ---- Coordinate conversion ---------------------------------------------- */

/** Convert screen pixel (sx, sy) to graph space (gx, gy). */
void lui_nodegraph_screen_to_graph(const lui_nodegraph_t *ng,
                                    int sx, int sy,
                                    float *gx, float *gy);

/** Convert graph space to screen pixel. */
void lui_nodegraph_graph_to_screen(const lui_nodegraph_t *ng,
                                    float gx, float gy,
                                    int *sx, int *sy);

/* ---- Auto-layout -------------------------------------------------------- */

/**
 * Automatically lay out all nodes.
 *
 * If ng->layout is set, calls that callback.
 * Otherwise uses the built-in layered (Sugiyama-style) algorithm:
 *   1. Topological sort
 *   2. Layer assignment (longest-path)
 *   3. Crossing minimisation (barycenter heuristic, 4 passes)
 *   4. Horizontal position assignment (average of connected nodes)
 */
void lui_nodegraph_auto_layout(lui_nodegraph_t *ng);

/** Select / deselect a node. */
void lui_nodegraph_select_node(lui_nodegraph_t *ng, int node_id, bool selected);

/** Deselect all nodes. */
void lui_nodegraph_deselect_all(lui_nodegraph_t *ng);

/* ---- Physics ------------------------------------------------------------ */

/**
 * Advance the physics simulation by dt seconds.
 *
 * Call this from your frame loop (e.g. at 60 Hz, dt ≈ 0.016).
 * Applies:
 *   - Node-node overlap repulsion
 *   - Link spring forces (pulls connected nodes to rest length)
 *   - Velocity damping
 *   - Inertial movement (throw gesture continues after release)
 *   - Throw-remove: nodes flung beyond throw_remove_dist are deleted
 *
 * Returns true if any node is still moving (caller should keep animating).
 */
bool lui_nodegraph_step_physics(lui_nodegraph_t *ng, float dt);

/** Apply an impulse (instantaneous velocity change) to a node. */
void lui_nodegraph_impulse(lui_nodegraph_t *ng, int node_id,
                            float dvx, float dvy);

/** Stop all node movement. */
void lui_nodegraph_stop_all(lui_nodegraph_t *ng);

static inline lui_widget_t *lui_nodegraph_widget(lui_nodegraph_t *ng) {
    return &ng->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_NODEGRAPH_H */
