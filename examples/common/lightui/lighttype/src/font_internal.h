/*
 * src/fonts/font_internal.h — Shared internal definitions for font backends
 *
 * Extracted from font.c to allow the FreeType/HarfBuzz backend
 * (font_freetype.c) to share cache types, LUT externs, and the
 * extended ltt_font_s struct.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTTYPE_FONT_INTERNAL_H
#define LIGHTTYPE_FONT_INTERNAL_H

#include <lighttype/font.h>
#include <lighttype/ttf_parse.h>

#include <stdint.h>

/* =========================================================================
 * Compile-time constants
 * ========================================================================= */

#define LTT_CACHE_SIZE      512
#define LTT_CACHE_MASK      (LTT_CACHE_SIZE - 1)
#define LTT_CACHE_MAX_FILL  384

/* Direct-mapped advance cache for simple_measure / simple_shape.
 * 128 entries → each ASCII codepoint gets its own slot; no eviction for
 * plain-text / markdown documents.  Codepoint==0 marks an empty slot. */
#define LTT_ADVANCE_CACHE_SIZE  128
#define LTT_ADVANCE_CACHE_MASK  (LTT_ADVANCE_CACHE_SIZE - 1)

typedef struct {
    uint32_t  codepoint;
    uint16_t  glyph_id;
    int16_t   advance;       /* unscaled font units */
} ltt_advance_entry_t;

#define LTT_ATLAS_DIM       512
#define LTT_ATLAS_PAD       1

#define LTT_SLOT_EMPTY(s)   (~(s))

/* =========================================================================
 * sRGB <-> linear LUTs (defined in font.c, used by both backends)
 * ========================================================================= */

extern uint16_t ltt__s2l[256];
extern uint8_t  ltt__l2s[4097];
extern int      ltt__lut_ready;

void ltt__lut_init(void);

/* =========================================================================
 * Glyph cache types
 * ========================================================================= */

typedef struct {
    uint32_t  glyph_id;
    int       occupied;
    uint8_t  *atlas_ptr;
    int16_t   width, height;
    int16_t   bearing_x, bearing_y;
    int16_t   advance_x;
    int16_t   lru_prev, lru_next;
} ltt_glyph_entry_t;

typedef struct {
    ltt_glyph_entry_t slots[LTT_CACHE_SIZE];
    int               count;
    int               lru_head;
    int               lru_tail;
    uint8_t           atlas[LTT_ATLAS_DIM * LTT_ATLAS_DIM];
    int               shelf_x;
    int               shelf_y;
    int               shelf_h;
} ltt_glyph_cache_t;

/* =========================================================================
 * Cache operations (defined in font.c)
 * ========================================================================= */

void              ltt__cache_init(ltt_glyph_cache_t *c);
void              ltt__cache_flush(ltt_glyph_cache_t *c);
ltt_glyph_entry_t *ltt__cache_lookup(ltt_glyph_cache_t *c, uint32_t glyph_id);
ltt_glyph_entry_t *ltt__cache_insert(ltt_glyph_cache_t *c,
                                      uint32_t glyph_id,
                                      const uint8_t *bitmap,
                                      int bm_w, int bm_h,
                                      int bearing_x, int bearing_y,
                                      int advance_x);

/* =========================================================================
 * Shaping result
 * ========================================================================= */

#define SHAPE_STACK_MAX 256

typedef struct {
    uint16_t *glyph_ids;
    int16_t  *advances;
    int       count;
    uint16_t  gid_stack[SHAPE_STACK_MAX];
    int16_t   adv_stack[SHAPE_STACK_MAX];
    int       heap_alloc;
} ltt_shape_result_t;

/* =========================================================================
 * Font context — extended with dual-backend state
 * ========================================================================= */

struct ltt_font_s {
    uint8_t          *font_data;
    size_t            font_data_len;
    ttf_font_t        ttf;
    float             scale;
    int               pixel_size;
    int               ascent;
    int               descent;
    int               line_height;

    /* Active backend */
    ltt_font_backend_t active_backend;

    /* Advance cache — direct-mapped, codepoint-indexed.  calloc-initialised
     * to zero means every slot starts empty (codepoint == 0). */
    ltt_advance_entry_t advance_cache[LTT_ADVANCE_CACHE_SIZE];

    /* Custom backend cache (always present) */
    ltt_glyph_cache_t  cache_custom;

#ifdef LTT_HAVE_FREETYPE
    /* FreeType/HarfBuzz backend state — opaque pointers to avoid header
     * pollution. Actual types (FT_Face, hb_font_t*) are cast in
     * font_freetype.c only. */
    void              *ft_face;      /* FT_Face */
    void              *ft_library;   /* FT_Library */
    void              *hb_font;      /* hb_font_t* */
    ltt_glyph_cache_t  cache_ft;
    int                ft_ascent;
    int                ft_descent;
    int                ft_line_height;
#endif
};

/* =========================================================================
 * Blit function (shared by both backends, defined in font.c)
 * ========================================================================= */

void ltt__blit_glyph(ltt_target_t             *target,
                     int pen_x, int pen_y,
                     const ltt_glyph_entry_t  *g,
                     uint8_t tr, uint8_t tg, uint8_t tb,
                     unsigned text_alpha,
                     unsigned lin_src_r, unsigned lin_src_g, unsigned lin_src_b);

/* =========================================================================
 * FreeType backend functions (defined in font_freetype.c)
 * ========================================================================= */

#ifdef LTT_HAVE_FREETYPE
int  ltt__ft_init(ltt_font_t *f);
void ltt__ft_destroy(ltt_font_t *f);
void ltt__ft_shape(ltt_font_t *f, const char *utf8, int len,
                   ltt_shape_result_t *sr);
ltt_glyph_entry_t *ltt__ft_rasterize_and_cache(ltt_font_t *f, uint16_t glyph_id);
#endif

#endif /* LIGHTTYPE_FONT_INTERNAL_H */
