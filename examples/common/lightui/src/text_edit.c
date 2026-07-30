/*
 * src/text_edit.c — Multi-line interactive text editor
 *
 * Gap buffer + text layout integration.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <lightui/text_edit.h>
#include "utf8_util.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Internal clipboard (independent from text_input's) */
static char *g_te_clipboard = NULL;
static int   g_te_clipboard_len = 0;

/* Local aliases for the shared UTF-8 helpers */
#define utf8_cp_len(buf, pos, len) lui__utf8_cp_len((buf), (pos), (len))
#define utf8_prev(buf, pos)        lui__utf8_prev((buf), (pos))

/* ---- Gap buffer internals ----------------------------------------------- */

#define GAP_INIT_CAP 256
#define GAP_MIN_SIZE 64

static int gap_text_len(const lui_text_edit_t *te)
{
    return te->buf_cap - (te->gap_end - te->gap_start);
}

static void gap_ensure(lui_text_edit_t *te, int needed)
{
    int gap_size = te->gap_end - te->gap_start;
    if (gap_size >= needed) return;

    int text_len = gap_text_len(te);
    int new_cap = te->buf_cap * 2;
    if (new_cap < text_len + needed + GAP_MIN_SIZE)
        new_cap = text_len + needed + GAP_MIN_SIZE;

    char *new_buf = (char *)malloc(new_cap);
    if (!new_buf) return;

    /* Copy pre-gap */
    if (te->gap_start > 0)
        memcpy(new_buf, te->buf, te->gap_start);

    /* Copy post-gap at the end of new buffer */
    int tail_len = te->buf_cap - te->gap_end;
    if (tail_len > 0)
        memcpy(new_buf + new_cap - tail_len, te->buf + te->gap_end, tail_len);

    free(te->buf);
    te->buf = new_buf;
    te->gap_end = new_cap - tail_len;
    te->buf_cap = new_cap;
}

/* Move gap so that gap_start == pos (byte offset in logical text) */
static void gap_move_to(lui_text_edit_t *te, int pos)
{
    int text_len = gap_text_len(te);
    if (pos < 0) pos = 0;
    if (pos > text_len) pos = text_len;

    if (pos == te->gap_start) return;

    if (pos < te->gap_start) {
        /* Move text from before gap into after gap */
        int move = te->gap_start - pos;
        memmove(te->buf + te->gap_end - move,
                te->buf + pos, move);
        te->gap_start = pos;
        te->gap_end -= move;
    } else {
        /* Move text from after gap into before gap */
        int move = pos - te->gap_start;
        memmove(te->buf + te->gap_start,
                te->buf + te->gap_end, move);
        te->gap_start += move;
        te->gap_end += move;
    }
}

/* Flatten gap buffer to contiguous string for layout engine */
static void gap_flatten(lui_text_edit_t *te)
{
    int text_len = gap_text_len(te);
    if (text_len + 1 > te->flat_cap) {
        int new_cap = text_len + 1;
        char *new_buf = (char *)realloc(te->flat_buf, new_cap);
        if (!new_buf) return;
        te->flat_buf = new_buf;
        te->flat_cap = new_cap;
    }

    /* Copy pre-gap */
    if (te->gap_start > 0)
        memcpy(te->flat_buf, te->buf, te->gap_start);

    /* Copy post-gap */
    int tail_len = te->buf_cap - te->gap_end;
    if (tail_len > 0)
        memcpy(te->flat_buf + te->gap_start,
               te->buf + te->gap_end, tail_len);

    te->flat_buf[text_len] = '\0';
    te->flat_len = text_len;
}

/* ---- Undo coalescing helpers -------------------------------------------- */

/* Action types for coalescing */
#define ACTION_NONE        0
#define ACTION_INSERT      1
#define ACTION_DELETE_BACK 2
#define ACTION_DELETE_FWD  3

/* Coalescing time window (seconds) */
#define COALESCE_TIME_WINDOW 0.25

/* Reset undo coalescing state — call after non-typing actions */
static void reset_coalesce(lui_text_edit_t *te)
{
    te->last_action_type = ACTION_NONE;
}

/* Should the current edit coalesce with the previous one? */
static bool should_coalesce(lui_text_edit_t *te, int action_type)
{
    if (te->last_action_type != action_type)
        return false;

    /* For insert: coalesce if cursor is at the position after the previous insert */
    if (action_type == ACTION_INSERT &&
        te->cursor == te->last_cursor_pos)
        return true;

    /* For backspace: coalesce if cursor is just before the previous delete position */
    if (action_type == ACTION_DELETE_BACK &&
        te->cursor == te->last_cursor_pos)
        return true;

    return false;
}

