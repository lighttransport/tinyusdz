/*
 * lightui/export.h — Canvas recording + SVG / PDF export
 *
 * Records canvas drawing commands into a command buffer, then serializes
 * them to SVG or minimal PDF (no external dependencies).
 *
 * Usage:
 *   lui_recorder_t *rec = lui_recorder_create(800, 600);
 *   lui_rec_fill_rect(rec, 10, 10, 100, 50, LVG_COLOR_RGB(0xFF, 0, 0));
 *   lui_rec_draw_text(rec, 20, 40, "Hello", -1, "sans-serif", 14.0f,
 *                     LVG_COLOR_BLACK);
 *   lui_export_svg(rec, "output.svg");
 *   lui_export_pdf(rec, "output.pdf");
 *   lui_recorder_destroy(rec);
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_EXPORT_H
#define LIGHTUI_EXPORT_H

#include <lightvg/canvas.h>
#include <lightvg/types.h>

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Command types ------------------------------------------------------ */

typedef enum {
    LUI_CMD_CLEAR,
    LUI_CMD_SET_CLIP,
    LUI_CMD_RESET_CLIP,
    LUI_CMD_FILL_RECT,
    LUI_CMD_STROKE_RECT,
    LUI_CMD_FILL_CIRCLE,
    LUI_CMD_STROKE_CIRCLE,
    LUI_CMD_FILL_ELLIPSE,
    LUI_CMD_STROKE_ELLIPSE,
    LUI_CMD_FILL_ROUNDED_RECT,
    LUI_CMD_STROKE_ROUNDED_RECT,
    LUI_CMD_FILL_TRIANGLE,
    LUI_CMD_FILL_POLYGON,
    LUI_CMD_STROKE_POLYGON,
    LUI_CMD_DRAW_LINE,
    LUI_CMD_DRAW_POLYLINE,
    LUI_CMD_DRAW_LINE_AA,
    LUI_CMD_DRAW_POLYLINE_AA,
    LUI_CMD_DRAW_LINE_DASHED,
    LUI_CMD_DRAW_POLYLINE_DASHED,
    LUI_CMD_DRAW_THICK_POLYLINE,
    LUI_CMD_DRAW_ARROW,
    LUI_CMD_FILL_RECT_HATCHED,
    LUI_CMD_FILL_POLYGON_HATCHED,
    LUI_CMD_DRAW_IMAGE,
    LUI_CMD_DRAW_TEXT,
} lui_cmd_type_t;

/* ---- Command structure -------------------------------------------------- */

typedef struct {
    lui_cmd_type_t type;
    lvg_color_t    color;         /* primary colour */
    lvg_color_t    color2;        /* secondary (bg for hatch, fg for hatch) */
    int            stroke_width;

    union {
        struct { int x, y, w, h; }                           rect;
        struct { int cx, cy, r; }                            circle;
        struct { int cx, cy, rx, ry; }                       ellipse;
        struct { int x, y, w, h, radius; }                   rounded_rect;
        struct { int x0, y0, x1, y1, x2, y2; }              triangle;
        struct { int data_offset; int count; }               polygon;
        struct { int x0, y0, x1, y1; }                       line;
        struct { int data_offset; int count; }               polyline;
        struct { float x0, y0, x1, y1; }                     line_aa;
        struct { int data_offset; int count; }               polyline_aa;
        struct { int x0, y0, x1, y1;
                 int dash_offset; int dash_count;
                 int dash_phase; }                           line_dashed;
        struct { int pts_offset; int pts_count;
                 int dash_offset; int dash_count;
                 int dash_phase; }                           polyline_dashed;
        struct { int data_offset; int count;
                 float width; bool closed; }                 thick_polyline;
        struct { int x0, y0, x1, y1;
                 lvg_arrow_style_t head, tail;
                 int head_size; }                            arrow;
        struct { int x, y, w, h;
                 lvg_hatch_style_t style;
                 int spacing, line_width; }                  hatch_rect;
        struct { int data_offset; int count;
                 lvg_hatch_style_t style;
                 int spacing, line_width; }                  hatch_polygon;
        struct { int dst_x, dst_y, dst_w, dst_h;
                 int img_w, img_h, img_stride;
                 int data_offset; }                          image;
        struct { int x, y;
                 int text_offset, text_len;
                 int font_offset;
                 float font_size; }                          text;
        struct { lvg_rect_t rect; }                          clip;
    } d;
} lui_cmd_t;

/* ---- Recorder ----------------------------------------------------------- */

typedef struct {
    int           width, height;   /* logical canvas dimensions */
    lui_cmd_t    *cmds;
    int           cmd_count, cmd_cap;
    uint8_t      *data;            /* variable-length blob storage */
    int           data_used, data_cap;
} lui_recorder_t;

/* ---- Lifecycle ---------------------------------------------------------- */

lui_recorder_t *lui_recorder_create(int width, int height);
void            lui_recorder_destroy(lui_recorder_t *rec);
void            lui_recorder_reset(lui_recorder_t *rec);

/* ---- Recording functions ------------------------------------------------ */

