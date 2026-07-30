/*
 * src/fonts/ttf_parse.c — Minimal TrueType / CFF font file parser
 *
 * Zero-copy: all reads are validated offsets into the original file buffer.
 * Supports sfnt-wrapped fonts with TrueType (glyf) or CFF outlines.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lighttype/ttf_parse.h>

#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * Safe byte-reading helpers (big-endian)
 * ========================================================================= */

static inline int safe_off(const ttf_font_t *f, uint32_t off, uint32_t need)
{
    return (off <= f->data_len && need <= f->data_len - off);
}

static inline uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static inline int16_t rd16s(const uint8_t *p)
{
    return (int16_t)rd16(p);
}

static inline uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |  (uint32_t)p[3];
}

static inline uint32_t tag4(const char *s)
{
    return ((uint32_t)(uint8_t)s[0] << 24) | ((uint32_t)(uint8_t)s[1] << 16) |
           ((uint32_t)(uint8_t)s[2] <<  8) |  (uint32_t)(uint8_t)s[3];
}

/* =========================================================================
 * Table directory parsing
 * ========================================================================= */

static int find_table(const ttf_font_t *font, uint32_t tag, ttf_table_t *out)
{
    if (!safe_off(font, 0, 12)) return -1;

    uint16_t num_tables = rd16(font->data + 4);
    if (!safe_off(font, 12, (uint32_t)num_tables * 16)) return -1;

    const uint8_t *entry = font->data + 12;
    for (int i = 0; i < num_tables; i++, entry += 16) {
        if (rd32(entry) == tag) {
            out->off = rd32(entry + 8);
            out->len = rd32(entry + 12);
            /* Validate the table fits in the file */
            if (!safe_off(font, out->off, out->len)) return -1;
            return 0;
        }
    }
    return -1; /* table not found */
}

/* =========================================================================
 * Parse head table
 * ========================================================================= */

static int parse_head(ttf_font_t *font)
{
    if (font->tab_head.len < 54) return -1;
    const uint8_t *h = font->data + font->tab_head.off;
    font->units_per_em        = rd16(h + 18);
    font->index_to_loc_format = rd16s(h + 50);
    return (font->units_per_em > 0) ? 0 : -1;
}

/* =========================================================================
 * Parse hhea table
 * ========================================================================= */

static int parse_hhea(ttf_font_t *font)
{
    if (font->tab_hhea.len < 36) return -1;
    const uint8_t *h = font->data + font->tab_hhea.off;
    font->ascent         = rd16s(h + 4);
    font->descent        = rd16s(h + 6);
    font->line_gap       = rd16s(h + 8);
    font->num_h_metrics  = rd16(h + 34);
    return 0;
}

/* =========================================================================
 * Parse maxp table
 * ========================================================================= */

static int parse_maxp(ttf_font_t *font)
{
    if (font->tab_maxp.len < 6) return -1;
    const uint8_t *h = font->data + font->tab_maxp.off;
    font->num_glyphs = rd16(h + 4);
    return (font->num_glyphs > 0) ? 0 : -1;
}

/* =========================================================================
 * Parse OS/2 table (optional, preferred metrics)
 * ========================================================================= */

static void parse_os2(ttf_font_t *font)
{
    if (font->tab_os2.len < 72) return;
    const uint8_t *h = font->data + font->tab_os2.off;

    /*
     * OS/2 fsSelection bit 7 (USE_TYPO_METRICS) indicates the font
     * prefers sTypo* metrics for line spacing.  When this bit is NOT
     * set, the hhea ascender/descender (already parsed) should be used
     * — this matches FreeType's behaviour and produces consistent
     * baselines across applications.
     *
     * Only override hhea metrics when the font explicitly requests it.
     */
    uint16_t fs_selection = (font->tab_os2.len >= 64) ? rd16(h + 62) : 0;
    int use_typo = (fs_selection >> 7) & 1;

    if (use_typo) {
        int16_t typo_asc  = rd16s(h + 68);
        int16_t typo_desc = rd16s(h + 70);
        int16_t typo_gap  = (font->tab_os2.len >= 74) ? rd16s(h + 72) : 0;
        if (typo_asc != 0 || typo_desc != 0) {
            font->ascent   = typo_asc;
            font->descent  = typo_desc;
            font->line_gap = typo_gap;
        }
    }
}

/* =========================================================================
 * Parse cmap table — find a suitable Unicode subtable
 * ========================================================================= */

static int parse_cmap(ttf_font_t *font)
{
    uint32_t base = font->tab_cmap.off;
    if (font->tab_cmap.len < 4) return -1;
    const uint8_t *h = font->data + base;
    uint16_t num_subtables = rd16(h + 2);
    if (font->tab_cmap.len < 4u + num_subtables * 8u) return -1;

    /* First pass: look for format 12 (platform 3, encoding 10 or platform 0 encoding 4+) */
    const uint8_t *entry = h + 4;
    for (int i = 0; i < num_subtables; i++, entry += 8) {
        uint16_t plat = rd16(entry);
        uint16_t enc  = rd16(entry + 2);
        uint32_t off  = rd32(entry + 4);
        if (off + 4 > font->tab_cmap.len) continue;
        uint16_t fmt = rd16(font->data + base + off);
        if (fmt == 12 && ((plat == 3 && enc == 10) || (plat == 0 && enc >= 4))) {
            font->cmap_format       = 12;
            font->cmap_subtable_off = base + off;
            return 0;
        }
    }

    /* Second pass: look for format 4 (BMP) */
    entry = h + 4;
    for (int i = 0; i < num_subtables; i++, entry += 8) {
        uint16_t plat = rd16(entry);
        uint16_t enc  = rd16(entry + 2);
        uint32_t off  = rd32(entry + 4);
        if (off + 4 > font->tab_cmap.len) continue;
        uint16_t fmt = rd16(font->data + base + off);
        if (fmt == 4 && ((plat == 3 && enc == 1) || (plat == 0))) {
            font->cmap_format       = 4;
            font->cmap_subtable_off = base + off;
            return 0;
        }
    }

    return -1;
}

/* =========================================================================
 * cmap format 4 lookup (BMP only, binary search)
 * ========================================================================= */

static uint16_t cmap4_lookup(const ttf_font_t *font, uint32_t cp)
{
    if (cp > 0xFFFF) return 0;
    uint32_t base = font->cmap_subtable_off;
    if (!safe_off(font, base, 14)) return 0;
    const uint8_t *h = font->data + base;
    uint16_t seg_count2 = rd16(h + 6);
    uint16_t seg_count  = seg_count2 / 2;
    if (seg_count == 0) return 0;

    uint32_t end_off   = base + 14;
    uint32_t start_off = end_off + seg_count2 + 2; /* +2 for reservedPad */
    uint32_t delta_off = start_off + seg_count2;
    uint32_t range_off = delta_off + seg_count2;

    if (!safe_off(font, range_off, seg_count2)) return 0;

    /* Binary search for the segment containing cp */
    int lo = 0, hi = seg_count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        uint16_t end_code = rd16(font->data + end_off + mid * 2);
        if (cp > end_code) {
            lo = mid + 1;
        } else {
            uint16_t start_code = rd16(font->data + start_off + mid * 2);
            if (cp < start_code) {
                hi = mid - 1;
            } else {
                /* cp is in [start_code, end_code] */
                uint16_t range = rd16(font->data + range_off + mid * 2);
                if (range == 0) {
                    int16_t delta = rd16s(font->data + delta_off + mid * 2);
                    return (uint16_t)(cp + (uint16_t)delta);
                } else {
                    uint32_t glyph_off = range_off + mid * 2 + range +
                                         (cp - start_code) * 2;
                    if (!safe_off(font, glyph_off, 2)) return 0;
                    uint16_t gid = rd16(font->data + glyph_off);
                    if (gid == 0) return 0;
                    int16_t delta = rd16s(font->data + delta_off + mid * 2);
                    return (uint16_t)(gid + (uint16_t)delta);
                }
            }
        }
    }
    return 0;
}

/* =========================================================================
 * cmap format 12 lookup (full Unicode, binary search)
 * ========================================================================= */

static uint16_t cmap12_lookup(const ttf_font_t *font, uint32_t cp)
{
    uint32_t base = font->cmap_subtable_off;
    if (!safe_off(font, base, 16)) return 0;
    const uint8_t *h = font->data + base;
    uint32_t num_groups = rd32(h + 12);
    uint32_t groups_off = base + 16;
    if (!safe_off(font, groups_off, num_groups * 12)) return 0;

    /* Binary search */
    uint32_t lo = 0, hi = num_groups;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        const uint8_t *g = font->data + groups_off + mid * 12;
        uint32_t start = rd32(g);
        uint32_t end   = rd32(g + 4);
        if (cp < start) {
            hi = mid;
        } else if (cp > end) {
            lo = mid + 1;
        } else {
            uint32_t start_gid = rd32(g + 8);
            uint32_t gid = start_gid + (cp - start);
            return (gid < 0x10000) ? (uint16_t)gid : 0;
        }
    }
    return 0;
}

/* =========================================================================
 * Public: cmap lookup
 * ========================================================================= */

uint16_t ttf_cmap_lookup(const ttf_font_t *font, uint32_t codepoint)
{
    if (font->cmap_format == 12)
        return cmap12_lookup(font, codepoint);
    else if (font->cmap_format == 4)
        return cmap4_lookup(font, codepoint);
    return 0;
}

/* =========================================================================
 * Public: hmtx advance width and LSB
 * ========================================================================= */

