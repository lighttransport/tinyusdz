#include "nanoimage_gif.h"

#include "nanoimage_write_internal.h"

#include <string.h>

typedef struct {
  ni_stream_writer *writer;
  uint8_t subblock[255];
  size_t subblock_size;
  uint32_t bit_buffer;
  unsigned bit_count;
} ni_gif_bit_writer;

static void ni_gif_fill_grayscale_palette(uint8_t *palette) {
  uint16_t i;
  for (i = 0u; i < 256u; i++) {
    palette[(size_t)i * 3u + 0u] = (uint8_t)i;
    palette[(size_t)i * 3u + 1u] = (uint8_t)i;
    palette[(size_t)i * 3u + 2u] = (uint8_t)i;
  }
}

static void ni_gif_fill_grayscale_alpha_palette(uint8_t *palette) {
  uint16_t i;
  memset(palette, 0, 256u * 3u);
  for (i = 1u; i < 256u; i++) {
    const uint8_t v = (uint8_t)((((uint32_t)(i - 1u) * 255u) + 127u) / 254u);
    palette[(size_t)i * 3u + 0u] = v;
    palette[(size_t)i * 3u + 1u] = v;
    palette[(size_t)i * 3u + 2u] = v;
  }
}

static void ni_gif_fill_rgb332_palette(uint8_t *palette) {
  uint16_t i;
  for (i = 0u; i < 256u; i++) {
    const uint8_t r = (uint8_t)(i >> 5u);
    const uint8_t g = (uint8_t)((i >> 2u) & 0x07u);
    const uint8_t b = (uint8_t)(i & 0x03u);
    palette[(size_t)i * 3u + 0u] = (uint8_t)((r * 255u) / 7u);
    palette[(size_t)i * 3u + 1u] = (uint8_t)((g * 255u) / 7u);
    palette[(size_t)i * 3u + 2u] = (uint8_t)((b * 255u) / 3u);
  }
}

static void ni_gif_fill_rgb676_alpha_palette(uint8_t *palette) {
  uint16_t r;
  memset(palette, 0, 256u * 3u);
  for (r = 0u; r < 6u; r++) {
    uint16_t g;
    for (g = 0u; g < 7u; g++) {
      uint16_t b;
      for (b = 0u; b < 6u; b++) {
        const uint16_t idx = (uint16_t)(1u + r * 42u + g * 6u + b);
        palette[(size_t)idx * 3u + 0u] = (uint8_t)((r * 255u) / 5u);
        palette[(size_t)idx * 3u + 1u] = (uint8_t)((g * 255u) / 6u);
        palette[(size_t)idx * 3u + 2u] = (uint8_t)((b * 255u) / 5u);
      }
    }
  }
}

static int ni_gif_flush_subblock(ni_gif_bit_writer *bw) {
  if (bw->subblock_size == 0u) {
    return 1;
  }
  if (!ni_stream_write_u8(bw->writer, (uint8_t)bw->subblock_size) ||
      !ni_stream_write(bw->writer, bw->subblock, bw->subblock_size)) {
    return 0;
  }
  bw->subblock_size = 0u;
  return 1;
}

static int ni_gif_emit_byte(ni_gif_bit_writer *bw, uint8_t byte) {
  bw->subblock[bw->subblock_size++] = byte;
  if (bw->subblock_size == sizeof(bw->subblock)) {
    return ni_gif_flush_subblock(bw);
  }
  return 1;
}

static int ni_gif_emit_code(ni_gif_bit_writer *bw, uint16_t code) {
  bw->bit_buffer |= (uint32_t)code << bw->bit_count;
  bw->bit_count += 9u;
  while (bw->bit_count >= 8u) {
    if (!ni_gif_emit_byte(bw, (uint8_t)(bw->bit_buffer & 0xffu))) {
      return 0;
    }
    bw->bit_buffer >>= 8u;
    bw->bit_count -= 8u;
  }
  return 1;
}

static int ni_gif_finish_codes(ni_gif_bit_writer *bw) {
  if (bw->bit_count > 0u) {
    if (!ni_gif_emit_byte(bw, (uint8_t)(bw->bit_buffer & 0xffu))) {
      return 0;
    }
    bw->bit_buffer = 0u;
    bw->bit_count = 0u;
  }
  if (!ni_gif_flush_subblock(bw) || !ni_stream_write_u8(bw->writer, 0u)) {
    return 0;
  }
  return 1;
}

static uint8_t ni_gif_map_gray(uint8_t gray, int has_transparency, uint8_t alpha) {
  if (has_transparency && (alpha < 128u)) {
    return 0u;
  }
  if (has_transparency) {
    return (uint8_t)(1u + ((((uint32_t)gray) * 254u + 127u) / 255u));
  }
  return gray;
}

