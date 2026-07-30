/*
 * lightui/svg.h — Minimal SVG-subset parser and renderer
 *
 * Scope: just enough to load the Ghostscript tiger and similar
 * static-vector test assets. Supports:
 *
 *   - <svg viewBox=...>, width/height
 *   - <g transform="..."> nesting (matrix, translate, scale, rotate, skewX/Y)
 *   - <path d="..."> with M/L/H/V/C/S/Q/T/Z (uppercase = absolute,
 *     lowercase = relative). Elliptical arcs (A/a) are skipped with a warn.
 *   - Inline fill / stroke / stroke-width / stroke-linecap / stroke-linejoin
 *     / fill-rule attributes, plus the same keys via style="...".
 *   - #rgb / #rrggbb / "none" / a small named-colour table.
 *
 * Out of scope: <use>, <defs>, <text>, gradients, filters, masks,
 * clipPaths, animation, CSS selectors. Those tags are skipped silently.
 *
 * The parser is opt-in (LUI_BUILD_SVG_TINY=ON) so the default lightui
 * build remains dependency-free.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_SVG_H
#define LIGHTUI_SVG_H

#include <lightvg/canvas.h>
#include <lightvg/types.h>

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lui_svg_doc lui_svg_doc_t;

typedef struct {
    float min_x, min_y, width, height;
} lui_svg_viewbox_t;

/**
 * Load and parse an SVG document from disk. Returns NULL on I/O or
 * parse failure (the parser itself is tolerant — it logs warnings and
 * skips unsupported constructs rather than failing the whole load).
 *
 * Free the returned document with lui_svg_destroy().
 */
lui_svg_doc_t *lui_svg_load_file(const char *path);

/**
 * Same as lui_svg_load_file(), but parses from an in-memory buffer.
 * @xml does not need to be NUL-terminated; @len is authoritative.
 */
lui_svg_doc_t *lui_svg_load_mem(const char *xml, size_t len);

/** Free a document returned by lui_svg_load_*. NULL-safe. */
void lui_svg_destroy(lui_svg_doc_t *doc);

/**
 * Get the document viewBox. Falls back to (0, 0, width, height) if no
 * viewBox attribute was present (and to (0, 0, 0, 0) if neither was).
 */
lui_svg_viewbox_t lui_svg_get_viewbox(const lui_svg_doc_t *doc);

/**
 * Render @doc into @canvas. The viewport transform is
 *
 *     screen = (svg_world * scale) + (tx, ty)
 *
 * applied on top of any baked-in <g transform>. @tol_dev_px controls
 * adaptive Bézier flattening (0.5 is a sensible default for hi-DPI
 * surfaces; raise to 1.0 for fast preview rendering).
 */
lvg_result_t lui_svg_render(const lui_svg_doc_t *doc,
                            lvg_canvas_t *canvas,
                            float tx, float ty, float scale,
                            float tol_dev_px);

/**
 * Number of parsed shapes in the document. Useful for tests and for
 * progress reporting on very large files.
 */
int lui_svg_shape_count(const lui_svg_doc_t *doc);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_SVG_H */
