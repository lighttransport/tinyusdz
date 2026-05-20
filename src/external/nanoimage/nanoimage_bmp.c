#include "nanoimage_bmp.h"

#include "nanoimage_alloc_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static void ni_set_error(char *err, size_t cap, const char *fmt, ...) {
  va_list args;

  if ((err == NULL) || (cap == 0u)) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err, cap, fmt, args);
  va_end(args);
}

static uint16_t ni_read_u16le(const uint8_t *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static uint32_t ni_read_u32le(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) | ((uint32_t)p[2] << 16u) |
         ((uint32_t)p[3] << 24u);
}

static int32_t ni_read_i32le(const uint8_t *p) {
  return (int32_t)ni_read_u32le(p);
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

int ni_load_bmp_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity) {
  uint32_t pixel_offset;
  uint32_t dib_size;
  int32_t width_i;
  int32_t height_i;
  uint32_t width;
  uint32_t height;
  uint16_t planes;
  uint16_t bit_count;
  uint32_t compression;
  uint32_t colors_used;
  size_t row_stride;
  size_t output_stride;
  size_t output_size;
  uint8_t channels;
  uint8_t top_down = 0u;
  uint8_t *pixels = NULL;

  if ((bytes == NULL) || (out == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }
  memset(out, 0, sizeof(*out));

  if ((size < 54u) || (bytes[0] != 'B') || (bytes[1] != 'M')) {
    ni_set_error(err, err_capacity, "invalid BMP signature");
    return 0;
  }

  pixel_offset = ni_read_u32le(bytes + 10u);
  dib_size = ni_read_u32le(bytes + 14u);
  if ((dib_size < 40u) || (size < (14u + (size_t)dib_size)) ||
      (pixel_offset < 14u + dib_size) || (pixel_offset > size)) {
    ni_set_error(err, err_capacity, "invalid BMP header layout");
    return 0;
  }

  width_i = ni_read_i32le(bytes + 18u);
  height_i = ni_read_i32le(bytes + 22u);
  planes = ni_read_u16le(bytes + 26u);
  bit_count = ni_read_u16le(bytes + 28u);
  compression = ni_read_u32le(bytes + 30u);
  colors_used = ni_read_u32le(bytes + 46u);

  if ((width_i <= 0) || (height_i == 0) || (planes != 1u)) {
    ni_set_error(err, err_capacity, "unsupported BMP dimensions or planes");
    return 0;
  }
  if (height_i < 0) {
    if (height_i == INT32_MIN) {
      ni_set_error(err, err_capacity, "invalid BMP height");
      return 0;
    }
    top_down = 1u;
    height = (uint32_t)(-height_i);
  } else {
    height = (uint32_t)height_i;
  }
  width = (uint32_t)width_i;

  if (compression != 0u) {
    ni_set_error(err, err_capacity, "unsupported BMP compression");
    return 0;
  }
  if ((bit_count != 8u) && (bit_count != 24u) && (bit_count != 32u)) {
    ni_set_error(err, err_capacity, "unsupported BMP bit depth");
    return 0;
  }

  channels = (bit_count == 32u) ? 4u : ((bit_count == 24u) ? 3u : 3u);
  if (!ni_size_mul((size_t)width, (size_t)bit_count, &row_stride)) {
    ni_set_error(err, err_capacity, "BMP row size overflow");
    return 0;
  }
  row_stride = ((row_stride + 31u) >> 5u) << 2u;
  if (!ni_size_mul((size_t)width, (size_t)channels, &output_stride) ||
      !ni_size_mul(output_stride, (size_t)height, &output_size)) {
    ni_set_error(err, err_capacity, "BMP output size overflow");
    return 0;
  }
  if (!ni_size_mul(row_stride, (size_t)height, &output_stride) ||
      !ni_size_add((size_t)pixel_offset, output_stride, &output_stride) ||
      (output_stride > size)) {
    ni_set_error(err, err_capacity, "truncated BMP pixel data");
    return 0;
  }

  pixels = (uint8_t *)ni_stbi_malloc(output_size);
  if (pixels == NULL) {
    ni_set_error(err, err_capacity, "out of memory for BMP pixels");
    return 0;
  }

  if (bit_count == 8u) {
    size_t palette_entries;
    size_t palette_bytes;
    const uint8_t *palette;
    uint32_t y;

    palette_entries = (colors_used != 0u) ? (size_t)colors_used : 256u;
    if ((palette_entries == 0u) || (palette_entries > 256u) ||
        !ni_size_mul(palette_entries, 4u, &palette_bytes) ||
        (14u + (size_t)dib_size + palette_bytes > (size_t)pixel_offset)) {
      ni_set_error(err, err_capacity, "invalid BMP palette");
      goto fail;
    }
    palette = bytes + 14u + (size_t)dib_size;
    for (y = 0u; y < height; y++) {
      const uint8_t *src_row;
      uint8_t *dst_row;
      uint32_t x;
      const uint32_t src_y = top_down ? y : (height - 1u - y);
      src_row = bytes + (size_t)pixel_offset + (size_t)src_y * row_stride;
      dst_row = pixels + (size_t)y * (size_t)width * 3u;
      for (x = 0u; x < width; x++) {
        const uint8_t idx = src_row[x];
        if ((size_t)idx >= palette_entries) {
          ni_set_error(err, err_capacity, "BMP palette index out of range");
          goto fail;
        }
        dst_row[(size_t)x * 3u + 0u] = palette[(size_t)idx * 4u + 2u];
        dst_row[(size_t)x * 3u + 1u] = palette[(size_t)idx * 4u + 1u];
        dst_row[(size_t)x * 3u + 2u] = palette[(size_t)idx * 4u + 0u];
      }
    }
  } else {
    const size_t src_channels = (bit_count == 32u) ? 4u : 3u;
    uint32_t y;

    for (y = 0u; y < height; y++) {
      const uint8_t *src_row;
      uint8_t *dst_row;
      uint32_t x;
      const uint32_t src_y = top_down ? y : (height - 1u - y);
      src_row = bytes + (size_t)pixel_offset + (size_t)src_y * row_stride;
      dst_row = pixels + (size_t)y * (size_t)width * (size_t)channels;
      for (x = 0u; x < width; x++) {
        const uint8_t *src = src_row + (size_t)x * src_channels;
        uint8_t *dst = dst_row + (size_t)x * (size_t)channels;
        dst[0] = src[2];
        dst[1] = src[1];
        dst[2] = src[0];
        if (channels == 4u) {
          dst[3] = src[3];
        }
      }
    }
  }

  out->width = width;
  out->height = height;
  out->channels = channels;
  out->bit_depth = 8u;
  out->data_size = output_size;
  out->data = pixels;
  return 1;

fail:
  ni_stbi_free(pixels);
  return 0;
}