/* Update coalescing state after a successful edit */
static void update_coalesce(lui_text_edit_t *te, int action_type)
{
    te->last_action_type  = action_type;
    te->last_cursor_pos   = te->cursor;
}

/* ---- Undo helpers ------------------------------------------------------- */

/* Internal: snapshot layout matches the anonymous structs in text_edit.h. */
typedef struct {
    char  *text;
    int    len;
    int    cursor;
    int    sel_start, sel_end;
} lui__undo_snap_t;

/* Save current editor state as a snapshot.  Invalidates redo stack. */
static void undo_save_state_raw(lui_text_edit_t *te)
{
    int text_len = gap_text_len(te);
    if (te->undo_len >= LUI_TEXT_EDIT_UNDO_MAX) {
        free(te->undo_stack[0].text);
        memmove(&te->undo_stack[0], &te->undo_stack[1],
                (size_t)(LUI_TEXT_EDIT_UNDO_MAX - 1) * sizeof(lui__undo_snap_t));
        te->undo_len--;
    }
    int i = te->undo_len;
    te->undo_stack[i].len = text_len;
    te->undo_stack[i].text = (char *)malloc((size_t)text_len + 1);
    if (te->undo_stack[i].text) {
        gap_flatten(te);
        memcpy(te->undo_stack[i].text, te->flat_buf, (size_t)text_len);
        te->undo_stack[i].text[text_len] = '\0';
    }
    te->undo_stack[i].cursor    = te->cursor;
    te->undo_stack[i].sel_start = te->sel_start;
    te->undo_stack[i].sel_end   = te->sel_end;
    te->undo_len++;
    /* New action invalidates redo */
    for (i = 0; i < te->redo_len; i++)
        free(te->redo_stack[i].text);
    te->redo_len = 0;
}

/* Save state with undo coalescing: consecutive similar edits share one entry. */
static void undo_save_state(lui_text_edit_t *te, int action_type)
{
    if (should_coalesce(te, action_type))
        return;
    undo_save_state_raw(te);
}

/* Restore editor state from the top of undo stack and free its text. */
static void undo_pop_undo(lui_text_edit_t *te)
{
    if (te->undo_len <= 0) return;
    int i = te->undo_len - 1;
    if (te->undo_stack[i].text) {
        gap_ensure(te, te->undo_stack[i].len);
        memcpy(te->buf, te->undo_stack[i].text, (size_t)te->undo_stack[i].len);
        te->gap_start = te->undo_stack[i].len;
        te->gap_end   = te->buf_cap;
        te->cursor    = te->undo_stack[i].cursor;
        te->sel_start = te->undo_stack[i].sel_start;
        te->sel_end   = te->undo_stack[i].sel_end;
        te->needs_layout = true;
    }
    free(te->undo_stack[i].text);
    te->undo_stack[i].text = NULL;
    te->undo_len--;
}

/* Restore editor state from the top of redo stack and free its text. */
static void undo_pop_redo(lui_text_edit_t *te)
{
    if (te->redo_len <= 0) return;
    int i = te->redo_len - 1;
    if (te->redo_stack[i].text) {
        gap_ensure(te, te->redo_stack[i].len);
        memcpy(te->buf, te->redo_stack[i].text, (size_t)te->redo_stack[i].len);
        te->gap_start = te->redo_stack[i].len;
        te->gap_end   = te->buf_cap;
        te->cursor    = te->redo_stack[i].cursor;
        te->sel_start = te->redo_stack[i].sel_start;
        te->sel_end   = te->redo_stack[i].sel_end;
        te->needs_layout = true;
    }
    free(te->redo_stack[i].text);
    te->redo_stack[i].text = NULL;
    te->redo_len--;
}

void lui_text_edit_init(lui_text_edit_t *te, lui_font_t *font, int max_width)
{
    if (!te) return;
    memset(te, 0, sizeof(*te));

    te->buf_cap = GAP_INIT_CAP;
    te->buf = (char *)calloc(te->buf_cap, 1);
    te->gap_start = 0;
    te->gap_end = te->buf_cap;

    te->cursor = 0;
    te->sel_start = -1;
    te->sel_end = -1;

    te->font = font;
    te->max_width = max_width;
    te->text_color = LVG_COLOR_WHITE;
    te->sel_color = LVG_COLOR_ARGB(0x80, 0x58, 0x9C, 0xE0);
    te->cursor_color = LVG_COLOR_WHITE;
    te->cursor_width   = 2;
    te->blink_period   = 0.53;

    lui_text_layout_init(&te->layout, font, max_width);
    te->needs_layout = true;
}

