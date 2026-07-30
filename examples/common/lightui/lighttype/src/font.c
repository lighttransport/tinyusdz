/*
 * lighttype/src/font.c — CPU font shaping and rendering (dispatch layer)
 *
 * Architecture:
 *   Custom TTF/CFF parser (ttf_parse.c) + rasterizer (rasterize.c)
 *   Shared glyph cache infrastructure (open-addressing hash + LRU + atlas)
 *   Shared sRGB LUT and blit_glyph
 *   Optional FreeType/HarfBuzz backend (font_freetype.c)
 *   Backend dispatch: all public functions check active_backend
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "font_internal.h"
#include <lighttype/rasterize.h>

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* =========================================================================
 * sRGB <-> linear colour-space LUTs (shared with font_freetype.c)
 * ========================================================================= */

uint16_t ltt__s2l[256];
uint8_t  ltt__l2s[4097];
int      ltt__lut_ready = 0;

void ltt__lut_init(void)
{
    if (ltt__lut_ready) return;
    for (int i = 0; i < 256; i++) {
        double v = i / 255.0;
        v = (v <= 0.04045) ? v / 12.92 : pow((v + 0.055) / 1.055, 2.4);
        ltt__s2l[i] = (uint16_t)(v * 4096.0 + 0.5);
    }
    for (int i = 0; i <= 4096; i++) {
        double v = i / 4096.0;
        v = (v <= 0.0031308) ? v * 12.92 : 1.055 * pow(v, 1.0 / 2.4) - 0.055;
        ltt__l2s[i] = (uint8_t)(v * 255.0 + 0.5);
    }
    ltt__lut_ready = 1;
}

/*
 * Blend one 8-bit channel: composite src_chan over dst_chan with coverage
 * alpha (0-255).  All arithmetic is in 12-bit linear space.
 */
static inline uint8_t blend_chan(uint8_t dst, uint8_t src, unsigned alpha)
{
    unsigned inv = 255u - alpha;
    unsigned lin = ((unsigned)ltt__s2l[src] * alpha +
                    (unsigned)ltt__s2l[dst] * inv + 127u) / 255u;
    if (lin > 4096u) lin = 4096u;
    return ltt__l2s[lin];
}

/* =========================================================================
 * Glyph cache (shared implementation used by both backends)
 * ========================================================================= */

/* ---- Hash table helpers ------------------------------------------------- */

static int cache_probe(ltt_glyph_cache_t *c, uint32_t glyph_id)
{
    int base = (int)((glyph_id * 2654435761u) & (uint32_t)LTT_CACHE_MASK);
    for (int i = 0; i < LTT_CACHE_SIZE; i++) {
        int s = (base + i) & LTT_CACHE_MASK;
        if (!c->slots[s].occupied)  return LTT_SLOT_EMPTY(s);
        if (c->slots[s].glyph_id == glyph_id) return s;
    }
    return LTT_SLOT_EMPTY(LTT_CACHE_SIZE);
}

/* ---- LRU helpers -------------------------------------------------------- */

static void lru_prepend(ltt_glyph_cache_t *c, int idx)
{
    ltt_glyph_entry_t *e = &c->slots[idx];
    e->lru_prev = -1;
    e->lru_next = (int16_t)c->lru_head;
    if (c->lru_head >= 0)
        c->slots[c->lru_head].lru_prev = (int16_t)idx;
    c->lru_head = idx;
    if (c->lru_tail < 0)
        c->lru_tail = idx;
}

static void lru_unlink(ltt_glyph_cache_t *c, int idx)
{
    ltt_glyph_entry_t *e = &c->slots[idx];
    if (e->lru_prev >= 0)
        c->slots[e->lru_prev].lru_next = e->lru_next;
    else
        c->lru_head = e->lru_next;
    if (e->lru_next >= 0)
        c->slots[e->lru_next].lru_prev = e->lru_prev;
    else
        c->lru_tail = e->lru_prev;
    e->lru_prev = e->lru_next = -1;
}

static void lru_touch(ltt_glyph_cache_t *c, int idx)
{
    if (c->lru_head == idx) return;
    lru_unlink(c, idx);
    lru_prepend(c, idx);
}

/* ---- Atlas shelf packer ------------------------------------------------- */

