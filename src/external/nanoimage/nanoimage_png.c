#include "nanoimage_png.h"

#include "nanoimage_alloc_internal.h"
#include "nanoimage_zlib.h"
#include "nanoimage_zlib_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static const uint8_t k_png_signature[8] = {0x89u, 0x50u, 0x4eu, 0x47u,
                                           0x0du, 0x0au, 0x1au, 0x0au};

static const uint8_t k_adam7_x_start[7] = {0u, 4u, 0u, 2u, 0u, 1u, 0u};
static const uint8_t k_adam7_y_start[7] = {0u, 0u, 4u, 0u, 2u, 0u, 1u};
static const uint8_t k_adam7_x_step[7] = {8u, 8u, 4u, 4u, 2u, 2u, 1u};
static const uint8_t k_adam7_y_step[7] = {8u, 8u, 8u, 4u, 4u, 2u, 2u};

static void ni_set_error(char *err, size_t cap, const char *fmt, ...) {
  va_list args;
  if ((err == NULL) || (cap == 0u)) {
    return;
  }
  va_start(args, fmt);
  (void)vsnprintf(err, cap, fmt, args);
  va_end(args);
}

static uint32_t ni_read_u32be(const uint8_t *p) {
  return ((uint32_t)p[0] << 24u) | ((uint32_t)p[1] << 16u) |
         ((uint32_t)p[2] << 8u) | (uint32_t)p[3];
}