void lui_rec_clear(lui_recorder_t *rec, lvg_color_t color);
void lui_rec_set_clip(lui_recorder_t *rec, int x, int y, int w, int h);
void lui_rec_reset_clip(lui_recorder_t *rec);

void lui_rec_fill_rect(lui_recorder_t *rec,
                       int x, int y, int w, int h, lvg_color_t color);
void lui_rec_stroke_rect(lui_recorder_t *rec,
                         int x, int y, int w, int h,
                         lvg_color_t color, int stroke_width);

void lui_rec_fill_circle(lui_recorder_t *rec,
                         int cx, int cy, int r, lvg_color_t color);
void lui_rec_stroke_circle(lui_recorder_t *rec,
                           int cx, int cy, int r,
                           lvg_color_t color, int stroke_width);

void lui_rec_fill_ellipse(lui_recorder_t *rec,
                          int cx, int cy, int rx, int ry, lvg_color_t color);
void lui_rec_stroke_ellipse(lui_recorder_t *rec,
                            int cx, int cy, int rx, int ry,
                            lvg_color_t color, int stroke_width);

void lui_rec_fill_rounded_rect(lui_recorder_t *rec,
                               int x, int y, int w, int h,
                               int radius, lvg_color_t color);
void lui_rec_stroke_rounded_rect(lui_recorder_t *rec,
                                 int x, int y, int w, int h,
                                 int radius, lvg_color_t color,
                                 int stroke_width);

void lui_rec_fill_triangle(lui_recorder_t *rec,
                           int x0, int y0, int x1, int y1,
                           int x2, int y2, lvg_color_t color);

void lui_rec_fill_polygon(lui_recorder_t *rec,
                          const lvg_point_t *points, int count,
                          lvg_color_t color);
void lui_rec_stroke_polygon(lui_recorder_t *rec,
                            const lvg_point_t *points, int count,
                            lvg_color_t color, int stroke_width);

void lui_rec_draw_line(lui_recorder_t *rec,
                       int x0, int y0, int x1, int y1,
                       lvg_color_t color, int stroke_width);
void lui_rec_draw_polyline(lui_recorder_t *rec,
                           const lvg_point_t *points, int count,
                           lvg_color_t color, int stroke_width);

void lui_rec_draw_line_aa(lui_recorder_t *rec,
                          float x0, float y0, float x1, float y1,
                          lvg_color_t color);
void lui_rec_draw_polyline_aa(lui_recorder_t *rec,
                              const float *xy_pairs, int count,
                              lvg_color_t color);

void lui_rec_draw_line_dashed(lui_recorder_t *rec,
                              int x0, int y0, int x1, int y1,
                              lvg_color_t color, int stroke_width,
                              const lvg_dash_t *dash);
void lui_rec_draw_polyline_dashed(lui_recorder_t *rec,
                                  const lvg_point_t *points, int count,
                                  lvg_color_t color, int stroke_width,
                                  const lvg_dash_t *dash);

void lui_rec_draw_thick_polyline(lui_recorder_t *rec,
                                 const lvg_pointf_t *points, int count,
                                 lvg_color_t color, float width, bool closed);

void lui_rec_draw_arrow(lui_recorder_t *rec,
                        int x0, int y0, int x1, int y1,
                        lvg_color_t color, int stroke_width,
                        lvg_arrow_style_t head, lvg_arrow_style_t tail,
                        int head_size);

void lui_rec_fill_rect_hatched(lui_recorder_t *rec,
                               int x, int y, int w, int h,
                               lvg_color_t bg_color, lvg_color_t fg_color,
                               lvg_hatch_style_t style,
                               int spacing, int line_width);
void lui_rec_fill_polygon_hatched(lui_recorder_t *rec,
                                  const lvg_point_t *points, int count,
                                  lvg_color_t bg_color, lvg_color_t fg_color,
                                  lvg_hatch_style_t style,
                                  int spacing, int line_width);

void lui_rec_draw_image(lui_recorder_t *rec,
                        int dst_x, int dst_y, int dst_w, int dst_h,
                        const lvg_surface_t *src, const lvg_rect_t *src_rect);

/**
 * Record a text drawing command.
 * @font_family  CSS-like font family name (e.g. "sans-serif", "monospace").
 *               Copied internally.
 * @font_size    Font size in points.
 */
void lui_rec_draw_text(lui_recorder_t *rec,
                       int x, int y,
                       const char *utf8, int len,
                       const char *font_family, float font_size,
                       lvg_color_t color);

/* ---- Export ------------------------------------------------------------- */

/** Export recorded commands to an SVG file.  Returns 0 on success. */
int lui_export_svg(const lui_recorder_t *rec, const char *path);

/** Export recorded commands to a PDF file.  Returns 0 on success. */
int lui_export_pdf(const lui_recorder_t *rec, const char *path);

/** Export to an SVG string (caller must free).  Sets *out_len if non-NULL. */
char *lui_export_svg_string(const lui_recorder_t *rec, int *out_len);

/** Export to a PDF byte buffer (caller must free).  Sets *out_len. */
uint8_t *lui_export_pdf_bytes(const lui_recorder_t *rec, int *out_len);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_EXPORT_H */