int ttf_hmtx_advance(const ttf_font_t *font, uint16_t glyph_id)
{
    if (glyph_id >= font->num_glyphs) return 0;
    uint32_t base = font->tab_hmtx.off;
    if (glyph_id < font->num_h_metrics) {
        uint32_t off = base + (uint32_t)glyph_id * 4;
        if (!safe_off(font, off, 4)) return 0;
        return (int)rd16(font->data + off);
    }
    /* Past num_h_metrics: use last advance width */
    uint32_t off = base + (uint32_t)(font->num_h_metrics - 1) * 4;
    if (!safe_off(font, off, 4)) return 0;
    return (int)rd16(font->data + off);
}

int ttf_hmtx_lsb(const ttf_font_t *font, uint16_t glyph_id)
{
    if (glyph_id >= font->num_glyphs) return 0;
    uint32_t base = font->tab_hmtx.off;
    if (glyph_id < font->num_h_metrics) {
        uint32_t off = base + (uint32_t)glyph_id * 4 + 2;
        if (!safe_off(font, off, 2)) return 0;
        return (int)rd16s(font->data + off);
    }
    /* Past num_h_metrics: LSBs continue sequentially */
    uint32_t off = base + (uint32_t)font->num_h_metrics * 4 +
                   ((uint32_t)glyph_id - font->num_h_metrics) * 2;
    if (!safe_off(font, off, 2)) return 0;
    return (int)rd16s(font->data + off);
}

/* =========================================================================
 * TrueType glyf outline extraction
 * ========================================================================= */

static uint32_t glyf_offset(const ttf_font_t *font, uint16_t glyph_id)
{
    uint32_t loca_base = font->tab_loca.off;
    if (font->index_to_loc_format == 0) {
        uint32_t off = loca_base + (uint32_t)glyph_id * 2;
        if (!safe_off(font, off, 2)) return 0;
        return (uint32_t)rd16(font->data + off) * 2;
    } else {
        uint32_t off = loca_base + (uint32_t)glyph_id * 4;
        if (!safe_off(font, off, 4)) return 0;
        return rd32(font->data + off);
    }
}

static uint32_t glyf_length(const ttf_font_t *font, uint16_t glyph_id)
{
    uint32_t off0 = glyf_offset(font, glyph_id);
    uint32_t off1 = glyf_offset(font, glyph_id + 1);
    return (off1 > off0) ? (off1 - off0) : 0;
}

/* Forward declaration for recursive composite glyph resolution */
static int extract_glyf_outline(const ttf_font_t *font, uint16_t glyph_id,
                                ttf_outline_t *out, int depth);

/* Dynamic outline builder */
typedef struct {
    ttf_point_t *pts;
    int          num_pts, cap_pts;
    int         *ends;
    int          num_contours, cap_contours;
} outline_builder_t;

static void ob_init(outline_builder_t *ob)
{
    memset(ob, 0, sizeof(*ob));
}

static void ob_free(outline_builder_t *ob)
{
    free(ob->pts);
    free(ob->ends);
}

static int ob_add_point(outline_builder_t *ob, float x, float y, int on_curve)
{
    if (ob->num_pts >= ob->cap_pts) {
        int nc = ob->cap_pts ? ob->cap_pts * 2 : 64;
        ttf_point_t *np = (ttf_point_t *)realloc(ob->pts, (size_t)nc * sizeof(*np));
        if (!np) return -1;
        ob->pts = np;
        ob->cap_pts = nc;
    }
    ob->pts[ob->num_pts].x = x;
    ob->pts[ob->num_pts].y = y;
    ob->pts[ob->num_pts].on_curve = on_curve;
    ob->num_pts++;
    return 0;
}

static int ob_end_contour(outline_builder_t *ob)
{
    if (ob->num_pts == 0) return 0;
    if (ob->num_contours >= ob->cap_contours) {
        int nc = ob->cap_contours ? ob->cap_contours * 2 : 16;
        int *ne = (int *)realloc(ob->ends, (size_t)nc * sizeof(*ne));
        if (!ne) return -1;
        ob->ends = ne;
        ob->cap_contours = nc;
    }
    ob->ends[ob->num_contours++] = ob->num_pts - 1;
    return 0;
}

static int extract_simple_glyf(const ttf_font_t *font, uint16_t glyph_id,
                                ttf_outline_t *out)
{
    uint32_t goff = font->tab_glyf.off + glyf_offset(font, glyph_id);
    uint32_t glen = glyf_length(font, glyph_id);
    if (glen < 10) return -1;
    if (!safe_off(font, goff, glen)) return -1;

    const uint8_t *g = font->data + goff;
    int16_t num_contours = rd16s(g);
    if (num_contours <= 0) return -1;

    out->x_min = rd16s(g + 2);
    out->y_min = rd16s(g + 4);
    out->x_max = rd16s(g + 6);
    out->y_max = rd16s(g + 8);

    /* Read contour end points */
    uint32_t p = 10;
    if (!safe_off(font, goff + p, (uint32_t)num_contours * 2)) return -1;

    out->contour_ends = (int *)malloc((size_t)num_contours * sizeof(int));
    if (!out->contour_ends) return -1;
    out->num_contours = num_contours;

    int total_points = 0;
    for (int i = 0; i < num_contours; i++) {
        out->contour_ends[i] = (int)rd16(font->data + goff + p);
        p += 2;
        if (out->contour_ends[i] >= total_points)
            total_points = out->contour_ends[i] + 1;
    }

    /* Skip instructions */
    if (!safe_off(font, goff + p, 2)) goto fail;
    uint16_t instr_len = rd16(font->data + goff + p);
    p += 2 + instr_len;

    /* Decode flags */
    uint8_t *flags = (uint8_t *)malloc((size_t)total_points);
    if (!flags) goto fail;

    int fi = 0;
    while (fi < total_points) {
        if (!safe_off(font, goff + p, 1)) { free(flags); goto fail; }
        uint8_t flag = font->data[goff + p++];
        flags[fi++] = flag;
        if (flag & 0x08) { /* repeat */
            if (!safe_off(font, goff + p, 1)) { free(flags); goto fail; }
            int repeat = font->data[goff + p++];
            for (int r = 0; r < repeat && fi < total_points; r++)
                flags[fi++] = flag;
        }
    }

    /* Decode X coordinates */
    out->points = (ttf_point_t *)calloc((size_t)total_points, sizeof(ttf_point_t));
    if (!out->points) { free(flags); goto fail; }
    out->num_points = total_points;

    int x = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & 0x02) { /* x is 1 byte */
            if (!safe_off(font, goff + p, 1)) { free(flags); goto fail2; }
            int dx = font->data[goff + p++];
            x += (flags[i] & 0x10) ? dx : -dx;
        } else if (!(flags[i] & 0x10)) { /* x is 2 bytes signed */
            if (!safe_off(font, goff + p, 2)) { free(flags); goto fail2; }
            x += rd16s(font->data + goff + p);
            p += 2;
        }
        /* else: same as previous (delta = 0) */
        out->points[i].x = (float)x;
        out->points[i].on_curve = (flags[i] & 0x01) ? 1 : 0;
    }

    /* Decode Y coordinates */
    int y = 0;
    for (int i = 0; i < total_points; i++) {
        if (flags[i] & 0x04) { /* y is 1 byte */
            if (!safe_off(font, goff + p, 1)) { free(flags); goto fail2; }
            int dy = font->data[goff + p++];
            y += (flags[i] & 0x20) ? dy : -dy;
        } else if (!(flags[i] & 0x20)) { /* y is 2 bytes signed */
            if (!safe_off(font, goff + p, 2)) { free(flags); goto fail2; }
            y += rd16s(font->data + goff + p);
            p += 2;
        }
        out->points[i].y = (float)y;
    }

    free(flags);
    return 0;

fail2:
    free(out->points);
    out->points = NULL;
fail:
    free(out->contour_ends);
    out->contour_ends = NULL;
    return -1;
}

