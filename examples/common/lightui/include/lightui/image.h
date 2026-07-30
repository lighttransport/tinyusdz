/*
 * lightui/image.h — Image loading (PNG, JPEG, GIF, BMP) via Wuffs
 *
 * Decodes common image formats into lvg_surface_t pixel buffers.
 * Uses Wuffs (Wrangling Untrusted File Formats Safely) for memory-safe,
 * high-performance decoding with no external dependencies.
 *
 * Supported formats: PNG, JPEG, GIF (first frame), BMP, TIFF, WebP, TGA
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_IMAGE_H
#define LIGHTUI_IMAGE_H

#include <lightvg/surface.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Image format detection --------------------------------------------- */

typedef enum {
    LUI_IMAGE_UNKNOWN = 0,
    LUI_IMAGE_PNG,
    LUI_IMAGE_JPEG,
    LUI_IMAGE_GIF,
    LUI_IMAGE_BMP,
    LUI_IMAGE_TIFF,
    LUI_IMAGE_WEBP,
    LUI_IMAGE_TGA,
} lui_image_format_t;

/**
 * Detect the image format from the first bytes of a buffer.
 * Needs at least 16 bytes for reliable detection.
 */
lui_image_format_t lui_image_detect(const void *data, int len);

/* ---- Loading from memory ------------------------------------------------ */

/**
 * Load an image from a memory buffer and return a new surface.
 * The surface pixel format is ARGB (0xAARRGGBB), matching lvg_surface_t.
 *
 * @data   Pointer to the encoded image bytes (PNG, JPEG, etc.).
 * @len    Length in bytes.
 * @return Newly allocated surface, or NULL on failure.
 *         Caller must free with lvg_surface_destroy().
 */
lvg_surface_t *lui_image_load_mem(const void *data, int len);

/**
 * Query image dimensions without decoding the full image.
 *
 * @data       Pointer to the encoded image bytes.
 * @len        Length in bytes.
 * @out_width  Receives the image width (may be NULL).
 * @out_height Receives the image height (may be NULL).
 * @return     0 on success, -1 on failure.
 */
int lui_image_info_mem(const void *data, int len,
                       int *out_width, int *out_height);

/* ---- Loading from file -------------------------------------------------- */

/**
 * Load an image from a file path and return a new surface.
 *
 * @path   File path (UTF-8 on all platforms).
 * @return Newly allocated surface, or NULL on failure.
 *         Caller must free with lvg_surface_destroy().
 */
lvg_surface_t *lui_image_load_file(const char *path);

/**
 * Query image dimensions from a file without decoding.
 *
 * @path       File path.
 * @out_width  Receives the image width (may be NULL).
 * @out_height Receives the image height (may be NULL).
 * @return     0 on success, -1 on failure.
 */
int lui_image_info_file(const char *path,
                        int *out_width, int *out_height);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUI_IMAGE_H */