static uint8_t ni_gif_map_rgb(const uint8_t *src, int has_alpha, int has_transparency) {
  if (has_alpha && has_transparency && (src[3] < 128u)) {
    return 0u;
  }
  if (has_transparency) {
    const uint8_t r = (uint8_t)(((uint32_t)src[0] * 5u + 127u) / 255u);
    const uint8_t g = (uint8_t)(((uint32_t)src[1] * 6u + 127u) / 255u);
    const uint8_t b = (uint8_t)(((uint32_t)src[2] * 5u + 127u) / 255u);
    return (uint8_t)(1u + r * 42u + g * 6u + b);
  }
  return (uint8_t)((src[0] & 0xE0u) | ((src[1] >> 3u) & 0x1Cu) | (src[2] >> 6u));
}

static int ni_write_gif_impl(const ni_image *image, ni_stream_writer *writer) {
  size_t row_stride = 0u;
  size_t required_size = 0u;
  uint8_t palette[256u * 3u];
  int has_transparency = 0;
  uint32_t y;
  ni_gif_bit_writer bw;

  if (!ni_write_image_layout(image, 4u, &row_stride, &required_size, writer->err,
                             writer->err_capacity)) {
    return 0;
  }
  (void)required_size;

  if (image->bit_depth != 8u) {
    ni_write_set_error(writer->err, writer->err_capacity,
                       "GIF writer supports only 8-bit images");
    return 0;
  }
  if ((image->width > (uint32_t)UINT16_MAX) || (image->height > (uint32_t)UINT16_MAX)) {
    ni_write_set_error(writer->err, writer->err_capacity, "GIF dimensions exceed 16-bit limit");
    return 0;
  }

  if ((image->channels == 2u) || (image->channels == 4u)) {
    size_t i;
    for (i = 0u; i < image->data_size; i += (size_t)image->channels) {
      if (image->data[i + (size_t)image->channels - 1u] < 128u) {
        has_transparency = 1;
        break;
      }
    }
  }

  if ((image->channels == 1u) || (image->channels == 2u)) {
    if (has_transparency) {
      ni_gif_fill_grayscale_alpha_palette(palette);
    } else {
      ni_gif_fill_grayscale_palette(palette);
    }
  } else if (has_transparency) {
    ni_gif_fill_rgb676_alpha_palette(palette);
  } else {
    ni_gif_fill_rgb332_palette(palette);
  }

  if (!ni_stream_write(writer, "GIF89a", 6u) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->width) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->height) ||
      !ni_stream_write_u8(writer, 0xF7u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write(writer, palette, sizeof(palette))) {
    return 0;
  }

  if (has_transparency) {
    const uint8_t gce[] = {0x21u, 0xF9u, 0x04u, 0x01u, 0x00u, 0x00u, 0x00u, 0x00u};
    if (!ni_stream_write(writer, gce, sizeof(gce))) {
      return 0;
    }
  }

  if (!ni_stream_write_u8(writer, 0x2Cu) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->width) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->height) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, 8u)) {
    return 0;
  }

  memset(&bw, 0, sizeof(bw));
  bw.writer = writer;
  for (y = 0u; y < image->height; y++) {
    const uint8_t *src = image->data + (size_t)y * row_stride;
    uint32_t x;
    for (x = 0u; x < image->width; x++) {
      uint8_t idx;
      if (!ni_gif_emit_code(&bw, 256u)) {
        return 0;
      }
      if ((image->channels == 1u) || (image->channels == 2u)) {
        const uint8_t gray = src[(size_t)x * (size_t)image->channels];
        const uint8_t alpha =
            (image->channels == 2u) ? src[(size_t)x * 2u + 1u] : 255u;
        idx = ni_gif_map_gray(gray, has_transparency, alpha);
      } else {
        const uint8_t *p = src + (size_t)x * (size_t)image->channels;
        idx = ni_gif_map_rgb(p, image->channels == 4u, has_transparency);
      }
      if (!ni_gif_emit_code(&bw, idx)) {
        return 0;
      }
    }
  }

  if (!ni_gif_emit_code(&bw, 257u) ||
      !ni_gif_finish_codes(&bw) ||
      !ni_stream_write_u8(writer, 0x3Bu)) {
    return 0;
  }
  return 1;
}

int ni_write_gif(const ni_image *image, ni_write_callback write_fn,
                 void *user_data, char *err, size_t err_capacity) {
  ni_stream_writer writer;
  if (write_fn == NULL) {
    ni_write_set_error(err, err_capacity, "invalid writer callback");
    return 0;
  }
  writer.write_fn = write_fn;
  writer.user_data = user_data;
  writer.err = err;
  writer.err_capacity = err_capacity;
  return ni_write_gif_impl(image, &writer);
}

int ni_write_gif_to_memory(const ni_image *image, ni_buffer *out, char *err,
                           size_t err_capacity) {
  ni_memory_writer memory_writer;
  ni_stream_writer writer;
  if (!ni_memory_writer_init(&memory_writer, out)) {
    ni_write_set_error(err, err_capacity, "invalid output buffer");
    return 0;
  }
  writer.write_fn = ni_memory_write_callback;
  writer.user_data = &memory_writer;
  writer.err = err;
  writer.err_capacity = err_capacity;
  if (!ni_write_gif_impl(image, &writer)) {
    ni_buffer_free(out);
    return 0;
  }
  return 1;
}
