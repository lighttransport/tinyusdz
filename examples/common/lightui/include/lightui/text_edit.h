/*
 * lightui/text_edit.h — Multi-line interactive text editor
 *
 * Gap buffer for O(1) insert/delete, backed by lui_text_layout_t for
 * rendering.  Supports cursor movement, selection, hit-testing, and
 * per-glyph animation callbacks.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_TEXT_EDIT_H
#define LIGHTUI_TEXT_EDIT_H

#include <lightvg/canvas.h>
#include "font.h"
#include "text_layout.h"
#include <lightvg/types.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Animation callback ------------------------------------------------- */

/**
 * Per-glyph draw callback for animated text effects.
 *
 * Called before each glyph is drawn.  The callback may modify *x, *y to
 * offset the glyph position, and *color to change its colour.
 *
 * @glyph_index  Index of the glyph within the current text.
 * @x, @y        Pointer to the glyph's pen position (modifiable).
 * @color        Pointer to the glyph colour (modifiable).
 * @time_s       Current animation time in seconds (from frame clock or user).
 * @user         User data pointer.
 */
typedef void (*lui_text_anim_fn)(int glyph_index,
                                  int *x, int *y,
                                  lvg_color_t *color,
                                  double time_s,
                                  void *user);

/* ---- Text editor -------------------------------------------------------- */

#define LUI_TEXT_EDIT_UNDO_MAX 200

typedef struct {
    /* Gap buffer: [0..gap_start) + [gap_end..buf_cap) = text content */
    char   *buf;
    int     buf_cap;
    int     gap_start;     /* byte offset of gap start (= cursor position) */
    int     gap_end;       /* byte offset of gap end */

    /* Cursor and selection (byte offsets into logical text) */
    int     cursor;        /* byte offset in logical text */
    int     sel_start;     /* selection start (byte offset); -1 if no selection */
    int     sel_end;       /* selection end (byte offset) */

    /* Layout engine */
    lui_text_layout_t layout;
    char             *flat_buf;  /* flattened text for layout (heap) */
    int               flat_len;
    int               flat_cap;

    /* Scrolling */
    int           scroll_x;        /* horizontal scroll offset in pixels */
    int           scroll_y;        /* vertical scroll offset in pixels */
    int           viewport_h;      /* cached viewport height (set by caller) */

    /* Font and styling */
    lui_font_t   *font;
    lvg_color_t   text_color;
    lvg_color_t   sel_color;      /* selection highlight background */
    lvg_color_t   cursor_color;
    int           cursor_width;

    /* Animation */
    lui_text_anim_fn  anim_fn;
    void             *anim_user;
    double            anim_time;

    /* Cursor blink */
    double        blink_phase;   /* cursor blink phase (0–1), recomputed each draw */
    double        blink_period;  /* blink period in seconds (default 0.53)   */

    /* Undo coalescing — group consecutive edits into one undo entry */
    double        last_action_time;
    int           last_action_type; /* 0=none, 1=insert, 2=delete_back, 3=delete_fwd */
    int           last_cursor_pos;  /* cursor position after last action        */

    /* State */
    int           max_width;     /* wrap width; 0 = no wrap */
    bool          needs_layout;  /* true when gap buffer changed */

    /* Undo / redo stack (snapshot-based) */
    struct {
        char  *text;
        int    len;
        int    cursor;
        int    sel_start, sel_end;
    } undo_stack[LUI_TEXT_EDIT_UNDO_MAX];
    int           undo_len;
    struct {
        char  *text;
        int    len;
        int    cursor;
        int    sel_start, sel_end;
    } redo_stack[LUI_TEXT_EDIT_UNDO_MAX];
    int           redo_len;
} lui_text_edit_t;

/* ---- Lifecycle ---------------------------------------------------------- */

/**
 * Initialise a text editor.
 * @font       Font for rendering.
 * @max_width  Wrap width in pixels (0 = no wrapping).
 */
void lui_text_edit_init(lui_text_edit_t *te, lui_font_t *font, int max_width);

/** Destroy and free all resources. */
void lui_text_edit_destroy(lui_text_edit_t *te);

/* ---- Content ------------------------------------------------------------ */

/** Set the entire text content (replaces existing). */
void lui_text_edit_set_text(lui_text_edit_t *te, const char *utf8, int len);

/** Get the current text length in bytes (excluding gap). */
int lui_text_edit_text_len(const lui_text_edit_t *te);

/**
 * Copy the current text into @out (must have room for at least
 * lui_text_edit_text_len()+1 bytes).  NUL-terminates the output.
 */
void lui_text_edit_get_text(const lui_text_edit_t *te, char *out, int out_cap);

/* ---- Editing ------------------------------------------------------------ */

/** Insert UTF-8 text at the cursor position. */
void lui_text_edit_insert(lui_text_edit_t *te, const char *utf8, int len);

