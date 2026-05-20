#ifndef NANOIMAGE_JPEG_H_
#define NANOIMAGE_JPEG_H_

#include <stddef.h>
#include <stdint.h>

#include "nanoimage.h"

typedef struct {
  int quality;
} ni_jpeg_write_options;

int ni_load_jpeg_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                             char *err, size_t err_capacity);
int ni_write_jpeg(const ni_image *image, const ni_jpeg_write_options *options,
                  ni_write_callback write_fn, void *user_data, char *err,
                  size_t err_capacity);
int ni_write_jpeg_to_memory(const ni_image *image,
                            const ni_jpeg_write_options *options,
                            ni_buffer *out, char *err, size_t err_capacity);

#endif