static uint8_t *atlas_alloc(ltt_glyph_cache_t *c, int w, int h)
{
    int pw = w + LTT_ATLAS_PAD;
    int ph = h + LTT_ATLAS_PAD;

    if (c->shelf_x + pw > LTT_ATLAS_DIM) {
        c->shelf_y += c->shelf_h + LTT_ATLAS_PAD;
        c->shelf_x  = 0;
        c->shelf_h  = 0;
    }
    if (c->shelf_y + ph > LTT_ATLAS_DIM)
        return NULL;

    uint8_t *ptr = &c->atlas[c->shelf_y * LTT_ATLAS_DIM + c->shelf_x];
    if (h > c->shelf_h) c->shelf_h = h;
    c->shelf_x += pw;
    return ptr;
}

/* ---- Cache lifecycle (exported for font_freetype.c) --------------------- */

void ltt__cache_init(ltt_glyph_cache_t *c)
{
    memset(c, 0, sizeof(*c));
    c->lru_head = -1;
    c->lru_tail = -1;
}

void ltt__cache_flush(ltt_glyph_cache_t *c)
{
    memset(c->slots, 0, sizeof(c->slots));
    c->count    = 0;
    c->lru_head = -1;
    c->lru_tail = -1;
    c->shelf_x  = 0;
    c->shelf_y  = 0;
    c->shelf_h  = 0;
}

ltt_glyph_entry_t *ltt__cache_lookup(ltt_glyph_cache_t *c, uint32_t glyph_id)
{
    int s = cache_probe(c, glyph_id);
    if (s < 0) return NULL;
    lru_touch(c, s);
    return &c->slots[s];
}

ltt_glyph_entry_t *ltt__cache_insert(ltt_glyph_cache_t *c,
                                      uint32_t glyph_id,
                                      const uint8_t *bitmap,
                                      int bm_w, int bm_h,
                                      int bearing_x, int bearing_y,
                                      int advance_x)
{
    if (c->count >= LTT_CACHE_MAX_FILL)
        ltt__cache_flush(c);

    uint8_t *ptr = atlas_alloc(c, bm_w, bm_h);
    if (!ptr) {
        ltt__cache_flush(c);
        ptr = atlas_alloc(c, bm_w, bm_h);
        if (!ptr) return NULL;
    }

    for (int row = 0; row < bm_h; row++) {
        memcpy(ptr + row * LTT_ATLAS_DIM,
               bitmap + row * bm_w,
               (size_t)bm_w);
    }

    int raw = cache_probe(c, glyph_id);
    int slot = (raw >= 0) ? raw : ~raw;
    if (slot >= LTT_CACHE_SIZE) return NULL;

    ltt_glyph_entry_t *e = &c->slots[slot];
    e->glyph_id  = glyph_id;
    e->occupied  = 1;
    e->atlas_ptr = ptr;
    e->width     = (int16_t)bm_w;
    e->height    = (int16_t)bm_h;
    e->bearing_x = (int16_t)bearing_x;
    e->bearing_y = (int16_t)bearing_y;
    e->advance_x = (int16_t)advance_x;
    e->lru_prev  = -1;
    e->lru_next  = -1;
    c->count++;
    lru_prepend(c, slot);
    return e;
}

/* =========================================================================
 * Blit glyph (shared, exported for font_freetype.c)
 * ========================================================================= */

