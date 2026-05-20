#include "nanoimage_tga.h"

#include "nanoimage_alloc_internal.h"
#include "nanoimage_write_internal.h"

#include <limits.h>
#include <string.h>

static int ni_write_tga_impl(const ni_image *image, ni_stream_writer *writer) {
  size_t row_stride = 0u;
  size_t required_size = 0u;
  uint8_t image_type;
  uint8_t pixel_depth;
  uint8_t descriptor;
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
                       "TGA writer supports only 8-bit images");
    return 0;
  }
  if ((image->width > (uint32_t)UINT16_MAX) || (image->height > (uint32_t)UINT16_MAX)) {
    ni_write_set_error(writer->err, writer->err_capacity, "TGA dimensions exceed 16-bit limit");
    return 0;
  }

  if (image->channels == 1u) {
    image_type = 3u;
    pixel_depth = 8u;
    descriptor = 0x20u;
    disk_row_stride = (size_t)image->width;
  } else if (image->channels == 3u) {
    image_type = 2u;
    pixel_depth = 24u;
    descriptor = 0x20u;
    if (!ni_write_size_mul((size_t)image->width, 3u, &disk_row_stride)) {
      ni_write_set_error(writer->err, writer->err_capacity, "TGA row size overflow");
      return 0;
    }
  } else {
    image_type = 2u;
    pixel_depth = 32u;
    descriptor = 0x28u;
    if (!ni_write_size_mul((size_t)image->width, 4u, &disk_row_stride)) {
      ni_write_set_error(writer->err, writer->err_capacity, "TGA row size overflow");
      return 0;
    }
  }

  row = (uint8_t *)ni_stbi_malloc(disk_row_stride);
  if (row == NULL) {
    ni_write_set_error(writer->err, writer->err_capacity, "out of memory for TGA row");
    return 0;
  }

  if (!ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u8(writer, image_type) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u8(writer, 0u) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, 0u) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->width) ||
      !ni_stream_write_u16le(writer, (uint16_t)image->height) ||
      !ni_stream_write_u8(writer, pixel_depth) ||
      !ni_stream_write_u8(writer, descriptor)) {
    ni_stbi_free(row);
    return 0;
  }

  for (y = 0u; y < image->height; y++) {
    const uint8_t *src = image->data + (size_t)y * row_stride;
    uint32_t x;

    if (image->channels == 1u) {
      memcpy(row, src, disk_row_stride);
    } else if (image->channels == 2u) {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * 2u;
        row[(size_t)x * 4u + 0u] = p[0];
        row[(size_t)x * 4u + 1u] = p[0];
        row[(size_t)x * 4u + 2u] = p[0];
        row[(size_t)x * 4u + 3u] = p[1];
      }
    } else if (image->channels == 3u) {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * 3u;
        row[(size_t)x * 3u + 0u] = p[2];
        row[(size_t)x * 3u + 1u] = p[1];
        row[(size_t)x * 3u + 2u] = p[0];
      }
    } else {
      for (x = 0u; x < image->width; x++) {
        const uint8_t *p = src + (size_t)x * 4u;
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

int ni_write_tga(const ni_image *image, ni_write_callback write_fn,
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
  return ni_write_tga_impl(image, &writer);
}

int ni_write_tga_to_memory(const ni_image *image, ni_buffer *out, char *err,
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
  if (!ni_write_tga_impl(image, &writer)) {
    ni_buffer_free(out);
    return 0;
  }
  return 1;
}
