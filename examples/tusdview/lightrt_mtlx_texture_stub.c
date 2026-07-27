// SPDX-License-Identifier: Apache-2.0
// Texture-cache stub for tusdview's constant MaterialX graph evaluator.
//
// The full LightRT texture cache owns stb_image and is compile-checked
// separately. This stub lets tusdview link the real MaterialX graph evaluator
// without adding another image-loader implementation to the viewer. Image nodes
// therefore evaluate through their MaterialX `default` input for now.
#include "texture.h"

#include <stdlib.h>

struct TextureCache {
  int unused;
};

TextureCache *texcache_create(const char *base_dir) {
  (void)base_dir;
  return (TextureCache *)calloc(1, sizeof(TextureCache));
}

void texcache_free(TextureCache *tc) {
  free(tc);
}

void texcache_preload(TextureCache *tc, const struct MtlxDoc *doc) {
  (void)tc;
  (void)doc;
}

int texcache_get(TextureCache *tc, const char *rel_path, int srgb) {
  (void)tc;
  (void)rel_path;
  (void)srgb;
  return -1;
}

void texcache_sample(TextureCache *tc, int id, float u, float v, float out[4]) {
  (void)tc;
  (void)id;
  (void)u;
  (void)v;
  out[0] = 0.0f;
  out[1] = 0.0f;
  out[2] = 0.0f;
  out[3] = 1.0f;
}
