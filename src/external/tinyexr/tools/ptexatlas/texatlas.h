/* SPDX-License-Identifier: BSD-3-Clause
 * Small C11 texture-atlas helpers shared by TinyEXR tools and TinyUSDZ.
 */
#ifndef TINYEXR_TEXATLAS_H_
#define TINYEXR_TEXATLAS_H_

#include <stddef.h>
#include <stdint.h>

typedef struct tinyexr_atlas_image {
  uint32_t width, height;
  uint32_t channels;
  uint8_t *pixels;
} tinyexr_atlas_image;

/* Packs square/rectangular RGBA8 tiles into a row-major atlas. The returned
 * pixels are malloc-owned by the caller. */
int tinyexr_atlas_pack_rgba8(const tinyexr_atlas_image *tiles, size_t tile_count,
                             uint32_t tile_edge, uint32_t max_columns,
                             tinyexr_atlas_image *atlas,
                             uint32_t *columns, uint32_t *rows);
void tinyexr_atlas_free(tinyexr_atlas_image *image);

#endif