/* Composite glyph: combine sub-glyphs with transforms */
static int extract_composite_glyf(const ttf_font_t *font, uint16_t glyph_id,
                                    ttf_outline_t *out, int depth)
{
    if (depth > 32) return -1; /* prevent infinite recursion */

    uint32_t goff = font->tab_glyf.off + glyf_offset(font, glyph_id);
    uint32_t glen = glyf_length(font, glyph_id);
    if (glen < 10) return -1;
    if (!safe_off(font, goff, glen)) return -1;

    const uint8_t *g = font->data + goff;
    out->x_min = rd16s(g + 2);
    out->y_min = rd16s(g + 4);
    out->x_max = rd16s(g + 6);
    out->y_max = rd16s(g + 8);

    outline_builder_t ob;
    ob_init(&ob);

    uint32_t p = 10;
    uint16_t flags;
    do {
        if (!safe_off(font, goff + p, 4)) { ob_free(&ob); return -1; }
        flags = rd16(font->data + goff + p);
        uint16_t sub_gid = rd16(font->data + goff + p + 2);
        p += 4;

        /* Read offsets */
        float dx = 0, dy = 0;
        float a = 1, b = 0, c = 0, d = 1;

        if (flags & 0x0001) { /* ARG_1_AND_2_ARE_WORDS */
            if (!safe_off(font, goff + p, 4)) { ob_free(&ob); return -1; }
            if (flags & 0x0002) { /* ARGS_ARE_XY_VALUES */
                dx = (float)rd16s(font->data + goff + p);
                dy = (float)rd16s(font->data + goff + p + 2);
            }
            p += 4;
        } else {
            if (!safe_off(font, goff + p, 2)) { ob_free(&ob); return -1; }
            if (flags & 0x0002) {
                dx = (float)(int8_t)font->data[goff + p];
                dy = (float)(int8_t)font->data[goff + p + 1];
            }
            p += 2;
        }

        if (flags & 0x0008) { /* WE_HAVE_A_SCALE */
            if (!safe_off(font, goff + p, 2)) { ob_free(&ob); return -1; }
            a = d = (float)rd16s(font->data + goff + p) / 16384.0f;
            p += 2;
        } else if (flags & 0x0040) { /* WE_HAVE_AN_X_AND_Y_SCALE */
            if (!safe_off(font, goff + p, 4)) { ob_free(&ob); return -1; }
            a = (float)rd16s(font->data + goff + p)     / 16384.0f;
            d = (float)rd16s(font->data + goff + p + 2) / 16384.0f;
            p += 4;
        } else if (flags & 0x0080) { /* WE_HAVE_A_TWO_BY_TWO */
            if (!safe_off(font, goff + p, 8)) { ob_free(&ob); return -1; }
            a = (float)rd16s(font->data + goff + p)     / 16384.0f;
            b = (float)rd16s(font->data + goff + p + 2) / 16384.0f;
            c = (float)rd16s(font->data + goff + p + 4) / 16384.0f;
            d = (float)rd16s(font->data + goff + p + 6) / 16384.0f;
            p += 8;
        }

        /* Recursively extract the sub-glyph outline */
        ttf_outline_t sub;
        memset(&sub, 0, sizeof(sub));
        if (extract_glyf_outline(font, sub_gid, &sub, depth + 1) == 0) {
            /* Transform and append points */
            for (int i = 0; i < sub.num_points; i++) {
                float sx = sub.points[i].x;
                float sy = sub.points[i].y;
                float tx = a * sx + c * sy + dx;
                float ty = b * sx + d * sy + dy;
                ob_add_point(&ob, tx, ty, sub.points[i].on_curve);
            }
            /* Adjust contour end indices */
            int base_pts = ob.num_pts - sub.num_points;
            for (int i = 0; i < sub.num_contours; i++) {
                int end = sub.contour_ends[i] + base_pts;
                ob_end_contour(&ob);
                /* Overwrite the end we just pushed with the correct value */
                ob.ends[ob.num_contours - 1] = end;
            }
            ttf_outline_free(&sub);
        }

    } while (flags & 0x0020); /* MORE_COMPONENTS */

    /* Transfer to output */
    out->points       = ob.pts;
    out->num_points   = ob.num_pts;
    out->contour_ends = ob.ends;
    out->num_contours = ob.num_contours;
    return (ob.num_pts > 0) ? 0 : -1;
}

static int extract_glyf_outline(const ttf_font_t *font, uint16_t glyph_id,
                                ttf_outline_t *out, int depth)
{
    if (glyph_id >= font->num_glyphs) return -1;
    uint32_t glen = glyf_length(font, glyph_id);
    if (glen == 0) return -1;

    uint32_t goff = font->tab_glyf.off + glyf_offset(font, glyph_id);
    if (!safe_off(font, goff, 2)) return -1;
    int16_t num_contours = rd16s(font->data + goff);

    if (num_contours >= 0)
        return extract_simple_glyf(font, glyph_id, out);
    else
        return extract_composite_glyf(font, glyph_id, out, depth);
}

/* =========================================================================
 * CFF parser — Type 2 charstring interpreter
 * ========================================================================= */

/* Read a CFF INDEX structure: returns count and sets *data_off to
 * the offset of the first data byte, *offsets to the start of the
 * offset array. Returns -1 on error. */
static int cff_read_index(const ttf_font_t *font, uint32_t off,
                           uint32_t *next_off, uint32_t *offsets_start,
                           int *off_size_out)
{
    if (!safe_off(font, off, 2)) return -1;
    uint16_t count = rd16(font->data + off);
    if (count == 0) {
        if (next_off) *next_off = off + 2;
        return 0;
    }
    if (!safe_off(font, off + 2, 1)) return -1;
    int off_size = font->data[off + 2];
    if (off_size < 1 || off_size > 4) return -1;
    if (off_size_out) *off_size_out = off_size;

    uint32_t offsets = off + 3;
    if (offsets_start) *offsets_start = offsets;

    /* Read last offset to compute end of data */
    uint32_t last_off_pos = offsets + (uint32_t)count * (uint32_t)off_size;
    if (!safe_off(font, last_off_pos, (uint32_t)off_size)) return -1;

    uint32_t data_start = offsets + ((uint32_t)count + 1) * (uint32_t)off_size;

    uint32_t last_offset = 0;
    const uint8_t *lp = font->data + last_off_pos;
    for (int i = 0; i < off_size; i++)
        last_offset = (last_offset << 8) | lp[i];

    if (next_off) *next_off = data_start + last_offset - 1;
    return (int)count;
}

static uint32_t cff_index_get(const ttf_font_t *font, uint32_t offsets_start,
                               int off_size, int index,
                               uint32_t *out_len)
{
    /* offset[i] is at offsets_start + i * off_size.
     * We read offset[index] and offset[index+1]. */
    uint32_t pos0 = offsets_start + (uint32_t)index * (uint32_t)off_size;
    uint32_t pos1 = pos0 + (uint32_t)off_size;
    if (!safe_off(font, pos1, (uint32_t)off_size)) { *out_len = 0; return 0; }

    uint32_t off0 = 0, off1 = 0;
    const uint8_t *p0 = font->data + pos0;
    const uint8_t *p1 = font->data + pos1;
    for (int i = 0; i < off_size; i++) {
        off0 = (off0 << 8) | p0[i];
        off1 = (off1 << 8) | p1[i];
    }

    *out_len = off1 - off0;
    /* Data starts right after the offset array. We need the total count
     * to know where that is. But we don't have it here. Instead, the
     * caller should track this. For simplicity, we pass the INDEX base
     * offset and recompute. */
    return off0; /* relative offset within the data portion (1-based) */
}

static int cff_subr_bias(int count)
{
    if (count < 1240) return 107;
    if (count < 33900) return 1131;
    return 32768;
}

