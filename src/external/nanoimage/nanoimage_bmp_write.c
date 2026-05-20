#include "nanoimage_bmp.h"

#include "nanoimage_alloc_internal.h"
#include "nanoimage_write_internal.h"

#include <limits.h>
#include <string.h>

static int ni_write_bmp_impl(const ni_image *image, ni_stream_writer *writer) {
  size_t row_stride = 0u;
  size_t required_size = 0u;
  uint16_t bit_count;
  uint32_t pixel_offset;
  size_t file_size;
  size_t disk_row_stride;
  uint8_t *row = NULL;
  uint32_t y;

  if (!ni_write_image_layout(image, 4u, &row_stride, &required_size, writer->err,
                             writer->err_capacity)) {
    return 0;
  }
  (void)required_size;

  if (image->bit_depth != 8u) {
    ni_write_set_error(writer->err, writer->err_capacity,
                       "BMP writer supports only 8-bit images");
    return 0;
  }
  if ((image->width > (uint32_t)INT32_MAX) || (image->height > (uint32_t)INT32_MAX)) {
    ni_write_set_error(writer->err, writer->err_capacity, "BMP dimensions exceed 32-bit limit");
    return 0;
  }

  if (image->channels == 1u) {
    bit_count = 8u;
    disk_row_stride = (((size_t)image->width + 3u) / 4u) * 4u;
    pixel_offset = 14u + 40u + 256u * 4u;
  } else if (image->channels == 3u) {
    bit_count = 24u;
    if (!ni_write_size_mul((size_t)image->width, 3u, &disk_row_stride)) {
      ni_write_set_error(writer->err, writer->err_capacity, "BMP row size overflow");
      return 0;
    }
    disk_row_stride = ((disk_row_stride + 3u) / 4u) * 4u;
    pixel_offset = 14u + 40u;
  } else {
    bit_count = 32u;
    if (!ni_write_size_mul((size_t)image->width, 4u, &disk_row_stride)) {
      ni_write_set_error(writer->err, writer->err_capacity, "BMP row size overflow");
      return 0;
    }
    pixel_offset = 14u + 40u;
  }

  if (!ni_write_size_mul(disk_row_stride, (size_t)image->height, &file_size) ||
      !ni_write_size_add(file_size, (size_t)pixel_offset, &file_size) ||
      (file_size > (size_t)UINT32_MAX)) {
    ni_write_set_error(writer->err, writer->err_capacity, "BMP file size overflow");
    return 0;
  }

  row = (uint8_t *)ni_stbi_malloc(disk_row_stride);
  if (row == NULL) {
    ni_write_set_error(writer->err, writer->err_capacity, "out of memory for BMP row");
    return 0;
  }

  if (!ni_stream_write(writer, "BM", 2u) ||
      !ni_stream_write_u32le(writer, (uint32_t)file_size) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u32le(writer, pixel_offset) ||
      !ni_stream_write_u32le(writer, 40u) ||
      !ni_stream_write_u32le(writer, image->width) ||
      !ni_stream_write_u32le(writer, (uint32_t)(-(int32_t)image->height)) ||
      !ni_stream_write_u16le(writer, 1u) ||
      !ni_stream_write_u16le(writer, bit_count) ||
      !ni_stream_write_u32le(writer, 0u) ||
      !ni_stream_write_u32le(writer, (uint32_t)(file_size - (size_t)pixel_offset)) ||
      !ni_stream_write_u32le(writer, 2835u) ||
      !ni_stream_write_u32le(writer, 2835u) ||
      !ni_stream_write_u32le(writer, (bit_count == 8u) ? 256u : 0u) ||
      !ni_stream_write_u32le(writer, 0u)) {
    ni_stbi_free(row);
    return 0;
  }

  if (bit_count == 8u) {
    uint16_t i;
    for (i = 0u; i < 256u; i++) {
      const uint8_t entry[4] = {(uint8_t)i, (uint8_t)i, (uint8_t)i, 0u};
      if (!ni_stream_write(writer, entry, sizeof(entry))) {
        ni_stbi_free(row);
        return 0;
      }
    }
  }

  for (y = 0u; y < image->height; y++) {
    const uint8_t *src = image->data + (size_t)y * row_stride;
    uint32_t x;
    memset(row, 0, disk_row_stride);

    if (bit_count == 8u) {
      memcpy(row, src, (size_t)image->width);
    } else if (bit_count == 24u) {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * 3u;
        row[(size_t)x * 3u + 0u] = p[2];
        row[(size_t)x * 3u + 1u] = p[1];
        row[(size_t)x * 3u + 2u] = p[0];
      }
    } else if (image->channels == 2u) {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * 2u;
        row[(size_t)x * 4u + 0u] = p[0];
        row[(size_t)x * 4u + 1u] = p[0];
        row[(size_t)x * 4u + 2u] = p[0];
        row[(size_t)x * 4u + 3u] = p[1];
      }
    } else {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * image->channels;
        row[(size_t)x * 4u + 0u] = p[2];
        row[(size_t)x * 4u + 1u] = p[1];
        row[(size_t)x * 4u + 2u] = p[0];
        row[(size_t)x * 4u + 3u] = p[3];
      }
    }

    if (!ni_stream_write(writer, row, disk_row_stride)) {
      ni_stbi_free(row);
      return 0;
    }
  }

  ni_stbi_free(row);
  return 1;
}

int ni_write_bmp(const ni_image *image, ni_write_callback write_fn,
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
  return ni_write_bmp_impl(image, &writer);
}

int ni_write_bmp_to_memory(const ni_image *image, ni_buffer *out, char *err,
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
  if (!ni_write_bmp_impl(image, &writer)) {
    ni_buffer_free(out);
    return 0;
  }
  return 1;
}
