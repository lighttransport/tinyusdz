/*
 * lightui/filebrowser.h — Virtual file/directory browser widget
 *
 * Displays a list of file and directory entries provided by the application.
 * No actual filesystem access; the app populates entries via the API.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_FILEBROWSER_H
#define LIGHTUI_FILEBROWSER_H

#include "alloc.h"
#include "layout.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default capacity used by lui_filebrowser_init().
 * Use lui_filebrowser_init_ex() to pick a different size. */
#define LUI_FILEBROWSER_MAX  128

/* ---- Entry types -------------------------------------------------------- */

typedef enum {
    LUI_ENTRY_FILE      = 0,
    LUI_ENTRY_DIRECTORY = 1,
} lui_entry_type_t;

/* ---- Entry -------------------------------------------------------------- */

typedef struct {
    char             name[64];
    lui_entry_type_t type;
    uint64_t         size;
    bool             selected;
} lui_fb_entry_t;

/* ---- Actions ------------------------------------------------------------ */

#define LUI_FB_SELECT  0
#define LUI_FB_OPEN    1

typedef void (*lui_filebrowser_action_fn)(int index, int action, void *user);

/* ---- Sort mode ---------------------------------------------------------- */

typedef enum {
    LUI_FB_SORT_NAME = 0,   /* alphabetical by name (default)  */
    LUI_FB_SORT_TYPE = 1,   /* directories first, then by name */
} lui_fb_sort_t;

/* ---- File browser widget ------------------------------------------------ */

typedef struct {
    lui_widget_t  widget;

    /* Entries — heap-allocated, capacity = max_entries. */
    lui_fb_entry_t *entries;
    int             entry_count;
    int             max_entries;
    char            current_path[256];

    /* Allocator paired with `entries` — used by destroy. */
    lui_alloc_fn    alloc_fn;
    lui_free_fn     free_fn;
    void           *alloc_user;

    /* Layout */
    int            item_height;      /* height of each row (24)           */
    int            scroll_offset;    /* first visible item index          */
    int            hovered;          /* hovered item index (-1 = none)    */

    /* Double-click tracking */
    int            last_click_index; /* index of last clicked item        */
    float          click_time;       /* time accumulator for double-click */

    /* Sort */
    lui_fb_sort_t  sort_mode;

    /* Colors */
    lvg_color_t    bg_color;
    lvg_color_t    item_bg;
    lvg_color_t    item_hover_bg;
    lvg_color_t    item_selected_bg;
    lvg_color_t    text_color;
    lvg_color_t    dir_color;
    lvg_color_t    border_color;
    lvg_color_t    path_bg;

    /* Callback */
    lui_filebrowser_action_fn on_action;
    void                     *on_action_user;
} lui_filebrowser_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise with default capacity using malloc/free. Pair with destroy. */
bool lui_filebrowser_init(lui_filebrowser_t *fb);

/** Initialise with caller-supplied capacity / allocator (NULL/NULL = default). */
bool lui_filebrowser_init_ex(lui_filebrowser_t *fb, int max_entries,
                              lui_alloc_fn alloc_fn,
                              lui_free_fn  free_fn,
                              void        *alloc_user);

/** Free heap arrays owned by `fb`. */
void lui_filebrowser_destroy(lui_filebrowser_t *fb);

/** Set the current displayed path. */
void lui_filebrowser_set_path(lui_filebrowser_t *fb, const char *path);

/**
 * Add an entry.  Returns the entry index or -1 on failure.
 * The name is copied internally (max 63 bytes).
 */
int lui_filebrowser_add_entry(lui_filebrowser_t *fb, const char *name,
                               lui_entry_type_t type, uint64_t size);

/** Remove all entries. */
void lui_filebrowser_clear(lui_filebrowser_t *fb);

/** Get the index of the first selected entry, or -1 if none. */
int lui_filebrowser_get_selected(const lui_filebrowser_t *fb);

/** Sort entries according to the current sort mode. */
void lui_filebrowser_sort(lui_filebrowser_t *fb);

/** Get the widget node. */
static inline lui_widget_t *lui_filebrowser_widget(lui_filebrowser_t *fb) {
    return &fb->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_FILEBROWSER_H */
