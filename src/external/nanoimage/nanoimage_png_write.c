#include "nanoimage_png.h"

#include "nanoimage_write_internal.h"

#include <limits.h>
#include <string.h>
#include <zlib.h>

static const uint8_t k_png_signature[8] = {0x89u, 0x50u, 0x4Eu, 0x47u,
                                           0x0Du, 0x0Au, 0x1Au, 0x0Au};

static uint32_t ni_png_crc32(const uint8_t *data, size_t n) {
  uint32_t crc = 0xffffffffu;
  size_t i;
  for (i = 0u; i < n; i++) {
    int j;
    crc ^= (uint32_t)data[i];
    for (j = 0; j < 8; j++) {
      const uint32_t mask = (uint32_t)-(int32_t)(crc & 1u);
      crc = (crc >> 1u) ^ (0xedb88320u & mask);
    }
  }
  return crc ^ 0xffffffffu;
}

static int ni_png_write_chunk(ni_stream_writer *writer, const char type[4],
                              const uint8_t *data, uint32_t size) {
  uint8_t combined[4u + 4096u];
  uint32_t crc_value;

  if (size > 0u) {
    if (size <= 4096u) {
      memcpy(combined, type, 4u);
      memcpy(combined + 4u, data, size);
      crc_value = ni_png_crc32(combined, 4u + (size_t)size);
    } else {
      crc_value = (uint32_t)crc32(0L, Z_NULL, 0);
      crc_value = (uint32_t)crc32(crc_value, (const Bytef *)type, 4u);
      crc_value = (uint32_t)crc32(crc_value, data, size);
    }
  } else {
    crc_value = ni_png_crc32((const uint8_t *)type, 4u);
  }

  if (!ni_stream_write_u32be(writer, size) ||
      !ni_stream_write(writer, type, 4u) ||
      ((size > 0u) && !ni_stream_write(writer, data, size))) {
    return 0;
  }
  return ni_stream_write_u32be(writer, crc_value);
}

static int ni_png_deflate_data(ni_stream_writer *writer, z_stream *zs,
                               const uint8_t *data, size_t size, int flush) {
  uint8_t outbuf[4096];
  int zret = Z_OK;

  if (size > (size_t)UINT_MAX) {
    ni_write_set_error(writer->err, writer->err_capacity, "PNG row exceeds zlib limit");
    return 0;
  }

  zs->next_in = (Bytef *)data;
  zs->avail_in = (uInt)size;
  do {
    size_t produced;
    zs->next_out = outbuf;
    zs->avail_out = (uInt)sizeof(outbuf);
    zret = deflate(zs, flush);
    if ((zret != Z_OK) && (zret != Z_STREAM_END)) {
      ni_write_set_error(writer->err, writer->err_capacity,
                         "zlib deflate failed (%d)", zret);
      return 0;
    }
    produced = sizeof(outbuf) - (size_t)zs->avail_out;
    if ((produced > 0u) &&
        !ni_png_write_chunk(writer, "IDAT", outbuf, (uint32_t)produced)) {
      return 0;
    }
  } while ((zs->avail_in > 0u) || (zs->avail_out == 0u) ||
           ((flush == Z_FINISH) && (zret != Z_STREAM_END)));

  return 1;
}

static int ni_write_png_impl(const ni_image *image, ni_stream_writer *writer) {
  size_t row_stride = 0u;
  size_t required_size = 0u;
  uint8_t ihdr[13];
  uint8_t color_type;
  z_stream zs;
  uint32_t y;

  if (!ni_write_image_layout(image, 4u, &row_stride, &required_size, writer->err,
                             writer->err_capacity)) {
    return 0;
  }
  (void)required_size;

  if ((image->bit_depth != 8u) && (image->bit_depth != 16u)) {
    ni_write_set_error(writer->err, writer->err_capacity,
                       "PNG writer supports only 8-bit and 16-bit images");
    return 0;
  }

  if (image->channels == 1u) {
    color_type = 0u;
  } else if (image->channels == 2u) {
    color_type = 4u;
  } else if (image->channels == 3u) {
    color_type = 2u;
  } else {
    color_type = 6u;
  }

  if (!ni_stream_write(writer, k_png_signature, sizeof(k_png_signature))) {
    return 0;
  }

  ihdr[0] = (uint8_t)(image->width >> 24u);
  ihdr[1] = (uint8_t)(image->width >> 16u);
  ihdr[2] = (uint8_t)(image->width >> 8u);
  ihdr[3] = (uint8_t)(image->width & 0xffu);
  ihdr[4] = (uint8_t)(image->height >> 24u);
  ihdr[5] = (uint8_t)(image->height >> 16u);
  ihdr[6] = (uint8_t)(image->height >> 8u);
  ihdr[7] = (uint8_t)(image->height & 0xffu);
  ihdr[8] = image->bit_depth;
  ihdr[9] = color_type;
  ihdr[10] = 0u;
  ihdr[11] = 0u;
  ihdr[12] = 0u;
  if (!ni_png_write_chunk(writer, "IHDR", ihdr, sizeof(ihdr))) {
    return 0;
  }

  memset(&zs, 0, sizeof(zs));
  if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
    ni_write_set_error(writer->err, writer->err_capacity, "zlib init failed");
    return 0;
  }

  for (y = 0u; y < image->height; y++) {
    const uint8_t filter = 0u;
    const uint8_t *row = image->data + (size_t)y * row_stride;
    if (!ni_png_deflate_data(writer, &zs, &filter, 1u, Z_NO_FLUSH) ||
        !ni_png_deflate_data(writer, &zs, row, row_stride, Z_NO_FLUSH)) {
      (void)deflateEnd(&zs);
      return 0;
    }
  }

  if (!ni_png_deflate_data(writer, &zs, NULL, 0u, Z_FINISH)) {
    (void)deflateEnd(&zs);
    return 0;
  }
  (void)deflateEnd(&zs);

  return ni_png_write_chunk(writer, "IEND", NULL, 0u);
}

int ni_write_png(const ni_image *image, ni_write_callback write_fn,
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
  return ni_write_png_impl(image, &writer);
}

int ni_write_png_to_memory(const ni_image *image, ni_buffer *out, char *err,
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
  if (!ni_write_png_impl(image, &writer)) {
    ni_buffer_free(out);
    return 0;
  }
  return 1;
}