void ltt__blit_glyph(ltt_target_t             *target,
                     int pen_x, int pen_y,
                     const ltt_glyph_entry_t  *g,
                     uint8_t tr, uint8_t tg, uint8_t tb,
                     unsigned text_alpha,
                     unsigned lin_src_r, unsigned lin_src_g, unsigned lin_src_b)
{
    if (!g->atlas_ptr || g->width <= 0 || g->height <= 0) return;

    int gx = pen_x + g->bearing_x;
    int gy = pen_y - g->bearing_y;

    const ltt_rect_t *clip = &target->clip;
    int x0 = gx > clip->x ? gx : clip->x;
    int y0 = gy > clip->y ? gy : clip->y;
    int x1 = (gx + g->width)  < (clip->x + clip->width)  ?
             (gx + g->width)  : (clip->x + clip->width);
    int y1 = (gy + g->height) < (clip->y + clip->height) ?
             (gy + g->height) : (clip->y + clip->height);
    if (x0 >= x1 || y0 >= y1) return;

    int bx = x0 - gx;
    int by = y0 - gy;
    int bw = x1 - x0;
    int bh = y1 - y0;

    for (int row = 0; row < bh; row++) {
        const uint8_t *src =
            g->atlas_ptr + (by + row) * LTT_ATLAS_DIM + bx;
        uint32_t *dst =
            &target->pixels[(y0 + row) * target->stride + x0];

        for (int col = 0; col < bw; col++) {
            unsigned coverage = *src++;
            if (coverage == 0) { dst++; continue; }

            if (text_alpha < 255u)
                coverage = (coverage * text_alpha + 127u) / 255u;
            if (coverage == 0) { dst++; continue; }

            /* Fast path: fully opaque pixel — direct store, no blend. */
            if (coverage >= 255u) {
                *dst++ = (0xFFu << 24) |
                         ((uint32_t)tr << 16) |
                         ((uint32_t)tg <<  8) |
                          (uint32_t)tb;
                continue;
            }

            uint32_t d  = *dst;
            uint8_t  dr = (uint8_t)(d >> 16);
            uint8_t  dg = (uint8_t)(d >>  8);
            uint8_t  db = (uint8_t)(d);

            /* Pre-computed linear source from parameters */
            unsigned inv = 255u - coverage;
            unsigned lr = (lin_src_r * coverage + (unsigned)ltt__s2l[dr] * inv + 127u) / 255u;
            unsigned lg = (lin_src_g * coverage + (unsigned)ltt__s2l[dg] * inv + 127u) / 255u;
            unsigned lb = (lin_src_b * coverage + (unsigned)ltt__s2l[db] * inv + 127u) / 255u;

            *dst++ = (0xFFu << 24) |
                     ((uint32_t)ltt__l2s[lr] << 16) |
                     ((uint32_t)ltt__l2s[lg] <<  8) |
                      (uint32_t)ltt__l2s[lb];
        }
    }
}

/* =========================================================================
 * Internal — UTF-8 decoding
 * ========================================================================= */

static uint32_t utf8_decode(const char **p, const char *end)
{
    const uint8_t *s = (const uint8_t *)*p;
    if (s >= (const uint8_t *)end) return 0;

    uint32_t cp;
    int extra;
    uint8_t b = *s++;

    if (b < 0x80) {
        cp = b; extra = 0;
    } else if (b < 0xC0) {
        *p = (const char *)s;
        return 0xFFFD;
    } else if (b < 0xE0) {
        cp = b & 0x1F; extra = 1;
    } else if (b < 0xF0) {
        cp = b & 0x0F; extra = 2;
    } else if (b < 0xF8) {
        cp = b & 0x07; extra = 3;
    } else {
        *p = (const char *)s;
        return 0xFFFD;
    }

    for (int i = 0; i < extra; i++) {
        if (s >= (const uint8_t *)end || (*s & 0xC0) != 0x80) {
            *p = (const char *)s;
            return 0xFFFD;
        }
        cp = (cp << 6) | (*s++ & 0x3F);
    }

    *p = (const char *)s;
    return cp;
}

/* =========================================================================
 * Internal — simple shaping (custom backend)
 * ========================================================================= */

static void simple_shape(ltt_font_t *f, const char *utf8, int len,
                          ltt_shape_result_t *sr)
{
    sr->count = 0;
    sr->heap_alloc = 0;
    sr->glyph_ids = sr->gid_stack;
    sr->advances  = sr->adv_stack;

    const char *p   = utf8;
    const char *end = utf8 + len;
    int idx = 0;
    int capacity = SHAPE_STACK_MAX;
    uint16_t prev_gid = 0;
    int prev_advance = 0;

    while (p < end) {
        /* Grow buffers when stack capacity exhausted. */
        if (idx >= capacity) {
            int new_cap = capacity * 2;
            uint16_t *new_gids = (uint16_t *)malloc((size_t)new_cap * sizeof(uint16_t));
            int16_t  *new_advs = (int16_t *)malloc((size_t)new_cap * sizeof(int16_t));
            if (!new_gids || !new_advs) {
                free(new_gids);
                free(new_advs);
                break;  /* partial result */
            }
            memcpy(new_gids, sr->glyph_ids, (size_t)idx * sizeof(uint16_t));
            memcpy(new_advs, sr->advances,  (size_t)idx * sizeof(int16_t));
            if (sr->heap_alloc)
                free(sr->glyph_ids);
            if (sr->heap_alloc)
                free(sr->advances);
            sr->glyph_ids  = new_gids;
            sr->advances   = new_advs;
            sr->heap_alloc = 1;
            capacity = new_cap;
        }

        uint32_t cp = utf8_decode(&p, end);

        uint16_t gid;
        int advance;
        ltt_advance_entry_t *ace =
            &f->advance_cache[cp & LTT_ADVANCE_CACHE_MASK];
        if (ace->codepoint == cp) {
            gid     = ace->glyph_id;
            advance = ace->advance;
        } else {
            gid     = ttf_cmap_lookup(&f->ttf, cp);
            advance = ttf_hmtx_advance(&f->ttf, gid);
            ace->codepoint = cp;
            ace->glyph_id  = gid;
            ace->advance   = (int16_t)advance;
        }

        if (idx > 0 && prev_gid != 0) {
            int kern = ttf_kern_lookup(&f->ttf, prev_gid, gid);
            if (kern != 0) {
                int combined = prev_advance + kern;
                sr->advances[idx - 1] =
                    (int16_t)((int)(combined * f->scale * 64.0f + 0.5f) >> 6);
            }
        }

        int px_advance = (int)(advance * f->scale * 64.0f + 0.5f) >> 6;

        sr->glyph_ids[idx] = gid;
        sr->advances[idx]  = (int16_t)px_advance;
        prev_gid = gid;
        prev_advance = advance;
        idx++;
    }
    sr->count = idx;
}

