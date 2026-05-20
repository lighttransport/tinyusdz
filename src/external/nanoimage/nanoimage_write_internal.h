#ifndef NANOIMAGE_WRITE_INTERNAL_H_
#define NANOIMAGE_WRITE_INTERNAL_H_

#include "nanoimage.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
  ni_write_callback write_fn;
  void *user_data;
  char *err;
  size_t err_capacity;
} ni_stream_writer;

typedef struct {
  ni_buffer *buffer;
  size_t capacity;
} ni_memory_writer;

void ni_write_set_error(char *err, size_t cap, const char *fmt, ...);
int ni_write_size_add(size_t a, size_t b, size_t *out);
int ni_write_size_mul(size_t a, size_t b, size_t *out);
int ni_write_image_layout(const ni_image *image, uint8_t max_channels,
                          size_t *row_stride, size_t *required_size, char *err,
                          size_t err_capacity);
int ni_stream_write(ni_stream_writer *writer, const void *data, size_t size);
int ni_stream_write_u8(ni_stream_writer *writer, uint8_t value);
int ni_stream_write_u16le(ni_stream_writer *writer, uint16_t value);
int ni_stream_write_u16be(ni_stream_writer *writer, uint16_t value);
int ni_stream_write_u32le(ni_stream_writer *writer, uint32_t value);
int ni_stream_write_u32be(ni_stream_writer *writer, uint32_t value);
int ni_memory_writer_init(ni_memory_writer *writer, ni_buffer *buffer);
int ni_memory_write_callback(const uint8_t *data, size_t size, void *user_data);

#endif