/* Parse the CFF header and locate CharStrings INDEX, Subr INDEXes */
static int parse_cff(ttf_font_t *font)
{
    uint32_t cff_off = font->tab_cff.off;
    if (!safe_off(font, cff_off, 4)) return -1;

    const uint8_t *cff = font->data + cff_off;
    /* uint8_t major = cff[0]; */
    /* uint8_t minor = cff[1]; */
    uint8_t hdr_size = cff[2];

    /* Skip header */
    uint32_t pos = cff_off + hdr_size;

    /* Name INDEX */
    uint32_t next;
    int count = cff_read_index(font, pos, &next, NULL, NULL);
    if (count < 0) return -1;
    pos = next;

    /* Top DICT INDEX */
    uint32_t dict_offsets;
    int dict_off_size;
    count = cff_read_index(font, pos, &next, &dict_offsets, &dict_off_size);
    if (count < 1) return -1;

    /* Read first Top DICT */
    uint32_t dict_len;
    uint32_t dict_rel = cff_index_get(font, dict_offsets, dict_off_size, 0, &dict_len);
    /* Compute absolute data start of the Top DICT INDEX */
    /* count entries, off_size bytes each, +1 for count field + off_size field */
    uint32_t dict_data_start = dict_offsets + ((uint32_t)count + 1) * (uint32_t)dict_off_size;
    uint32_t dict_abs = dict_data_start + dict_rel - 1;
    if (!safe_off(font, dict_abs, dict_len)) return -1;

    /* Parse Top DICT for CharStrings offset, Private DICT, FDArray, FDSelect */
    uint32_t charstrings_off = 0;
    uint32_t private_off = 0, private_len = 0;
    uint32_t fd_array_off = 0;
    uint32_t fd_select_off = 0;
    font->cff_default_width = 0;
    font->cff_nominal_width = 0;
    font->cff_is_cid = 0;
    font->cff_num_fds = 0;

    {
        const uint8_t *dp = font->data + dict_abs;
        const uint8_t *dend = dp + dict_len;
        int32_t stack[48];
        int sp = 0;

        while (dp < dend) {
            uint8_t b0 = *dp++;
            if (b0 >= 32 && b0 <= 246) {
                if (sp < 48) stack[sp++] = (int32_t)b0 - 139;
            } else if (b0 >= 247 && b0 <= 250) {
                if (dp >= dend) break;
                int32_t v = ((int32_t)b0 - 247) * 256 + *dp++ + 108;
                if (sp < 48) stack[sp++] = v;
            } else if (b0 >= 251 && b0 <= 254) {
                if (dp >= dend) break;
                int32_t v = -((int32_t)b0 - 251) * 256 - *dp++ - 108;
                if (sp < 48) stack[sp++] = v;
            } else if (b0 == 28) {
                if (dp + 1 >= dend) break;
                int32_t v = (int16_t)((dp[0] << 8) | dp[1]);
                dp += 2;
                if (sp < 48) stack[sp++] = v;
            } else if (b0 == 29) {
                if (dp + 3 >= dend) break;
                int32_t v = (int32_t)rd32(dp);
                dp += 4;
                if (sp < 48) stack[sp++] = v;
            } else if (b0 == 30) {
                /* Real number — skip nibbles until end marker */
                while (dp < dend) {
                    uint8_t nb = *dp++;
                    if ((nb & 0x0F) == 0x0F || (nb >> 4) == 0x0F) break;
                }
                if (sp < 48) stack[sp++] = 0; /* approximate as 0 */
            } else if (b0 == 12) {
                /* Two-byte operator */
                if (dp >= dend) break;
                uint8_t b1 = *dp++;
                switch (b1) {
                case 30: /* ROS — marks this as CID-keyed font */
                    font->cff_is_cid = 1;
                    break;
                case 36: /* FDArray */
                    if (sp > 0) fd_array_off = (uint32_t)stack[sp - 1];
                    break;
                case 37: /* FDSelect */
                    if (sp > 0) fd_select_off = (uint32_t)stack[sp - 1];
                    break;
                default:
                    break;
                }
                sp = 0;
            } else {
                /* One-byte operator */
                switch (b0) {
                case 17: /* CharStrings */
                    if (sp > 0) charstrings_off = (uint32_t)stack[sp - 1];
                    break;
                case 18: /* Private */
                    if (sp >= 2) {
                        private_len = (uint32_t)stack[sp - 2];
                        private_off = (uint32_t)stack[sp - 1];
                    }
                    break;
                }
                sp = 0;
            }
        }
    }

    if (charstrings_off == 0) return -1;

    /* Store Top DICT INDEX end for String INDEX skip */
    pos = next;

    /* String INDEX — skip */
    count = cff_read_index(font, pos, &next, NULL, NULL);
    if (count < 0) return -1;
    pos = next;

    /* Global Subr INDEX */
    uint32_t gsubr_offsets;
    int gsubr_off_size;
    int gsubr_count = cff_read_index(font, pos, &next, &gsubr_offsets, &gsubr_off_size);
    if (gsubr_count > 0) {
        font->cff_gsubr_off   = pos; /* store INDEX base for later lookup */
        font->cff_gsubr_count = gsubr_count;
        font->cff_gsubr_bias  = cff_subr_bias(gsubr_count);
    }

    /* CharStrings INDEX (relative to CFF table start) */
    font->cff_char_strings_off = cff_off + charstrings_off;

    /* Private DICT */
    if (private_off > 0 && private_len > 0) {
        uint32_t priv_abs = cff_off + private_off;
        if (safe_off(font, priv_abs, private_len)) {
            const uint8_t *dp = font->data + priv_abs;
            const uint8_t *dend = dp + private_len;
            int32_t stack[48];
            int sp = 0;

            while (dp < dend) {
                uint8_t b0 = *dp++;
                if (b0 >= 32 && b0 <= 246) {
                    if (sp < 48) stack[sp++] = (int32_t)b0 - 139;
                } else if (b0 >= 247 && b0 <= 250) {
                    if (dp >= dend) break;
                    int32_t v = ((int32_t)b0 - 247) * 256 + *dp++ + 108;
                    if (sp < 48) stack[sp++] = v;
                } else if (b0 >= 251 && b0 <= 254) {
                    if (dp >= dend) break;
                    int32_t v = -((int32_t)b0 - 251) * 256 - *dp++ - 108;
                    if (sp < 48) stack[sp++] = v;
                } else if (b0 == 28) {
                    if (dp + 1 >= dend) break;
                    int32_t v = (int16_t)((dp[0] << 8) | dp[1]);
                    dp += 2;
                    if (sp < 48) stack[sp++] = v;
                } else if (b0 == 29) {
                    if (dp + 3 >= dend) break;
                    int32_t v = (int32_t)rd32(dp);
                    dp += 4;
                    if (sp < 48) stack[sp++] = v;
                } else if (b0 == 30) {
                    while (dp < dend) {
                        uint8_t nb = *dp++;
                        if ((nb & 0x0F) == 0x0F || (nb >> 4) == 0x0F) break;
                    }
                    if (sp < 48) stack[sp++] = 0;
                } else if (b0 == 12) {
                    if (dp >= dend) break;
                    dp++; /* skip two-byte operator */
                    sp = 0;
                } else {
                    switch (b0) {
                    case 19: /* Subrs offset (relative to Private DICT start) */
                        if (sp > 0) {
                            uint32_t subr_abs = priv_abs + (uint32_t)stack[sp - 1];
                            font->cff_lsubr_off = subr_abs;
                            /* Read Local Subr INDEX to get count */
                            int lcount = cff_read_index(font, subr_abs, NULL, NULL, NULL);
                            if (lcount > 0) {
                                font->cff_lsubr_count = lcount;
                                font->cff_lsubr_bias  = cff_subr_bias(lcount);
                            }
                        }
                        break;
                    case 20: /* defaultWidthX */
                        if (sp > 0) font->cff_default_width = stack[sp - 1];
                        break;
                    case 21: /* nominalWidthX */
                        if (sp > 0) font->cff_nominal_width = stack[sp - 1];
                        break;
                    }
                    sp = 0;
                }
            }
        }
    }

    /* ---- CID-keyed font: parse FDArray and FDSelect ---- */
    if (font->cff_is_cid && fd_array_off > 0 && fd_select_off > 0) {
        /* FDSelect — store offset and format for runtime lookup */
        uint32_t fds_abs = cff_off + fd_select_off;
        if (safe_off(font, fds_abs, 1)) {
            font->cff_fd_select_off    = fds_abs;
            font->cff_fd_select_format = font->data[fds_abs];
        }

        /* FDArray INDEX — each entry is a Font DICT with its own Private DICT */
        uint32_t fda_abs = cff_off + fd_array_off;
        uint32_t fda_offsets;
        int fda_off_size;
        int fda_count = cff_read_index(font, fda_abs, NULL,
                                        &fda_offsets, &fda_off_size);
        if (fda_count > CFF_MAX_FDS) fda_count = CFF_MAX_FDS;
        font->cff_num_fds = fda_count;

        uint32_t fda_data_start = fda_offsets +
                                   ((uint32_t)fda_count + 1) * (uint32_t)fda_off_size;

        for (int fd = 0; fd < fda_count; fd++) {
            uint32_t fd_len;
            uint32_t fd_rel = cff_index_get(font, fda_offsets, fda_off_size,
                                             fd, &fd_len);
            uint32_t fd_abs = fda_data_start + fd_rel - 1;
            if (!safe_off(font, fd_abs, fd_len)) continue;

            /* Parse this Font DICT for its Private DICT location */
            uint32_t fd_priv_off = 0, fd_priv_len = 0;
            {
                const uint8_t *dp = font->data + fd_abs;
                const uint8_t *dend = dp + fd_len;
                int32_t stack[48];
                int sp = 0;

                while (dp < dend) {
                    uint8_t b0 = *dp++;
                    if (b0 >= 32 && b0 <= 246) {
                        if (sp < 48) stack[sp++] = (int32_t)b0 - 139;
                    } else if (b0 >= 247 && b0 <= 250) {
                        if (dp >= dend) break;
                        if (sp < 48) stack[sp++] = ((int32_t)b0 - 247) * 256 + *dp++ + 108;
                    } else if (b0 >= 251 && b0 <= 254) {
                        if (dp >= dend) break;
                        if (sp < 48) stack[sp++] = -((int32_t)b0 - 251) * 256 - *dp++ - 108;
                    } else if (b0 == 28) {
                        if (dp + 1 >= dend) break;
                        if (sp < 48) stack[sp++] = (int16_t)((dp[0] << 8) | dp[1]);
                        dp += 2;
                    } else if (b0 == 29) {
                        if (dp + 3 >= dend) break;
                        if (sp < 48) stack[sp++] = (int32_t)rd32(dp);
                        dp += 4;
                    } else if (b0 == 30) {
                        while (dp < dend) {
                            uint8_t nb = *dp++;
                            if ((nb & 0x0F) == 0x0F || (nb >> 4) == 0x0F) break;
                        }
                        if (sp < 48) stack[sp++] = 0;
                    } else if (b0 == 12) {
                        if (dp >= dend) break;
                        dp++;
                        sp = 0;
                    } else {
                        if (b0 == 18 && sp >= 2) { /* Private */
                            fd_priv_len = (uint32_t)stack[sp - 2];
                            fd_priv_off = (uint32_t)stack[sp - 1];
                        }
                        sp = 0;
                    }
                }
            }

            /* Parse this FD's Private DICT for local subrs */
            if (fd_priv_off > 0 && fd_priv_len > 0) {
                uint32_t priv_abs2 = cff_off + fd_priv_off;
                if (safe_off(font, priv_abs2, fd_priv_len)) {
                    const uint8_t *dp = font->data + priv_abs2;
                    const uint8_t *dend = dp + fd_priv_len;
                    int32_t stack[48];
                    int sp = 0;

                    while (dp < dend) {
                        uint8_t b0 = *dp++;
                        if (b0 >= 32 && b0 <= 246) {
                            if (sp < 48) stack[sp++] = (int32_t)b0 - 139;
                        } else if (b0 >= 247 && b0 <= 250) {
                            if (dp >= dend) break;
                            if (sp < 48) stack[sp++] = ((int32_t)b0 - 247) * 256 + *dp++ + 108;
                        } else if (b0 >= 251 && b0 <= 254) {
                            if (dp >= dend) break;
                            if (sp < 48) stack[sp++] = -((int32_t)b0 - 251) * 256 - *dp++ - 108;
                        } else if (b0 == 28) {
                            if (dp + 1 >= dend) break;
                            if (sp < 48) stack[sp++] = (int16_t)((dp[0] << 8) | dp[1]);
                            dp += 2;
                        } else if (b0 == 29) {
                            if (dp + 3 >= dend) break;
                            if (sp < 48) stack[sp++] = (int32_t)rd32(dp);
                            dp += 4;
                        } else if (b0 == 30) {
                            while (dp < dend) {
                                uint8_t nb = *dp++;
                                if ((nb & 0x0F) == 0x0F || (nb >> 4) == 0x0F) break;
                            }
                            if (sp < 48) stack[sp++] = 0;
                        } else if (b0 == 12) {
                            if (dp >= dend) break;
                            dp++;
                            sp = 0;
                        } else {
                            switch (b0) {
                            case 19: { /* Subrs offset (relative to this Private DICT) */
                                if (sp > 0) {
                                    uint32_t subr_abs2 = priv_abs2 + (uint32_t)stack[sp - 1];
                                    font->cff_fd[fd].lsubr_off = subr_abs2;
                                    int lc = cff_read_index(font, subr_abs2, NULL, NULL, NULL);
                                    if (lc > 0) {
                                        font->cff_fd[fd].lsubr_count = lc;
                                        font->cff_fd[fd].lsubr_bias  = cff_subr_bias(lc);
                                    }
                                }
                                break;
                            }
                            case 20:
                                if (sp > 0) font->cff_fd[fd].default_width = stack[sp - 1];
                                break;
                            case 21:
                                if (sp > 0) font->cff_fd[fd].nominal_width = stack[sp - 1];
                                break;
                            }
                            sp = 0;
                        }
                    }
                }
            }
        }
    }

    return 0;
}

