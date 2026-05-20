#include "nanoimage_gif.h"

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

static int ni_size_add(size_t a, size_t b, size_t *out) {
  if (a > (SIZE_MAX - b)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int ni_skip_gif_subblocks(const uint8_t *bytes, size_t size, size_t *off,
                                 char *err, size_t err_capacity) {
  while (*off < size) {
    const uint8_t block_len = bytes[*off];
    *off += 1u;
    if (block_len == 0u) {
      return 1;
    }
    if (*off + (size_t)block_len > size) {
      ni_set_error(err, err_capacity, "truncated GIF sub-block");
      return 0;
    }
    *off += (size_t)block_len;
  }

  ni_set_error(err, err_capacity, "truncated GIF sub-block stream");
  return 0;
}

static int ni_collect_gif_subblocks(const uint8_t *bytes, size_t size,
                                    size_t *off, uint8_t **out_data,
                                    size_t *out_size, char *err,
                                    size_t err_capacity) {
  uint8_t *data = NULL;
  size_t total = 0u;

  while (*off < size) {
    uint8_t block_len = bytes[*off];
    *off += 1u;
    if (block_len == 0u) {
      *out_data = data;
      *out_size = total;
      return 1;
    }
    if (*off + (size_t)block_len > size) {
      ni_stbi_free(data);
      ni_set_error(err, err_capacity, "truncated GIF image data");
      return 0;
    }
    if (!ni_size_add(total, (size_t)block_len, &total)) {
      ni_stbi_free(data);
      ni_set_error(err, err_capacity, "GIF image data too large");
      return 0;
    }
    {
      uint8_t *new_data = (uint8_t *)ni_stbi_realloc(data, total);
      if (new_data == NULL) {
        ni_stbi_free(data);
        ni_set_error(err, err_capacity, "out of memory for GIF image data");
        return 0;
      }
      data = new_data;
    }
    memcpy(data + total - (size_t)block_len, bytes + *off, (size_t)block_len);
    *off += (size_t)block_len;
  }

  ni_stbi_free(data);
  ni_set_error(err, err_capacity, "truncated GIF image data stream");
  return 0;
}

static int ni_gif_read_code(const uint8_t *data, size_t size, size_t *bit_pos,
                            unsigned code_size, uint16_t *code) {
  size_t byte_pos = *bit_pos >> 3u;
  unsigned shift = (unsigned)(*bit_pos & 7u);
  uint32_t bits = 0u;
  unsigned i;

  if ((code == NULL) || (code_size == 0u) || (code_size > 12u)) {
    return 0;
  }
  if ((*bit_pos + (size_t)code_size) > size * 8u) {
    return 0;
  }

  for (i = 0u; i < 3u && (byte_pos + i) < size; i++) {
    bits |= (uint32_t)data[byte_pos + i] << (8u * i);
  }
  bits >>= shift;
  *code = (uint16_t)(bits & ((1u << code_size) - 1u));
  *bit_pos += (size_t)code_size;
  return 1;
}

typedef struct {
  uint8_t *pixels;
  uint16_t width;
  uint16_t height;
  const uint8_t *color_table;
  size_t color_table_entries;
  uint8_t transparent_index;
  int has_transparent;
  int interlaced;
  size_t out_pos;
  uint8_t pass;
  uint32_t x;
  uint32_t y;
} ni_gif_output_state;

static int ni_gif_advance_interlaced_row(ni_gif_output_state *state) {
  static const uint8_t k_starts[4] = {0u, 4u, 2u, 1u};
  static const uint8_t k_steps[4] = {8u, 8u, 4u, 2u};

  state->y += (uint32_t)k_steps[state->pass];
  while ((state->pass < 4u) && (state->y >= state->height)) {
    state->pass++;
    if (state->pass >= 4u) {
      return 1;
    }
    state->y = (uint32_t)k_starts[state->pass];
  }

  return 1;
}

static void ni_gif_output_init(ni_gif_output_state *state, uint8_t *pixels,
                               uint16_t width, uint16_t height,
                               const uint8_t *color_table,
                               size_t color_table_entries,
                               uint8_t transparent_index,
                               int has_transparent, int interlaced) {
  state->pixels = pixels;
  state->width = width;
  state->height = height;
  state->color_table = color_table;
  state->color_table_entries = color_table_entries;
  state->transparent_index = transparent_index;
  state->has_transparent = has_transparent;
  state->interlaced = interlaced;
  state->out_pos = 0u;
  state->pass = 0u;
  state->x = 0u;
  state->y = interlaced ? 0u : 0u;
}

static int ni_gif_emit_index(ni_gif_output_state *state, uint8_t idx, char *err,
                             size_t err_capacity) {
  uint8_t *dst;

  if ((uint32_t)state->y >= (uint32_t)state->height) {
    ni_set_error(err, err_capacity, "GIF image data overflow");
    return 0;
  }
  if ((size_t)idx >= state->color_table_entries) {
    ni_set_error(err, err_capacity, "GIF palette index out of range");
    return 0;
  }

  dst = state->pixels +
        (((size_t)state->y * (size_t)state->width) + (size_t)state->x) * 4u;
  dst[0] = state->color_table[(size_t)idx * 3u + 0u];
  dst[1] = state->color_table[(size_t)idx * 3u + 1u];
  dst[2] = state->color_table[(size_t)idx * 3u + 2u];
  dst[3] = (state->has_transparent && (idx == state->transparent_index)) ? 0u : 255u;

  state->out_pos++;
  state->x++;
  if (state->x >= state->width) {
    state->x = 0u;
    if (state->interlaced) {
      if (!ni_gif_advance_interlaced_row(state)) {
        ni_set_error(err, err_capacity, "GIF interlace overflow");
        return 0;
      }
    } else {
      state->y++;
    }
  }

  return 1;
}

static int ni_gif_lzw_decode(const uint8_t *data, size_t size,
                             uint8_t min_code_size, ni_gif_output_state *state,
                             size_t out_size, char *err, size_t err_capacity) {
  uint16_t prefix[4096];
  uint8_t suffix[4096];
  uint8_t stack[4096];
  size_t bit_pos = 0u;
  size_t out_pos = 0u;
  uint16_t clear_code;
  uint16_t end_code;
  uint16_t next_code;
  uint16_t old_code = 0u;
  uint8_t first_char = 0u;
  unsigned code_size;
  int have_old = 0;

  if ((data == NULL) || (state == NULL) || (min_code_size < 2u) ||
      (min_code_size > 8u)) {
    ni_set_error(err, err_capacity, "invalid GIF LZW parameters");
    return 0;
  }

  clear_code = (uint16_t)(1u << min_code_size);
  end_code = (uint16_t)(clear_code + 1u);
  next_code = (uint16_t)(end_code + 1u);
  code_size = (unsigned)min_code_size + 1u;

  while (out_pos < out_size) {
    uint16_t code;
    uint16_t in_code;
    size_t stack_size = 0u;

    if (!ni_gif_read_code(data, size, &bit_pos, code_size, &code)) {
      ni_set_error(err, err_capacity, "truncated GIF LZW stream");
      return 0;
    }
    if (code == clear_code) {
      next_code = (uint16_t)(end_code + 1u);
      code_size = (unsigned)min_code_size + 1u;
      have_old = 0;
      continue;
    }
    if (code == end_code) {
      break;
    }
    if ((code > next_code) || ((code == next_code) && !have_old)) {
      ni_set_error(err, err_capacity, "invalid GIF LZW code");
      return 0;
    }

    in_code = code;
    if (code == next_code) {
      stack[stack_size++] = first_char;
      code = old_code;
    }

    while (code >= clear_code) {
      if ((code >= next_code) || (stack_size >= sizeof(stack))) {
        ni_set_error(err, err_capacity, "invalid GIF LZW dictionary chain");
        return 0;
      }
      stack[stack_size++] = suffix[code];
      code = prefix[code];
    }

    first_char = (uint8_t)code;
    if (stack_size >= sizeof(stack)) {
      ni_set_error(err, err_capacity, "GIF LZW stack overflow");
      return 0;
    }
    stack[stack_size++] = first_char;

    while (stack_size > 0u) {
      if (out_pos >= out_size) {
        ni_set_error(err, err_capacity, "GIF LZW output overflow");
        return 0;
      }
      if (!ni_gif_emit_index(state, stack[--stack_size], err, err_capacity)) {
        return 0;
      }
      out_pos++;
    }

    if (have_old && (next_code < 4096u)) {
      prefix[next_code] = old_code;
      suffix[next_code] = first_char;
      next_code++;
      if ((next_code == (1u << code_size)) && (code_size < 12u)) {
        code_size++;
      }
    }

    old_code = in_code;
    have_old = 1;
  }

  if (out_pos != out_size) {
    ni_set_error(err, err_capacity, "GIF image data ended early");
    return 0;
  }
  return 1;
}

int ni_load_gif_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity) {
  uint16_t screen_width;
  uint16_t screen_height;
  uint8_t packed;
  uint8_t *global_table = NULL;
  size_t global_table_size = 0u;
  uint8_t transparent_index = 0u;
  int has_transparent = 0;
  size_t off = 13u;
  uint8_t *pixels = NULL;

  if ((bytes == NULL) || (out == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }
  memset(out, 0, sizeof(*out));

  if ((size < 13u) ||
      ((memcmp(bytes, "GIF87a", 6u) != 0) && (memcmp(bytes, "GIF89a", 6u) != 0))) {
    ni_set_error(err, err_capacity, "invalid GIF signature");
    return 0;
  }

  screen_width = ni_read_u16le(bytes + 6u);
  screen_height = ni_read_u16le(bytes + 8u);
  packed = bytes[10];
  if ((screen_width == 0u) || (screen_height == 0u)) {
    ni_set_error(err, err_capacity, "GIF dimensions must be non-zero");
    return 0;
  }

  if ((packed & 0x80u) != 0u) {
    size_t table_entries = (size_t)1u << ((packed & 0x07u) + 1u);
    if (!ni_size_mul(table_entries, 3u, &global_table_size) ||
        (off + global_table_size > size)) {
      ni_set_error(err, err_capacity, "truncated GIF global color table");
      return 0;
    }
    global_table = (uint8_t *)(bytes + off);
    off += global_table_size;
  }

  while (off < size) {
    const uint8_t introducer = bytes[off++];
    if (introducer == 0x3bu) {
      break;
    }
    if (introducer == 0x21u) {
      if (off >= size) {
        ni_set_error(err, err_capacity, "truncated GIF extension");
        return 0;
      }
      {
        const uint8_t label = bytes[off++];
        if (label == 0xf9u) {
          if ((off + 6u) > size || (bytes[off] != 4u)) {
            ni_set_error(err, err_capacity, "invalid GIF graphic control extension");
            return 0;
          }
          has_transparent = ((bytes[off + 1u] & 0x01u) != 0u);
          transparent_index = bytes[off + 4u];
          off += 5u;
          if (bytes[off] != 0u) {
            ni_set_error(err, err_capacity, "invalid GIF graphic control terminator");
            return 0;
          }
          off++;
        } else {
          if (!ni_skip_gif_subblocks(bytes, size, &off, err, err_capacity)) {
            return 0;
          }
        }
      }
      continue;
    }
    if (introducer == 0x2cu) {
      uint16_t image_width;
      uint16_t image_height;
      uint8_t image_packed;
      const uint8_t *color_table = global_table;
      size_t color_table_entries = global_table_size / 3u;
      uint8_t min_code_size;
      uint8_t *compressed = NULL;
      size_t compressed_size = 0u;
      ni_gif_output_state output_state;
      size_t pixel_count;
      size_t output_size;

      if ((off + 9u) > size) {
        ni_set_error(err, err_capacity, "truncated GIF image descriptor");
        return 0;
      }
      (void)ni_read_u16le(bytes + off + 0u);
      (void)ni_read_u16le(bytes + off + 2u);
      image_width = ni_read_u16le(bytes + off + 4u);
      image_height = ni_read_u16le(bytes + off + 6u);
      image_packed = bytes[off + 8u];
      off += 9u;

      if ((image_width == 0u) || (image_height == 0u)) {
        ni_set_error(err, err_capacity, "GIF image descriptor has zero size");
        return 0;
      }

      if ((image_packed & 0x80u) != 0u) {
        size_t local_entries = (size_t)1u << ((image_packed & 0x07u) + 1u);
        size_t local_size = 0u;
        if (!ni_size_mul(local_entries, 3u, &local_size) || (off + local_size > size)) {
          ni_set_error(err, err_capacity, "truncated GIF local color table");
          return 0;
        }
        color_table = bytes + off;
        color_table_entries = local_entries;
        off += local_size;
      }
      if ((color_table == NULL) || (color_table_entries == 0u)) {
        ni_set_error(err, err_capacity, "GIF image is missing a color table");
        return 0;
      }
      if (off >= size) {
        ni_set_error(err, err_capacity, "truncated GIF LZW header");
        return 0;
      }
      min_code_size = bytes[off++];
      if (!ni_collect_gif_subblocks(bytes, size, &off, &compressed, &compressed_size,
                                    err, err_capacity)) {
        return 0;
      }

      if (!ni_size_mul((size_t)image_width, (size_t)image_height, &pixel_count) ||
          !ni_size_mul(pixel_count, 4u, &output_size)) {
        ni_stbi_free(compressed);
        ni_set_error(err, err_capacity, "GIF output size overflow");
        return 0;
      }

      pixels = (uint8_t *)ni_stbi_malloc(output_size);
      if (pixels == NULL) {
        ni_stbi_free(compressed);
        ni_stbi_free(pixels);
        ni_set_error(err, err_capacity, "out of memory for GIF pixels");
        return 0;
      }
      ni_gif_output_init(&output_state, pixels, image_width, image_height, color_table,
                         color_table_entries, transparent_index, has_transparent,
                         (image_packed & 0x40u) != 0u);
      if (!ni_gif_lzw_decode(compressed, compressed_size, min_code_size, &output_state,
                             pixel_count, err, err_capacity)) {
        ni_stbi_free(compressed);
        ni_stbi_free(pixels);
        return 0;
      }
      ni_stbi_free(compressed);
      out->width = image_width;
      out->height = image_height;
      out->channels = 4u;
      out->bit_depth = 8u;
      out->data_size = output_size;
      out->data = pixels;
      return 1;
    }

    ni_set_error(err, err_capacity, "invalid GIF block introducer");
    return 0;
  }

  ni_set_error(err, err_capacity, "no GIF image descriptor found");
  return 0;
}
