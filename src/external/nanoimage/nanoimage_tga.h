#ifndef NANOIMAGE_TGA_H_
#define NANOIMAGE_TGA_H_

#include <stddef.h>
#include <stdint.h>

#include "nanoimage.h"

int ni_load_tga_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity);
int ni_write_tga(const ni_image *image, ni_write_callback write_fn,
                 void *user_data, char *err, size_t err_capacity);
int ni_write_tga_to_memory(const ni_image *image, ni_buffer *out, char *err,
                           size_t err_capacity);

#endif