/* Direct-accumulation measure — no array allocation, no glyph IDs stored.
 * ~30% faster than simple_shape + sum for the common non-kerning case. */
static int simple_measure(ltt_font_t *f, const char *utf8, int len)
{
    const char *end = utf8 + len;
    int total = 0;
    uint16_t prev_gid = 0;
    int prev_adv_funits = 0;
    int prev_rounded = 0;

    while (utf8 < end) {
        uint32_t cp = utf8_decode(&utf8, end);

        uint16_t gid;
        int advance;
        ltt_advance_entry_t *ace =
            &f->advance_cache[cp & LTT_ADVANCE_CACHE_MASK];
        if (ace->codepoint == cp) {
            gid     = ace->glyph_id;
            advance = ace->advance;
        } else {
            gid     = ttf_cmap_lookup(&f->ttf, cp);
            advance = ttf_hmtx_advance(&f->ttf, gid);
            ace->codepoint = cp;
            ace->glyph_id  = gid;
            ace->advance   = (int16_t)advance;
        }

        int px = (int)(advance * f->scale * 64.0f + 0.5f) >> 6;

        if (prev_gid != 0) {
            int kern = ttf_kern_lookup(&f->ttf, prev_gid, gid);
            if (kern != 0) {
                /* Remove previous unkerned advance, add kerned advance */
                total -= prev_rounded;
                total += (int)((prev_adv_funits + kern) * f->scale * 64.0f + 0.5f) >> 6;
            }
        }

        total += px;
        prev_gid = gid;
        prev_adv_funits = advance;
        prev_rounded = px;
    }
    return total;
}

static void shape_free(ltt_shape_result_t *sr)
{
    if (sr->heap_alloc) {
        free(sr->glyph_ids);
        free(sr->advances);
    }
    sr->glyph_ids = NULL;
    sr->advances  = NULL;
}

/* =========================================================================
 * Internal — rasterize and cache a glyph (custom backend)
 * ========================================================================= */

static ltt_glyph_entry_t *rasterize_and_cache(ltt_font_t *f, uint16_t glyph_id)
{
    ttf_outline_t outline;
    if (ttf_glyph_outline(&f->ttf, glyph_id, &outline) != 0) {
        int advance = ttf_hmtx_advance(&f->ttf, glyph_id);
        int px_advance = (int)(advance * f->scale * 64.0f + 0.5f) >> 6;
        uint8_t dummy = 0;
        return ltt__cache_insert(&f->cache_custom, (uint32_t)glyph_id,
                                  &dummy, 0, 0, 0, 0, px_advance);
    }

    rast_bitmap_t bm;
    if (rast_glyph(&outline, f->scale, &bm) != 0) {
        ttf_outline_free(&outline);
        int advance = ttf_hmtx_advance(&f->ttf, glyph_id);
        int px_advance = (int)(advance * f->scale * 64.0f + 0.5f) >> 6;
        uint8_t dummy = 0;
        return ltt__cache_insert(&f->cache_custom, (uint32_t)glyph_id,
                                  &dummy, 0, 0, 0, 0, px_advance);
    }
    ttf_outline_free(&outline);

    int advance = ttf_hmtx_advance(&f->ttf, glyph_id);
    int px_advance = (int)(advance * f->scale * 64.0f + 0.5f) >> 6;

    ltt_glyph_entry_t *e = ltt__cache_insert(&f->cache_custom,
                                              (uint32_t)glyph_id,
                                              bm.pixels, bm.width, bm.height,
                                              bm.bearing_x, bm.bearing_y,
                                              px_advance);
    free(bm.pixels);
    return e;
}

