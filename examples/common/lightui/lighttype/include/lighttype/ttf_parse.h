/*
 * src/fonts/ttf_parse.h — Minimal TrueType / CFF font file parser
 *
 * Zero-copy parser for sfnt-wrapped fonts. Takes a raw font file buffer and
 * provides glyph ID lookup (cmap), advance widths (hmtx), outline extraction
 * (glyf for TrueType, CFF charstrings for OpenType-CFF), and optional pair
 * kerning (kern table format 0).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTTYPE_TTF_PARSE_H
#define LIGHTTYPE_TTF_PARSE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Table location ----------------------------------------------------- */

typedef struct {
    uint32_t off;
    uint32_t len;
} ttf_table_t;

/* ---- Font context ------------------------------------------------------- */

typedef struct {
    const uint8_t *data;
    size_t         data_len;

    uint16_t units_per_em;
    uint16_t num_glyphs;
    int16_t  ascent;
    int16_t  descent;
    int16_t  line_gap;
    uint16_t num_h_metrics;

    int      index_to_loc_format;   /* 0 = short, 1 = long */
    int      cmap_format;           /* 4 or 12 */
    uint32_t cmap_subtable_off;

    int      has_glyf;             /* 1 = TrueType glyf, 0 = CFF */

    /* Table locations (validated at init) */
    ttf_table_t tab_head;
    ttf_table_t tab_hhea;
    ttf_table_t tab_maxp;
    ttf_table_t tab_os2;
    ttf_table_t tab_cmap;
    ttf_table_t tab_hmtx;
    ttf_table_t tab_loca;
    ttf_table_t tab_glyf;
    ttf_table_t tab_cff;
    ttf_table_t tab_kern;
    ttf_table_t tab_gpos;

    /* Cached GPOS kern lookup (PairPos subtables) */
    uint32_t gpos_kern_off[4];    /* offsets of PairPos subtables (0=unused) */
    int      gpos_kern_count;     /* number of cached PairPos subtables */

    /* CFF-specific cached offsets */
    uint32_t cff_char_strings_off;  /* offset of CharStrings INDEX in data */
    uint32_t cff_gsubr_off;         /* Global Subr INDEX offset (0=none) */
    uint32_t cff_lsubr_off;         /* Local Subr INDEX offset (0=none) */
    int      cff_gsubr_count;
    int      cff_lsubr_count;
    int      cff_gsubr_bias;
    int      cff_lsubr_bias;
    int      cff_default_width;
    int      cff_nominal_width;

    /* CID-keyed CFF support (FDArray + FDSelect) */
    int      cff_is_cid;
    uint32_t cff_fd_select_off;     /* absolute offset of FDSelect in data */
    int      cff_fd_select_format;  /* 0 or 3 */
    int      cff_num_fds;

#define CFF_MAX_FDS 64
    struct {
        uint32_t lsubr_off;
        int      lsubr_count;
        int      lsubr_bias;
        int      default_width;
        int      nominal_width;
    } cff_fd[CFF_MAX_FDS];
} ttf_font_t;

/* ---- Outline data ------------------------------------------------------- */

typedef struct {
    float x, y;
    int   on_curve;  /* 1 = on-curve, 0 = quadratic control, 2 = cubic control */
} ttf_point_t;

typedef struct {
    ttf_point_t *points;
    int         *contour_ends;
    int          num_points;
    int          num_contours;
    int16_t      x_min, y_min, x_max, y_max;
} ttf_outline_t;

/* ---- API ---------------------------------------------------------------- */

/*
 * Parse the sfnt table directory and required tables.
 * data/len must remain valid for the lifetime of the font.
 * Returns 0 on success, -1 on failure.
 */
int ttf_font_init(ttf_font_t *font, const uint8_t *data, size_t len);

/*
 * Look up a Unicode codepoint in the cmap table.
 * Returns the glyph ID, or 0 (.notdef) if not found.
 */
uint16_t ttf_cmap_lookup(const ttf_font_t *font, uint32_t codepoint);

/*
 * Get the horizontal advance width for a glyph ID (in font units).
 */
int ttf_hmtx_advance(const ttf_font_t *font, uint16_t glyph_id);

/*
 * Get the left side bearing for a glyph ID (in font units).
 */
int ttf_hmtx_lsb(const ttf_font_t *font, uint16_t glyph_id);

/*
 * Extract the outline for a glyph ID.
 * The caller must call ttf_outline_free() when done.
 * Returns 0 on success, -1 on failure (e.g. no outline for space).
 */
int ttf_glyph_outline(const ttf_font_t *font, uint16_t glyph_id,
                       ttf_outline_t *out);

/*
 * Free outline data allocated by ttf_glyph_outline().
 */
void ttf_outline_free(ttf_outline_t *out);

/*
 * Look up pair kerning adjustment between two glyph IDs (font units).
 * Checks both the kern table and GPOS PairPos (kern feature).
 * Returns 0 if no kerning data or no entry for this pair.
 */
int ttf_kern_lookup(const ttf_font_t *font, uint16_t left, uint16_t right);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTTYPE_TTF_PARSE_H */
