#include "nanoimage_tga.h"

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

static int ni_size_mul(size_t a, size_t b, size_t *out) {
  if ((a != 0u) && (b > (SIZE_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

static int ni_tga_write_pixel(uint8_t *dst, uint8_t channels,
                              const uint8_t *src) {
  if (channels == 1u) {
    dst[0] = src[0];
    return 1;
  }
  if (channels == 3u) {
    dst[0] = src[2];
    dst[1] = src[1];
    dst[2] = src[0];
    return 1;
  }
  if (channels == 4u) {
    dst[0] = src[2];
    dst[1] = src[1];
    dst[2] = src[0];
    dst[3] = src[3];
    return 1;
  }
  return 0;
}

int ni_load_tga_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity) {
  uint8_t id_length;
  uint8_t color_map_type;
  uint8_t image_type;
  uint16_t width;
  uint16_t height;
  uint8_t pixel_depth;
  uint8_t descriptor;
  uint8_t channels;
  uint8_t top_origin;
  uint8_t right_origin;
  size_t off;
  size_t output_size;
  uint8_t *pixels = NULL;
  size_t pixel_index = 0u;
  int rle;

  if ((bytes == NULL) || (out == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }
  memset(out, 0, sizeof(*out));

  if (size < 18u) {
    ni_set_error(err, err_capacity, "truncated TGA header");
    return 0;
  }

  id_length = bytes[0];
  color_map_type = bytes[1];
  image_type = bytes[2];
  width = ni_read_u16le(bytes + 12u);
  height = ni_read_u16le(bytes + 14u);
  pixel_depth = bytes[16];
  descriptor = bytes[17];
  top_origin = (uint8_t)((descriptor & 0x20u) != 0u);
  right_origin = (uint8_t)((descriptor & 0x10u) != 0u);

  if ((width == 0u) || (height == 0u)) {
    ni_set_error(err, err_capacity, "TGA dimensions must be non-zero");
    return 0;
  }
  if (color_map_type != 0u) {
    ni_set_error(err, err_capacity, "unsupported TGA color map");
    return 0;
  }
  if ((image_type != 2u) && (image_type != 3u) && (image_type != 10u) &&
      (image_type != 11u)) {
    ni_set_error(err, err_capacity, "unsupported TGA image type");
    return 0;
  }

  rle = (image_type == 10u) || (image_type == 11u);
  if ((image_type == 3u) || (image_type == 11u)) {
    if (pixel_depth != 8u) {
      ni_set_error(err, err_capacity, "unsupported TGA grayscale depth");
      return 0;
    }
    channels = 1u;
  } else if (pixel_depth == 24u) {
    channels = 3u;
  } else if (pixel_depth == 32u) {
    channels = 4u;
  } else {
    ni_set_error(err, err_capacity, "unsupported TGA pixel depth");
    return 0;
  }

  off = 18u + (size_t)id_length;
  if (off > size) {
    ni_set_error(err, err_capacity, "truncated TGA image ID");
    return 0;
  }

  if (!ni_size_mul((size_t)width, (size_t)height, &output_size) ||
      !ni_size_mul(output_size, (size_t)channels, &output_size)) {
    ni_set_error(err, err_capacity, "TGA output size overflow");
    return 0;
  }

  pixels = (uint8_t *)ni_stbi_malloc(output_size);
  if (pixels == NULL) {
    ni_set_error(err, err_capacity, "out of memory for TGA pixels");
    return 0;
  }

  while (pixel_index < (size_t)width * (size_t)height) {
    size_t count = 1u;
    uint8_t packet_header = 0u;
    uint8_t sample[4] = {0u, 0u, 0u, 255u};
    size_t i;

    if (rle) {
      if (off >= size) {
        ni_set_error(err, err_capacity, "truncated TGA RLE data");
        goto fail;
      }
      packet_header = bytes[off++];
      count = (size_t)(packet_header & 0x7fu) + 1u;
    }
    if (count > ((size_t)width * (size_t)height - pixel_index)) {
      ni_set_error(err, err_capacity, "invalid TGA packet length");
      goto fail;
    }

    if (!rle || ((packet_header & 0x80u) != 0u)) {
      size_t sample_bytes = (size_t)((channels == 1u) ? 1u : channels);
      if (off + sample_bytes > size) {
        ni_set_error(err, err_capacity, "truncated TGA pixel data");
        goto fail;
      }
      memcpy(sample, bytes + off, sample_bytes);
      off += sample_bytes;
    }

    for (i = 0u; i < count; i++) {
      const uint8_t *src_sample = sample;
      uint8_t raw_sample[4];
      uint32_t x;
      uint32_t y;
      uint32_t dst_x;
      uint32_t dst_y;
      uint8_t *dst;

      if (rle && ((packet_header & 0x80u) == 0u)) {
        size_t sample_bytes = (size_t)((channels == 1u) ? 1u : channels);
        if (off + sample_bytes > size) {
          ni_set_error(err, err_capacity, "truncated TGA raw packet");
          goto fail;
        }
        memcpy(raw_sample, bytes + off, sample_bytes);
        off += sample_bytes;
        src_sample = raw_sample;
      }

      x = (uint32_t)(pixel_index % (size_t)width);
      y = (uint32_t)(pixel_index / (size_t)width);
      dst_x = right_origin ? (uint32_t)(width - 1u - x) : x;
      dst_y = top_origin ? y : (uint32_t)(height - 1u - y);
      dst = pixels + ((size_t)dst_y * (size_t)width + (size_t)dst_x) *
                         (size_t)channels;
      if (!ni_tga_write_pixel(dst, channels, src_sample)) {
        ni_set_error(err, err_capacity, "invalid TGA channels");
        goto fail;
      }
      pixel_index++;
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
