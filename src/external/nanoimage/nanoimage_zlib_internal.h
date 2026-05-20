#ifndef NANOIMAGE_ZLIB_INTERNAL_H_
#define NANOIMAGE_ZLIB_INTERNAL_H_

#include <stddef.h>
#include <stdint.h>

typedef struct {
  const uint8_t *data;
  size_t size;
} ni_zlib_span;

int ni_zlib_inflate_stored_spans(const ni_zlib_span *spans, size_t span_count,
                                 uint8_t *output, size_t output_len,
                                 size_t *out_written, char *err,
                                 size_t err_capacity);
int ni_zlib_inflate_raw_spans(const ni_zlib_span *spans, size_t span_count,
                              uint8_t *output, size_t output_len,
                              size_t *out_written, char *err,
                              size_t err_capacity);

#endif