/* =========================================================================
 * Helper — get the active cache for the current backend
 * ========================================================================= */

static inline ltt_glyph_cache_t *active_cache(ltt_font_t *f)
{
#ifdef LTT_HAVE_FREETYPE
    if (f->active_backend == LTT_FONT_BACKEND_FREETYPE)
        return &f->cache_ft;
#endif
    return &f->cache_custom;
}

/*
 * Compute font metrics using integer arithmetic matching FreeType 2.13's
 * FT_MulFix (16.16 fixed-point) + FT_PIX_CEIL/FLOOR/ROUND.
 */
static void compute_custom_metrics(ltt_font_t *f)
{
    int upem = f->ttf.units_per_em;
    int64_t y_scale = ((int64_t)f->pixel_size << 22) / upem;
    #define MULFIX(a) ((int)(((int64_t)(a) * y_scale + 0x8000) >> 16))
    #define PIX_CEIL(x)  (((x) + 63) & ~63)
    #define PIX_FLOOR(x) ((x) & ~63)
    #define PIX_ROUND(x) (((x) + 32) & ~63)

    f->ascent      = PIX_CEIL(MULFIX(f->ttf.ascent))  >> 6;
    f->descent     = -(PIX_FLOOR(MULFIX(f->ttf.descent)) >> 6);
    if (f->descent < 0) f->descent = -f->descent;

    int height_fu  = f->ttf.ascent - f->ttf.descent + f->ttf.line_gap;
    f->line_height = PIX_ROUND(MULFIX(height_fu)) >> 6;
    if (f->line_height <= 0)
        f->line_height = f->ascent + f->descent;

    #undef MULFIX
    #undef PIX_CEIL
    #undef PIX_FLOOR
    #undef PIX_ROUND
}

/* =========================================================================
 * Public API — backend switching
 * ========================================================================= */

int ltt_font_set_backend(ltt_font_t *f, ltt_font_backend_t backend)
{
    if (!f) return -1;
    if (backend == LTT_FONT_BACKEND_CUSTOM) {
        f->active_backend = backend;
        compute_custom_metrics(f);
        return 0;
    }
#ifdef LTT_HAVE_FREETYPE
    if (backend == LTT_FONT_BACKEND_FREETYPE) {
        /* Lazy init: initialise FT/HB on first switch */
        if (!f->ft_face) {
            if (ltt__ft_init(f) != 0)
                return -1;
        }
        f->active_backend = LTT_FONT_BACKEND_FREETYPE;
        f->ascent      = f->ft_ascent;
        f->descent     = f->ft_descent;
        f->line_height = f->ft_line_height;
        return 0;
    }
#endif
    (void)backend;
    return -1;
}

ltt_font_backend_t ltt_font_get_backend(const ltt_font_t *f)
{
    if (!f) return LTT_FONT_BACKEND_CUSTOM;
    return f->active_backend;
}

int ltt_font_has_freetype(const ltt_font_t *f)
{
    (void)f;
#ifdef LTT_HAVE_FREETYPE
    return 1;
#else
    return 0;
#endif
}

/* =========================================================================
 * Public API — lifecycle
 * ========================================================================= */

ltt_font_t *ltt_font_create(const char *path, int pixel_size)
{
    ltt__lut_init();

    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (file_size <= 0) { fclose(fp); return NULL; }

    uint8_t *data = (uint8_t *)malloc((size_t)file_size);
    if (!data) { fclose(fp); return NULL; }

    if (fread(data, 1, (size_t)file_size, fp) != (size_t)file_size) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);

    ltt_font_t *f = (ltt_font_t *)calloc(1, sizeof(*f));
    if (!f) { free(data); return NULL; }

    f->font_data     = data;
    f->font_data_len = (size_t)file_size;

    if (ttf_font_init(&f->ttf, data, (size_t)file_size) != 0) {
        free(data);
        free(f);
        return NULL;
    }

    f->pixel_size = pixel_size;
    f->scale      = (float)pixel_size / (float)f->ttf.units_per_em;

    compute_custom_metrics(f);

    f->active_backend = LTT_FONT_BACKEND_CUSTOM;
    ltt__cache_init(&f->cache_custom);

    return f;
}

