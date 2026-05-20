#ifndef NANOIMAGE_ZLIB_H_
#define NANOIMAGE_ZLIB_H_

#include <stddef.h>
#include <stdint.h>

int ni_zlib_inflate_stored(const uint8_t *input, size_t input_len,
                           uint8_t *output, size_t output_len,
                           size_t *out_written, char *err,
                           size_t err_capacity);
int ni_zlib_inflate_raw(const uint8_t *input, size_t input_len,
                        uint8_t *output, size_t output_len,
                        size_t *out_written, char *err,
                        size_t err_capacity);

#endif
