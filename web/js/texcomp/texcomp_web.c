/*
 * texcomp_web.c — thin Emscripten ABI over the tinyexr `texcomp` library for the
 * web texture-compression demo (web/js/texcomp.{js,html}).
 *
 * Basis-free: the browser compresses an RGBA8 image to the `uni` UASTC-subset
 * intermediate once, then transcodes it per-device to the GPU-native block
 * format the browser advertises (BC7 desktop, ASTC/ETC2 mobile) or decodes it
 * back to RGBA8 as a universal fallback. No basis_universal / KTX2Loader needed.
 *
 * All buffers are caller-managed: JS allocates with Module._malloc, fills the
 * input, calls the transcode, reads the output from HEAPU8, then frees.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include <emscripten.h>
#include <stddef.h>
#include <stdint.h>

#include "texcomp.h"

/* Transcode targets (must match TARGET_* in texcomp.js). */
enum {
  TCW_TARGET_BC7 = 0,
  TCW_TARGET_ASTC_4x4 = 1,
  TCW_TARGET_ETC2_RGBA = 2,
  TCW_TARGET_RGBA8 = 3, /* uncompressed fallback */
};

/* Size in bytes of the `uni` intermediate for a w*h image. */
EMSCRIPTEN_KEEPALIVE
size_t tcw_uni_size(uint32_t w, uint32_t h) {
  return tc_uni_compressed_size(w, h);
}

/* BC7 / ASTC 4x4 / ETC2_RGBA all pack one 4x4 block into 16 bytes. */
EMSCRIPTEN_KEEPALIVE
size_t tcw_block_size(uint32_t w, uint32_t h) {
  return (size_t)((w + 3u) / 4u) * (size_t)((h + 3u) / 4u) * 16u;
}

/* Encode tightly-packed RGBA8 -> uni. Returns 0 (TC_SUCCESS) on success. */
EMSCRIPTEN_KEEPALIVE
int tcw_compress_uni(const uint8_t *rgba, uint32_t w, uint32_t h, uint8_t *out,
                     size_t out_size) {
  return (int)tc_uni_compress_rgba8(rgba, w, h, (size_t)w * 4u, out, out_size);
}

/* Transcode uni -> target block format. Returns 0 on success, -1 for an
 * unknown target. */
EMSCRIPTEN_KEEPALIVE
int tcw_transcode(const uint8_t *uni, uint32_t w, uint32_t h, int target,
                  uint8_t *out, size_t out_size) {
  switch (target) {
    case TCW_TARGET_BC7:
      return (int)tc_uni_transcode_bc7(uni, w, h, out, out_size);
    case TCW_TARGET_ASTC_4x4:
      return (int)tc_uni_transcode_astc(uni, w, h, out, out_size);
    case TCW_TARGET_ETC2_RGBA:
      return (int)tc_uni_transcode_etc2(uni, w, h, /*alpha=*/1, out, out_size);
    default:
      return -1;
  }
}

/* Decode uni -> tightly-packed RGBA8 (universal fallback). */
EMSCRIPTEN_KEEPALIVE
int tcw_decompress_rgba8(const uint8_t *uni, uint32_t w, uint32_t h,
                         uint8_t *out, size_t out_size) {
  return (int)tc_uni_decompress_rgba8(uni, w, h, (size_t)w * 4u, out, out_size);
}
