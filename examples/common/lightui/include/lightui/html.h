/*
 * lightui/html.h — HTML parser, renderer, and widget
 *
 * Three-layer architecture (mirrors the markdown implementation):
 *   1. Parser:   tokenizer + tree builder → DOM-like tree
 *   2. Renderer: walks DOM, emits styled spans into lui_text_layout_t
 *   3. Widget:   owns document, drives rendering, handles scroll
 *
 * Supported HTML subset (rich text display level, not a browser):
 *
 * Block:  <p> <h1>-<h6> <div> <blockquote> <pre> <code> <ul> <ol> <li>
 *         <hr> <br> <table> <tr> <td> <th>
 * Inline: <b> <strong> <i> <em> <u> <s> <strike> <code> <a> <img>
 *         <span> <sub> <sup>
 * Style:  inline style="" attribute (color, background-color, font-weight,
 *         font-style, text-decoration, text-align)
 * Entities: &amp; &lt; &gt; &quot; &nbsp; &#NNN; &#xHHH;
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_HTML_H
#define LIGHTUI_HTML_H

#include "layout.h"
#include "text_layout.h"
#include <lightvg/types.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- DOM node types ---------------------------------------------------- */

typedef enum {
    LUI_HTML_DOCUMENT = 0,

    /* Block elements */
    LUI_HTML_P,
    LUI_HTML_H1, LUI_HTML_H2, LUI_HTML_H3,
    LUI_HTML_H4, LUI_HTML_H5, LUI_HTML_H6,
    LUI_HTML_DIV,
    LUI_HTML_BLOCKQUOTE,
    LUI_HTML_PRE,
    LUI_HTML_UL,
    LUI_HTML_OL,
    LUI_HTML_LI,
    LUI_HTML_HR,
    LUI_HTML_TABLE,
    LUI_HTML_TR,
    LUI_HTML_TD,
    LUI_HTML_TH,

    /* Inline elements */
    LUI_HTML_SPAN,
    LUI_HTML_B,
    LUI_HTML_I,
    LUI_HTML_U,
    LUI_HTML_S,
    LUI_HTML_CODE,
    LUI_HTML_A,
    LUI_HTML_IMG,
    LUI_HTML_BR,
    LUI_HTML_SUB,
    LUI_HTML_SUP,

    /* Pseudo-node for text content */
    LUI_HTML_TEXT,
} lui_html_type_t;

/* ---- Inline style (from style="" attribute) ---------------------------- */

typedef struct {
    lvg_color_t  color;            /* LVG_COLOR_TRANSPARENT = inherit */
    lvg_color_t  background_color; /* LVG_COLOR_TRANSPARENT = inherit */
    uint8_t      font_weight;      /* 0=inherit, 1=normal, 2=bold */
    uint8_t      font_style;       /* 0=inherit, 1=normal, 2=italic */
    uint8_t      text_decoration;  /* 0=inherit, bitmask: 1=underline, 2=line-through */
    uint8_t      text_align;       /* 0=inherit, 1=left, 2=center, 3=right */
} lui_html_inline_style_t;

/* ---- DOM node ---------------------------------------------------------- */

typedef struct lui_html_node lui_html_node_t;

struct lui_html_node {
    lui_html_type_t          type;
    lui_html_node_t         *parent;
    lui_html_node_t         *first_child;
    lui_html_node_t         *last_child;
    lui_html_node_t         *next_sibling;

    /* Text content (TEXT nodes) — points into doc->source */
    const char              *text;
    int                      text_len;

    /* Attributes */
    const char              *href;        /* <a href="..."> */
    int                      href_len;
    const char              *src_url;     /* <img src="..."> */
    int                      src_url_len;
    const char              *alt;         /* <img alt="..."> */
    int                      alt_len;

    int                      ol_start;    /* <ol start="N"> */

    /* Parsed inline style */
    lui_html_inline_style_t  style;
};

/* ---- Document (parser output) ------------------------------------------ */

typedef struct {
    lui_html_node_t  *root;
    lui_html_node_t  *nodes;       /* arena: flat array */
    int               node_count;
    int               node_cap;
    char             *source;      /* owned, entity-decoded copy */
    int               source_len;
} lui_html_doc_t;

