/*
 * lightui/chat.h — Chat / message list widget
 *
 * A scrollable list of chat messages with sender labels,
 * text content, and timestamps.  Messages are displayed
 * in a bubble-style layout with alternating alignment
 * for "self" vs "other" messages.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_CHAT_H
#define LIGHTUI_CHAT_H

#include "alloc.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Limits ------------------------------------------------------------- */

/* Default capacity used by lui_chat_init().
 * Use lui_chat_init_ex() to pick a different size. */
#define LUI_CHAT_MAX_MESSAGES    256
#define LUI_CHAT_MAX_SENDER_LEN   31  /* excl. NUL */
#define LUI_CHAT_MAX_TEXT_LEN    511  /* excl. NUL */

/* ---- Message ------------------------------------------------------------ */

typedef enum {
    LUI_CHAT_SELF  = 0,   /* right-aligned bubble (the local user)     */
    LUI_CHAT_OTHER = 1,   /* left-aligned bubble (remote user)         */
    LUI_CHAT_SYSTEM = 2,  /* centred system/info message               */
} lui_chat_role_t;

typedef struct {
    int              id;
    lui_chat_role_t  role;
    char             sender[LUI_CHAT_MAX_SENDER_LEN + 1];
    char             text[LUI_CHAT_MAX_TEXT_LEN + 1];
    int              text_len;     /* byte length of text                  */
} lui_chat_msg_t;

/* ---- Events ------------------------------------------------------------- */

typedef void (*lui_chat_submit_fn)(const char *text, void *user);

/* ---- Chat widget -------------------------------------------------------- */

typedef struct {
    lui_widget_t     widget;

    /* Messages — heap-allocated, capacity = max_messages. */
    lui_chat_msg_t  *messages;
    int              msg_count;
    int              max_messages;
    int              next_id;

    /* Allocator paired with `messages` — used by destroy. */
    lui_alloc_fn     alloc_fn;
    lui_free_fn      free_fn;
    void            *alloc_user;

    /* Scroll state */
    int              scroll_y;       /* pixel offset from bottom            */
    int              content_height; /* total computed content height        */
    bool             auto_scroll;    /* scroll to bottom on new message     */

    /* Appearance — bubbles */
    int              bubble_padding;    /* internal padding (8)             */
    int              bubble_spacing;    /* gap between messages (6)         */
    int              bubble_radius;     /* corner radius (8)                */
    int              bubble_max_width;  /* max bubble width (0 = 70% of w) */
    int              sender_height;     /* sender label height (16)         */
    int              line_height;       /* text line height (18)            */

    /* Colors */
    lvg_color_t      bg;               /* background color                  */
    lvg_color_t      self_bubble;      /* self message bubble color          */
    lvg_color_t      other_bubble;     /* other message bubble color         */
    lvg_color_t      system_bubble;    /* system message bubble color        */
    lvg_color_t      self_text;        /* self message text color            */
    lvg_color_t      other_text;       /* other message text color           */
    lvg_color_t      system_text;      /* system message text color          */
    lvg_color_t      sender_color;     /* sender label text color            */
    lvg_color_t      scrollbar_color;  /* scrollbar thumb color              */
    int              scrollbar_width;  /* scrollbar width (6)                */

    /* Callback */
    lui_chat_submit_fn on_submit;
    void              *on_submit_user;
} lui_chat_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise with default capacity using malloc/free. Pair with destroy. */
bool lui_chat_init(lui_chat_t *chat);

/** Initialise with caller-supplied capacity / allocator (NULL/NULL = default). */
bool lui_chat_init_ex(lui_chat_t *chat, int max_messages,
                       lui_alloc_fn alloc_fn,
                       lui_free_fn  free_fn,
                       void        *alloc_user);

/** Free heap arrays owned by `chat`. */
void lui_chat_destroy(lui_chat_t *chat);

/**
 * Add a message to the chat.  Returns the message ID (> 0) or -1 on failure.
 * Sender and text are copied internally (truncated to max lengths).
 */
int lui_chat_add_message(lui_chat_t *chat,
                          lui_chat_role_t role,
                          const char *sender,
                          const char *text);

/** Remove all messages. */
void lui_chat_clear(lui_chat_t *chat);

/** Get message by ID. Returns NULL if not found. */
const lui_chat_msg_t *lui_chat_get_message(const lui_chat_t *chat, int msg_id);

/** Scroll to the bottom of the chat. */
void lui_chat_scroll_to_bottom(lui_chat_t *chat);

/** Get the widget node. */
static inline lui_widget_t *lui_chat_widget(lui_chat_t *chat) {
    return &chat->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_CHAT_H */