/* Look up FDSelect: returns FD index for a glyph in CID-keyed fonts */
static int cff_fd_select(const ttf_font_t *font, uint16_t glyph_id)
{
    if (!font->cff_is_cid || font->cff_num_fds <= 1) return 0;

    uint32_t off = font->cff_fd_select_off;
    if (!safe_off(font, off, 1)) return 0;

    int format = font->data[off];
    off++;

    if (format == 0) {
        /* Format 0: one byte per glyph */
        if (!safe_off(font, off + glyph_id, 1)) return 0;
        return font->data[off + glyph_id];
    } else if (format == 3) {
        /* Format 3: range-based */
        if (!safe_off(font, off, 2)) return 0;
        uint16_t n_ranges = rd16(font->data + off);
        off += 2;
        /* Each range: uint16 first_glyph + uint8 fd_index = 3 bytes */
        if (!safe_off(font, off, (uint32_t)n_ranges * 3 + 2)) return 0;
        const uint8_t *p = font->data + off;
        for (int i = 0; i < n_ranges; i++) {
            uint16_t first = rd16(p + i * 3);
            uint16_t next_first = rd16(p + (i + 1) * 3);
            if (glyph_id >= first && glyph_id < next_first)
                return p[i * 3 + 2];
        }
        return 0;
    }

    return 0;
}

/* Get charstring data for a glyph from the CharStrings INDEX */
static const uint8_t *cff_get_charstring(const ttf_font_t *font,
                                          uint16_t glyph_id,
                                          uint32_t *out_len)
{
    uint32_t cs_off = font->cff_char_strings_off;
    if (!safe_off(font, cs_off, 2)) return NULL;
    uint16_t count = rd16(font->data + cs_off);
    if (glyph_id >= count) return NULL;
    if (!safe_off(font, cs_off + 2, 1)) return NULL;
    int off_size = font->data[cs_off + 2];
    uint32_t offsets = cs_off + 3;

    uint32_t pos0 = offsets + (uint32_t)glyph_id * (uint32_t)off_size;
    uint32_t pos1 = pos0 + (uint32_t)off_size;
    if (!safe_off(font, pos1, (uint32_t)off_size)) return NULL;

    uint32_t off0 = 0, off1 = 0;
    for (int i = 0; i < off_size; i++) {
        off0 = (off0 << 8) | font->data[pos0 + (uint32_t)i];
        off1 = (off1 << 8) | font->data[pos1 + (uint32_t)i];
    }

    uint32_t data_start = offsets + ((uint32_t)count + 1) * (uint32_t)off_size;
    uint32_t abs0 = data_start + off0 - 1;
    *out_len = off1 - off0;
    if (!safe_off(font, abs0, *out_len)) return NULL;
    return font->data + abs0;
}

/* Get subr data from a Subr INDEX */
static const uint8_t *cff_get_subr(const ttf_font_t *font,
                                    uint32_t index_off,
                                    int subr_index, int count,
                                    uint32_t *out_len)
{
    if (subr_index < 0 || subr_index >= count) return NULL;
    if (!safe_off(font, index_off, 3)) return NULL;
    int off_size = font->data[index_off + 2];
    uint32_t offsets = index_off + 3;

    uint32_t pos0 = offsets + (uint32_t)subr_index * (uint32_t)off_size;
    uint32_t pos1 = pos0 + (uint32_t)off_size;
    if (!safe_off(font, pos1, (uint32_t)off_size)) return NULL;

    uint32_t off0 = 0, off1 = 0;
    for (int i = 0; i < off_size; i++) {
        off0 = (off0 << 8) | font->data[pos0 + (uint32_t)i];
        off1 = (off1 << 8) | font->data[pos1 + (uint32_t)i];
    }

    uint32_t data_start = offsets + ((uint32_t)count + 1) * (uint32_t)off_size;
    uint32_t abs0 = data_start + off0 - 1;
    *out_len = off1 - off0;
    if (!safe_off(font, abs0, *out_len)) return NULL;
    return font->data + abs0;
}

/* Type 2 charstring interpreter state */
typedef struct {
    outline_builder_t ob;
    float stack[48];
    int   sp;
    float x, y;
    int   started;        /* have we issued the first moveto? */
    int   num_hints;
    int   width_parsed;
    const ttf_font_t *font;
    /* Per-glyph local subr info (set from FD for CID fonts) */
    uint32_t lsubr_off;
    int      lsubr_count;
    int      lsubr_bias;
} cff_interp_t;

static void cff_close_contour(cff_interp_t *ci)
{
    if (ci->started && ci->ob.num_pts > 0) {
        ob_end_contour(&ci->ob);
    }
    ci->started = 0;
}

static int cff_run_charstring(cff_interp_t *ci, const uint8_t *cs, uint32_t len,
                               int depth);

