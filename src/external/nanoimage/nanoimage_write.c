#include "nanoimage_write_internal.h"

#include "nanoimage_alloc_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void ni_write_set_error(char *err, size_t cap, const char *fmt, ...) {
  va_list args;

  if ((err == NULL) || (cap == 0u)) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err, cap, fmt, args);
  va_end(args);
}

int ni_write_size_add(size_t a, size_t b, size_t *out) {
  if (a > (SIZE_MAX - b)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

int ni_write_size_mul(size_t a, size_t b, size_t *out) {
  if ((a != 0u) && (b > (SIZE_MAX / a))) {
    return 0;
  }
  *out = a * b;
  return 1;
}

int ni_write_image_layout(const ni_image *image, uint8_t max_channels,
                          size_t *row_stride, size_t *required_size, char *err,
                          size_t err_capacity) {
  size_t sample_bytes;
  size_t stride;
  size_t total;

  if ((image == NULL) || (row_stride == NULL) || (required_size == NULL)) {
    ni_write_set_error(err, err_capacity, "invalid argument");
    return 0;
  }
  if ((image->width == 0u) || (image->height == 0u) || (image->data == NULL)) {
    ni_write_set_error(err, err_capacity, "image must have pixels and non-zero dimensions");
    return 0;
  }
  if ((image->channels == 0u) || (image->channels > max_channels)) {
    ni_write_set_error(err, err_capacity, "unsupported image channel count");
    return 0;
  }
  if ((image->bit_depth != 8u) && (image->bit_depth != 16u)) {
    ni_write_set_error(err, err_capacity, "unsupported image bit depth");
    return 0;
  }

  sample_bytes = (image->bit_depth == 16u) ? 2u : 1u;
  if (!ni_write_size_mul((size_t)image->width, (size_t)image->channels, &stride) ||
      !ni_write_size_mul(stride, sample_bytes, &stride) ||
      !ni_write_size_mul(stride, (size_t)image->height, &total)) {
    ni_write_set_error(err, err_capacity, "image buffer size overflow");
    return 0;
  }
  if (image->data_size < total) {
    ni_write_set_error(err, err_capacity, "image buffer is truncated");
    return 0;
  }

  *row_stride = stride;
  *required_size = total;
  return 1;
}

int ni_stream_write(ni_stream_writer *writer, const void *data, size_t size) {
  if ((writer == NULL) || (writer->write_fn == NULL) ||
      ((size > 0u) && (data == NULL))) {
    ni_write_set_error((writer != NULL) ? writer->err : NULL,
                       (writer != NULL) ? writer->err_capacity : 0u,
                       "invalid writer");
    return 0;
  }
  if ((size > 0u) &&
      !writer->write_fn((const uint8_t *)data, size, writer->user_data)) {
    ni_write_set_error(writer->err, writer->err_capacity, "writer callback failed");
    return 0;
  }
  return 1;
}

int ni_stream_write_u8(ni_stream_writer *writer, uint8_t value) {
  return ni_stream_write(writer, &value, 1u);
}

int ni_stream_write_u16le(ni_stream_writer *writer, uint16_t value) {
  uint8_t bytes[2];
  bytes[0] = (uint8_t)(value & 0xffu);
  bytes[1] = (uint8_t)(value >> 8u);
  return ni_stream_write(writer, bytes, sizeof(bytes));
}

int ni_stream_write_u16be(ni_stream_writer *writer, uint16_t value) {
  uint8_t bytes[2];
  bytes[0] = (uint8_t)(value >> 8u);
  bytes[1] = (uint8_t)(value & 0xffu);
  return ni_stream_write(writer, bytes, sizeof(bytes));
}

int ni_stream_write_u32le(ni_stream_writer *writer, uint32_t value) {
  uint8_t bytes[4];
  bytes[0] = (uint8_t)(value & 0xffu);
  bytes[1] = (uint8_t)((value >> 8u) & 0xffu);
  bytes[2] = (uint8_t)((value >> 16u) & 0xffu);
  bytes[3] = (uint8_t)(value >> 24u);
  return ni_stream_write(writer, bytes, sizeof(bytes));
}

int ni_stream_write_u32be(ni_stream_writer *writer, uint32_t value) {
  uint8_t bytes[4];
  bytes[0] = (uint8_t)(value >> 24u);
  bytes[1] = (uint8_t)((value >> 16u) & 0xffu);
  bytes[2] = (uint8_t)((value >> 8u) & 0xffu);
  bytes[3] = (uint8_t)(value & 0xffu);
  return ni_stream_write(writer, bytes, sizeof(bytes));
}

int ni_memory_writer_init(ni_memory_writer *writer, ni_buffer *buffer) {
  if ((writer == NULL) || (buffer == NULL)) {
    return 0;
  }

  writer->buffer = buffer;
  writer->capacity = 0u;
  buffer->data = NULL;
  buffer->size = 0u;
  return 1;
}

int ni_memory_write_callback(const uint8_t *data, size_t size, void *user_data) {
  ni_memory_writer *writer = (ni_memory_writer *)user_data;
  size_t needed;
  size_t new_capacity;
  uint8_t *new_data;

  if ((writer == NULL) || (writer->buffer == NULL) ||
      ((size > 0u) && (data == NULL))) {
    return 0;
  }
  if (size == 0u) {
    return 1;
  }
  if (!ni_write_size_add(writer->buffer->size, size, &needed)) {
    return 0;
  }

  new_capacity = writer->capacity;
  if (new_capacity == 0u) {
    new_capacity = 256u;
  }
  while (new_capacity < needed) {
    if (new_capacity > (SIZE_MAX / 2u)) {
      new_capacity = needed;
      break;
    }
    new_capacity *= 2u;
  }

  if (new_capacity != writer->capacity) {
    new_data = (uint8_t *)ni_stbi_realloc(writer->buffer->data, new_capacity);
    if (new_data == NULL) {
      return 0;
    }
    writer->buffer->data = new_data;
    writer->capacity = new_capacity;
  }

  memcpy(writer->buffer->data + writer->buffer->size, data, size);
  writer->buffer->size = needed;
  return 1;
}
