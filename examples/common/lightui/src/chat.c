/*
 * chat.c — Chat / message list widget
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightui/chat.h>
#include <lightvg/canvas.h>
#include <lightui/event.h>

#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------
 * Helpers
 * ------------------------------------------------------------------------- */

/* Estimate the number of wrapped lines for a message given a max width. */
static int msg_line_count(const lui_chat_msg_t *msg, int max_w, int char_w)
{
    if (msg->text_len <= 0 || max_w <= 0) return 1;
    int chars_per_line = (max_w - 16) / char_w;  /* padding inside bubble */
    if (chars_per_line < 1) chars_per_line = 1;
    int lines = (msg->text_len + chars_per_line - 1) / chars_per_line;
    return lines < 1 ? 1 : lines;
}

/* Compute the pixel height of a single message bubble. */
static int msg_bubble_height(const lui_chat_t *chat, const lui_chat_msg_t *msg,
                              int widget_w)
{
    int max_bw = chat->bubble_max_width > 0
               ? chat->bubble_max_width
               : widget_w * 7 / 10;
    int char_w = 7;  /* approximate character width without font */
    int lines = msg_line_count(msg, max_bw, char_w);
    int h = chat->bubble_padding * 2 + lines * chat->line_height;
    if (msg->role != LUI_CHAT_SYSTEM)
        h += chat->sender_height;
    return h;
}

/* Recompute total content height. */
static void chat_update_content_height(lui_chat_t *chat)
{
    lvg_rect_t r = lui_widget_absolute_rect(&chat->widget);
    int w = r.width > 0 ? r.width : 400;
    int total = chat->bubble_spacing;  /* top margin */
    for (int i = 0; i < chat->msg_count; i++) {
        total += msg_bubble_height(chat, &chat->messages[i], w);
        total += chat->bubble_spacing;
    }
    chat->content_height = total;
}

/* Clamp scroll_y to valid range. */
static void chat_clamp_scroll(lui_chat_t *chat)
{
    lvg_rect_t r = lui_widget_absolute_rect(&chat->widget);
    int view_h = r.height > 0 ? r.height : 300;
    int max_scroll = chat->content_height - view_h;
    if (max_scroll < 0) max_scroll = 0;
    if (chat->scroll_y < 0) chat->scroll_y = 0;
    if (chat->scroll_y > max_scroll) chat->scroll_y = max_scroll;
}

/* -------------------------------------------------------------------------
 * Callbacks
 * ------------------------------------------------------------------------- */

static int chat_measure(const lui_widget_t *w, int *out_w, int *out_h,
                          void *user)
{
    (void)w; (void)user;
    *out_w = 300;
    *out_h = 400;
    return 0;
}

