#include "nanoimage_zlib.h"
#include "nanoimage_zlib_internal.h"

#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef NI_USE_MINIZ
#include "miniz.h"
#else
#include <zlib.h>
#endif

static void ni_set_error(char *err, size_t cap, const char *fmt, ...) {
  va_list args;

  if ((err == NULL) || (cap == 0)) {
    return;
  }

  va_start(args, fmt);
  (void)vsnprintf(err, cap, fmt, args);
  va_end(args);
}

static int ni_size_add(size_t a, size_t b, size_t *out) {
  if (a > (SIZE_MAX - b)) {
    return 0;
  }
  *out = a + b;
  return 1;
}

static int ni_zlib_inflate_impl(const uint8_t *input, size_t input_len,
                                uint8_t *output, size_t output_len,
                                size_t *out_written, char *err,
                                size_t err_capacity, int raw_stream) {
  z_stream zs;
  int zret;

  if ((input == NULL) || (output == NULL) || (out_written == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }

  *out_written = 0;

  if (input_len == 0u) {
    ni_set_error(err, err_capacity, "empty deflate input");
    return 0;
  }
  if ((input_len > (size_t)UINT_MAX) || (output_len > (size_t)UINT_MAX)) {
    ni_set_error(err, err_capacity, "zlib buffer size exceeds 32-bit limit");
    return 0;
  }

  memset(&zs, 0, sizeof(zs));
  zs.next_in = (Bytef *)input;
  zs.avail_in = (uInt)input_len;
  zs.next_out = output;
  zs.avail_out = (uInt)output_len;

  zret = inflateInit2(&zs, raw_stream ? -MAX_WBITS : MAX_WBITS);
  if (zret != Z_OK) {
    ni_set_error(err, err_capacity, "zlib init failed (%d)", zret);
    return 0;
  }

  zret = inflate(&zs, Z_FINISH);
  if ((zret == Z_STREAM_END) && (zs.total_out <= output_len)) {
    *out_written = (size_t)zs.total_out;
    (void)inflateEnd(&zs);
    return 1;
  }

  (void)inflateEnd(&zs);
  if (zret == Z_BUF_ERROR) {
    ni_set_error(err, err_capacity, "inflate output buffer too small");
  } else if (zret == Z_MEM_ERROR) {
    ni_set_error(err, err_capacity, "out of memory while inflating zlib");
  } else if (zret == Z_DATA_ERROR) {
    ni_set_error(err, err_capacity, "invalid zlib stream");
  } else {
    ni_set_error(err, err_capacity, "zlib inflate failed (%d)", zret);
  }
  return 0;
}

static int ni_zlib_handle_error(int zret, char *err, size_t err_capacity) {
  if (zret == Z_BUF_ERROR) {
    ni_set_error(err, err_capacity, "inflate output buffer too small");
  } else if (zret == Z_MEM_ERROR) {
    ni_set_error(err, err_capacity, "out of memory while inflating zlib");
  } else if (zret == Z_DATA_ERROR) {
    ni_set_error(err, err_capacity, "invalid zlib stream");
  } else {
    ni_set_error(err, err_capacity, "zlib inflate failed (%d)", zret);
  }
  return 0;
}

static int ni_zlib_inflate_spans_impl(const ni_zlib_span *spans, size_t span_count,
                                      uint8_t *output, size_t output_len,
                                      size_t *out_written, char *err,
                                      size_t err_capacity, int raw_stream) {
  z_stream zs;
  size_t i;
  size_t total_in = 0u;
  int zret;

  if ((spans == NULL) || (output == NULL) || (out_written == NULL)) {
    ni_set_error(err, err_capacity, "invalid argument");
    return 0;
  }

  *out_written = 0u;
  if (output_len > (size_t)UINT_MAX) {
    ni_set_error(err, err_capacity, "zlib buffer size exceeds 32-bit limit");
    return 0;
  }

  for (i = 0u; i < span_count; i++) {
    if (spans[i].size > (size_t)UINT_MAX) {
      ni_set_error(err, err_capacity, "zlib buffer size exceeds 32-bit limit");
      return 0;
    }
    if (!ni_size_add(total_in, spans[i].size, &total_in)) {
      ni_set_error(err, err_capacity, "zlib input size overflow");
      return 0;
    }
  }

  if (total_in == 0u) {
    ni_set_error(err, err_capacity, "empty deflate input");
    return 0;
  }

  memset(&zs, 0, sizeof(zs));
  zs.next_out = output;
  zs.avail_out = (uInt)output_len;

  zret = inflateInit2(&zs, raw_stream ? -MAX_WBITS : MAX_WBITS);
  if (zret != Z_OK) {
    ni_set_error(err, err_capacity, "zlib init failed (%d)", zret);
    return 0;
  }

  for (i = 0u; i < span_count; i++) {
    zs.next_in = (Bytef *)spans[i].data;
    zs.avail_in = (uInt)spans[i].size;

    while (zs.avail_in > 0u) {
      zret = inflate(&zs, Z_NO_FLUSH);
      if (zret == Z_STREAM_END) {
        *out_written = (size_t)zs.total_out;
        (void)inflateEnd(&zs);
        return 1;
      }
      if (zret != Z_OK) {
        (void)inflateEnd(&zs);
        return ni_zlib_handle_error(zret, err, err_capacity);
      }
    }
  }

  do {
    zret = inflate(&zs, Z_FINISH);
    if (zret == Z_STREAM_END) {
      *out_written = (size_t)zs.total_out;
      (void)inflateEnd(&zs);
      return 1;
    }
  } while (zret == Z_OK);

  (void)inflateEnd(&zs);
  return ni_zlib_handle_error(zret, err, err_capacity);
}

int ni_zlib_inflate_stored(const uint8_t *input, size_t input_len,
                           uint8_t *output, size_t output_len,
                           size_t *out_written, char *err,
                           size_t err_capacity) {
  return ni_zlib_inflate_impl(input, input_len, output, output_len, out_written,
                              err, err_capacity, 0);
}

int ni_zlib_inflate_raw(const uint8_t *input, size_t input_len, uint8_t *output,
                        size_t output_len, size_t *out_written, char *err,
                        size_t err_capacity) {
  return ni_zlib_inflate_impl(input, input_len, output, output_len, out_written,
                              err, err_capacity, 1);
}

int ni_zlib_inflate_stored_spans(const ni_zlib_span *spans, size_t span_count,
                                 uint8_t *output, size_t output_len,
                                 size_t *out_written, char *err,
                                 size_t err_capacity) {
  return ni_zlib_inflate_spans_impl(spans, span_count, output, output_len,
                                    out_written, err, err_capacity, 0);
}

int ni_zlib_inflate_raw_spans(const ni_zlib_span *spans, size_t span_count,
                              uint8_t *output, size_t output_len,
                              size_t *out_written, char *err,
                              size_t err_capacity) {
  return ni_zlib_inflate_spans_impl(spans, span_count, output, output_len,
                                    out_written, err, err_capacity, 1);
}