static int cff_run_charstring(cff_interp_t *ci, const uint8_t *cs, uint32_t len,
                               int depth)
{
    if (depth > 10) return -1;
    const uint8_t *end = cs + len;

    while (cs < end) {
        uint8_t b0 = *cs++;

        /* Operand encoding */
        if (b0 >= 32) {
            float val;
            if (b0 <= 246) {
                val = (float)((int)b0 - 139);
            } else if (b0 <= 250) {
                if (cs >= end) return -1;
                val = (float)(((int)b0 - 247) * 256 + *cs++ + 108);
            } else if (b0 <= 254) {
                if (cs >= end) return -1;
                val = (float)(-((int)b0 - 251) * 256 - *cs++ - 108);
            } else { /* b0 == 255: 32-bit fixed-point 16.16 */
                if (cs + 3 >= end) return -1;
                int32_t fixed = (int32_t)rd32(cs);
                cs += 4;
                val = (float)fixed / 65536.0f;
            }
            if (ci->sp < 48) ci->stack[ci->sp++] = val;
            continue;
        }

        if (b0 == 28) { /* 16-bit integer */
            if (cs + 1 >= end) return -1;
            int16_t v = (int16_t)((cs[0] << 8) | cs[1]);
            cs += 2;
            if (ci->sp < 48) ci->stack[ci->sp++] = (float)v;
            continue;
        }

        /* Operators */
        switch (b0) {
        case 1:  /* hstem */
        case 3:  /* vstem */
            ci->num_hints += ci->sp / 2;
            if (!ci->width_parsed && (ci->sp & 1)) {
                ci->width_parsed = 1;
            }
            ci->sp = 0;
            break;

        case 4: { /* vmoveto */
            if (!ci->width_parsed && ci->sp > 1) ci->width_parsed = 1;
            cff_close_contour(ci);
            float dy = (ci->sp > 0) ? ci->stack[ci->sp - 1] : 0;
            ci->y += dy;
            ob_add_point(&ci->ob, ci->x, ci->y, 1);
            ci->started = 1;
            ci->sp = 0;
            break;
        }

        case 5: { /* rlineto */
            for (int i = 0; i + 1 < ci->sp; i += 2) {
                ci->x += ci->stack[i];
                ci->y += ci->stack[i + 1];
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 6: { /* hlineto */
            int horiz = 1;
            for (int i = 0; i < ci->sp; i++) {
                if (horiz) ci->x += ci->stack[i];
                else       ci->y += ci->stack[i];
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
                horiz = !horiz;
            }
            ci->sp = 0;
            break;
        }

        case 7: { /* vlineto */
            int horiz = 0;
            for (int i = 0; i < ci->sp; i++) {
                if (horiz) ci->x += ci->stack[i];
                else       ci->y += ci->stack[i];
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
                horiz = !horiz;
            }
            ci->sp = 0;
            break;
        }

        case 8: { /* rrcurveto */
            for (int i = 0; i + 5 < ci->sp; i += 6) {
                float x1 = ci->x + ci->stack[i];
                float y1 = ci->y + ci->stack[i + 1];
                float x2 = x1    + ci->stack[i + 2];
                float y2 = y1    + ci->stack[i + 3];
                ci->x = x2       + ci->stack[i + 4];
                ci->y = y2       + ci->stack[i + 5];
                ob_add_point(&ci->ob, x1, y1, 2);
                ob_add_point(&ci->ob, x2, y2, 2);
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 10: { /* callsubr (local) */
            if (ci->sp < 1) break;
            int idx = (int)ci->stack[--ci->sp] + ci->lsubr_bias;
            uint32_t slen;
            const uint8_t *subr = cff_get_subr(ci->font, ci->lsubr_off,
                                                idx, ci->lsubr_count,
                                                &slen);
            if (subr) cff_run_charstring(ci, subr, slen, depth + 1);
            break;
        }

        case 11: /* return */
            return 0;

        case 12: { /* Two-byte operators */
            if (cs >= end) return -1;
            uint8_t b1 = *cs++;
            switch (b1) {
            case 34: { /* hflex */
                if (ci->sp >= 7) {
                    float x1 = ci->x + ci->stack[0]; float y1 = ci->y;
                    float x2 = x1 + ci->stack[1];    float y2 = y1 + ci->stack[2];
                    float x3 = x2 + ci->stack[3];    float y3 = y2;
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, x3, y3, 1);
                    float x4 = x3 + ci->stack[4]; float y4 = y3;
                    float x5 = x4 + ci->stack[5]; float y5 = ci->y;
                    ci->x = x5 + ci->stack[6]; ci->y = y5;
                    ob_add_point(&ci->ob, x4, y4, 2);
                    ob_add_point(&ci->ob, x5, y5, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                }
                ci->sp = 0;
                break;
            }
            case 35: { /* flex */
                if (ci->sp >= 13) {
                    for (int i = 0; i < 12; i += 6) {
                        float x1 = ci->x + ci->stack[i];
                        float y1 = ci->y + ci->stack[i + 1];
                        float x2 = x1 + ci->stack[i + 2];
                        float y2 = y1 + ci->stack[i + 3];
                        ci->x = x2 + ci->stack[i + 4];
                        ci->y = y2 + ci->stack[i + 5];
                        ob_add_point(&ci->ob, x1, y1, 2);
                        ob_add_point(&ci->ob, x2, y2, 2);
                        ob_add_point(&ci->ob, ci->x, ci->y, 1);
                    }
                }
                ci->sp = 0;
                break;
            }
            case 36: { /* hflex1 */
                if (ci->sp >= 9) {
                    float x1 = ci->x + ci->stack[0]; float y1 = ci->y + ci->stack[1];
                    float x2 = x1 + ci->stack[2]; float y2 = y1 + ci->stack[3];
                    float x3 = x2 + ci->stack[4]; float y3 = y2;
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, x3, y3, 1);
                    float x4 = x3 + ci->stack[5]; float y4 = y3;
                    float x5 = x4 + ci->stack[6]; float y5 = y4 + ci->stack[7];
                    ci->x = x5 + ci->stack[8]; ci->y = y5;
                    ob_add_point(&ci->ob, x4, y4, 2);
                    ob_add_point(&ci->ob, x5, y5, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                }
                ci->sp = 0;
                break;
            }
            case 37: { /* flex1 */
                if (ci->sp >= 11) {
                    float sx = ci->x, sy = ci->y;
                    for (int i = 0; i < 2; i++) {
                        int bi = i * 6;
                        float x1 = ci->x + ci->stack[bi];
                        float y1 = ci->y + ci->stack[bi + 1];
                        float x2 = x1 + ci->stack[bi + 2];
                        float y2 = y1 + ci->stack[bi + 3];
                        if (i == 0) {
                            ci->x = x2 + ci->stack[bi + 4];
                            ci->y = y2 + ci->stack[bi + 5];
                        } else {
                            /* Last arg: determine which coordinate gets the value */
                            float dx = ci->x + ci->stack[bi + 4] - sx;
                            float dy = ci->y + ci->stack[bi + 5] - sy;
                            if (dx < 0) dx = -dx;
                            if (dy < 0) dy = -dy;
                            if (dx > dy) {
                                ci->x = sx + ci->stack[10]; /* hmm, not exact */
                                ci->y = y2 + ci->stack[bi + 5];
                            } else {
                                ci->x = x2 + ci->stack[bi + 4];
                                ci->y = sy + ci->stack[10];
                            }
                        }
                        ob_add_point(&ci->ob, x1, y1, 2);
                        ob_add_point(&ci->ob, x2, y2, 2);
                        ob_add_point(&ci->ob, ci->x, ci->y, 1);
                    }
                }
                ci->sp = 0;
                break;
            }
            default:
                ci->sp = 0;
                break;
            }
            break;
        }

        case 14: /* endchar */
            if (!ci->width_parsed && ci->sp > 0) ci->width_parsed = 1;
            cff_close_contour(ci);
            return 0;

        case 18: /* hstemhm */
        case 23: /* vstemhm */
            ci->num_hints += ci->sp / 2;
            if (!ci->width_parsed && (ci->sp & 1)) ci->width_parsed = 1;
            ci->sp = 0;
            break;

        case 19: /* hintmask */
        case 20: /* cntrmask */
            if (ci->sp > 0) {
                ci->num_hints += ci->sp / 2;
                if (!ci->width_parsed && (ci->sp & 1)) ci->width_parsed = 1;
                ci->sp = 0;
            }
            /* Skip hint mask bytes */
            {
                int bytes = (ci->num_hints + 7) / 8;
                cs += bytes;
                if (cs > end) return -1;
            }
            break;

        case 21: { /* rmoveto */
            if (!ci->width_parsed && ci->sp > 2) ci->width_parsed = 1;
            cff_close_contour(ci);
            float dx = (ci->sp >= 2) ? ci->stack[ci->sp - 2] : 0;
            float dy = (ci->sp >= 1) ? ci->stack[ci->sp - 1] : 0;
            ci->x += dx;
            ci->y += dy;
            ob_add_point(&ci->ob, ci->x, ci->y, 1);
            ci->started = 1;
            ci->sp = 0;
            break;
        }

        case 22: { /* hmoveto */
            if (!ci->width_parsed && ci->sp > 1) ci->width_parsed = 1;
            cff_close_contour(ci);
            float dx = (ci->sp > 0) ? ci->stack[ci->sp - 1] : 0;
            ci->x += dx;
            ob_add_point(&ci->ob, ci->x, ci->y, 1);
            ci->started = 1;
            ci->sp = 0;
            break;
        }

        case 24: { /* rcurveline */
            int i;
            for (i = 0; i + 5 < ci->sp - 2; i += 6) {
                float x1 = ci->x + ci->stack[i];
                float y1 = ci->y + ci->stack[i + 1];
                float x2 = x1 + ci->stack[i + 2];
                float y2 = y1 + ci->stack[i + 3];
                ci->x = x2 + ci->stack[i + 4];
                ci->y = y2 + ci->stack[i + 5];
                ob_add_point(&ci->ob, x1, y1, 2);
                ob_add_point(&ci->ob, x2, y2, 2);
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            /* Final line */
            if (i + 1 < ci->sp) {
                ci->x += ci->stack[i];
                ci->y += ci->stack[i + 1];
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 25: { /* rlinecurve */
            int i;
            for (i = 0; i + 1 < ci->sp - 6; i += 2) {
                ci->x += ci->stack[i];
                ci->y += ci->stack[i + 1];
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            /* Final curve */
            if (i + 5 < ci->sp) {
                float x1 = ci->x + ci->stack[i];
                float y1 = ci->y + ci->stack[i + 1];
                float x2 = x1 + ci->stack[i + 2];
                float y2 = y1 + ci->stack[i + 3];
                ci->x = x2 + ci->stack[i + 4];
                ci->y = y2 + ci->stack[i + 5];
                ob_add_point(&ci->ob, x1, y1, 2);
                ob_add_point(&ci->ob, x2, y2, 2);
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 26: { /* vvcurveto */
            int i = 0;
            if (ci->sp & 1) { /* odd: first arg is dx1 */
                ci->x += ci->stack[0];
                i = 1;
            }
            for (; i + 3 < ci->sp; i += 4) {
                float x1 = ci->x;
                float y1 = ci->y + ci->stack[i];
                float x2 = x1 + ci->stack[i + 1];
                float y2 = y1 + ci->stack[i + 2];
                ci->x = x2;
                ci->y = y2 + ci->stack[i + 3];
                ob_add_point(&ci->ob, x1, y1, 2);
                ob_add_point(&ci->ob, x2, y2, 2);
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 27: { /* hhcurveto */
            int i = 0;
            if (ci->sp & 1) { /* odd: first arg is dy1 */
                ci->y += ci->stack[0];
                i = 1;
            }
            for (; i + 3 < ci->sp; i += 4) {
                float x1 = ci->x + ci->stack[i];
                float y1 = ci->y;
                float x2 = x1 + ci->stack[i + 1];
                float y2 = y1 + ci->stack[i + 2];
                ci->x = x2 + ci->stack[i + 3];
                ci->y = y2;
                ob_add_point(&ci->ob, x1, y1, 2);
                ob_add_point(&ci->ob, x2, y2, 2);
                ob_add_point(&ci->ob, ci->x, ci->y, 1);
            }
            ci->sp = 0;
            break;
        }

        case 29: { /* callgsubr (global) */
            if (ci->sp < 1) break;
            int idx = (int)ci->stack[--ci->sp] + ci->font->cff_gsubr_bias;
            uint32_t slen;
            const uint8_t *subr = cff_get_subr(ci->font, ci->font->cff_gsubr_off,
                                                idx, ci->font->cff_gsubr_count,
                                                &slen);
            if (subr) cff_run_charstring(ci, subr, slen, depth + 1);
            break;
        }

        case 30: { /* vhcurveto */
            int i;
            for (i = 0; i + 3 < ci->sp; ) {
                if ((i / 4) & 1) {
                    /* h-then-v */
                    float x1 = ci->x + ci->stack[i];
                    float y1 = ci->y;
                    float x2 = x1 + ci->stack[i + 1];
                    float y2 = y1 + ci->stack[i + 2];
                    ci->x = x2;
                    ci->y = y2 + ci->stack[i + 3];
                    /* If this is the last set and there's one extra arg */
                    if (i + 4 >= ci->sp - 1 && (ci->sp - i) == 5)
                        ci->x += ci->stack[i + 4];
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                } else {
                    /* v-then-h */
                    float x1 = ci->x;
                    float y1 = ci->y + ci->stack[i];
                    float x2 = x1 + ci->stack[i + 1];
                    float y2 = y1 + ci->stack[i + 2];
                    ci->x = x2 + ci->stack[i + 3];
                    ci->y = y2;
                    /* If this is the last set and there's one extra arg */
                    if (i + 4 >= ci->sp - 1 && (ci->sp - i) == 5)
                        ci->y += ci->stack[i + 4];
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                }
                i += 4;
                if (i + 4 >= ci->sp && (ci->sp - i) == 1) {
                    i++; /* consumed the trailing arg above */
                    break;
                }
            }
            ci->sp = 0;
            break;
        }

        case 31: { /* hvcurveto */
            int i;
            for (i = 0; i + 3 < ci->sp; ) {
                if ((i / 4) & 1) {
                    /* v-then-h */
                    float x1 = ci->x;
                    float y1 = ci->y + ci->stack[i];
                    float x2 = x1 + ci->stack[i + 1];
                    float y2 = y1 + ci->stack[i + 2];
                    ci->x = x2 + ci->stack[i + 3];
                    ci->y = y2;
                    if (i + 4 >= ci->sp - 1 && (ci->sp - i) == 5)
                        ci->y += ci->stack[i + 4];
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                } else {
                    /* h-then-v */
                    float x1 = ci->x + ci->stack[i];
                    float y1 = ci->y;
                    float x2 = x1 + ci->stack[i + 1];
                    float y2 = y1 + ci->stack[i + 2];
                    ci->x = x2;
                    ci->y = y2 + ci->stack[i + 3];
                    if (i + 4 >= ci->sp - 1 && (ci->sp - i) == 5)
                        ci->x += ci->stack[i + 4];
                    ob_add_point(&ci->ob, x1, y1, 2);
                    ob_add_point(&ci->ob, x2, y2, 2);
                    ob_add_point(&ci->ob, ci->x, ci->y, 1);
                }
                i += 4;
                if (i + 4 >= ci->sp && (ci->sp - i) == 1) {
                    i++;
                    break;
                }
            }
            ci->sp = 0;
            break;
        }

        default:
            /* Unknown operator: clear stack */
            ci->sp = 0;
            break;
        }
    }
    return 0;
}

static int extract_cff_outline(const ttf_font_t *font, uint16_t glyph_id,
                                ttf_outline_t *out)
{
    uint32_t cs_len;
    const uint8_t *cs = cff_get_charstring(font, glyph_id, &cs_len);
    if (!cs || cs_len == 0) return -1;

    cff_interp_t ci;
    memset(&ci, 0, sizeof(ci));
    ob_init(&ci.ob);
    ci.font = font;

    /* Set local subr info: per-FD for CID fonts, top-level otherwise */
    if (font->cff_is_cid && font->cff_num_fds > 0) {
        int fd = cff_fd_select(font, glyph_id);
        if (fd >= 0 && fd < font->cff_num_fds) {
            ci.lsubr_off   = font->cff_fd[fd].lsubr_off;
            ci.lsubr_count = font->cff_fd[fd].lsubr_count;
            ci.lsubr_bias  = font->cff_fd[fd].lsubr_bias;
        }
    } else {
        ci.lsubr_off   = font->cff_lsubr_off;
        ci.lsubr_count = font->cff_lsubr_count;
        ci.lsubr_bias  = font->cff_lsubr_bias;
    }

    if (cff_run_charstring(&ci, cs, cs_len, 0) != 0) {
        ob_free(&ci.ob);
        return -1;
    }

    if (ci.ob.num_pts == 0) {
        ob_free(&ci.ob);
        return -1;
    }

    out->points       = ci.ob.pts;
    out->num_points   = ci.ob.num_pts;
    out->contour_ends = ci.ob.ends;
    out->num_contours = ci.ob.num_contours;

    /* Compute bounding box */
    out->x_min = out->x_max = (int16_t)ci.ob.pts[0].x;
    out->y_min = out->y_max = (int16_t)ci.ob.pts[0].y;
    for (int i = 1; i < ci.ob.num_pts; i++) {
        int16_t px = (int16_t)ci.ob.pts[i].x;
        int16_t py = (int16_t)ci.ob.pts[i].y;
        if (px < out->x_min) out->x_min = px;
        if (px > out->x_max) out->x_max = px;
        if (py < out->y_min) out->y_min = py;
        if (py > out->y_max) out->y_max = py;
    }

    return 0;
}

/* =========================================================================
 * Public: glyph outline extraction
 * ========================================================================= */

int ttf_glyph_outline(const ttf_font_t *font, uint16_t glyph_id,
                       ttf_outline_t *out)
{
    memset(out, 0, sizeof(*out));
    if (font->has_glyf)
        return extract_glyf_outline(font, glyph_id, out, 0);
    else
        return extract_cff_outline(font, glyph_id, out);
}

void ttf_outline_free(ttf_outline_t *out)
{
    free(out->points);
    free(out->contour_ends);
    memset(out, 0, sizeof(*out));
}

/* =========================================================================
 * GPOS pair positioning (kern feature) — Format 1 & 2
 * ========================================================================= */

/*
 * Search a Coverage table for glyph_id.
 * Returns the coverage index (>=0) or -1 if not found.
 */
static int gpos_coverage_index(const ttf_font_t *font, uint32_t cov_off,
                                uint16_t glyph_id)
{
    if (!safe_off(font, cov_off, 4)) return -1;
    const uint8_t *d = font->data + cov_off;
    uint16_t fmt = rd16(d);
    uint16_t count = rd16(d + 2);

    if (fmt == 1) {
        /* Format 1: array of glyph IDs */
        if (!safe_off(font, cov_off + 4, (uint32_t)count * 2)) return -1;
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            uint16_t gid = rd16(d + 4 + mid * 2);
            if (glyph_id < gid)      hi = mid - 1;
            else if (glyph_id > gid) lo = mid + 1;
            else return mid;
        }
    } else if (fmt == 2) {
        /* Format 2: ranges */
        if (!safe_off(font, cov_off + 4, (uint32_t)count * 6)) return -1;
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t *r = d + 4 + mid * 6;
            uint16_t start = rd16(r);
            uint16_t end   = rd16(r + 2);
            if (glyph_id < start)     hi = mid - 1;
            else if (glyph_id > end)  lo = mid + 1;
            else {
                uint16_t start_idx = rd16(r + 4);
                return start_idx + (glyph_id - start);
            }
        }
    }
    return -1;
}

/*
 * Look up a glyph's class in a ClassDef table.
 * Returns class (0 if not found, which is the default class).
 */
static uint16_t gpos_class_lookup(const ttf_font_t *font, uint32_t cd_off,
                                    uint16_t glyph_id)
{
    if (cd_off == 0 || !safe_off(font, cd_off, 4)) return 0;
    const uint8_t *d = font->data + cd_off;
    uint16_t fmt = rd16(d);

    if (fmt == 1) {
        /* Format 1: range of glyphs starting at startGlyphID */
        if (!safe_off(font, cd_off, 6)) return 0;
        uint16_t start = rd16(d + 2);
        uint16_t count = rd16(d + 4);
        if (glyph_id < start || glyph_id >= start + count) return 0;
        uint32_t idx = glyph_id - start;
        if (!safe_off(font, cd_off + 6 + idx * 2, 2)) return 0;
        return rd16(d + 6 + idx * 2);
    } else if (fmt == 2) {
        /* Format 2: ranges */
        if (!safe_off(font, cd_off, 4)) return 0;
        uint16_t count = rd16(d + 2);
        if (!safe_off(font, cd_off + 4, (uint32_t)count * 6)) return 0;
        int lo = 0, hi = count - 1;
        while (lo <= hi) {
            int mid = (lo + hi) / 2;
            const uint8_t *r = d + 4 + mid * 6;
            uint16_t start = rd16(r);
            uint16_t end   = rd16(r + 2);
            if (glyph_id < start)     hi = mid - 1;
            else if (glyph_id > end)  lo = mid + 1;
            else return rd16(r + 4);
        }
    }
    return 0;
}

/*
 * Get the size of a value record from ValueFormat flags.
 */
static int gpos_value_size(uint16_t vfmt)
{
    int n = 0;
    for (int i = 0; i < 8; i++)
        if (vfmt & (1 << i)) n += 2;
    return n;
}

/*
 * Read XAdvance from a value record.
 * ValueFormat bit layout: 0=XPlacement, 1=YPlacement, 2=XAdvance, 3=YAdvance, ...
 */
static int16_t gpos_read_x_advance(const uint8_t *vr, uint16_t vfmt)
{
    if (!(vfmt & 0x04)) return 0;  /* no XAdvance field */
    int off = 0;
    if (vfmt & 0x01) off += 2;  /* XPlacement */
    if (vfmt & 0x02) off += 2;  /* YPlacement */
    return rd16s(vr + off);
}

/*
 * Search GPOS PairPos subtables for kern adjustment.
 */
static int gpos_kern_lookup(const ttf_font_t *font, uint16_t left, uint16_t right)
{
    for (int s = 0; s < font->gpos_kern_count; s++) {
        uint32_t st_off = font->gpos_kern_off[s];
        if (!safe_off(font, st_off, 10)) continue;
        const uint8_t *d = font->data + st_off;
        uint16_t fmt = rd16(d);
        uint16_t cov_off_rel = rd16(d + 2);
        uint16_t vfmt1 = rd16(d + 4);
        uint16_t vfmt2 = rd16(d + 6);

        uint32_t cov_abs = st_off + cov_off_rel;
        int cov_idx = gpos_coverage_index(font, cov_abs, left);
        if (cov_idx < 0) continue;

        if (fmt == 1) {
            /* Format 1: individual pair sets */
            if (!safe_off(font, st_off + 8, 2)) continue;
            uint16_t pair_set_count = rd16(d + 8);
            if (cov_idx >= pair_set_count) continue;

            /* Read PairSet offset */
            uint32_t ps_off_pos = st_off + 10 + (uint32_t)cov_idx * 2;
            if (!safe_off(font, ps_off_pos, 2)) continue;
            uint16_t ps_rel = rd16(font->data + ps_off_pos);
            uint32_t ps_abs = st_off + ps_rel;

            if (!safe_off(font, ps_abs, 2)) continue;
            uint16_t pvr_count = rd16(font->data + ps_abs);
            int rec_size = 2 + gpos_value_size(vfmt1) + gpos_value_size(vfmt2);
            if (!safe_off(font, ps_abs + 2, (uint32_t)pvr_count * rec_size))
                continue;

            /* Binary search within PairSet (sorted by SecondGlyph) */
            int lo = 0, hi = pvr_count - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                const uint8_t *rec = font->data + ps_abs + 2 + mid * rec_size;
                uint16_t gid2 = rd16(rec);
                if (right < gid2)      hi = mid - 1;
                else if (right > gid2) lo = mid + 1;
                else return (int)gpos_read_x_advance(rec + 2, vfmt1);
            }
        } else if (fmt == 2) {
            /* Format 2: class-based pairs */
            if (!safe_off(font, st_off + 8, 8)) continue;
            uint16_t cd1_rel = rd16(d + 8);
            uint16_t cd2_rel = rd16(d + 10);
            uint16_t c1count = rd16(d + 12);
            uint16_t c2count = rd16(d + 14);

            uint32_t cd1_abs = st_off + cd1_rel;
            uint32_t cd2_abs = st_off + cd2_rel;

            uint16_t cls1 = gpos_class_lookup(font, cd1_abs, left);
            uint16_t cls2 = gpos_class_lookup(font, cd2_abs, right);

            if (cls1 >= c1count || cls2 >= c2count) continue;

            int v1sz = gpos_value_size(vfmt1);
            int v2sz = gpos_value_size(vfmt2);
            int row_size = c2count * (v1sz + v2sz);
            uint32_t rec_off = st_off + 16
                               + (uint32_t)cls1 * row_size
                               + (uint32_t)cls2 * (v1sz + v2sz);
            if (!safe_off(font, rec_off, (uint32_t)(v1sz + v2sz))) continue;

            int16_t xa = gpos_read_x_advance(font->data + rec_off, vfmt1);
            if (xa != 0) return (int)xa;
        }
    }
    return 0;
}

/*
 * Parse GPOS table to find PairPos subtables referenced by the kern feature.
 */
static void parse_gpos_kern(ttf_font_t *font)
{
    font->gpos_kern_count = 0;
    if (font->tab_gpos.len == 0) return;

    uint32_t base = font->tab_gpos.off;
    if (!safe_off(font, base, 10)) return;
    const uint8_t *g = font->data + base;

    uint16_t feature_off = rd16(g + 6);
    uint16_t lookup_off  = rd16(g + 8);

    /* Find 'kern' feature */
    uint32_t fl_abs = base + feature_off;
    if (!safe_off(font, fl_abs, 2)) return;
    uint16_t feat_count = rd16(font->data + fl_abs);
    if (!safe_off(font, fl_abs + 2, (uint32_t)feat_count * 6)) return;

    uint32_t kern_tag = tag4("kern");
    int kern_lookup_indices[8];
    int kern_lookup_count = 0;

    for (int i = 0; i < feat_count && kern_lookup_count < 8; i++) {
        const uint8_t *fr = font->data + fl_abs + 2 + i * 6;
        uint32_t ftag = rd32(fr);
        if (ftag != kern_tag) continue;

        /* Read the feature table (fr+4 is offset from FeatureList) */
        uint32_t feat_abs = fl_abs + rd16(fr + 4);
        if (!safe_off(font, feat_abs, 4)) continue;
        uint16_t li_count = rd16(font->data + feat_abs + 2);
        if (!safe_off(font, feat_abs + 4, (uint32_t)li_count * 2)) continue;

        for (int j = 0; j < li_count && kern_lookup_count < 8; j++) {
            kern_lookup_indices[kern_lookup_count++] =
                (int)rd16(font->data + feat_abs + 4 + j * 2);
        }
        break;  /* use first kern feature found */
    }

    if (kern_lookup_count == 0) return;

    /* Resolve lookup indices to PairPos subtable offsets */
    uint32_t ll_abs = base + lookup_off;
    if (!safe_off(font, ll_abs, 2)) return;
    uint16_t lookup_count = rd16(font->data + ll_abs);
    if (!safe_off(font, ll_abs + 2, (uint32_t)lookup_count * 2)) return;

    for (int k = 0; k < kern_lookup_count; k++) {
        int li = kern_lookup_indices[k];
        if (li < 0 || li >= lookup_count) continue;

        uint16_t lk_rel = rd16(font->data + ll_abs + 2 + li * 2);
        uint32_t lk_abs = ll_abs + lk_rel;
        if (!safe_off(font, lk_abs, 6)) continue;

        uint16_t lk_type = rd16(font->data + lk_abs);
        uint16_t st_count = rd16(font->data + lk_abs + 4);

        /* Type 2 = PairPos */
        if (lk_type != 2) continue;
        if (!safe_off(font, lk_abs + 6, (uint32_t)st_count * 2)) continue;

        for (int si = 0; si < st_count && font->gpos_kern_count < 4; si++) {
            uint16_t st_rel = rd16(font->data + lk_abs + 6 + si * 2);
            uint32_t st_abs = lk_abs + st_rel;
            if (!safe_off(font, st_abs, 2)) continue;
            uint16_t fmt = rd16(font->data + st_abs);
            if (fmt == 1 || fmt == 2) {
                font->gpos_kern_off[font->gpos_kern_count++] = st_abs;
            }
        }
    }
}

/* =========================================================================
 * Public: kern table lookup (format 0, horizontal pairs only)
 * Also checks GPOS PairPos (kern feature) if kern table has no entry.
 * ========================================================================= */

int ttf_kern_lookup(const ttf_font_t *font, uint16_t left, uint16_t right)
{
    /* First try GPOS kern (more common in modern fonts) */
    if (font->gpos_kern_count > 0) {
        int gpos_val = gpos_kern_lookup(font, left, right);
        if (gpos_val != 0) return gpos_val;
    }

    if (font->tab_kern.len == 0) return 0;
    uint32_t base = font->tab_kern.off;
    if (!safe_off(font, base, 4)) return 0;

    const uint8_t *k = font->data + base;
    /* uint16_t version = rd16(k); */
    uint16_t n_tables = rd16(k + 2);
    uint32_t pos = base + 4;

    for (int t = 0; t < n_tables; t++) {
        if (!safe_off(font, pos, 6)) return 0;
        /* uint16_t sub_version = rd16(font->data + pos); */
        uint16_t sub_length  = rd16(font->data + pos + 2);
        uint16_t coverage    = rd16(font->data + pos + 4);

        /* We only support format 0 (horizontal kerning) */
        uint8_t format = (uint8_t)(coverage >> 8);
        int horizontal = (coverage & 0x01);

        if (format == 0 && horizontal) {
            if (!safe_off(font, pos + 6, 8)) return 0;
            uint16_t n_pairs = rd16(font->data + pos + 6);
            uint32_t pairs_off = pos + 14;
            if (!safe_off(font, pairs_off, (uint32_t)n_pairs * 6)) return 0;

            /* Binary search */
            uint32_t key = ((uint32_t)left << 16) | right;
            int lo = 0, hi = n_pairs - 1;
            while (lo <= hi) {
                int mid = (lo + hi) / 2;
                uint32_t pair = rd32(font->data + pairs_off + (uint32_t)mid * 6);
                if (key < pair) {
                    hi = mid - 1;
                } else if (key > pair) {
                    lo = mid + 1;
                } else {
                    return (int)rd16s(font->data + pairs_off + (uint32_t)mid * 6 + 4);
                }
            }
        }

        pos += sub_length;
    }
    return 0;
}

/* =========================================================================
 * Public: font initialization
 * ========================================================================= */

int ttf_font_init(ttf_font_t *font, const uint8_t *data, size_t len)
{
    memset(font, 0, sizeof(*font));
    font->data     = data;
    font->data_len = len;

    if (len < 12) return -1;

    /* Validate sfnt signature */
    uint32_t sfnt_ver = rd32(data);
    if (sfnt_ver != 0x00010000 &&   /* TrueType */
        sfnt_ver != tag4("OTTO") && /* CFF-based OpenType */
        sfnt_ver != tag4("true"))   /* Apple TrueType */
        return -1;

    /* Find required tables */
    if (find_table(font, tag4("head"), &font->tab_head) != 0) return -1;
    if (find_table(font, tag4("hhea"), &font->tab_hhea) != 0) return -1;
    if (find_table(font, tag4("maxp"), &font->tab_maxp) != 0) return -1;
    if (find_table(font, tag4("cmap"), &font->tab_cmap) != 0) return -1;
    if (find_table(font, tag4("hmtx"), &font->tab_hmtx) != 0) return -1;

    /* Optional tables */
    find_table(font, tag4("OS/2"), &font->tab_os2);
    find_table(font, tag4("kern"), &font->tab_kern);
    find_table(font, tag4("GPOS"), &font->tab_gpos);

    /* Determine outline format */
    int has_glyf = (find_table(font, tag4("loca"), &font->tab_loca) == 0 &&
                    find_table(font, tag4("glyf"), &font->tab_glyf) == 0);
    int has_cff  = (find_table(font, tag4("CFF "), &font->tab_cff) == 0);

    if (!has_glyf && !has_cff) return -1;
    font->has_glyf = has_glyf;

    /* Parse required tables */
    if (parse_head(font) != 0) return -1;
    if (parse_hhea(font) != 0) return -1;
    if (parse_maxp(font) != 0) return -1;
    if (parse_cmap(font) != 0) return -1;

    /* OS/2 overrides hhea metrics if available */
    parse_os2(font);

    /* Parse GPOS kern feature if available */
    parse_gpos_kern(font);

    /* Parse CFF tables if needed */
    if (!has_glyf) {
        if (parse_cff(font) != 0) return -1;
    }

    return 0;
}