ltt_font_t *ltt_font_create_from_memory(const uint8_t *data, size_t len,
                                          int pixel_size)
{
    if (!data || len == 0 || pixel_size <= 0) return NULL;
    ltt__lut_init();

    uint8_t *copy = (uint8_t *)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, data, len);

    ltt_font_t *f = (ltt_font_t *)calloc(1, sizeof(*f));
    if (!f) { free(copy); return NULL; }

    f->font_data     = copy;
    f->font_data_len = len;

    if (ttf_font_init(&f->ttf, copy, len) != 0) {
        free(copy);
        free(f);
        return NULL;
    }

    f->pixel_size = pixel_size;
    f->scale      = (float)pixel_size / (float)f->ttf.units_per_em;

    compute_custom_metrics(f);

    f->active_backend = LTT_FONT_BACKEND_CUSTOM;
    ltt__cache_init(&f->cache_custom);

    return f;
}

void ltt_font_destroy(ltt_font_t *f)
{
    if (!f) return;
#ifdef LTT_HAVE_FREETYPE
    ltt__ft_destroy(f);
#endif
    free(f->font_data);
    free(f);
}

int ltt_font_ascent(const ltt_font_t *f)      { return f ? f->ascent      : 0; }
int ltt_font_descent(const ltt_font_t *f)     { return f ? f->descent     : 0; }
int ltt_font_line_height(const ltt_font_t *f) { return f ? f->line_height : 0; }

/* =========================================================================
 * Public API — measure and draw (with backend dispatch)
 * ========================================================================= */

int ltt_font_measure_text(ltt_font_t *f, const char *utf8, int len)
{
    if (!f || !utf8) return 0;
    if (len < 0) len = (int)strlen(utf8);
    if (len == 0) return 0;

#ifdef LTT_HAVE_FREETYPE
    if (f->active_backend == LTT_FONT_BACKEND_FREETYPE) {
        ltt_shape_result_t sr;
        ltt__ft_shape(f, utf8, len, &sr);
        int width = 0;
        for (int i = 0; i < sr.count; i++)
            width += sr.advances[i];
        return width;
    }
#endif
    return simple_measure(f, utf8, len);
}

static void draw_text_internal(ltt_target_t *target,
                                int x, int y,
                                const char *utf8, int len,
                                ltt_font_t *f,
                                ltt_color_t color,
                                int letter_spacing)
{
    if (!target || !f || !utf8) return;
    if (len < 0) len = (int)strlen(utf8);
    if (len == 0) return;

    unsigned text_alpha = LTT_COLOR_A(color);
    if (text_alpha == 0) return;

    uint8_t tr = LTT_COLOR_R(color);
    uint8_t tg = LTT_COLOR_G(color);
    uint8_t tb = LTT_COLOR_B(color);

    /* Pre-computed linear-space source for blend loop */
    unsigned lin_tr = ltt__s2l[tr];
    unsigned lin_tg = ltt__s2l[tg];
    unsigned lin_tb = ltt__s2l[tb];

    ltt_shape_result_t sr;
    ltt_glyph_cache_t *cache;

#ifdef LTT_HAVE_FREETYPE
    if (f->active_backend == LTT_FONT_BACKEND_FREETYPE) {
        ltt__ft_shape(f, utf8, len, &sr);
        cache = &f->cache_ft;
    } else
#endif
    {
        simple_shape(f, utf8, len, &sr);
        cache = &f->cache_custom;
    }

    int pen_x = x;
    int pen_y = y;

    for (int i = 0; i < sr.count; i++) {
        uint16_t gid = sr.glyph_ids[i];

        ltt_glyph_entry_t *e = ltt__cache_lookup(cache, (uint32_t)gid);
        if (!e) {
#ifdef LTT_HAVE_FREETYPE
            if (f->active_backend == LTT_FONT_BACKEND_FREETYPE)
                e = ltt__ft_rasterize_and_cache(f, gid);
            else
#endif
                e = rasterize_and_cache(f, gid);
        }
        if (e)
            ltt__blit_glyph(target, pen_x, pen_y, e, tr, tg, tb, text_alpha,
                           lin_tr, lin_tg, lin_tb);

        pen_x += sr.advances[i] + letter_spacing;
    }

    shape_free(&sr);
}

