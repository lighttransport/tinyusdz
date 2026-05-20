#ifndef NANOIMAGE_GIF_H_
#define NANOIMAGE_GIF_H_

#include <stddef.h>
#include <stdint.h>

#include "nanoimage.h"

int ni_load_gif_from_memory(const uint8_t *bytes, size_t size, ni_image *out,
                            char *err, size_t err_capacity);
int ni_write_gif(const ni_image *image, ni_write_callback write_fn,
                 void *user_data, char *err, size_t err_capacity);
int ni_write_gif_to_memory(const ni_image *image, ni_buffer *out, char *err,
                           size_t err_capacity);

#endif
