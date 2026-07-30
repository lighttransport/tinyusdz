/*
 * lightui/propgrid.h -- Key-value property grid widget
 *
 * A two-column property editor for floats, ints, bools, colors, and strings.
 * Supports click-drag editing of numeric values and checkbox toggle for bools.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_PROPGRID_H
#define LIGHTUI_PROPGRID_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LUI_PROPGRID_MAX 64

typedef enum {
    LUI_PROP_FLOAT,
    LUI_PROP_INT,
    LUI_PROP_BOOL,
    LUI_PROP_COLOR,
    LUI_PROP_STRING,
    LUI_PROP_SEPARATOR,
} lui_prop_type_t;

typedef struct {
    char            name[48];
    lui_prop_type_t type;
    union {
        float       f;
        int         i;
        bool        b;
        lvg_color_t color;
        char        str[32];
    } value;
    float           min_f;          /* float/int range min              */
    float           max_f;          /* float/int range max              */
    bool            expanded;       /* for group headers                */
} lui_prop_t;

typedef void (*lui_propgrid_change_fn)(int index, void *user);

typedef struct {
    lui_widget_t          widget;
    lui_prop_t            props[LUI_PROPGRID_MAX];
    int                   prop_count;
    int                   row_height;       /* default 24                   */
    int                   name_width;       /* name column width, default 120 */
    int                   scroll_offset;    /* vertical scroll in pixels    */

    /* drag state for numeric editing */
    int                   drag_index;       /* -1 when idle                 */
    int                   drag_start_x;
    float                 drag_start_val;

    /* colours */
    lvg_color_t           bg_color;
    lvg_color_t           name_bg;
    lvg_color_t           value_bg;
    lvg_color_t           text_color;
    lvg_color_t           separator_color;
    lvg_color_t           border_color;
    lvg_color_t           bool_true_color;
    lvg_color_t           bool_false_color;

    lui_propgrid_change_fn on_change;
    void                  *on_change_user;
} lui_propgrid_t;

/** Initialise a property grid with default appearance. */
void lui_propgrid_init(lui_propgrid_t *pg);

/** Add a float property. Returns the property index or -1 on overflow. */
int lui_propgrid_add_float(lui_propgrid_t *pg, const char *name,
                           float value, float min_val, float max_val);

/** Add an integer property. */
int lui_propgrid_add_int(lui_propgrid_t *pg, const char *name,
                         int value, int min_val, int max_val);

/** Add a boolean property. */
int lui_propgrid_add_bool(lui_propgrid_t *pg, const char *name, bool value);

/** Add a colour swatch property. */
int lui_propgrid_add_color(lui_propgrid_t *pg, const char *name,
                           lvg_color_t color);

/** Add a string property. */
int lui_propgrid_add_string(lui_propgrid_t *pg, const char *name,
                            const char *text);

/** Add a separator / group header. */
int lui_propgrid_add_separator(lui_propgrid_t *pg, const char *label);

/** Set a float property value by index (does not trigger callback). */
void lui_propgrid_set_float(lui_propgrid_t *pg, int index, float value);

/** Get a float property value by index. */
float lui_propgrid_get_float(const lui_propgrid_t *pg, int index);

/** Remove all properties. */
void lui_propgrid_clear(lui_propgrid_t *pg);

/** Get the widget node. */
static inline lui_widget_t *lui_propgrid_widget(lui_propgrid_t *pg) {
    return &pg->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_PROPGRID_H */
