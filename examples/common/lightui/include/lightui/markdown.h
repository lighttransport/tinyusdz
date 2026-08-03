/*
 * lightui/markdown.h — Markdown parser, renderer, and widget
 *
 * Three-layer architecture:
 *   1. Parser:   converts raw markdown into an AST (document tree).
 *   2. Renderer: walks the AST and emits spans into a lui_text_layout_t.
 *   3. Widget:   owns the document, drives rendering, handles scroll.
 *
 * Supported CommonMark subset:
 *   - Headings (# through ######)
 *   - Paragraphs
 *   - Bold (**text**), italic (*text*), code (`text`)
 *   - Underline (<u>text</u>)
 *   - Links [text](url), images ![alt](url)
 *   - Ordered and unordered lists
 *   - Blockquotes (> prefix)
 *   - Fenced code blocks (``` or ~~~)
 *   - Tables (| Col | Col |)
 *   - Thematic breaks (---, ***, ___)
 *   - Hard breaks (trailing double space)
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_MARKDOWN_H
#define LIGHTUI_MARKDOWN_H

#include "layout.h"
#include "text_layout.h"
#include <lightvg/types.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- AST node types ---------------------------------------------------- */

typedef enum {
    LUI_MD_DOCUMENT = 0,
    LUI_MD_PARAGRAPH,
    LUI_MD_HEADING,        /* level 1-6 */
    LUI_MD_BLOCKQUOTE,
    LUI_MD_CODE_BLOCK,     /* fenced or indented */
    LUI_MD_LIST,           /* ordered or unordered */
    LUI_MD_LIST_ITEM,
    LUI_MD_THEMATIC_BREAK, /* horizontal rule */
    LUI_MD_TABLE,          /* GFM table */
    LUI_MD_TABLE_ROW,      /* table row */
    LUI_MD_TABLE_CELL,     /* table cell */
    /* Inline nodes */
    LUI_MD_TEXT,           /* literal text run */
    LUI_MD_EMPHASIS,       /* *italic* */
    LUI_MD_STRONG,         /* **bold** */
    LUI_MD_UNDERLINE,      /* <u>underline</u> */
    LUI_MD_CODE_SPAN,      /* `inline code` */
    LUI_MD_LINK,           /* [text](url) */
    LUI_MD_IMAGE,          /* ![alt](url) */
    LUI_MD_SOFTBREAK,      /* soft line break */
    LUI_MD_HARDBREAK,      /* hard line break */
} lui_md_type_t;

typedef struct lui_md_node lui_md_node_t;

struct lui_md_node {
    lui_md_type_t    type;
    lui_md_node_t   *parent;
    lui_md_node_t   *first_child;
    lui_md_node_t   *last_child;
    lui_md_node_t   *next_sibling;

    /* Text content (TEXT, CODE_BLOCK, CODE_SPAN) — points into doc->source */
    const char      *text;
    int              text_len;

    /* Heading level (1-6) */
    int              heading_level;

    /* List metadata */
    bool             list_ordered;
    int              list_start;     /* starting number for ordered lists */

    /* Link/image URL */
    const char      *url;
    int              url_len;

    /* Table cell alignment (0=left, 1=center, 2=right) */
    uint8_t          align;

    /* Pre-computed display width (for table cells, filled by parser) */
    int              content_width;
};

/* ---- Document (parser output) ------------------------------------------ */

typedef struct {
    lui_md_node_t   *root;
    lui_md_node_t   *nodes;      /* arena: flat array of all nodes */
    int              node_count;
    int              node_cap;
    char            *source;     /* owned copy of source markdown */
    int              source_len;
} lui_md_doc_t;

/**
 * Parse a CommonMark subset from UTF-8 source text.
 * The document owns a copy of @src.
 * Returns 0 on success, -1 on allocation failure.
 */
int lui_md_parse(lui_md_doc_t *doc, const char *src, int len);

/** Free all storage owned by the document. */
void lui_md_destroy(lui_md_doc_t *doc);

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

    int          blockquote_bar_width;   /* vertical bar width (3) */
    int          blockquote_indent;      /* left indent (16) */
    int          list_indent;            /* per-level indent (16) */
    int          code_block_padding;     /* padding inside code blocks (6) */
    int          paragraph_spacing;      /* vertical gap between blocks (10) */
    int          heading_spacing_above;  /* extra space above headings (6) */
    int          heading_spacing_below;  /* extra space below headings (4) */
} lui_md_style_t;