static void chat_draw(lui_widget_t *w, lvg_canvas_t *canvas)
{
    lui_chat_t *chat = (lui_chat_t *)w;
    lvg_rect_t r = lui_widget_absolute_rect(w);
    if (lvg_rect_is_empty(&r)) return;

    /* Background */
    lvg_canvas_fill_rect(canvas, r.x, r.y, r.width, r.height, chat->bg);

    /* Clip to widget bounds */
    lvg_rect_t old_clip = canvas->_clip;
    lvg_rect_t clip = lvg_rect_intersect(&old_clip, &r);
    canvas->_clip = clip;

    chat_update_content_height(chat);
    chat_clamp_scroll(chat);

    /* Draw messages from top, offset by scroll */
    int y = r.y + chat->bubble_spacing - chat->scroll_y;

    for (int i = 0; i < chat->msg_count; i++) {
        const lui_chat_msg_t *msg = &chat->messages[i];

        int max_bw = chat->bubble_max_width > 0
                   ? chat->bubble_max_width
                   : r.width * 7 / 10;
        int bh = msg_bubble_height(chat, msg, r.width);
        int char_w = 7;
        int text_w = msg->text_len * char_w + 16;
        if (text_w > max_bw) text_w = max_bw;
        if (text_w < 40) text_w = 40;

        int bx;
        lvg_color_t bubble_color, text_color;

        switch (msg->role) {
        case LUI_CHAT_SELF:
            bx = r.x + r.width - text_w - chat->scrollbar_width - 8;
            bubble_color = chat->self_bubble;
            text_color = chat->self_text;
            break;
        case LUI_CHAT_SYSTEM:
            bx = r.x + (r.width - text_w) / 2;
            bubble_color = chat->system_bubble;
            text_color = chat->system_text;
            break;
        default: /* OTHER */
            bx = r.x + 8;
            bubble_color = chat->other_bubble;
            text_color = chat->other_text;
            break;
        }

        /* Only draw if visible */
        if (y + bh > r.y && y < r.y + r.height) {
            /* Bubble */
            lvg_canvas_fill_rounded_rect(canvas, bx, y, text_w, bh,
                                          chat->bubble_radius, bubble_color);

            int tx = bx + 8;
            int ty = y + chat->bubble_padding;

            /* Sender label (not for system messages) */
            if (msg->role != LUI_CHAT_SYSTEM) {
                int slen = (int)strlen(msg->sender);
                for (int c = 0; c < slen && tx + c * 6 < bx + text_w - 8; c++)
                    lvg_canvas_fill_rect(canvas, tx + c * 6, ty, 4, 8,
                                          chat->sender_color);
                ty += chat->sender_height;
            }

            /* Text (character rectangles, with wrapping) */
            int chars_per_line = (text_w - 16) / char_w;
            if (chars_per_line < 1) chars_per_line = 1;
            int pos = 0;
            while (pos < msg->text_len) {
                int line_len = msg->text_len - pos;
                if (line_len > chars_per_line) line_len = chars_per_line;
                for (int c = 0; c < line_len; c++)
                    lvg_canvas_fill_rect(canvas, tx + c * char_w, ty,
                                          5, 10, text_color);
                pos += line_len;
                ty += chat->line_height;
            }
        }

        y += bh + chat->bubble_spacing;
    }

    /* Scrollbar */
    if (chat->content_height > r.height) {
        int sb_x = r.x + r.width - chat->scrollbar_width;
        int track_h = r.height;
        int thumb_h = (r.height * r.height) / chat->content_height;
        if (thumb_h < 20) thumb_h = 20;
        int max_scroll = chat->content_height - r.height;
        int thumb_y = r.y;
        if (max_scroll > 0)
            thumb_y += (chat->scroll_y * (track_h - thumb_h)) / max_scroll;
        lvg_canvas_fill_rounded_rect(canvas, sb_x, thumb_y,
                                      chat->scrollbar_width, thumb_h,
                                      chat->scrollbar_width / 2,
                                      chat->scrollbar_color);
    }

    canvas->_clip = old_clip;
}

static int chat_event(lui_widget_t *w, const lui_event_t *event)
{
    lui_chat_t *chat = (lui_chat_t *)w;

    if (event->type == LUI_EVENT_SCROLL) {
        lvg_rect_t r = lui_widget_absolute_rect(w);
        if (lvg_rect_contains_point(&r,
                event->data.scroll.x, event->data.scroll.y)) {
            int delta = (int)(event->data.scroll.delta_y * -30.0f);
            chat->scroll_y += delta;
            chat_clamp_scroll(chat);
            return 1;
        }
    }

    return 0;
}

/* -------------------------------------------------------------------------
 * Public API
 * ------------------------------------------------------------------------- */

static void *chat_default_alloc(void *u, size_t n) { (void)u; return malloc(n); }
static void  chat_default_free(void *u, void *p)    { (void)u; free(p); }

