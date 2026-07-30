/*
 * lightui/image_cmp.h — Pixel-level image comparison (RMSE, PSNR, SSIM)
 *
 * Pure C99 comparison framework for evaluating rendering output.
 * Computes per-pixel statistics in a single pass, plus SSIM on luminance
 * with an 8x8 sliding window.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_IMAGE_CMP_H
#define LIGHTUI_IMAGE_CMP_H

#include <lightvg/surface.h>
#include <lightvg/types.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Channel mode for per-channel stats --------------------------------- */

typedef enum {
    LUI_CMP_RGB       = 0,   /* aggregate across R, G, B          */
    LUI_CMP_RED       = 1,   /* red channel only                   */
    LUI_CMP_GREEN     = 2,   /* green channel only                 */
    LUI_CMP_BLUE      = 3,   /* blue channel only                  */
    LUI_CMP_ALPHA     = 4,   /* alpha channel only                 */
    LUI_CMP_LUMINANCE = 5,   /* 0.2126R + 0.7152G + 0.0722B       */
} lui_cmp_channel_t;

/* ---- Comparison result -------------------------------------------------- */

typedef struct {
    int    diff_pixels;       /* number of pixels with any channel diff > 0 */
    int    total_pixels;      /* width * height                              */
    int    max_channel_diff;  /* worst single-channel absolute difference    */
    double mean_diff;         /* mean absolute difference (per channel)      */
    double rmse;              /* root mean square error                      */
    double psnr_db;           /* peak signal-to-noise ratio in dB (inf if identical) */
    double ssim;              /* structural similarity index (1.0 = identical) */
} lui_cmp_result_t;

/* ---- Compare two surfaces ----------------------------------------------- */

/**
 * Compare surfaces @a and @b, filling @result with statistics.
 *
 * @channel  Which channel(s) to measure.
 * Returns 0 on success, -1 if dimensions differ or surfaces are NULL.
 */
int lui_image_cmp(const lvg_surface_t *a, const lvg_surface_t *b,
                  lui_cmp_channel_t channel, lui_cmp_result_t *result);

/* ---- Threshold check ---------------------------------------------------- */

/**
 * Convenience: returns 1 if the comparison passes thresholds, 0 otherwise.
 *
 * @min_ssim       Minimum acceptable SSIM (e.g. 0.99).
 * @max_diff_pix   Maximum number of differing pixels allowed.
 */
int lui_image_cmp_passes(const lui_cmp_result_t *result,
                         double min_ssim, int max_diff_pix);

/* ---- Side-by-side composite --------------------------------------------- */

/**
 * Generate a side-by-side comparison image:
 *   [ surface A | heatmap diff | surface B ]
 *
 * The output surface is 3x the width of the inputs.
 * Heatmap: identical=dim original, different=red intensity proportional
 * to the max channel difference.
 *
 * Returns a newly allocated surface (caller owns), or NULL on failure.
 */
lvg_surface_t *lui_image_cmp_side_by_side(const lvg_surface_t *a,
                                           const lvg_surface_t *b);

/* ---- Diff surface ------------------------------------------------------- */

/**
 * Generate a diff heatmap surface (same dimensions as inputs).
 *
 * Identical pixels are dimmed to 20% of the average.
 * Different pixels show red with intensity proportional to the error.
 *
 * Returns a newly allocated surface, or NULL on failure.
 */
lvg_surface_t *lui_image_cmp_diff_surface(const lvg_surface_t *a,
                                           const lvg_surface_t *b);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTUI_IMAGE_CMP_H */
