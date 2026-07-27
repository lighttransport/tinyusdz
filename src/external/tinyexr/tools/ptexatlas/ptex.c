/* SPDX-License-Identifier: BSD-3-Clause */
#include "ptex.h"
#include <stdlib.h>
#include <string.h>
#include "miniz.h"

static uint16_t u16(const uint8_t *p) { return (uint16_t)p[0] | (uint16_t)p[1] << 8; }
static uint32_t u32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t u64(const uint8_t *p) { return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32; }
static int range(size_t o, size_t n, size_t z) { return o <= z && n <= z - o; }
static size_t bpc(tinyexr_ptex_type t) { return t == TINYEXR_PTEX_UINT8 ? 1u : (t == TINYEXR_PTEX_UINT16 || t == TINYEXR_PTEX_HALF) ? 2u : 4u; }
static int inflate_block(const uint8_t *s, size_t n, size_t outn, uint8_t **out) {
  mz_ulong z = (mz_ulong)outn;
  *out = (uint8_t *)malloc(outn ? outn : 1);
  if (!*out || mz_uncompress(*out, &z, s, (mz_ulong)n) != MZ_OK || z != outn) { free(*out); *out = 0; return 0; }
  return 1;
}
static int parse_header(const uint8_t *d, size_t z, tinyexr_ptex_info *i,
                        uint32_t *ext, uint32_t *face_size, uint32_t *const_size,
                        uint32_t *level_info_size, uint64_t *level_data_size) {
  if (!d || z < 64 || memcmp(d, "Ptex", 4) || u32(d + 4) != 1 || u32(d + 12) > 3) return 0;
  i->type = (tinyexr_ptex_type)u32(d + 12); i->channels = u16(d + 20);
  i->levels = u16(d + 22); i->faces = u32(d + 24);
  *ext = u32(d + 28); *face_size = u32(d + 32); *const_size = u32(d + 36);
  *level_info_size = u32(d + 40); *level_data_size = u64(d + 48);
  return i->channels && i->levels && i->faces && *level_info_size == 16u * i->levels;
}
int tinyexr_ptex_info_memory(const uint8_t *d, size_t z, tinyexr_ptex_info *i) {
  uint32_t e, f, c, l; uint64_t s; return i && parse_header(d, z, i, &e, &f, &c, &l, &s);
}
void tinyexr_ptex_free(tinyexr_ptex_face *f) { if (f) { free(f->pixels); memset(f, 0, sizeof(*f)); } }

static void planar_to_interleaved(uint8_t *dst, const uint8_t *src, size_t samples,
                                  uint32_t channels, size_t bytes) {
  for (size_t p = 0; p < samples; ++p)
    for (uint32_t c = 0; c < channels; ++c)
      memcpy(dst + (p * channels + c) * bytes, src + (c * samples + p) * bytes, bytes);
}
static int undifference(uint8_t *p, size_t samples, uint32_t channels,
                        tinyexr_ptex_type type) {
  if (type == TINYEXR_PTEX_UINT8) {
    for (uint32_t c = 0; c < channels; ++c)
      for (size_t n = 1; n < samples; ++n)
        p[(size_t)c * samples + n] =
            (uint8_t)(p[(size_t)c * samples + n] + p[(size_t)c * samples + n - 1]);
    return 1;
  }
  if (type == TINYEXR_PTEX_UINT16) {
    for (uint32_t c = 0; c < channels; ++c) for (size_t n = 1; n < samples; ++n) {
      size_t at = ((size_t)c * samples + n) * 2, prev = at - 2;
      uint16_t v = (uint16_t)p[at] | (uint16_t)p[at + 1] << 8;
      uint16_t q = (uint16_t)p[prev] | (uint16_t)p[prev + 1] << 8;
      v = (uint16_t)(v + q); p[at] = (uint8_t)v; p[at + 1] = (uint8_t)(v >> 8);
    }
    return 1;
  }
  return 0;
}

