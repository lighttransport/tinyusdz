/*
 * lightui/geomap.h — Vector map (Mapbox Vector Tile) parser and renderer
 *
 * Loads MVT/PBF tiles (e.g. those served by Japan's GSI optimal_bvmap)
 * and renders them as a 2.5D oblique-projection map. Designed for the
 * GSI "optimal_bvmap-v1" layer set:
 *
 *   BldA      — building footprints       → extruded polygons (2.5D)
 *   RdCL      — road centerlines          → styled polylines
 *   RailCL    — railway centerlines       → styled polylines
 *   WA        — water areas               → flat polygons
 *   RvrCL     — river centerlines         → polylines
 *   SpcfArea  — parks / special areas     → flat polygons
 *   Anno      — labels                    — currently skipped
 *
 * Generic MVT input is also supported (any layer name); unknown layers
 * are categorised as LUI_GEOMAP_OTHER and rendered as muted polylines.
 *
 * Opt-in: LUI_BUILD_GEOMAP_TINY (OFF unless geomap_demo is requested).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_GEOMAP_H
#define LIGHTUI_GEOMAP_H

#include <lightvg/canvas.h>
#include <lightvg/types.h>

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct lui_geomap_doc lui_geomap_doc_t;

/** Bounding box over the union of loaded tiles, in tile-grid units. */
typedef struct {
    int z;                       /* zoom level all tiles share         */
    int x_min, y_min;            /* inclusive top-left tile coords     */
    int x_max, y_max;            /* inclusive bottom-right             */
    int extent;                  /* MVT tile extent (typically 4096)   */
} lui_geomap_tile_bbox_t;

/** Feature kind, derived from layer name. */
typedef enum {
    LUI_GEOMAP_OTHER     = 0,
    LUI_GEOMAP_BUILDING,         /* BldA                                */
    LUI_GEOMAP_ROAD,             /* RdCL                                */
    LUI_GEOMAP_RAIL,             /* RailCL                              */
    LUI_GEOMAP_WATER,            /* WA, RvrCL                           */
    LUI_GEOMAP_PARK,             /* SpcfArea                            */
    LUI_GEOMAP_ADMIN,            /* AdmArea, AdmBdry                    */
    LUI_GEOMAP_CONTOUR,          /* Cntr                                */
} lui_geomap_kind_t;

/** Construct an empty document. Add tiles with lui_geomap_add_tile_*. */
lui_geomap_doc_t *lui_geomap_create(void);

/** Free the document and all loaded tile geometry. NULL-safe. */
void              lui_geomap_destroy(lui_geomap_doc_t *doc);

/**
 * Parse an in-memory PBF tile and append its features to @doc.
 *
 * @z, @x, @y  Tile coordinates (used for world-space placement).
 * @pbf_data, @pbf_len  Uncompressed MVT/PBF bytes.
 * @return     LVG_OK on success, LVG_ERR_INVALID on malformed input,
 *             LVG_ERR_NOMEM on allocation failure.
 *
 * Tiles must share a common @z; mixing zoom levels is rejected.
 */
lvg_result_t      lui_geomap_add_tile(lui_geomap_doc_t *doc,
                                      int z, int x, int y,
                                      const void *pbf_data, size_t pbf_len);

/** Convenience: read the file then forward to lui_geomap_add_tile. */
lvg_result_t      lui_geomap_add_tile_file(lui_geomap_doc_t *doc,
                                           int z, int x, int y,
                                           const char *path);

lui_geomap_tile_bbox_t lui_geomap_get_tile_bbox(const lui_geomap_doc_t *doc);
int               lui_geomap_feature_count(const lui_geomap_doc_t *doc);

/**
 * View parameters for rendering.
 *
 *   tx, ty     world → screen translation (added after scale)
 *   scale      world → pixels (1.0 = native tile pixels)
 *   lift_x     screen-space displacement (world units) per world unit
 *   lift_y     of building height. Cabinet projection: a 30° receding
 *              direction with 0.5 foreshortening gives lift_x ≈ -0.25,
 *              lift_y ≈ -0.43. Pure top-down: lift_x = lift_y = 0.
 *              For a "tall pillar" view (one-axis), set lift_x = 0
 *              and lift_y < 0.
 *   story_h    story height in world units; building extrusion is
 *              `floors * story_h`.
 */
typedef struct {
    float tx, ty, scale;
    float lift_x, lift_y;
    float story_h;
} lui_geomap_view_t;

/** Reasonable defaults for browsing GSI optimal_bvmap tiles. */
void              lui_geomap_view_init_default(lui_geomap_view_t *v);

/**
 * Render @doc into @canvas using the supplied view. Buildings are
 * painted back-to-front (painter's algorithm) so closer buildings
 * occlude further ones in the oblique projection.
 *
 * Note on `const`: although @doc is declared `const`, the function
 * memoises a per-kind feature index and grows internal scratch
 * buffers on the document. Callers may render the same doc many
 * times (the cache speeds up the second-and-later frames), and
 * repeated renders with the same args produce identical output —
 * but rendering the same doc *concurrently* from multiple threads
 * is not safe. Hold an external lock per doc.
 *
 * Returns LVG_ERR_INVALID for NULL args or non-finite view fields.
 */
lvg_result_t      lui_geomap_render(const lui_geomap_doc_t *doc,
                                    lvg_canvas_t *canvas,
                                    const lui_geomap_view_t *view);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_GEOMAP_H */
