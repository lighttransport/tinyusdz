#include "nanoimage_jpeg.h"

#include "nanoimage_write_internal.h"

#include <math.h>
#include <string.h>

static const uint8_t k_zigzag[64] = {
    0,  1,  8, 16,  9,  2,  3, 10, 17, 24, 32, 25, 18, 11,  4,  5,
   12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13,  6,  7, 14, 21, 28,
   35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51,
   58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static const double k_pi = 3.14159265358979323846;

static const uint8_t k_luma_quant_base[64] = {
    16, 11, 10, 16, 24,  40,  51,  61,
    12, 12, 14, 19, 26,  58,  60,  55,
    14, 13, 16, 24, 40,  57,  69,  56,
    14, 17, 22, 29, 51,  87,  80,  62,
    18, 22, 37, 56, 68, 109, 103,  77,
    24, 35, 55, 64, 81, 104, 113,  92,
    49, 64, 78, 87,103, 121, 120, 101,
    72, 92, 95, 98,112, 100, 103,  99};

static const uint8_t k_chroma_quant_base[64] = {
    17, 18, 24, 47, 99, 99, 99, 99,
    18, 21, 26, 66, 99, 99, 99, 99,
    24, 26, 56, 99, 99, 99, 99, 99,
    47, 66, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99,
    99, 99, 99, 99, 99, 99, 99, 99};

static const uint8_t k_dc_luma_bits[16] = {
    0, 1, 5, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0, 0, 0};
static const uint8_t k_dc_luma_vals[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t k_dc_chroma_bits[16] = {
    0, 3, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static const uint8_t k_dc_chroma_vals[12] = {
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};

static const uint8_t k_ac_luma_bits[16] = {
    0, 2, 1, 3, 3, 2, 4, 3, 5, 5, 4, 4, 0, 0, 1, 125};
static const uint8_t k_ac_luma_vals[162] = {
    0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31, 0x41, 0x06,
    0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91, 0xA1, 0x08,
    0x23, 0x42, 0xB1, 0xC1, 0x15, 0x52, 0xD1, 0xF0, 0x24, 0x33, 0x62, 0x72,
    0x82, 0x09, 0x0A, 0x16, 0x17, 0x18, 0x19, 0x1A, 0x25, 0x26, 0x27, 0x28,
    0x29, 0x2A, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44, 0x45,
    0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59,
    0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74, 0x75,
    0x76, 0x77, 0x78, 0x79, 0x7A, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89,
    0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0xA2, 0xA3,
    0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4, 0xB5, 0xB6,
    0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7, 0xC8, 0xC9,
    0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA, 0xE1, 0xE2,
    0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF1, 0xF2, 0xF3, 0xF4,
    0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

static const uint8_t k_ac_chroma_bits[16] = {
    0, 2, 1, 2, 4, 4, 3, 4, 7, 5, 4, 4, 0, 1, 2, 119};
static const uint8_t k_ac_chroma_vals[162] = {
    0x00, 0x01, 0x02, 0x03, 0x11, 0x04, 0x05, 0x21, 0x31, 0x06, 0x12, 0x41,
    0x51, 0x07, 0x61, 0x71, 0x13, 0x22, 0x32, 0x81, 0x08, 0x14, 0x42, 0x91,
    0xA1, 0xB1, 0xC1, 0x09, 0x23, 0x33, 0x52, 0xF0, 0x15, 0x62, 0x72, 0xD1,
    0x0A, 0x16, 0x24, 0x34, 0xE1, 0x25, 0xF1, 0x17, 0x18, 0x19, 0x1A, 0x26,
    0x27, 0x28, 0x29, 0x2A, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3A, 0x43, 0x44,
    0x45, 0x46, 0x47, 0x48, 0x49, 0x4A, 0x53, 0x54, 0x55, 0x56, 0x57, 0x58,
    0x59, 0x5A, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6A, 0x73, 0x74,
    0x75, 0x76, 0x77, 0x78, 0x79, 0x7A, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8A, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A,
    0xA2, 0xA3, 0xA4, 0xA5, 0xA6, 0xA7, 0xA8, 0xA9, 0xAA, 0xB2, 0xB3, 0xB4,
    0xB5, 0xB6, 0xB7, 0xB8, 0xB9, 0xBA, 0xC2, 0xC3, 0xC4, 0xC5, 0xC6, 0xC7,
    0xC8, 0xC9, 0xCA, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8, 0xD9, 0xDA,
    0xE2, 0xE3, 0xE4, 0xE5, 0xE6, 0xE7, 0xE8, 0xE9, 0xEA, 0xF2, 0xF3, 0xF4,
    0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFA};

typedef struct {
  uint16_t code[256];
  uint8_t size[256];
} ni_jpeg_huff_codes;

typedef struct {
  ni_stream_writer *writer;
  uint32_t bit_buffer;
  unsigned bit_count;
} ni_jpeg_bit_writer;

static void ni_jpeg_fdct_block(const double in[64], int out[64]) {
  int v;
  for (v = 0; v < 8; v++) {
    int u;
    for (u = 0; u < 8; u++) {
      double sum = 0.0;
      int y;
      for (y = 0; y < 8; y++) {
        int x;
        for (x = 0; x < 8; x++) {
          sum += in[y * 8 + x] *
                 cos(((2.0 * (double)x + 1.0) * (double)u * k_pi) / 16.0) *
                 cos(((2.0 * (double)y + 1.0) * (double)v * k_pi) / 16.0);
        }
      }
      {
        const double cu = (u == 0) ? (1.0 / sqrt(2.0)) : 1.0;
        const double cv = (v == 0) ? (1.0 / sqrt(2.0)) : 1.0;
        out[v * 8 + u] = (int)lrint(0.25 * cu * cv * sum);
      }
    }
  }
}

static void ni_jpeg_build_huff_codes(const uint8_t *bits, const uint8_t *vals,
                                     ni_jpeg_huff_codes *out) {
  uint16_t code = 0u;
  uint16_t k = 0u;
  uint8_t i;
  memset(out, 0, sizeof(*out));
  for (i = 1u; i <= 16u; i++) {
    uint8_t j;
    for (j = 0u; j < bits[i - 1u]; j++) {
      out->code[vals[k]] = code;
      out->size[vals[k]] = i;
      code++;
      k++;
    }
    code <<= 1u;
  }
}

static int ni_jpeg_emit_byte(ni_jpeg_bit_writer *bw, uint8_t byte) {
  if (!ni_stream_write_u8(bw->writer, byte)) {
    return 0;
  }
  if (byte == 0xFFu) {
    return ni_stream_write_u8(bw->writer, 0x00u);
  }
  return 1;
}

static int ni_jpeg_emit_bits(ni_jpeg_bit_writer *bw, uint16_t bits, uint8_t count) {
  if (count == 0u) {
    return 1;
  }
  bw->bit_buffer = (bw->bit_buffer << count) | (uint32_t)bits;
  bw->bit_count += count;
  while (bw->bit_count >= 8u) {
    const uint8_t byte =
        (uint8_t)((bw->bit_buffer >> (bw->bit_count - 8u)) & 0xffu);
    if (!ni_jpeg_emit_byte(bw, byte)) {
      return 0;
    }
    bw->bit_count -= 8u;
    if (bw->bit_count == 0u) {
      bw->bit_buffer = 0u;
    } else {
      bw->bit_buffer &= (1u << bw->bit_count) - 1u;
    }
  }
  return 1;
}

static int ni_jpeg_flush_bits(ni_jpeg_bit_writer *bw) {
  if (bw->bit_count > 0u) {
    const uint8_t pad = (uint8_t)((bw->bit_buffer << (8u - bw->bit_count)) |
                                  ((1u << (8u - bw->bit_count)) - 1u));
    bw->bit_count = 0u;
    bw->bit_buffer = 0u;
    if (!ni_jpeg_emit_byte(bw, pad)) {
      return 0;
    }
  }
  return 1;
}

static void ni_jpeg_scaled_quant_table(const uint8_t *base, int quality,
                                       uint8_t *out) {
  int scale = (quality < 50) ? (5000 / quality) : (200 - quality * 2);
  int i;
  for (i = 0; i < 64; i++) {
    int v = (base[i] * scale + 50) / 100;
    if (v < 1) {
      v = 1;
    }
    if (v > 255) {
      v = 255;
    }
    out[i] = (uint8_t)v;
  }
}

static void ni_jpeg_value_bits(int value, uint16_t *bits, uint8_t *size) {
  unsigned abs_value;
  uint8_t n = 0u;
  if (value == 0) {
    *bits = 0u;
    *size = 0u;
    return;
  }
  abs_value = (unsigned)((value < 0) ? -value : value);
  while (abs_value != 0u) {
    abs_value >>= 1u;
    n++;
  }
  *size = n;
  if (value < 0) {
    *bits = (uint16_t)((1u << n) + value - 1);
  } else {
    *bits = (uint16_t)value;
  }
}

static int ni_jpeg_encode_block(ni_jpeg_bit_writer *bw, const int coeff[64],
                                const uint8_t *quant, int *prev_dc,
                                const ni_jpeg_huff_codes *dc_codes,
                                const ni_jpeg_huff_codes *ac_codes) {
  int qcoeff[64];
  int diff;
  uint16_t bits;
  uint8_t size;
  int k;
  qcoeff[0] = (int)lrint((double)coeff[0] / (double)quant[0]);
  diff = qcoeff[0] - *prev_dc;
  *prev_dc = qcoeff[0];
  ni_jpeg_value_bits(diff, &bits, &size);
  if (!ni_jpeg_emit_bits(bw, dc_codes->code[size], dc_codes->size[size]) ||
      !ni_jpeg_emit_bits(bw, bits, size)) {
    return 0;
  }

  for (k = 1; k < 64; k++) {
    const uint8_t idx = k_zigzag[k];
    qcoeff[idx] = (int)lrint((double)coeff[idx] / (double)quant[idx]);
  }

  {
    int run = 0;
    for (k = 1; k < 64; k++) {
      const int value = qcoeff[k_zigzag[k]];
      if (value == 0) {
        run++;
        continue;
      }
      while (run >= 16) {
        if (!ni_jpeg_emit_bits(bw, ac_codes->code[0xF0u], ac_codes->size[0xF0u])) {
          return 0;
        }
        run -= 16;
      }
      ni_jpeg_value_bits(value, &bits, &size);
      if (!ni_jpeg_emit_bits(bw, ac_codes->code[(run << 4u) | size],
                             ac_codes->size[(run << 4u) | size]) ||
          !ni_jpeg_emit_bits(bw, bits, size)) {
        return 0;
      }
      run = 0;
    }
    if (run > 0) {
      if (!ni_jpeg_emit_bits(bw, ac_codes->code[0x00u], ac_codes->size[0x00u])) {
        return 0;
      }
    }
  }
  return 1;
}

static int ni_jpeg_sample_component(const ni_image *image, size_t row_stride,
                                    uint32_t x, uint32_t y, int component) {
  if (x >= image->width) {
    x = image->width - 1u;
  }
  if (y >= image->height) {
    y = image->height - 1u;
  }
  {
    const uint8_t *p = image->data + (size_t)y * row_stride +
                       (size_t)x * (size_t)image->channels;
    if ((image->channels == 1u) || (image->channels == 2u)) {
      (void)component;
      return p[0];
    }
    return p[component];
  }
}

static void ni_jpeg_fill_gray_block(const ni_image *image, size_t row_stride,
                                    uint32_t bx, uint32_t by, double out[64]) {
  uint32_t y;
  for (y = 0u; y < 8u; y++) {
    uint32_t x;
    for (x = 0u; x < 8u; x++) {
      const int gray =
          ni_jpeg_sample_component(image, row_stride, bx + x, by + y, 0);
      out[y * 8u + x] = (double)(gray - 128);
    }
  }
}

static void ni_jpeg_fill_color_blocks(const ni_image *image, size_t row_stride,
                                      uint32_t bx, uint32_t by, double yb[64],
                                      double cbb[64], double crb[64]) {
  uint32_t y;
  for (y = 0u; y < 8u; y++) {
    uint32_t x;
    for (x = 0u; x < 8u; x++) {
      const int r =
          ni_jpeg_sample_component(image, row_stride, bx + x, by + y, 0);
      const int g =
          ni_jpeg_sample_component(image, row_stride, bx + x, by + y, 1);
      const int b =
          ni_jpeg_sample_component(image, row_stride, bx + x, by + y, 2);
      const size_t idx = (size_t)y * 8u + x;
      yb[idx] = 0.299 * (double)r + 0.587 * (double)g + 0.114 * (double)b - 128.0;
      cbb[idx] = -0.168736 * (double)r - 0.331264 * (double)g +
                 0.5 * (double)b;
      crb[idx] = 0.5 * (double)r - 0.418688 * (double)g -
                 0.081312 * (double)b;
    }
  }
}

static int ni_jpeg_write_dqt(ni_stream_writer *writer, uint8_t table_id,
                             const uint8_t *quant) {
  uint8_t payload[65];
  int i;
  payload[0] = table_id;
  for (i = 0; i < 64; i++) {
    payload[1 + i] = quant[k_zigzag[i]];
  }
  return ni_stream_write_u8(writer, 0xFFu) &&
         ni_stream_write_u8(writer, 0xDBu) &&
         ni_stream_write_u16be(writer, (uint16_t)(2u + sizeof(payload))) &&
         ni_stream_write(writer, payload, sizeof(payload));
}

static int ni_jpeg_write_dht(ni_stream_writer *writer, uint8_t table_class,
                             uint8_t table_id, const uint8_t *bits,
                             const uint8_t *vals, size_t val_count) {
  uint8_t info = (uint8_t)((table_class << 4u) | table_id);
  return ni_stream_write_u8(writer, 0xFFu) &&
         ni_stream_write_u8(writer, 0xC4u) &&
         ni_stream_write_u16be(writer, (uint16_t)(2u + 1u + 16u + val_count)) &&
         ni_stream_write_u8(writer, info) &&
         ni_stream_write(writer, bits, 16u) &&
         ni_stream_write(writer, vals, val_count);
}

static int ni_write_jpeg_impl(const ni_image *image,
                              const ni_jpeg_write_options *options,
                              ni_stream_writer *writer) {
  size_t row_stride = 0u;
  size_t required_size = 0u;
  uint8_t q_luma[64];
  uint8_t q_chroma[64];
  ni_jpeg_huff_codes dc_luma;
  ni_jpeg_huff_codes ac_luma;
  ni_jpeg_huff_codes dc_chroma;
  ni_jpeg_huff_codes ac_chroma;
  ni_jpeg_bit_writer bw;
  int quality = (options != NULL) ? options->quality : 90;
  uint8_t components;
  uint32_t by;
  int prev_dc[3] = {0, 0, 0};

  if (!ni_write_image_layout(image, 4u, &row_stride, &required_size, writer->err,
                             writer->err_capacity)) {
    return 0;
  }
  (void)required_size;

  if (image->bit_depth != 8u) {
    ni_write_set_error(writer->err, writer->err_capacity,
                       "JPEG writer supports only 8-bit images");
    return 0;
  }
  if ((image->width > (uint32_t)UINT16_MAX) || (image->height > (uint32_t)UINT16_MAX)) {
    ni_write_set_error(writer->err, writer->err_capacity, "JPEG dimensions exceed 16-bit limit");
    return 0;
  }
  if ((quality < 1) || (quality > 100)) {
    ni_write_set_error(writer->err, writer->err_capacity, "JPEG quality must be 1..100");
    return 0;
  }

  components = ((image->channels == 1u) || (image->channels == 2u)) ? 1u : 3u;

  ni_jpeg_scaled_quant_table(k_luma_quant_base, quality, q_luma);
  ni_jpeg_scaled_quant_table(k_chroma_quant_base, quality, q_chroma);
  ni_jpeg_build_huff_codes(k_dc_luma_bits, k_dc_luma_vals, &dc_luma);
  ni_jpeg_build_huff_codes(k_ac_luma_bits, k_ac_luma_vals, &ac_luma);
  ni_jpeg_build_huff_codes(k_dc_chroma_bits, k_dc_chroma_vals, &dc_chroma);
  ni_jpeg_build_huff_codes(k_ac_chroma_bits, k_ac_chroma_vals, &ac_chroma);

  if (!ni_stream_write_u8(writer, 0xFFu) ||
      !ni_stream_write_u8(writer, 0xD8u) ||
      !ni_stream_write_u8(writer, 0xFFu) ||
      !ni_stream_write_u8(writer, 0xE0u) ||
      !ni_stream_write_u16be(writer, 16u) ||
      !ni_stream_write(writer, "JFIF\0", 5u) ||
      !ni_stream_write_u8(writer, 1u) ||
      !ni_stream_write_u8(writer, 1u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u16be(writer, 1u) ||
      !ni_stream_write_u16be(writer, 1u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_jpeg_write_dqt(writer, 0u, q_luma) ||
      ((components == 3u) && !ni_jpeg_write_dqt(writer, 1u, q_chroma)) ||
      !ni_stream_write_u8(writer, 0xFFu) ||
      !ni_stream_write_u8(writer, 0xC0u) ||
      !ni_stream_write_u16be(writer, (uint16_t)(8u + 3u * components)) ||
      !ni_stream_write_u8(writer, 8u) ||
      !ni_stream_write_u16be(writer, (uint16_t)image->height) ||
      !ni_stream_write_u16be(writer, (uint16_t)image->width) ||
      !ni_stream_write_u8(writer, components)) {
    return 0;
  }

  if (components == 1u) {
    if (!ni_stream_write_u8(writer, 1u) ||
        !ni_stream_write_u8(writer, 0x11u) ||
        !ni_stream_write_u8(writer, 0u)) {
      return 0;
    }
  } else {
    const uint8_t sof[] = {1u, 0x11u, 0u, 2u, 0x11u, 1u, 3u, 0x11u, 1u};
    if (!ni_stream_write(writer, sof, sizeof(sof))) {
      return 0;
    }
  }

  if (!ni_jpeg_write_dht(writer, 0u, 0u, k_dc_luma_bits, k_dc_luma_vals,
                         sizeof(k_dc_luma_vals)) ||
      !ni_jpeg_write_dht(writer, 1u, 0u, k_ac_luma_bits, k_ac_luma_vals,
                         sizeof(k_ac_luma_vals)) ||
      ((components == 3u) &&
       (!ni_jpeg_write_dht(writer, 0u, 1u, k_dc_chroma_bits, k_dc_chroma_vals,
                           sizeof(k_dc_chroma_vals)) ||
        !ni_jpeg_write_dht(writer, 1u, 1u, k_ac_chroma_bits, k_ac_chroma_vals,
                           sizeof(k_ac_chroma_vals)))) ||
      !ni_stream_write_u8(writer, 0xFFu) ||
      !ni_stream_write_u8(writer, 0xDAu) ||
      !ni_stream_write_u16be(writer, (uint16_t)(6u + 2u * components)) ||
      !ni_stream_write_u8(writer, components)) {
    return 0;
  }

  if (components == 1u) {
    if (!ni_stream_write_u8(writer, 1u) ||
        !ni_stream_write_u8(writer, 0x00u)) {
      return 0;
    }
  } else {
    const uint8_t sos[] = {1u, 0x00u, 2u, 0x11u, 3u, 0x11u};
    if (!ni_stream_write(writer, sos, sizeof(sos))) {
      return 0;
    }
  }
  if (!ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, 63u) ||
      !ni_stream_write_u8(writer, 0u)) {
    return 0;
  }

  memset(&bw, 0, sizeof(bw));
  bw.writer = writer;

  for (by = 0u; by < image->height; by += 8u) {
    uint32_t bx;
    for (bx = 0u; bx < image->width; bx += 8u) {
      if (components == 1u) {
        double block[64];
        int coeff[64];
        ni_jpeg_fill_gray_block(image, row_stride, bx, by, block);
        ni_jpeg_fdct_block(block, coeff);
        if (!ni_jpeg_encode_block(&bw, coeff, q_luma, &prev_dc[0], &dc_luma,
                                  &ac_luma)) {
          return 0;
        }
      } else {
        double yb[64];
        double cbb[64];
        double crb[64];
        int coeff[64];
        ni_jpeg_fill_color_blocks(image, row_stride, bx, by, yb, cbb, crb);
        ni_jpeg_fdct_block(yb, coeff);
        if (!ni_jpeg_encode_block(&bw, coeff, q_luma, &prev_dc[0], &dc_luma,
                                  &ac_luma)) {
          return 0;
        }
        ni_jpeg_fdct_block(cbb, coeff);
        if (!ni_jpeg_encode_block(&bw, coeff, q_chroma, &prev_dc[1], &dc_chroma,
                                  &ac_chroma)) {
          return 0;
        }
        ni_jpeg_fdct_block(crb, coeff);
        if (!ni_jpeg_encode_block(&bw, coeff, q_chroma, &prev_dc[2], &dc_chroma,
                                  &ac_chroma)) {
          return 0;
        }
      }
    }
  }

  if (!ni_jpeg_flush_bits(&bw) ||
      !ni_stream_write_u8(writer, 0xFFu) ||
      !ni_stream_write_u8(writer, 0xD9u)) {
    return 0;
  }
  return 1;
}

int ni_write_jpeg(const ni_image *image, const ni_jpeg_write_options *options,
                  ni_write_callback write_fn, void *user_data, char *err,
                  size_t err_capacity) {
  ni_stream_writer writer;
  if (write_fn == NULL) {
    ni_write_set_error(err, err_capacity, "invalid writer callback");
    return 0;
  }
  writer.write_fn = write_fn;
  writer.user_data = user_data;
  writer.err = err;
  writer.err_capacity = err_capacity;
  return ni_write_jpeg_impl(image, options, &writer);
}

int ni_write_jpeg_to_memory(const ni_image *image,
                            const ni_jpeg_write_options *options,
                            ni_buffer *out, char *err, size_t err_capacity) {
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
  if (!ni_write_jpeg_impl(image, options, &writer)) {
    ni_buffer_free(out);
    return 0;
  }
  return 1;
}