static uint32_t ni_crc32(const uint8_t *data, size_t n) {
  uint32_t crc = 0xffffffffu;
  size_t i;
  for (i = 0; i < n; i++) {
    int j;
    crc ^= (uint32_t)data[i];
    for (j = 0; j < 8; j++) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

static int ni_size_mul(size_t a, size_t b, size_t *out) {
  if ((a != 0u) && (b > (SIZE_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int ni_size_add(size_t a, size_t b, size_t *out) {
  if (a > (SIZE_MAX - b)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int ni_scale_to_8bit(uint32_t v, uint8_t bit_depth, uint8_t *out) {
  uint32_t maxv;
  if ((bit_depth == 0u) || (bit_depth > 8u) || (out == NULL)) {
    return 0;
  }
  maxv = (1u << bit_depth) - 1u;
  *out = (uint8_t)((v * 255u + (maxv / 2u)) / maxv);
  return 1;
}

static uint32_t ni_read_packed_bits(const uint8_t *row, size_t pixel_index,
                                    uint8_t bit_depth) {
  const size_t bit_index = pixel_index * (size_t)bit_depth;
  const size_t byte_index = bit_index >> 3u;
  const uint8_t shift = (uint8_t)(8u - bit_depth - (bit_index & 7u));
  const uint8_t mask = (uint8_t)((1u << bit_depth) - 1u);
  return (uint32_t)((row[byte_index] >> shift) & mask);
}

static uint8_t ni_paeth(uint8_t a, uint8_t b, uint8_t c) {
  const int p = (int)a + (int)b - (int)c;
  const int pa = (p > (int)a) ? (p - (int)a) : ((int)a - p);
  const int pb = (p > (int)b) ? (p - (int)b) : ((int)b - p);
  const int pc = (p > (int)c) ? (p - (int)c) : ((int)c - p);
  if ((pa <= pb) && (pa <= pc)) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

static int ni_unfilter_row(uint8_t *row, const uint8_t *prev, size_t rowbytes,
                           size_t bytes_per_pixel, uint8_t filter) {
  size_t i;
  if (filter > 4u) {
    return 0;
  }
  for (i = 0; i < rowbytes; i++) {
    const uint8_t src = row[i];
    const uint8_t left = (i >= bytes_per_pixel) ? row[i - bytes_per_pixel] : 0u;
    const uint8_t up = (prev != NULL) ? prev[i] : 0u;
    const uint8_t up_left =
        ((prev != NULL) && (i >= bytes_per_pixel)) ? prev[i - bytes_per_pixel] : 0u;

    if (filter == 0u) {
      row[i] = src;
    } else if (filter == 1u) {
      row[i] = (uint8_t)(src + left);
    } else if (filter == 2u) {
      row[i] = (uint8_t)(src + up);
    } else if (filter == 3u) {
      row[i] = (uint8_t)(src + ((uint8_t)(((uint16_t)left + (uint16_t)up) >> 1u)));
    } else {
      row[i] = (uint8_t)(src + ni_paeth(left, up, up_left));
    }
  }
  return 1;
}

static uint32_t ni_pass_size(uint32_t full, uint8_t start, uint8_t step) {
  if (full <= start) {
    return 0u;
  }
  return (full - start + step - 1u) / step;
}

int ni_load_png_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity) {
  uint32_t width = 0u;
  uint32_t height = 0u;
  uint8_t bit_depth = 0u;
  uint8_t color_type = 0u;
  uint8_t interlace = 0u;
  uint8_t base_channels = 0u;
  int is_cgbi = 0;
  uint8_t out_channels = 0u;
  uint8_t out_sample_bytes = 0u;
  uint8_t out_bit_depth = 0u;
  size_t bits_per_pixel = 0u;
  size_t bytes_per_pixel_for_filter = 0u;
  size_t output_stride = 0u;
  size_t output_size = 0u;
  size_t idat_size = 0u;
  size_t idat_span_count = 0u;
  size_t raw_size = 0u;
  size_t max_rowbytes = 0u;
  size_t off;
  int saw_ihdr = 0;
  int saw_plte = 0;
  int saw_trns = 0;
  int saw_idat = 0;
  int saw_iend = 0;
  ni_zlib_span *idat_spans = NULL;
  uint8_t *raw = NULL;
  uint8_t *pixels = NULL;
  uint8_t *palette = NULL;
  size_t palette_entries = 0u;
  uint8_t *palette_alpha = NULL;
  size_t palette_alpha_count = 0u;

  if ((bytes == NULL) || (out == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }
  memset(out, 0, sizeof(*out));

  if ((size < sizeof(k_png_signature)) ||
      (memcmp(bytes, k_png_signature, sizeof(k_png_signature)) != 0)) {
    ni_set_error(err, err_capacity, "invalid PNG signature");
    return 0;
  }

  off = sizeof(k_png_signature);
  while ((off + 12u) <= size) {
    const size_t chunk_start = off;
    const uint32_t chunk_len = ni_read_u32be(bytes + off);
    const uint8_t *chunk_type = bytes + off + 4u;
    const uint8_t *chunk_data = bytes + off + 8u;
    size_t chunk_total = 0u;
    size_t chunk_crc_len = 0u;
    uint32_t expected_crc;
    uint32_t computed_crc;

    if (!ni_size_add(8u, (size_t)chunk_len, &chunk_crc_len) ||
        !ni_size_add(chunk_crc_len, 4u, &chunk_total)) {
      ni_set_error(err, err_capacity, "PNG chunk size overflow");
      goto fail;
    }
    if ((off + chunk_total) > size) {
      ni_set_error(err, err_capacity, "truncated PNG chunk");
      goto fail;
    }

    expected_crc = ni_read_u32be(bytes + off + 8u + (size_t)chunk_len);
    computed_crc = ni_crc32(chunk_type, chunk_crc_len - 4u);
    if (computed_crc != expected_crc) {
      ni_set_error(err, err_capacity, "PNG CRC mismatch");
      goto fail;
    }

    if (memcmp(chunk_type, "IHDR", 4u) == 0) {
      if (saw_ihdr || (chunk_len != 13u)) {
        ni_set_error(err, err_capacity, "invalid IHDR chunk");
        goto fail;
      }
      width = ni_read_u32be(chunk_data);
      height = ni_read_u32be(chunk_data + 4u);
      bit_depth = chunk_data[8];
      color_type = chunk_data[9];
      if ((width == 0u) || (height == 0u)) {
        ni_set_error(err, err_capacity, "PNG dimensions must be non-zero");
        goto fail;
      }
      if (chunk_data[10] != 0u || chunk_data[11] != 0u) {
        ni_set_error(err, err_capacity, "unsupported PNG compression/filter method");
        goto fail;
      }
      if ((chunk_data[12] != 0u) && (chunk_data[12] != 1u)) {
        ni_set_error(err, err_capacity, "unsupported PNG interlace method");
        goto fail;
      }
      interlace = chunk_data[12];
      saw_ihdr = 1;
    } else if (memcmp(chunk_type, "PLTE", 4u) == 0) {
      uint8_t *new_palette;
      if (!saw_ihdr || saw_plte || saw_idat || (chunk_len == 0u) ||
          ((chunk_len % 3u) != 0u)) {
        ni_set_error(err, err_capacity, "invalid PLTE chunk");
        goto fail;
      }
      if ((color_type != 2u) && (color_type != 3u) && (color_type != 6u)) {
        ni_set_error(err, err_capacity, "PLTE is not allowed for this PNG color type");
        goto fail;
      }
      if ((chunk_len / 3u) > 256u) {
        ni_set_error(err, err_capacity, "PLTE has too many entries");
        goto fail;
      }
      new_palette = (uint8_t *)ni_stbi_malloc((size_t)chunk_len);
      if (new_palette == NULL) {
        ni_set_error(err, err_capacity, "out of memory for PLTE");
        goto fail;
      }
      memcpy(new_palette, chunk_data, (size_t)chunk_len);
      ni_stbi_free(palette);
      palette = new_palette;
      palette_entries = (size_t)chunk_len / 3u;
      saw_plte = 1;
    } else if (memcmp(chunk_type, "CgBI", 4u) == 0) {
      if (saw_ihdr || is_cgbi) {
        ni_set_error(err, err_capacity, "invalid CgBI chunk order");
        goto fail;
      }
      is_cgbi = 1;
    } else if (memcmp(chunk_type, "tRNS", 4u) == 0) {
      uint8_t *new_alpha;
      if (!saw_ihdr || saw_trns || saw_idat) {
        ni_set_error(err, err_capacity, "invalid tRNS chunk order");
        goto fail;
      }
      if (color_type == 3u) {
        if ((palette_entries == 0u) || ((size_t)chunk_len > palette_entries)) {
          ni_set_error(err, err_capacity, "invalid tRNS chunk");
          goto fail;
        }
        new_alpha = (uint8_t *)ni_stbi_malloc((size_t)chunk_len);
        if ((chunk_len > 0u) && (new_alpha == NULL)) {
          ni_set_error(err, err_capacity, "out of memory for tRNS");
          goto fail;
        }
        if (chunk_len > 0u) {
          memcpy(new_alpha, chunk_data, (size_t)chunk_len);
        }
        ni_stbi_free(palette_alpha);
        palette_alpha = new_alpha;
        palette_alpha_count = (size_t)chunk_len;
      }
      saw_trns = 1;
    } else if (memcmp(chunk_type, "IDAT", 4u) == 0) {
      ni_zlib_span *new_idat_spans;
      size_t new_span_count;
      size_t spans_size;
      if (!saw_ihdr) {
        ni_set_error(err, err_capacity, "IDAT appears before IHDR");
        goto fail;
      }
      if (!ni_size_add(idat_size, (size_t)chunk_len, &idat_size)) {
        ni_set_error(err, err_capacity, "PNG IDAT too large");
        goto fail;
      }
      if (!ni_size_add(idat_span_count, 1u, &new_span_count) ||
          !ni_size_mul(new_span_count, sizeof(*idat_spans), &spans_size)) {
        ni_set_error(err, err_capacity, "PNG IDAT span table overflow");
        goto fail;
      }
      new_idat_spans = (ni_zlib_span *)ni_stbi_realloc(idat_spans, spans_size);
      if (new_idat_spans == NULL) {
        ni_set_error(err, err_capacity, "out of memory collecting IDAT spans");
        goto fail;
      }
      idat_spans = new_idat_spans;
      idat_spans[idat_span_count].data = chunk_data;
      idat_spans[idat_span_count].size = (size_t)chunk_len;
      idat_span_count = new_span_count;
      saw_idat = 1;
    } else if (memcmp(chunk_type, "IEND", 4u) == 0) {
      if (chunk_len != 0u) {
        ni_set_error(err, err_capacity, "invalid IEND chunk");
        goto fail;
      }
      saw_iend = 1;
      break;
    }

    off = chunk_start + chunk_total;
  }

  if (!saw_ihdr || !saw_iend || (idat_size == 0u)) {
    ni_set_error(err, err_capacity, "missing required PNG chunks");
    goto fail;
  }

  if (color_type == 0u) {
    base_channels = 1u;
    if (!(bit_depth == 1u || bit_depth == 2u || bit_depth == 4u ||
          bit_depth == 8u || bit_depth == 16u)) {
      ni_set_error(err, err_capacity, "unsupported grayscale PNG bit depth");
      goto fail;
    }
  } else if (color_type == 2u) {
    base_channels = 3u;
    if (!(bit_depth == 8u || bit_depth == 16u)) {
      ni_set_error(err, err_capacity, "unsupported truecolor PNG bit depth");
      goto fail;
    }
  } else if (color_type == 3u) {
    base_channels = 1u;
    if (!(bit_depth == 1u || bit_depth == 2u || bit_depth == 4u || bit_depth == 8u)) {
      ni_set_error(err, err_capacity, "unsupported indexed PNG bit depth");
      goto fail;
    }
    if ((palette == NULL) || (palette_entries == 0u)) {
      ni_set_error(err, err_capacity, "indexed PNG requires PLTE");
      goto fail;
    }
    if (palette_entries > ((size_t)1u << bit_depth)) {
      ni_set_error(err, err_capacity, "indexed PNG palette exceeds bit depth");
      goto fail;
    }
  } else if (color_type == 4u) {
    base_channels = 2u;
    if (!(bit_depth == 8u || bit_depth == 16u)) {
      ni_set_error(err, err_capacity, "unsupported gray+alpha PNG bit depth");
      goto fail;
    }
  } else if (color_type == 6u) {
    base_channels = 4u;
    if (!(bit_depth == 8u || bit_depth == 16u)) {
      ni_set_error(err, err_capacity, "unsupported RGBA PNG bit depth");
      goto fail;
    }
  } else {
    ni_set_error(err, err_capacity, "unsupported PNG color type");
    goto fail;
  }

  if (!ni_size_mul((size_t)base_channels, (size_t)bit_depth, &bits_per_pixel)) {
    ni_set_error(err, err_capacity, "PNG pixel size overflow");
    goto fail;
  }
  bytes_per_pixel_for_filter = (bits_per_pixel + 7u) >> 3u;
  if (bytes_per_pixel_for_filter == 0u) {
    bytes_per_pixel_for_filter = 1u;
  }

  if (color_type == 3u) {
    out_channels = (palette_alpha_count > 0u) ? 4u : 3u;
    out_bit_depth = 8u;
    out_sample_bytes = 1u;
  } else {
    out_channels = base_channels;
    out_bit_depth = bit_depth;
    out_sample_bytes = (bit_depth == 16u) ? 2u : 1u;
  }

  if (!ni_size_mul((size_t)width, (size_t)out_channels, &output_stride) ||
      !ni_size_mul(output_stride, (size_t)out_sample_bytes, &output_stride) ||
      !ni_size_mul(output_stride, (size_t)height, &output_size)) {
    ni_set_error(err, err_capacity, "PNG output buffer size overflow");
    goto fail;
  }
  pixels = (uint8_t *)ni_stbi_malloc(output_size);
  if (pixels == NULL) {
    ni_set_error(err, err_capacity, "out of memory for PNG pixels");
    goto fail;
  }
  memset(pixels, 0, output_size);

  if (interlace == 0u) {
    size_t rowbytes;
    if (!ni_size_mul((size_t)width, bits_per_pixel, &rowbytes)) {
      ni_set_error(err, err_capacity, "PNG row size overflow");
      goto fail;
    }
    rowbytes = (rowbytes + 7u) >> 3u;
    max_rowbytes = rowbytes;
    if (!ni_size_add(rowbytes, 1u, &rowbytes) ||
        !ni_size_mul(rowbytes, (size_t)height, &raw_size)) {
      ni_set_error(err, err_capacity, "PNG raw size overflow");
      goto fail;
    }
  } else {
    size_t pass;
    raw_size = 0u;
    for (pass = 0u; pass < 7u; pass++) {
      const uint32_t pw =
          ni_pass_size(width, k_adam7_x_start[pass], k_adam7_x_step[pass]);
      const uint32_t ph =
          ni_pass_size(height, k_adam7_y_start[pass], k_adam7_y_step[pass]);
      size_t rowbytes;
      size_t pass_size;
      if (pw == 0u || ph == 0u) {
        continue;
      }
      if (!ni_size_mul((size_t)pw, bits_per_pixel, &rowbytes)) {
        ni_set_error(err, err_capacity, "PNG interlaced row size overflow");
        goto fail;
      }
      rowbytes = (rowbytes + 7u) >> 3u;
      if (rowbytes > max_rowbytes) {
        max_rowbytes = rowbytes;
      }
      if (!ni_size_add(rowbytes, 1u, &rowbytes) ||
          !ni_size_mul(rowbytes, (size_t)ph, &pass_size) ||
          !ni_size_add(raw_size, pass_size, &raw_size)) {
        ni_set_error(err, err_capacity, "PNG interlaced raw size overflow");
        goto fail;
      }
    }
  }

  raw = (uint8_t *)ni_stbi_malloc(raw_size);
  if (raw == NULL) {
    ni_set_error(err, err_capacity, "out of memory for PNG raw buffer");
    goto fail;
  }
  {
    size_t inflated = 0u;
    if (!(is_cgbi ? ni_zlib_inflate_raw_spans(idat_spans, idat_span_count, raw,
                                              raw_size, &inflated, err,
                                              err_capacity)
                  : ni_zlib_inflate_stored_spans(idat_spans, idat_span_count, raw,
                                                 raw_size, &inflated, err,
                                                 err_capacity))) {
      goto fail;
    }
    if (inflated != raw_size) {
      ni_set_error(err, err_capacity, "unexpected PNG inflated size");
      goto fail;
    }
  }
  ni_stbi_free(idat_spans);
  idat_spans = NULL;

  {
    size_t pass;
    size_t raw_off = 0u;
    const size_t pass_count = (interlace == 0u) ? 1u : 7u;
    for (pass = 0u; pass < pass_count; pass++) {
      const uint8_t x_start = (interlace == 0u) ? 0u : k_adam7_x_start[pass];
      const uint8_t y_start = (interlace == 0u) ? 0u : k_adam7_y_start[pass];
      const uint8_t x_step = (interlace == 0u) ? 1u : k_adam7_x_step[pass];
      const uint8_t y_step = (interlace == 0u) ? 1u : k_adam7_y_step[pass];
      const uint32_t pw = (interlace == 0u) ? width : ni_pass_size(width, x_start, x_step);
      const uint32_t ph =
          (interlace == 0u) ? height : ni_pass_size(height, y_start, y_step);
      size_t rowbytes;
      uint32_t y;

      if (pw == 0u || ph == 0u) {
        continue;
      }
      if (!ni_size_mul((size_t)pw, bits_per_pixel, &rowbytes)) {
        ni_set_error(err, err_capacity, "PNG rowbytes overflow");
        goto fail;
      }
      rowbytes = (rowbytes + 7u) >> 3u;

      for (y = 0u; y < ph; y++) {
        const uint8_t filter = raw[raw_off++];
        uint8_t *cur_row = raw + raw_off;
        const uint8_t *prev_row = (y == 0u) ? NULL : (cur_row - (rowbytes + 1u));
        uint32_t x;
        raw_off += rowbytes;
        if (!ni_unfilter_row(cur_row, prev_row, rowbytes, bytes_per_pixel_for_filter,
                             filter)) {
          ni_set_error(err, err_capacity, "unsupported PNG filter type");
          goto fail;
        }

        for (x = 0u; x < pw; x++) {
          const uint32_t dst_x = (uint32_t)x_start + x * (uint32_t)x_step;
          const uint32_t dst_y = (uint32_t)y_start + y * (uint32_t)y_step;
          uint8_t *dst;
          if ((dst_x >= width) || (dst_y >= height)) {
            continue;
          }
          dst = pixels + (size_t)dst_y * output_stride +
                (size_t)dst_x * (size_t)out_channels * (size_t)out_sample_bytes;

          if (color_type == 0u) {
            if (bit_depth < 8u) {
              uint8_t g = 0u;
              if (!ni_scale_to_8bit(
                      ni_read_packed_bits(cur_row, (size_t)x, bit_depth), bit_depth, &g)) {
                ni_set_error(err, err_capacity, "invalid grayscale sample depth");
                goto fail;
              }
              dst[0] = g;
            } else if (bit_depth == 8u) {
              dst[0] = cur_row[(size_t)x];
            } else {
              dst[0] = cur_row[(size_t)x * 2u];
              dst[1] = cur_row[(size_t)x * 2u + 1u];
            }
          } else if (color_type == 2u) {
            if (bit_depth == 8u) {
              const uint8_t *src = cur_row + (size_t)x * 3u;
              dst[0] = is_cgbi ? src[2] : src[0];
              dst[1] = src[1];
              dst[2] = is_cgbi ? src[0] : src[2];
            } else {
              const uint8_t *src = cur_row + (size_t)x * 6u;
              if (is_cgbi) {
                dst[0] = src[4];
                dst[1] = src[5];
                dst[2] = src[2];
                dst[3] = src[3];
                dst[4] = src[0];
                dst[5] = src[1];
              } else {
                memcpy(dst, src, 6u);
              }
            }
          } else if (color_type == 3u) {
            const uint32_t idx = (bit_depth == 8u)
                                     ? (uint32_t)cur_row[(size_t)x]
                                     : ni_read_packed_bits(cur_row, (size_t)x, bit_depth);
            if (idx >= palette_entries) {
              ni_set_error(err, err_capacity, "palette index out of range");
              goto fail;
            }
            dst[0] = palette[idx * 3u + 0u];
            dst[1] = palette[idx * 3u + 1u];
            dst[2] = palette[idx * 3u + 2u];
            if (out_channels == 4u) {
              dst[3] = (idx < palette_alpha_count) ? palette_alpha[idx] : 255u;
            }
          } else if (color_type == 4u) {
            if (bit_depth == 8u) {
              const uint8_t *src = cur_row + (size_t)x * 2u;
              dst[0] = src[0];
              dst[1] = src[1];
            } else {
              const uint8_t *src = cur_row + (size_t)x * 4u;
              memcpy(dst, src, 4u);
            }
          } else {
            if (bit_depth == 8u) {
              const uint8_t *src = cur_row + (size_t)x * 4u;
              if (is_cgbi) {
                dst[0] = src[2];
                dst[1] = src[1];
                dst[2] = src[0];
                dst[3] = src[3];
              } else {
                memcpy(dst, src, 4u);
              }
            } else {
              const uint8_t *src = cur_row + (size_t)x * 8u;
              if (is_cgbi) {
                dst[0] = src[4];
                dst[1] = src[5];
                dst[2] = src[2];
                dst[3] = src[3];
                dst[4] = src[0];
                dst[5] = src[1];
                dst[6] = src[6];
                dst[7] = src[7];
              } else {
                memcpy(dst, src, 8u);
              }
            }
          }
        }

      }
    }
  }

  out->width = width;
  out->height = height;
  out->channels = out_channels;
  out->bit_depth = out_bit_depth;
  out->data_size = output_size;
  out->data = pixels;

  ni_stbi_free(raw);
  ni_stbi_free(idat_spans);
  ni_stbi_free(palette);
  ni_stbi_free(palette_alpha);
  return 1;

fail:
  ni_stbi_free(raw);
  ni_stbi_free(idat_spans);
  ni_stbi_free(pixels);
  ni_stbi_free(palette);
  ni_stbi_free(palette_alpha);
  return 0;
}
