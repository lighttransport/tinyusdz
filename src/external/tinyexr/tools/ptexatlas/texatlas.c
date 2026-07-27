/* SPDX-License-Identifier: BSD-3-Clause */
#include "texatlas.h"
#include <stdlib.h>
#include <string.h>

void tinyexr_atlas_free(tinyexr_atlas_image *image) {
  if (!image) return;
  free(image->pixels);
  memset(image, 0, sizeof(*image));
}

int tinyexr_atlas_pack_rgba8(const tinyexr_atlas_image *tiles, size_t count,
                             uint32_t edge, uint32_t max_columns,
                             tinyexr_atlas_image *atlas, uint32_t *cols,
                             uint32_t *rows) {
  size_t bytes;
  uint32_t c, r;
  if (!atlas || !cols || !rows || (!tiles && count) || !edge || !count) return 0;
  c = max_columns ? max_columns : count > 4096u ? 64u : (uint32_t)count;
  if (c > count) c = (uint32_t)count;
  r = (uint32_t)((count + c - 1u) / c);
  if (c > UINT32_MAX / edge || r > UINT32_MAX / edge) return 0;
  if ((size_t)c * edge > SIZE_MAX / ((size_t)r * edge) / 4u) return 0;
  bytes = (size_t)c * edge * (size_t)r * edge * 4u;
  atlas->pixels = (uint8_t *)calloc(1, bytes);
  if (!atlas->pixels) return 0;
  atlas->width = c * edge; atlas->height = r * edge; atlas->channels = 4;
  for (size_t i = 0; i < count; ++i) {
    const tinyexr_atlas_image *t = &tiles[i];
    uint32_t ox = (uint32_t)(i % c) * edge, oy = (uint32_t)(i / c) * edge;
    if (!t->pixels || t->channels != 4 || !t->width || !t->height) { tinyexr_atlas_free(atlas); return 0; }
    for (uint32_t y = 0; y < edge; ++y) {
      uint32_t sy = (uint32_t)(((uint64_t)y * t->height) / edge);
      if (sy >= t->height) sy = t->height - 1;
      for (uint32_t x = 0; x < edge; ++x) {
        uint32_t sx = (uint32_t)(((uint64_t)x * t->width) / edge);
        if (sx >= t->width) sx = t->width - 1;
        memcpy(atlas->pixels + ((size_t)(oy+y)*atlas->width + ox+x)*4,
               t->pixels + ((size_t)sy*t->width + sx)*4, 4);
      }
    }
  }
  *cols = c; *rows = r;
  return 1;
}