bool lui_chat_init_ex(lui_chat_t *chat, int max_messages,
                       lui_alloc_fn alloc_fn,
                       lui_free_fn  free_fn,
                       void        *alloc_user)
{
    if (!chat || max_messages <= 0) return false;
    if ((alloc_fn == NULL) != (free_fn == NULL)) return false;
    if (!alloc_fn) {
        alloc_fn   = chat_default_alloc;
        free_fn    = chat_default_free;
        alloc_user = NULL;
    }

    size_t bytes = (size_t)max_messages * sizeof(lui_chat_msg_t);
    if (bytes / sizeof(lui_chat_msg_t) != (size_t)max_messages) return false;
    lui_chat_msg_t *messages = (lui_chat_msg_t *)alloc_fn(alloc_user, bytes);
    if (!messages) return false;
    memset(messages, 0, bytes);

    memset(chat, 0, sizeof(*chat));
    chat->messages     = messages;
    chat->max_messages = max_messages;
    chat->alloc_fn     = alloc_fn;
    chat->free_fn      = free_fn;
    chat->alloc_user   = alloc_user;

    lui_widget_init(&chat->widget);
    chat->widget.width   = lvg_size_fill(1);
    chat->widget.height  = lvg_size_fill(1);
    chat->widget.measure = chat_measure;
    chat->widget.draw    = chat_draw;
    chat->widget.on_event = chat_event;
    chat->widget.flags   = LUI_WIDGET_DRAWS_CHILDREN;

    chat->msg_count      = 0;
    chat->next_id        = 1;
    chat->scroll_y       = 0;
    chat->content_height = 0;
    chat->auto_scroll    = true;

    chat->bubble_padding   = 8;
    chat->bubble_spacing   = 6;
    chat->bubble_radius    = 8;
    chat->bubble_max_width = 0;  /* auto: 70% of width */
    chat->sender_height    = 16;
    chat->line_height      = 18;

    chat->bg             = LVG_COLOR_RGB(0x1E, 0x1E, 0x2E);
    chat->self_bubble    = LVG_COLOR_RGB(0x2A, 0x5C, 0x8A);
    chat->other_bubble   = LVG_COLOR_RGB(0x35, 0x38, 0x3E);
    chat->system_bubble  = LVG_COLOR_RGB(0x28, 0x2A, 0x30);
    chat->self_text      = LVG_COLOR_RGB(0xE0, 0xE3, 0xE7);
    chat->other_text     = LVG_COLOR_RGB(0xD0, 0xD3, 0xD7);
    chat->system_text    = LVG_COLOR_RGB(0x88, 0x8C, 0x94);
    chat->sender_color   = LVG_COLOR_RGB(0x70, 0xB0, 0xE0);
    chat->scrollbar_color = LVG_COLOR_ARGB(0x80, 0x80, 0x84, 0x8A);
    chat->scrollbar_width = 6;

    chat->on_submit      = NULL;
    chat->on_submit_user = NULL;

    return true;
}

bool lui_chat_init(lui_chat_t *chat)
{
    return lui_chat_init_ex(chat, LUI_CHAT_MAX_MESSAGES, NULL, NULL, NULL);
}

void lui_chat_destroy(lui_chat_t *chat)
{
    if (!chat) return;
    if (chat->free_fn && chat->messages)
        chat->free_fn(chat->alloc_user, chat->messages);
    chat->messages     = NULL;
    chat->msg_count    = 0;
    chat->max_messages = 0;
}

int lui_chat_add_message(lui_chat_t *chat, lui_chat_role_t role,
                          const char *sender, const char *text)
{
    if (!chat || !text || chat->msg_count >= chat->max_messages)
        return -1;

    lui_chat_msg_t *msg = &chat->messages[chat->msg_count];
    msg->id = chat->next_id++;
    msg->role = role;

    /* Copy sender */
    if (sender) {
        int slen = (int)strlen(sender);
        if (slen > LUI_CHAT_MAX_SENDER_LEN) slen = LUI_CHAT_MAX_SENDER_LEN;
        memcpy(msg->sender, sender, slen);
        msg->sender[slen] = '\0';
    } else {
        msg->sender[0] = '\0';
    }

    /* Copy text */
    int tlen = (int)strlen(text);
    if (tlen > LUI_CHAT_MAX_TEXT_LEN) tlen = LUI_CHAT_MAX_TEXT_LEN;
    memcpy(msg->text, text, tlen);
    msg->text[tlen] = '\0';
    msg->text_len = tlen;

    chat->msg_count++;

    /* Auto-scroll to bottom */
    if (chat->auto_scroll) {
        chat_update_content_height(chat);
        lvg_rect_t r = lui_widget_absolute_rect(&chat->widget);
        int view_h = r.height > 0 ? r.height : 300;
        chat->scroll_y = chat->content_height - view_h;
        if (chat->scroll_y < 0) chat->scroll_y = 0;
    }

    return msg->id;
}

void lui_chat_clear(lui_chat_t *chat)
{
    if (!chat) return;
    chat->msg_count = 0;
    chat->scroll_y = 0;
    chat->content_height = 0;
}

const lui_chat_msg_t *lui_chat_get_message(const lui_chat_t *chat, int msg_id)
{
    if (!chat) return NULL;
    for (int i = 0; i < chat->msg_count; i++) {
        if (chat->messages[i].id == msg_id)
            return &chat->messages[i];
    }
    return NULL;
}

void lui_chat_scroll_to_bottom(lui_chat_t *chat)
{
    if (!chat) return;
    chat_update_content_height(chat);
    lvg_rect_t r = lui_widget_absolute_rect(&chat->widget);
    int view_h = r.height > 0 ? r.height : 300;
    chat->scroll_y = chat->content_height - view_h;
    if (chat->scroll_y < 0) chat->scroll_y = 0;
}
