/*
 * lightui/card.h — Content card container widget
 *
 * A card with optional header (title/subtitle), body, and footer sections.
 * The body and footer can host arbitrary child widgets.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_CARD_H
#define LIGHTUI_CARD_H

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Types -------------------------------------------------------------- */

typedef void (*lui_card_click_fn)(void *user);

/* ---- Card widget -------------------------------------------------------- */

typedef struct {
    lui_widget_t widget;

    /* Header text */
    char          title[64];
    char          subtitle[32];
    bool          show_header;
    bool          show_footer;

    /* Dimensions */
    int           corner_radius;       /* default 8                          */
    int           header_height;       /* default 40                         */
    int           footer_height;       /* default 36                         */

    /* Shadow */
    int           shadow_offset;       /* default 2                          */
    lvg_color_t   shadow_color;

    /* Appearance */
    lvg_color_t   bg_color;
    lvg_color_t   header_bg;
    lvg_color_t   header_text;
    lvg_color_t   body_bg;
    lvg_color_t   footer_bg;
    lvg_color_t   border_color;
    lvg_color_t   subtitle_color;

    /* Child widgets (optional, not owned) */
    lui_widget_t *body_widget;
    lui_widget_t *footer_widget;

    /* Callback */
    lui_card_click_fn on_click;
    void             *on_click_user;
} lui_card_t;

/* ---- API ---------------------------------------------------------------- */

/** Initialise a card widget with sensible defaults. */
void lui_card_init(lui_card_t *card);

/** Set the title and optional subtitle. Pass NULL for subtitle to clear. */
void lui_card_set_title(lui_card_t *card, const char *title,
                         const char *subtitle);

/** Set the body child widget (may be NULL). */
void lui_card_set_body(lui_card_t *card, lui_widget_t *widget);

/** Set the footer child widget (may be NULL). */
void lui_card_set_footer(lui_card_t *card, lui_widget_t *widget);

/** Get the widget node. */
static inline lui_widget_t *lui_card_widget(lui_card_t *card) {
    return &card->widget;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_CARD_H */
