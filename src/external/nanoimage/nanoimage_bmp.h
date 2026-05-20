#ifndef NANOIMAGE_BMP_H_
#define NANOIMAGE_BMP_H_

#include <stddef.h>
#include <stdint.h>

#include "nanoimage.h"

int ni_load_bmp_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity);
int ni_write_bmp(const ni_image *image, ni_write_callback write_fn,
                 void *user_data, char *err, size_t err_capacity);
int ni_write_bmp_to_memory(const ni_image *image, ni_buffer *out, char *err,
                           size_t err_capacity);

#endif