int tinyexr_ptex_read_memory(const uint8_t *d, size_t z, uint32_t face, uint32_t level,
                             size_t max, tinyexr_ptex_face *out) {
  tinyexr_ptex_info i; uint32_t ext, face_size, const_size, level_info_size; uint64_t level_data_size;
  uint8_t *face_info = 0, *headers = 0, *planar = 0, *tmp = 0; size_t base, cursor = 0;
  uint32_t block_faces, header_size; uint64_t block_size; const uint8_t *block;
  if (!out || !parse_header(d, z, &i, &ext, &face_size, &const_size, &level_info_size, &level_data_size) || face >= i.faces || level >= i.levels) return 0;
  base = 64; if (!range(base, ext + face_size + const_size + level_info_size, z)) return 0;
  if (!inflate_block(d + base + ext, face_size, (size_t)i.faces * 20, &face_info)) return 0;
  const size_t info_base = base + ext + face_size + const_size;
  if (!range(info_base + level_info_size, (size_t)level_data_size, z)) { free(face_info); return 0; }
  for (uint32_t n = 0; n < level; ++n) { uint64_t sz = u64(d + info_base + n * 16); if (sz > level_data_size - cursor) { free(face_info); return 0; } cursor += (size_t)sz; }
  block_size = u64(d + info_base + level * 16); header_size = u32(d + info_base + level * 16 + 8); block_faces = u32(d + info_base + level * 16 + 12);
  if (block_size > level_data_size - cursor || header_size > block_size || block_faces > i.faces) { free(face_info); return 0; }
  uint32_t w = 1u << face_info[face * 20], h = 1u << face_info[face * 20 + 1]; w >>= level; h >>= level; if (!w) w = 1; if (!h) h = 1;
  const size_t bytes_per_sample = bpc(i.type), bytes = (size_t)w * h * i.channels * bytes_per_sample;
  if (bytes > max) { free(face_info); return 0; }
  if (face_info[face * 20 + 3] & 1u) {
    uint8_t *constants = 0;
    const size_t constant_bytes = (size_t)i.faces * i.channels * bytes_per_sample;
    if (!inflate_block(d + 64 + ext + face_size, const_size, constant_bytes, &constants)) { free(face_info); return 0; }
    out->pixels = (uint8_t *)malloc(bytes);
    if (!out->pixels) { free(constants); free(face_info); return 0; }
    const uint8_t *value = constants + (size_t)face * i.channels * bytes_per_sample;
    for (size_t p = 0; p < (size_t)w * h; ++p) memcpy(out->pixels + p * i.channels * bytes_per_sample, value, i.channels * bytes_per_sample);
    free(constants); free(face_info); out->width = w; out->height = h; out->channels = i.channels; out->bytes_per_channel = (uint32_t)bytes_per_sample; return 1;
  }
  block = d + info_base + level_info_size + cursor;
  if (!inflate_block(block, header_size, (size_t)block_faces * 4, &headers)) { free(face_info); return 0; }
  uint32_t ordinal = face;
  if (level > 0) {
    ordinal = 0;
    const uint32_t face_min = (1u << face_info[face * 20]) < (1u << face_info[face * 20 + 1])
                                  ? (1u << face_info[face * 20]) : (1u << face_info[face * 20 + 1]);
    for (uint32_t n = 0; n < i.faces; ++n) {
      if (face_info[n * 20 + 3] & 1u) continue;
      const uint32_t nmin = (1u << face_info[n * 20]) < (1u << face_info[n * 20 + 1])
                                ? (1u << face_info[n * 20]) : (1u << face_info[n * 20 + 1]);
      if ((nmin >> level) > (face_min >> level) ||
          ((nmin >> level) == (face_min >> level) && n < face)) ++ordinal;
    }
  }
  if (ordinal >= block_faces) { free(face_info); free(headers); return 0; }
  size_t payload = header_size; for (uint32_t n = 0; n < ordinal; ++n) payload += u32(headers + n * 4) & 0x3fffffffu;
  uint32_t packed = u32(headers + ordinal * 4), encoding = packed >> 30, compressed = packed & 0x3fffffffu;
  free(headers); if (payload > block_size || compressed > block_size - payload) { free(face_info); return 0; }
  if (encoding == 1 || encoding == 2) {
    if (!inflate_block(block + payload, compressed, bytes, &planar)) { free(face_info); return 0; }
    if (encoding == 2 && !undifference(planar, (size_t)w * h, i.channels, i.type)) { free(face_info); free(planar); return 0; }
    out->pixels = (uint8_t *)malloc(bytes); if (!out->pixels) { free(face_info); free(planar); return 0; }
    planar_to_interleaved(out->pixels, planar, (size_t)w * h, i.channels, bytes_per_sample); free(planar);
  } else if (encoding == 3) {
    if (compressed < 6 || !range(payload + compressed, 0, (size_t)block_size)) { free(face_info); return 0; }
    const uint8_t *q = block + payload; uint32_t tw = 1u << q[0], th = 1u << q[1], tile_header_size = u32(q + 2);
    uint32_t nx = (w + tw - 1) / tw, ny = (h + th - 1) / th; size_t tile_count = (size_t)nx * ny;
    uint8_t *tile_headers = 0; if (!inflate_block(q + 6, tile_header_size, tile_count * 4, &tile_headers)) { free(face_info); return 0; }
    out->pixels = (uint8_t *)calloc(1, bytes); if (!out->pixels) { free(tile_headers); free(face_info); return 0; }
    size_t tile_cursor = 6 + tile_header_size;
    for (uint32_t ty = 0; ty < ny; ++ty) for (uint32_t tx = 0; tx < nx; ++tx) {
      uint32_t tp = u32(tile_headers + ((size_t)ty * nx + tx) * 4), te = tp >> 30, ts = tp & 0x3fffffffu;
      uint32_t cw = tw < w - tx * tw ? tw : w - tx * tw, ch = th < h - ty * th ? th : h - ty * th;
      size_t tile_bytes = (size_t)cw * ch * i.channels * bytes_per_sample; if (tile_cursor > compressed || ts > compressed - tile_cursor) { tinyexr_ptex_free(out); free(tile_headers); free(face_info); return 0; }
      uint8_t *tile = 0; if (te == 0) { size_t value = i.channels * bytes_per_sample; if (ts < value) { tinyexr_ptex_free(out); free(tile_headers); free(face_info); return 0; } tile = (uint8_t *)malloc(tile_bytes); for (size_t p = 0; p < (size_t)cw * ch; ++p) memcpy(tile + p * value, block + payload + tile_cursor, value); }
      else if ((te == 1 || te == 2) && inflate_block(block + payload + tile_cursor, ts, tile_bytes, &tmp)) {
        tile = (uint8_t *)malloc(tile_bytes);
        if (tile && (te != 2 || undifference(tmp, (size_t)cw * ch, i.channels, i.type)))
          planar_to_interleaved(tile, tmp, (size_t)cw * ch, i.channels, bytes_per_sample);
        else { free(tile); tile = 0; }
        free(tmp); tmp = 0;
      }
      if (!tile) { tinyexr_ptex_free(out); free(tile_headers); free(face_info); return 0; }
      size_t pixel_bytes = (size_t)i.channels * bytes_per_sample; for (uint32_t y = 0; y < ch; ++y) memcpy(out->pixels + ((size_t)(ty * th + y) * w + tx * tw) * pixel_bytes, tile + (size_t)y * cw * pixel_bytes, (size_t)cw * pixel_bytes); free(tile); tile_cursor += ts;
    }
    free(tile_headers);
  } else { free(face_info); return 0; }
  free(face_info); out->width = w; out->height = h; out->channels = i.channels; out->bytes_per_channel = (uint32_t)bytes_per_sample; return 1;
}