/** Initialise a style with dark-theme defaults. */
void lui_md_style_default(lui_md_style_t *style);

/* ---- Block decoration (drawn by widget after text layout) -------------- */

typedef enum {
    LUI_MD_DECO_BLOCKQUOTE_BAR,
    LUI_MD_DECO_HR,
    LUI_MD_DECO_CODE_BLOCK_BG,
    LUI_MD_DECO_TABLE_BORDER,
} lui_md_deco_type_t;

typedef struct {
    lui_md_deco_type_t type;
    int  span_start;   /* first layout span index */
    int  span_end;     /* exclusive */
    int  indent;       /* left indent for blockquote bar */
    lvg_color_t color;
} lui_md_deco_t;

/* ---- Renderer ---------------------------------------------------------- */

/** Callback to resolve image URLs to surfaces. */
typedef const lvg_surface_t *(*lui_md_resolve_image_fn)(
    const char *url, int url_len, void *user);

/** Render state (populated by lui_md_render, consumed by widget draw). */
typedef struct {
    lui_md_deco_t *decos;
    int            deco_count;
    int            deco_cap;
} lui_md_render_state_t;

/**
 * Render a parsed markdown document into a text layout.
 *
 * Clears @tl, then populates it with styled spans.
 * After this call, lui_text_layout_build(tl) produces the geometry.
 *
 * @tl             Initialised text layout (font + max_width set).
 * @doc            Parsed markdown document.
 * @style          Rendering style (NULL = defaults).
 * @rs             Render state for block decorations (may be NULL).
 * @resolve_image  Image resolver callback (NULL = skip images).
 * @resolve_user   User data for the resolver.
 */
void lui_md_render(lui_text_layout_t *tl,
                   const lui_md_doc_t *doc,
                   const lui_md_style_t *style,
                   lui_md_render_state_t *rs,
                   lui_md_resolve_image_fn resolve_image,
                   void *resolve_user);

/** Free render state storage. */
void lui_md_render_state_destroy(lui_md_render_state_t *rs);

/* ---- Link region (for hit-testing) ------------------------------------- */

typedef struct {
    lvg_rect_t   rect;
    const char  *url;
    int          url_len;
} lui_md_link_region_t;

/* ---- Widget ------------------------------------------------------------ */

typedef void (*lui_md_link_fn)(const char *url, int url_len, void *user);

typedef struct {
    lui_widget_t           widget;

    /* Content */
    lui_md_doc_t           doc;
    lui_text_layout_t      layout;
    lui_md_render_state_t  render_state;
    bool                   layout_dirty;

    /* Link regions for hit testing */
    lui_md_link_region_t  *link_regions;
    int                    link_region_count;
    int                    link_region_cap;

    /* Scroll */
    int                    scroll_y;
    int                    content_height;

    /* Appearance */
    lui_md_style_t         style;
    lvg_color_t            bg;
    lvg_color_t            scrollbar_color;
    int                    scrollbar_width;

    /* Font */
    lui_font_t            *font;

    /* Callbacks */
    lui_md_resolve_image_fn resolve_image;
    void                   *resolve_image_user;
    lui_md_link_fn          on_link_click;
    void                   *on_link_user;
} lui_markdown_t;

/** Initialise a markdown widget. @font may be NULL for character-rect mode. */
void lui_markdown_init(lui_markdown_t *md, lui_font_t *font);

/** Destroy the widget (frees doc + layout). */
void lui_markdown_destroy(lui_markdown_t *md);

/**
 * Set the markdown source text.  Parses and invalidates layout.
 * @src is copied internally.
 */
void lui_markdown_set_text(lui_markdown_t *md, const char *src, int len);

/** Scroll to a vertical offset. */
void lui_markdown_scroll_to(lui_markdown_t *md, int y);

/** Get the widget node. */
static inline lui_widget_t *lui_markdown_widget(lui_markdown_t *md) {
    return &md->widget;
}

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_MARKDOWN_H */