/** Delete @count bytes before the cursor (backspace). */
void lui_text_edit_delete_back(lui_text_edit_t *te, int count);

/** Delete @count bytes after the cursor (forward delete). */
void lui_text_edit_delete_forward(lui_text_edit_t *te, int count);

/** Delete the current selection (no-op if no selection). */
void lui_text_edit_delete_selection(lui_text_edit_t *te);

/* ---- Cursor movement ---------------------------------------------------- */

/** Move cursor left by one codepoint. */
void lui_text_edit_cursor_left(lui_text_edit_t *te);

/** Move cursor right by one codepoint. */
void lui_text_edit_cursor_right(lui_text_edit_t *te);

/** Move cursor to the beginning of the current line. */
void lui_text_edit_cursor_home(lui_text_edit_t *te);

/** Move cursor to the end of the current line. */
void lui_text_edit_cursor_end(lui_text_edit_t *te);

/** Move cursor up one line (approximate: nearest x position). */
void lui_text_edit_cursor_up(lui_text_edit_t *te);

/** Move cursor down one line (approximate: nearest x position). */
void lui_text_edit_cursor_down(lui_text_edit_t *te);

/* Cursor movement with selection extension (Shift+arrow). */
void lui_text_edit_cursor_left_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_right_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_home_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_end_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_up_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_down_extend(lui_text_edit_t *te);

/** Move cursor up/down by one page. */
void lui_text_edit_cursor_page_up(lui_text_edit_t *te);
void lui_text_edit_cursor_page_down(lui_text_edit_t *te);

/** Move cursor left/right by one word. */
void lui_text_edit_cursor_word_left(lui_text_edit_t *te);
void lui_text_edit_cursor_word_right(lui_text_edit_t *te);
void lui_text_edit_cursor_word_left_extend(lui_text_edit_t *te);
void lui_text_edit_cursor_word_right_extend(lui_text_edit_t *te);

/* ---- Selection ---------------------------------------------------------- */

/** Set selection range (byte offsets in logical text). */
void lui_text_edit_set_selection(lui_text_edit_t *te, int start, int end);

/** Clear the selection. */
void lui_text_edit_clear_selection(lui_text_edit_t *te);

/** Returns true if there is an active selection. */
bool lui_text_edit_has_selection(const lui_text_edit_t *te);

/** Select the entire text content. */
void lui_text_edit_select_all(lui_text_edit_t *te);

/* ---- Clipboard --------------------------------------------------------- */

/** Copy the current selection to the shared clipboard. */
bool lui_text_edit_copy(lui_text_edit_t *te);

/**
 * Cut the current selection to the shared clipboard
 * (copies then deletes the selection).
 */
bool lui_text_edit_cut(lui_text_edit_t *te);

/** Paste from the shared clipboard at the cursor, replacing selection. */
bool lui_text_edit_paste(lui_text_edit_t *te);

/* ---- Undo / Redo ------------------------------------------------------- */

/** Undo the last text-modifying action. */
void lui_text_edit_undo(lui_text_edit_t *te);

/** Redo the last undone action. */
void lui_text_edit_redo(lui_text_edit_t *te);

/* ---- Hit testing -------------------------------------------------------- */

/**
 * Convert a pixel position (relative to layout origin) to a byte offset
 * in the text.  Returns the byte offset, or -1 on failure.
 */
int lui_text_edit_hit_test(lui_text_edit_t *te, int px, int py);

/* ---- Layout & draw ------------------------------------------------------ */

/**
 * Rebuild the layout if needed.  Call before drawing or hit-testing
 * if the text has been modified.
 */
void lui_text_edit_build(lui_text_edit_t *te);

/**
 * Draw the text editor contents onto @canvas at (@x, @y).
 * Draws selection highlight, text, and cursor.
 *
 * @time_s  Animation time for per-glyph callbacks (0 to disable).
 * @view_h  Viewport height in pixels (used for scrollbar and clamping).
 */
void lui_text_edit_draw(lui_text_edit_t *te,
                         lvg_canvas_t *canvas,
                         int x, int y,
                         double time_s);

/** Scroll to make the cursor visible.  Call after cursor movement. */
void lui_text_edit_scroll_to_cursor(lui_text_edit_t *te, int view_h);

/** Ensure a y-range [y0..y1] is visible in a viewport of @view_h pixels. */
void lui_text_edit_scroll_visible(lui_text_edit_t *te, int y0, int y1, int view_h);

/* ---- Scrolling convenience ---------------------------------------------- */

/** Get the total content height in pixels. */
int lui_text_edit_content_height(const lui_text_edit_t *te);

/* ---- Animation ---------------------------------------------------------- */

/** Set a per-glyph animation callback. */
void lui_text_edit_set_animation(lui_text_edit_t *te,
                                  lui_text_anim_fn fn, void *user);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_TEXT_EDIT_H */