/**
 * Parse an HTML fragment from UTF-8 source.
 * The document owns a copy of @src.
 * Returns 0 on success, -1 on allocation failure.
 */
int  lui_html_parse(lui_html_doc_t *doc, const char *src, int len);

/** Free all storage owned by the document. */
void lui_html_doc_destroy(lui_html_doc_t *doc);

/* ---- Style ------------------------------------------------------------- */

typedef struct {
    lvg_color_t  text_color;
    lvg_color_t  heading_color;
    lvg_color_t  link_color;
    lvg_color_t  code_fg;
    lvg_color_t  code_bg;
    lvg_color_t  blockquote_fg;
    lvg_color_t  blockquote_bar_color;
    lvg_color_t  hr_color;
    lvg_color_t  table_border_color;
    lvg_color_t  th_bg;

    int          blockquote_bar_width;
    int          blockquote_indent;
    int          list_indent;
    int          code_block_padding;
    int          paragraph_spacing;
    int          heading_spacing_above;
    int          heading_spacing_below;
    int          table_cell_padding;
    int          table_border_width;
} lui_html_style_t;

/** Initialise a style with dark-theme defaults. */
void lui_html_style_default(lui_html_style_t *style);

/* ---- Block decorations ------------------------------------------------- */

typedef enum {
    LUI_HTML_DECO_BLOCKQUOTE_BAR,
    LUI_HTML_DECO_HR,
    LUI_HTML_DECO_CODE_BLOCK_BG,
    LUI_HTML_DECO_TABLE_BORDER,
    LUI_HTML_DECO_TH_BG,
} lui_html_deco_type_t;

typedef struct {
    lui_html_deco_type_t type;
    int          line_start;
    int          line_end;
    int          indent;
    int          x, width;
    lvg_color_t  color;
} lui_html_deco_t;

typedef struct {
    lui_html_deco_t *decos;
    int              deco_count;
    int              deco_cap;
} lui_html_render_state_t;

/* ---- Renderer ---------------------------------------------------------- */

typedef const lvg_surface_t *(*lui_html_resolve_image_fn)(
    const char *url, int url_len, void *user);

/**
 * Render a parsed HTML document into a text layout.
 * Clears @tl, then populates it with styled spans.
 */
void lui_html_render(lui_text_layout_t *tl,
                     const lui_html_doc_t *doc,
                     const lui_html_style_t *style,
                     lui_html_render_state_t *rs,
                     lui_html_resolve_image_fn resolve_image,
                     void *resolve_user);

void lui_html_render_state_destroy(lui_html_render_state_t *rs);

/* ---- Link region ------------------------------------------------------- */

typedef struct {
    lvg_rect_t   rect;
    const char  *url;
    int          url_len;
} lui_html_link_region_t;

/* ---- Widget ------------------------------------------------------------ */

typedef void (*lui_html_link_fn)(const char *url, int url_len, void *user);

typedef struct {
    lui_widget_t              widget;

    lui_html_doc_t            doc;
    lui_text_layout_t         layout;
    lui_html_render_state_t   render_state;
    bool                      layout_dirty;

    lui_html_link_region_t   *link_regions;
    int                       link_region_count;
    int                       link_region_cap;

    int                       scroll_y;
    int                       content_height;
    int                       max_width;

    lui_html_style_t          style;
    lvg_color_t               bg;
    lvg_color_t               scrollbar_color;
    int                       scrollbar_width;

    lui_font_t               *font;

    lui_html_resolve_image_fn resolve_image;
    void                     *resolve_image_user;
    lui_html_link_fn          on_link_click;
    void                     *on_link_user;
} lui_html_t;

/** Initialise an HTML widget. @font may be NULL for character-rect mode. */
void lui_html_init(lui_html_t *html, lui_font_t *font);

/** Destroy the widget. */
void lui_html_destroy(lui_html_t *html);

/**
 * Set the HTML source. Parses and invalidates layout.
 * @src is copied internally.
 */
void lui_html_set_text(lui_html_t *html, const char *src, int len);

/** Scroll to a vertical offset. */
void lui_html_scroll_to(lui_html_t *html, int y);

/** Get the widget node. */
static inline lui_widget_t *lui_html_widget(lui_html_t *html) {
    return &html->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_HTML_H */