void lui_text_edit_destroy(lui_text_edit_t *te)
{
    if (!te) return;
    free(te->buf);
    free(te->flat_buf);
    lui_text_layout_destroy(&te->layout);
    for (int i = 0; i < te->undo_len; i++)
        free(te->undo_stack[i].text);
    for (int i = 0; i < te->redo_len; i++)
        free(te->redo_stack[i].text);
    memset(te, 0, sizeof(*te));
}

/* ---- Content ------------------------------------------------------------ */

void lui_text_edit_set_text(lui_text_edit_t *te, const char *utf8, int len)
{
    if (!te) return;
    if (!utf8) { len = 0; utf8 = ""; }
    if (len < 0) len = (int)strlen(utf8);

    reset_coalesce(te);
    undo_save_state_raw(te);

    /* Reset gap buffer with new content */
    gap_ensure(te, len);
    memcpy(te->buf, utf8, len);
    te->gap_start = len;
    te->gap_end = te->buf_cap;

    te->cursor = len;
    te->sel_start = -1;
    te->sel_end = -1;
    te->needs_layout = true;
}

int lui_text_edit_text_len(const lui_text_edit_t *te)
{
    if (!te) return 0;
    return gap_text_len(te);
}

void lui_text_edit_get_text(const lui_text_edit_t *te, char *out, int out_cap)
{
    if (!te || !out || out_cap <= 0) return;
    int text_len = gap_text_len(te);
    int copy_len = text_len < out_cap - 1 ? text_len : out_cap - 1;

    int pre = te->gap_start < copy_len ? te->gap_start : copy_len;
    if (pre > 0)
        memcpy(out, te->buf, pre);

    int remaining = copy_len - pre;
    if (remaining > 0)
        memcpy(out + pre, te->buf + te->gap_end, remaining);

    out[copy_len] = '\0';
}

/* ---- Editing ------------------------------------------------------------ */

void lui_text_edit_insert(lui_text_edit_t *te, const char *utf8, int len)
{
    if (!te || !utf8) return;
    if (len < 0) len = (int)strlen(utf8);
    if (len == 0) return;

    /* If there's a selection, it's a replacement action — new undo group */
    if (lui_text_edit_has_selection(te)) {
        reset_coalesce(te);
        undo_save_state_raw(te);
        int start = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
        int end   = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
        gap_move_to(te, start);
        te->gap_end += (end - start);
        te->cursor = start;
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        undo_save_state(te, ACTION_INSERT);
    }

    gap_move_to(te, te->cursor);
    gap_ensure(te, len);

    memcpy(te->buf + te->gap_start, utf8, len);
    te->gap_start += len;
    te->cursor += len;
    te->needs_layout = true;

    update_coalesce(te, ACTION_INSERT);
}

void lui_text_edit_delete_back(lui_text_edit_t *te, int count)
{
    if (!te || count <= 0) return;
    if (te->cursor <= 0) return;

    undo_save_state(te, ACTION_DELETE_BACK);

    gap_move_to(te, te->cursor);

    int del = count < te->gap_start ? count : te->gap_start;
    te->gap_start -= del;
    te->cursor -= del;
    te->needs_layout = true;

    update_coalesce(te, ACTION_DELETE_BACK);
}

void lui_text_edit_delete_forward(lui_text_edit_t *te, int count)
{
    if (!te || count <= 0) return;
    int text_len = gap_text_len(te);
    if (te->cursor >= text_len) return;

    undo_save_state(te, ACTION_DELETE_FWD);

    gap_move_to(te, te->cursor);

    int tail = te->buf_cap - te->gap_end;
    int del = count < tail ? count : tail;
    te->gap_end += del;
    te->needs_layout = true;

    update_coalesce(te, ACTION_DELETE_FWD);
}

void lui_text_edit_delete_selection(lui_text_edit_t *te)
{
    if (!te || !lui_text_edit_has_selection(te)) return;

    reset_coalesce(te);
    undo_save_state_raw(te);

    int start = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
    int end   = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
    int text_len = gap_text_len(te);

    if (start < 0) start = 0;
    if (end > text_len) end = text_len;

    /* Move gap to start, then expand to end */
    gap_move_to(te, start);
    int del = end - start;
    te->gap_end += del;
    te->cursor = start;
    te->sel_start = -1;
    te->sel_end = -1;
    te->needs_layout = true;
}

/* ---- Cursor movement ---------------------------------------------------- */