void ltt_draw_text(ltt_target_t *target,
                    int x, int y,
                    const char *utf8, int len,
                    ltt_font_t *f,
                    ltt_color_t color)
{
    draw_text_internal(target, x, y, utf8, len, f, color, 0);
}

void ltt_draw_text_spaced(ltt_target_t *target,
                            int x, int y,
                            const char *utf8, int len,
                            ltt_font_t *f,
                            ltt_color_t color,
                            int letter_spacing)
{
    draw_text_internal(target, x, y, utf8, len, f, color, letter_spacing);
}

/* =========================================================================
 * Interruptible text rendering
 * ========================================================================= */

void ltt_text_draw_state_reset(ltt_text_draw_state_t *state)
{
    if (!state) return;
    free(state->glyph_ids);
    free(state->advances);
    memset(state, 0, sizeof(*state));
}

int ltt_draw_text_partial(
    ltt_target_t *target, int x, int y,
    const char *utf8, int len,
    ltt_font_t *f, ltt_color_t color,
    ltt_text_draw_state_t *state,
    ltt_deadline_fn deadline, void *deadline_ctx)
{
    if (!target || !f || !utf8 || !state) return 1;
    if (len < 0) len = (int)strlen(utf8);
    if (len == 0) { state->complete = 1; return 1; }

    unsigned text_alpha = LTT_COLOR_A(color);
    if (text_alpha == 0) { state->complete = 1; return 1; }

    /* First call: shape the string and cache results in state */
    if (state->total_glyphs == 0) {
        ltt_shape_result_t sr;

#ifdef LTT_HAVE_FREETYPE
        if (f->active_backend == LTT_FONT_BACKEND_FREETYPE)
            ltt__ft_shape(f, utf8, len, &sr);
        else
#endif
            simple_shape(f, utf8, len, &sr);

        if (sr.count == 0) {
            shape_free(&sr);
            state->complete = 1;
            return 1;
        }

        state->glyph_ids = (uint16_t *)malloc((size_t)sr.count * sizeof(uint16_t));
        state->advances  = (int16_t *)malloc((size_t)sr.count * sizeof(int16_t));
        if (!state->glyph_ids || !state->advances) {
            free(state->glyph_ids);
            free(state->advances);
            state->glyph_ids = NULL;
            state->advances  = NULL;
            shape_free(&sr);
            state->complete = 1;
            return 1;
        }
        memcpy(state->glyph_ids, sr.glyph_ids, (size_t)sr.count * sizeof(uint16_t));
        memcpy(state->advances, sr.advances, (size_t)sr.count * sizeof(int16_t));
        state->total_glyphs = sr.count;
        state->glyph_index = 0;
        state->pen_x = x;
        state->pen_y = y;

        shape_free(&sr);
    }

    uint8_t tr = LTT_COLOR_R(color);
    uint8_t tg = LTT_COLOR_G(color);
    uint8_t tb = LTT_COLOR_B(color);

    unsigned lin_tr = ltt__s2l[tr];
    unsigned lin_tg = ltt__s2l[tg];
    unsigned lin_tb = ltt__s2l[tb];

    ltt_glyph_cache_t *cache = active_cache(f);

    int i = state->glyph_index;
    int pen_x = state->pen_x;
    int pen_y = state->pen_y;

    while (i < state->total_glyphs) {
        uint16_t gid = state->glyph_ids[i];

        ltt_glyph_entry_t *e = ltt__cache_lookup(cache, (uint32_t)gid);
        if (!e) {
#ifdef LTT_HAVE_FREETYPE
            if (f->active_backend == LTT_FONT_BACKEND_FREETYPE)
                e = ltt__ft_rasterize_and_cache(f, gid);
            else
#endif
                e = rasterize_and_cache(f, gid);
        }
        if (e)
            ltt__blit_glyph(target, pen_x, pen_y, e, tr, tg, tb, text_alpha,
                           lin_tr, lin_tg, lin_tb);

        pen_x += state->advances[i];
        i++;

        if (deadline && (i & 3) == 0 && deadline(deadline_ctx)) {
            state->glyph_index = i;
            state->pen_x = pen_x;
            state->pen_y = pen_y;
            return 0;
        }
    }

    state->glyph_index = i;
    state->pen_x = pen_x;
    state->pen_y = pen_y;
    state->complete = 1;
    return 1;
}