static void do_cursor_left(lui_text_edit_t *te, bool extend)
{
    if (!te || te->cursor <= 0) return;

    reset_coalesce(te);
    gap_flatten(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    te->cursor = utf8_prev(te->flat_buf, te->cursor);

    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

static void do_cursor_right(lui_text_edit_t *te, bool extend)
{
    if (!te) return;
    int text_len = gap_text_len(te);
    if (te->cursor >= text_len) return;

    reset_coalesce(te);
    gap_flatten(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int cp_len = utf8_cp_len(te->flat_buf, te->cursor, te->flat_len);
    te->cursor += cp_len;

    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

static void do_cursor_home(lui_text_edit_t *te, bool extend)
{
    if (!te) return;
    reset_coalesce(te);
    gap_flatten(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int pos = te->cursor;
    while (pos > 0 && te->flat_buf[pos - 1] != '\n')
        pos--;
    te->cursor = pos;

    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

static void do_cursor_end(lui_text_edit_t *te, bool extend)
{
    if (!te) return;
    reset_coalesce(te);
    gap_flatten(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int pos = te->cursor;
    while (pos < te->flat_len && te->flat_buf[pos] != '\n')
        pos++;
    te->cursor = pos;

    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

void lui_text_edit_cursor_left(lui_text_edit_t *te) { do_cursor_left(te, false); }
void lui_text_edit_cursor_right(lui_text_edit_t *te) { do_cursor_right(te, false); }
void lui_text_edit_cursor_home(lui_text_edit_t *te) { do_cursor_home(te, false); }
void lui_text_edit_cursor_end(lui_text_edit_t *te) { do_cursor_end(te, false); }
void lui_text_edit_cursor_left_extend(lui_text_edit_t *te) { do_cursor_left(te, true); }
void lui_text_edit_cursor_right_extend(lui_text_edit_t *te) { do_cursor_right(te, true); }
void lui_text_edit_cursor_home_extend(lui_text_edit_t *te) { do_cursor_home(te, true); }
void lui_text_edit_cursor_end_extend(lui_text_edit_t *te) { do_cursor_end(te, true); }

/* Find the line index whose byte range contains @cursor.
 * Returns -1 if not found (handled by caller fallback). */
static int find_line_for_cursor(const lui_text_edit_t *te, int cursor)
{
    if (!te->flat_buf || te->layout.line_count == 0)
        return -1;

    for (int i = 0; i < te->layout.line_count; i++) {
        const lui_line_t *line = &te->layout.lines[i];
        if (line->run_count == 0) continue;

        int first = line->run_start;
        int last  = line->run_start + line->run_count - 1;

        const lui_run_t *fr = &te->layout.runs[first];
        const lui_run_t *lr = &te->layout.runs[last];

        if (fr->is_image || lr->is_image) continue;

        int start_byte = (int)(fr->utf8 - te->flat_buf);
        int end_byte   = (int)(lr->utf8 - te->flat_buf) + lr->len;

        if (cursor >= start_byte && cursor <= end_byte)
            return i;
    }

    /* Cursor after all runs → last line */
    if (cursor >= gap_text_len(te) && te->layout.line_count > 0)
        return te->layout.line_count - 1;

    return -1;
}

static void do_cursor_up(lui_text_edit_t *te, bool extend)
{
    if (!te || !te->font) return;

    reset_coalesce(te);
    lui_text_edit_build(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int cursor_x = 0;
    if (te->cursor > 0 && te->flat_buf)
        cursor_x = lui_font_measure_text(te->font, te->flat_buf, te->cursor);

    int cursor_line = find_line_for_cursor(te, te->cursor);
    if (cursor_line < 0) cursor_line = 0;

    if (cursor_line <= 0) {
        te->cursor = 0;
        goto done_up;
    }

    int lh = lui_font_line_height(te->font);
    const lui_line_t *prev_line = &te->layout.lines[cursor_line - 1];
    int hit = lui_text_edit_hit_test(te, cursor_x, prev_line->y + lh / 2);
    if (hit >= 0)
        te->cursor = hit;

done_up:
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

static void do_cursor_down(lui_text_edit_t *te, bool extend)
{
    if (!te || !te->font) return;

    reset_coalesce(te);
    lui_text_edit_build(te);

    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int cursor_x = 0;
    if (te->cursor > 0 && te->flat_buf)
        cursor_x = lui_font_measure_text(te->font, te->flat_buf, te->cursor);

    int cursor_line = find_line_for_cursor(te, te->cursor);
    if (cursor_line < 0) cursor_line = 0;

    if (cursor_line < 0 || cursor_line >= te->layout.line_count - 1) {
        te->cursor = gap_text_len(te);
        goto done_down;
    }

    int lh = lui_font_line_height(te->font);
    const lui_line_t *next_line = &te->layout.lines[cursor_line + 1];
    int hit = lui_text_edit_hit_test(te, cursor_x, next_line->y + lh / 2);
    if (hit >= 0)
        te->cursor = hit;

done_down:
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

void lui_text_edit_cursor_up(lui_text_edit_t *te) { do_cursor_up(te, false); }
void lui_text_edit_cursor_down(lui_text_edit_t *te) { do_cursor_down(te, false); }
void lui_text_edit_cursor_up_extend(lui_text_edit_t *te) { do_cursor_up(te, true); }
void lui_text_edit_cursor_down_extend(lui_text_edit_t *te) { do_cursor_down(te, true); }

/* ---- Page up/down ------------------------------------------------------- */

static int page_step(lui_text_edit_t *te)
{
    if (!te || te->layout.line_count == 0) return 1;
    int lh = lui_font_line_height(te->font);
    if (lh <= 0) return 1;
    int vh = te->viewport_h;
    if (vh <= 0) vh = 100;
    int n = vh / lh;
    return n > 0 ? n : 1;
}

static void do_page_up(lui_text_edit_t *te, bool extend)
{
    if (!te || te->layout.line_count == 0) return;
    gap_flatten(te);
    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int n = page_step(te);
    for (int i = 0; i < n; i++) {
        int old = te->cursor;
        do_cursor_up(te, extend);
        if (te->cursor == old) break;
    }
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    }
}

static void do_page_down(lui_text_edit_t *te, bool extend)
{
    if (!te || te->layout.line_count == 0) return;
    gap_flatten(te);
    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;

    int n = page_step(te);
    for (int i = 0; i < n; i++) {
        int old = te->cursor;
        do_cursor_down(te, extend);
        if (te->cursor == old) break;
    }
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    }
}

void lui_text_edit_cursor_page_up(lui_text_edit_t *te) { do_page_up(te, false); }
void lui_text_edit_cursor_page_down(lui_text_edit_t *te) { do_page_down(te, false); }

/* ---- Word left/right ---------------------------------------------------- */

static int word_boundary_left(lui_text_edit_t *te, int pos)
{
    if (pos <= 0) return 0;
    int new_pos = pos;

    /* Skip non-alphanumeric immediately left */
    while (new_pos > 0) {
        unsigned char c = te->flat_buf[new_pos - 1];
        if (c == '\n') return new_pos;
        if (isalnum(c) || c == '_') break;
        new_pos--;
    }
    /* Skip over the word */
    while (new_pos > 0) {
        unsigned char c = te->flat_buf[new_pos - 1];
        if (c == '\n') return new_pos;
        if (!isalnum(c) && c != '_') return new_pos;
        new_pos--;
    }
    return new_pos;
}

static int word_boundary_right(lui_text_edit_t *te, int pos)
{
    if (!te || !te->flat_buf) return 0;
    int len = gap_text_len(te);
    if (pos >= len) return len;
    int new_pos = pos;

    /* Skip spaces/tabs */
    while (new_pos < len) {
        unsigned char c = te->flat_buf[new_pos];
        if (c == ' ' || c == '\t') new_pos++;
        else break;
    }
    if (new_pos >= len) return len;
    if (te->flat_buf[new_pos] == '\n') return new_pos + 1;

    /* Skip non-alphanumeric */
    while (new_pos < len) {
        unsigned char c = te->flat_buf[new_pos];
        if (c == '\n') return new_pos + 1;
        if (isalnum(c) || c == '_') break;
        new_pos++;
    }
    /* Skip over the word */
    while (new_pos < len) {
        unsigned char c = te->flat_buf[new_pos];
        if (c == '\n') return new_pos + 1;
        if (!isalnum(c) && c != '_') break;
        new_pos++;
    }
    return new_pos;
}

static void do_word_left(lui_text_edit_t *te, bool extend)
{
    if (!te || !te->flat_buf) return;
    gap_flatten(te);
    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;
    te->cursor = word_boundary_left(te, te->cursor);
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

static void do_word_right(lui_text_edit_t *te, bool extend)
{
    if (!te || !te->flat_buf) return;
    gap_flatten(te);
    if (extend && te->sel_start < 0)
        te->sel_start = te->sel_end = te->cursor;
    te->cursor = word_boundary_right(te, te->cursor);
    if (!extend) {
        te->sel_start = -1;
        te->sel_end = -1;
    } else {
        te->sel_end = te->cursor;
    }
}

void lui_text_edit_cursor_word_left(lui_text_edit_t *te) { do_word_left(te, false); }
void lui_text_edit_cursor_word_right(lui_text_edit_t *te) { do_word_right(te, false); }
void lui_text_edit_cursor_word_left_extend(lui_text_edit_t *te) { do_word_left(te, true); }
void lui_text_edit_cursor_word_right_extend(lui_text_edit_t *te) { do_word_right(te, true); }

/* ---- Selection ---------------------------------------------------------- */

void lui_text_edit_set_selection(lui_text_edit_t *te, int start, int end)
{
    if (!te) return;
    reset_coalesce(te);
    te->sel_start = start;
    te->sel_end = end;
}

void lui_text_edit_clear_selection(lui_text_edit_t *te)
{
    if (!te) return;
    reset_coalesce(te);
    te->sel_start = -1;
    te->sel_end = -1;
}

bool lui_text_edit_has_selection(const lui_text_edit_t *te)
{
    if (!te) return false;
    return te->sel_start >= 0 && te->sel_end >= 0 && te->sel_start != te->sel_end;
}

void lui_text_edit_select_all(lui_text_edit_t *te)
{
    if (!te) return;
    reset_coalesce(te);
    te->sel_start = 0;
    te->sel_end = gap_text_len(te);
    te->cursor = te->sel_end;
}

/* ---- Undo / Redo -------------------------------------------------------- */

void lui_text_edit_undo(lui_text_edit_t *te)
{
    if (!te || te->undo_len <= 0) return;

    /* Save current state onto redo stack */
    int text_len = gap_text_len(te);
    if (te->redo_len >= LUI_TEXT_EDIT_UNDO_MAX) {
        free(te->redo_stack[0].text);
        memmove(&te->redo_stack[0], &te->redo_stack[1],
                (size_t)(LUI_TEXT_EDIT_UNDO_MAX - 1) * sizeof(lui__undo_snap_t));
        te->redo_len--;
    }
    {
        int i = te->redo_len;
        te->redo_stack[i].len = text_len;
        te->redo_stack[i].text = (char *)malloc((size_t)text_len + 1);
        if (te->redo_stack[i].text) {
            gap_flatten(te);
            memcpy(te->redo_stack[i].text, te->flat_buf, (size_t)text_len);
            te->redo_stack[i].text[text_len] = '\0';
        }
        te->redo_stack[i].cursor    = te->cursor;
        te->redo_stack[i].sel_start = te->sel_start;
        te->redo_stack[i].sel_end   = te->sel_end;
    }
    te->redo_len++;

    /* Restore undo snapshot */
    undo_pop_undo(te);
}

void lui_text_edit_redo(lui_text_edit_t *te)
{
    if (!te || te->redo_len <= 0) return;

    /* Save current state onto undo stack */
    int text_len = gap_text_len(te);
    if (te->undo_len >= LUI_TEXT_EDIT_UNDO_MAX) {
        free(te->undo_stack[0].text);
        memmove(&te->undo_stack[0], &te->undo_stack[1],
                (size_t)(LUI_TEXT_EDIT_UNDO_MAX - 1) * sizeof(lui__undo_snap_t));
        te->undo_len--;
    }
    {
        int i = te->undo_len;
        te->undo_stack[i].len = text_len;
        te->undo_stack[i].text = (char *)malloc((size_t)text_len + 1);
        if (te->undo_stack[i].text) {
            gap_flatten(te);
            memcpy(te->undo_stack[i].text, te->flat_buf, (size_t)text_len);
            te->undo_stack[i].text[text_len] = '\0';
        }
        te->undo_stack[i].cursor    = te->cursor;
        te->undo_stack[i].sel_start = te->sel_start;
        te->undo_stack[i].sel_end   = te->sel_end;
    }
    te->undo_len++;

    /* Restore redo snapshot */
    undo_pop_redo(te);
}

/* ---- Clipboard ---------------------------------------------------------- */

bool lui_text_edit_copy(lui_text_edit_t *te)
{
    if (!te || !lui_text_edit_has_selection(te)) return false;

    int s0 = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
    int s1 = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;
    int text_len = gap_text_len(te);
    if (s0 < 0) s0 = 0;
    if (s1 > text_len) s1 = text_len;
    if (s1 <= s0) return false;

    /* Extract selected text into shared clipboard */
    int copy_len = s1 - s0;

    /* Snapshot at cursor to flatten */
    gap_flatten(te);
    char *copy = (char *)malloc((size_t)copy_len + 1);
    if (!copy) return false;
    memcpy(copy, te->flat_buf + s0, (size_t)copy_len);
    copy[copy_len] = '\0';

    free(g_te_clipboard);
    g_te_clipboard = copy;
    g_te_clipboard_len = copy_len;
    return true;
}

bool lui_text_edit_paste(lui_text_edit_t *te)
{
    if (!te) return false;
    if (!g_te_clipboard || g_te_clipboard_len <= 0)
        return false;

    lui_text_edit_insert(te, g_te_clipboard, g_te_clipboard_len);
    return true;
}

bool lui_text_edit_cut(lui_text_edit_t *te)
{
    if (!te || !lui_text_edit_has_selection(te)) return false;
    if (!lui_text_edit_copy(te)) return false;
    lui_text_edit_delete_selection(te);
    return true;
}

/* ---- Hit testing -------------------------------------------------------- */

int lui_text_edit_hit_test(lui_text_edit_t *te, int px, int py)
{
    if (!te || !te->font) return -1;

    lui_text_edit_build(te);

    if (te->layout.line_count == 0) return 0;

    /* Find the line that contains py */
    int target_line = -1;
    for (int i = 0; i < te->layout.line_count; i++) {
        const lui_line_t *line = &te->layout.lines[i];
        if (py >= line->y && py < line->y + line->height) {
            target_line = i;
            break;
        }
    }

    /* If above all lines, return start of text */
    if (target_line < 0 && py < 0) return 0;

    /* If below all lines, return end of text */
    if (target_line < 0)
        return gap_text_len(te);

    /* Walk runs on this line to find the closest character boundary */
    const lui_line_t *line = &te->layout.lines[target_line];

    /* Find the run containing px */
    for (int ri = line->run_start;
         ri < line->run_start + line->run_count; ri++) {
        const lui_run_t *run = &te->layout.runs[ri];
        if (run->is_image) continue;

        if (px >= run->x && px < run->x + run->width && run->utf8 && run->len > 0) {
            /* Walk characters in this run */
            const char *rp = run->utf8;
            int byte_off = (int)(rp - te->flat_buf);
            int local_x = run->x;

            const char *rend = rp + run->len;
            int best = byte_off;
            int best_dist = px < local_x ? local_x - px : px - local_x;

            while (rp < rend) {
                int cp_len = utf8_cp_len(rp, 0, (int)(rend - rp));
                int advance = lui_font_measure_text(te->font, rp, cp_len);
                local_x += advance;
                byte_off += cp_len;
                rp += cp_len;

                int dist = px - local_x;
                if (dist < 0) dist = -dist;
                if (dist < best_dist) {
                    best_dist = dist;
                    best = byte_off;
                }
            }
            return best;
        }
    }

    /* If we're past the last run on the line, return the end offset of that line */
    if (line->run_count > 0) {
        const lui_run_t *last = &te->layout.runs[line->run_start + line->run_count - 1];
        if (!last->is_image && last->utf8) {
            return (int)(last->utf8 - te->flat_buf) + last->len;
        }
    }

    return gap_text_len(te);
}

/* ---- Layout & draw ------------------------------------------------------ */

/* Forward declarations for scrolling helpers used by lui_text_edit_draw */
int lui_text_edit_content_height(const lui_text_edit_t *te);
void lui_text_edit_scroll_to_cursor(lui_text_edit_t *te, int view_h);
void lui_text_edit_scroll_visible(lui_text_edit_t *te, int y0, int y1, int view_h);

void lui_text_edit_build(lui_text_edit_t *te)
{
    if (!te || !te->needs_layout) return;

    gap_flatten(te);

    /* Feed the flattened text to the layout engine */
    lui_text_layout_clear(&te->layout);
    if (te->flat_len > 0) {
        lui_text_layout_add_text(&te->layout,
                                 te->flat_buf, te->flat_len,
                                 te->text_color, LVG_COLOR_TRANSPARENT,
                                 0, te->font);
    }
    lui_text_layout_build(&te->layout);
    te->needs_layout = false;
}

void lui_text_edit_draw(lui_text_edit_t *te,
                         lvg_canvas_t *canvas,
                         int x, int y,
                         double time_s)
{
    if (!te || !canvas || !te->font) return;

    lui_text_edit_build(te);
    te->anim_time = time_s;

    /* Clamp scroll_y */
    int view_h = te->viewport_h > 0 ? te->viewport_h : canvas->_clip.height;
    int content_h = lui_text_edit_content_height(te);
    int max_scroll = content_h - view_h;
    if (max_scroll < 0) max_scroll = 0;
    if (te->scroll_y < 0) te->scroll_y = 0;
    if (te->scroll_y > max_scroll) te->scroll_y = max_scroll;

    /* Apply scroll offset */
    int sy = y - te->scroll_y;

    /* Draw selection highlight (with scroll offset) */
    if (lui_text_edit_has_selection(te)) {
        int s0 = te->sel_start < te->sel_end ? te->sel_start : te->sel_end;
        int s1 = te->sel_start > te->sel_end ? te->sel_start : te->sel_end;

        for (int ri = 0; ri < te->layout.run_count; ri++) {
            const lui_run_t *run = &te->layout.runs[ri];
            if (run->is_image || !run->utf8) continue;

            int run_start = (int)(run->utf8 - te->flat_buf);
            int run_end = run_start + run->len;

            int overlap_start = s0 > run_start ? s0 : run_start;
            int overlap_end = s1 < run_end ? s1 : run_end;
            if (overlap_start >= overlap_end) continue;

            int px_start = run->x;
            if (overlap_start > run_start)
                px_start += lui_font_measure_text(te->font,
                    run->utf8, overlap_start - run_start);

            int px_end = run->x;
            if (overlap_end > run_start)
                px_end += lui_font_measure_text(te->font,
                    run->utf8, overlap_end - run_start);

            int line_h = lui_font_line_height(te->font);
            int ry = run->y - lui_font_ascent(te->font);

            lvg_canvas_fill_rect(canvas,
                                  x + px_start, sy + ry,
                                  px_end - px_start, line_h,
                                  te->sel_color);
        }
    }

    /* Draw text via layout engine (with scroll offset) */
    lui_text_layout_draw(&te->layout, canvas, x, sy, NULL, NULL);

    /* Draw cursor (with blink + scroll offset) */
    if (te->flat_buf) {
        te->blink_phase = fmod(time_s, te->blink_period) / te->blink_period;
        if (te->blink_phase < 0.5) {
            int lh = lui_font_line_height(te->font);
            int cursor_px = 0;
            int cursor_y  = 0;

            int cursor_line = find_line_for_cursor(te, te->cursor);
            if (cursor_line >= 0 && cursor_line < te->layout.line_count) {
                cursor_y = te->layout.lines[cursor_line].y;
                int line_start = te->cursor;
                while (line_start > 0 &&
                       te->flat_buf[line_start - 1] != '\n')
                    line_start--;
                if (te->cursor > line_start)
                    cursor_px = lui_font_measure_text(te->font,
                        te->flat_buf + line_start,
                        te->cursor - line_start);
            } else {
                cursor_y = 0;
                if (te->cursor > 0)
                    cursor_px = lui_font_measure_text(te->font,
                        te->flat_buf, te->cursor);
            }

            lvg_canvas_fill_rect(canvas,
                                  x + cursor_px, sy + cursor_y,
                                  te->cursor_width, lh,
                                  te->cursor_color);
        }
    }

    /* Auto-scroll to keep cursor visible */
    lui_text_edit_scroll_to_cursor(te, view_h);

    /* Draw thin scrollbar */
    if (max_scroll > 0 && view_h > 20) {
        int sb_w = 6;
        int sb_x = x + canvas->_clip.width - sb_w;
        if (sb_x > x + view_h) sb_x = x + view_h - sb_w;
        int sb_h = view_h;
        lvg_canvas_fill_rect(canvas, sb_x, y, sb_w, sb_h,
                              LVG_COLOR_ARGB(0x20, 0xFF, 0xFF, 0xFF));
        float thumb_ratio = (float)view_h / (float)content_h;
        int thumb_h = (int)(sb_h * thumb_ratio);
        if (thumb_h < 8) thumb_h = 8;
        int thumb_y = y + (int)((float)te->scroll_y / (float)max_scroll *
                                 (float)(sb_h - thumb_h));
        lvg_canvas_fill_rect(canvas, sb_x, thumb_y, sb_w, thumb_h,
                              LVG_COLOR_ARGB(0x60, 0xFF, 0xFF, 0xFF));
    }
}

/* ---- Scrolling helpers -------------------------------------------------- */

int lui_text_edit_content_height(const lui_text_edit_t *te)
{
    if (!te) return 0;
    return te->layout.total_height;
}

void lui_text_edit_scroll_to_cursor(lui_text_edit_t *te, int view_h)
{
    if (!te || view_h <= 0) return;
    int cursor_line = find_line_for_cursor(te, te->cursor);
    if (cursor_line < 0 || cursor_line >= te->layout.line_count) return;
    int lh = lui_font_line_height(te->font);
    int cy = te->layout.lines[cursor_line].y;
    lui_text_edit_scroll_visible(te, cy, cy + lh, view_h);
}

void lui_text_edit_scroll_visible(lui_text_edit_t *te, int y0, int y1, int view_h)
{
    if (!te || view_h <= 0) return;
    if (y0 < te->scroll_y)
        te->scroll_y = y0;
    else if (y1 > te->scroll_y + view_h)
        te->scroll_y = y1 - view_h;
}

/* ---- Animation ---------------------------------------------------------- */

void lui_text_edit_set_animation(lui_text_edit_t *te,
                                  lui_text_anim_fn fn, void *user)
{
    if (!te) return;
    te->anim_fn = fn;
    te->anim_user = user;
}
