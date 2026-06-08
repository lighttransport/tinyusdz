/*
 * TinyEXR - a small, fast, portable OpenEXR image reader/writer.
 *
 * This is the pure-C11 public API. It has no C++ in the core and links with a
 * C compiler, but the header is safe to include from C++ as well.
 *
 * Two layers are provided:
 *   - High level:  exr_load_from_file/memory(), exr_save_to_file/memory().
 *   - Mid level:   exr_reader_* / exr_writer_* for partial reads, multipart,
 *                  deep images, and optional streaming (suspend/resume) I/O.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TINYEXR_EXR_H_
#define TINYEXR_EXR_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Version
 * ========================================================================== */

#define EXR_VERSION_MAJOR 3
#define EXR_VERSION_MINOR 0
#define EXR_VERSION_PATCH 0

/* ============================================================================
 * Result codes
 * ========================================================================== */

typedef enum exr_result {
    EXR_SUCCESS = 0,
    EXR_WOULD_BLOCK = 1, /* streaming source: fetch bytes then resume */

    EXR_ERROR_INVALID_ARGUMENT = -1,
    EXR_ERROR_INVALID_FILE = -2,    /* bad magic / version / structure */
    EXR_ERROR_UNSUPPORTED = -3,     /* feature/compression not implemented */
    EXR_ERROR_OUT_OF_MEMORY = -4,
    EXR_ERROR_IO = -5,              /* file open/read/write failure */
    EXR_ERROR_CORRUPT = -6,         /* bounds/overflow/decode failure */
} exr_result;

#define EXR_OK(r) ((int)(r) >= 0)

/* Human-readable description of a result code (static string). */
const char *exr_result_string(exr_result r);

/* ============================================================================
 * Allocator hook (optional; pass NULL to use malloc/free)
 * ========================================================================== */

typedef struct exr_allocator {
    void *user;
    void *(*alloc)(void *user, size_t size);
    void (*free)(void *user, void *ptr);
} exr_allocator;

/* ============================================================================
 * Enumerations mirroring the OpenEXR format
 * ========================================================================== */

typedef enum exr_pixel_type {
    EXR_PIXEL_UINT = 0,
    EXR_PIXEL_HALF = 1,
    EXR_PIXEL_FLOAT = 2
} exr_pixel_type;

typedef enum exr_compression {
    EXR_COMPRESSION_NONE = 0,
    EXR_COMPRESSION_RLE = 1,
    EXR_COMPRESSION_ZIPS = 2,  /* zlib, 1 scanline/block */
    EXR_COMPRESSION_ZIP = 3,   /* zlib, 16 scanlines/block */
    EXR_COMPRESSION_PIZ = 4,   /* wavelet + Huffman, 32 scanlines/block */
    EXR_COMPRESSION_PXR24 = 5, /* lossy 24-bit float, 16 scanlines/block */
    EXR_COMPRESSION_B44 = 6,   /* lossy 4x4, 32 scanlines/block */
    EXR_COMPRESSION_B44A = 7,  /* B44 with flat-block packing, 32/block */
    EXR_COMPRESSION_DWAA = 8,  /* not yet supported */
    EXR_COMPRESSION_DWAB = 9,  /* not yet supported */
    EXR_COMPRESSION_HTJ2K256 = 10, /* HTJ2K/JPH, 256 scanlines/block */
    EXR_COMPRESSION_HTJ2K32 = 11,  /* HTJ2K/JPH, 32 scanlines/block */
    EXR_COMPRESSION_ZSTD = 12  /* zstd, 32 scanlines/block */
} exr_compression;

typedef enum exr_line_order {
    EXR_LINEORDER_INCREASING_Y = 0,
    EXR_LINEORDER_DECREASING_Y = 1,
    EXR_LINEORDER_RANDOM_Y = 2
} exr_line_order;

typedef enum exr_part_type {
    EXR_PART_SCANLINE = 0,
    EXR_PART_TILED = 1,
    EXR_PART_DEEP_SCANLINE = 2,
    EXR_PART_DEEP_TILED = 3
} exr_part_type;

typedef enum exr_tile_level_mode {
    EXR_TILE_ONE_LEVEL = 0,
    EXR_TILE_MIPMAP_LEVELS = 1,
    EXR_TILE_RIPMAP_LEVELS = 2
} exr_tile_level_mode;

typedef enum exr_tile_rounding_mode {
    EXR_TILE_ROUND_DOWN = 0,
    EXR_TILE_ROUND_UP = 1
} exr_tile_rounding_mode;

/* ============================================================================
 * Basic geometry
 * ========================================================================== */

typedef struct exr_box2i {
    int32_t min_x, min_y, max_x, max_y;
} exr_box2i;

/* ============================================================================
 * Channel / header / part / image (plain data; caller may read fields)
 * ========================================================================== */

#define EXR_MAX_NAME 256

typedef struct exr_channel {
    char name[EXR_MAX_NAME];
    exr_pixel_type pixel_type;
    int32_t x_sampling;
    int32_t y_sampling;
    uint8_t p_linear; /* perceptually linear hint */
} exr_channel;

/* Opaque, parsed attribute list (custom/standard attributes, for round-trip). */
typedef struct exr_attr_list exr_attr_list;

typedef struct exr_header {
    exr_part_type part_type;
    exr_compression compression;
    exr_line_order line_order;

    exr_box2i data_window;
    exr_box2i display_window;
    float pixel_aspect_ratio;
    float screen_window_center_x;
    float screen_window_center_y;
    float screen_window_width;

    int32_t num_channels;
    exr_channel *channels; /* sorted by name; owned by the image/reader */

    /* Tiled parts only (part_type == EXR_PART_TILED / DEEP_TILED). */
    uint8_t tiled;
    uint32_t tile_x_size;
    uint32_t tile_y_size;
    exr_tile_level_mode level_mode;
    exr_tile_rounding_mode rounding_mode;

    char name[EXR_MAX_NAME]; /* multipart part name ("" if single part) */

    exr_attr_list *attrs; /* all parsed attributes (opaque); may be NULL */
} exr_header;

typedef struct exr_part {
    exr_header header;

    int32_t width;  /* data_window width  (max_x - min_x + 1) */
    int32_t height; /* data_window height (max_y - min_y + 1) */

    /*
     * Flat (non-deep) pixel storage, planar per channel. images[c] points to
     * width*height elements of header.channels[c].pixel_type, row-major within
     * the data window. NULL for deep parts.
     */
    void **images;

    /*
     * Deep storage (is_deep != 0). deep_sample_counts[y*width+x] holds the
     * sample count per pixel. deep_images[c] is a contiguous array of all
     * samples for channel c, in pixel (row-major) order, native pixel type;
     * the samples for pixel p start at offset sum(deep_sample_counts[0..p-1]).
     */
    uint8_t is_deep;
    int32_t *deep_sample_counts; /* width*height entries, or NULL */
    void **deep_images;          /* [channel] -> contiguous samples, or NULL */
    uint64_t deep_total_samples; /* sum of deep_sample_counts */
} exr_part;

typedef struct exr_image {
    int32_t num_parts;
    exr_part *parts; /* owned */
    exr_allocator alloc;
} exr_image;

/* Release everything an exr_image owns (channels, pixels, deep arrays, parts).
 * Safe to call on a zero-initialized image. */
void exr_image_free(exr_image *img);

/* Release a single part filled by the mid-level reader (exr_reader_read_part /
 * _read_scanlines / _read_tile). Pass the allocator the reader was opened with
 * (NULL for the default). Safe on a zero-initialized part. */
void exr_part_free(const exr_allocator *a, exr_part *part);

/* ============================================================================
 * High-level load / save
 * ========================================================================== */

/* Load an entire EXR (all parts, all channels, native pixel types). On success
 * *out is filled and must be released with exr_image_free(). */
exr_result exr_load_from_file(const char *path, const exr_allocator *alloc,
                              exr_image *out);
exr_result exr_load_from_memory(const void *data, size_t size,
                                const exr_allocator *alloc, exr_image *out);

/* Save an image. The image's parts/channels/pixels must be populated by the
 * caller (typically built by hand or returned from a loader). */
exr_result exr_save_to_file(const char *path, const exr_image *img,
                            exr_compression compression);
exr_result exr_save_to_memory(void **out_data, size_t *out_size,
                              const exr_allocator *alloc, const exr_image *img,
                              exr_compression compression);

/* ============================================================================
 * Mid-level reader
 * ========================================================================== */

typedef struct exr_reader exr_reader;

/*
 * Streaming data source. Only used by exr_reader_open_source(). The callback
 * must copy [off, off+len) of the file into dst and return EXR_SUCCESS, or
 * return EXR_WOULD_BLOCK if those bytes are not yet available (the host then
 * fetches them, calls exr_reader_supply(), and re-invokes the reader call).
 */
typedef struct exr_data_source {
    void *user;
    exr_result (*read)(void *user, uint64_t off, uint64_t len, void *dst);
    uint64_t total_size; /* 0 if unknown */
} exr_data_source;

/* The memory path is zero-copy and never returns EXR_WOULD_BLOCK. The data
 * buffer must stay valid for the lifetime of the reader. */
exr_result exr_reader_open_memory(const void *data, size_t size,
                                  const exr_allocator *alloc, exr_reader **out);
exr_result exr_reader_open_source(const exr_data_source *src,
                                  const exr_allocator *alloc, exr_reader **out);
/* Convenience: open a reader on a file path (stdio). Defined in the optional
 * exr_stdio.c module; unavailable in freestanding builds that omit it. */
exr_result exr_reader_open_file(const char *path, const exr_allocator *alloc,
                                exr_reader **out);
void exr_reader_close(exr_reader *r);

/* Parse the file/version + all part headers + offset tables (no pixel I/O).
 * May return EXR_WOULD_BLOCK in streaming mode. */
exr_result exr_reader_parse_header(exr_reader *r);

int32_t exr_reader_num_parts(const exr_reader *r);
const exr_header *exr_reader_part_header(const exr_reader *r, int32_t part);

/*
 * Read pixels. Each call appends ownership into *out (a caller-provided,
 * zero-initialized exr_part for the single-part calls). May return
 * EXR_WOULD_BLOCK in streaming mode.
 */
exr_result exr_reader_read_part(exr_reader *r, int32_t part, exr_part *out);
exr_result exr_reader_read_scanlines(exr_reader *r, int32_t part,
                                     int32_t y_start, int32_t y_count,
                                     exr_part *out);
exr_result exr_reader_read_tile(exr_reader *r, int32_t part, int32_t tile_x,
                                int32_t tile_y, int32_t level_x, int32_t level_y,
                                exr_part *out);

/* ---- Streaming suspend / resume (only meaningful with a data source) ---- */
typedef struct exr_pending_read {
    uint64_t offset;
    uint64_t size;
} exr_pending_read;

/* When a reader call returns EXR_WOULD_BLOCK, query the bytes it needs. */
exr_result exr_reader_pending(const exr_reader *r, exr_pending_read *out);
/* Hand the fetched bytes back, then re-call the same reader function. */
exr_result exr_reader_supply(exr_reader *r, const void *data, size_t size);

/* ============================================================================
 * Mid-level writer
 * ========================================================================== */

typedef struct exr_writer exr_writer;

exr_result exr_writer_create(const exr_allocator *alloc, exr_writer **out);
void exr_writer_destroy(exr_writer *w);

/* Add a part described by hdr (its channel list + windows + compression).
 * Returns the new part index in *out_part (may be NULL). */
exr_result exr_writer_add_part(exr_writer *w, const exr_header *hdr,
                               int32_t *out_part);

/* Provide planar pixel data for one channel of a part (width*height elements
 * of the channel's pixel type). Pointer must remain valid until finalize. */
exr_result exr_writer_set_channel(exr_writer *w, int32_t part, const char *name,
                                  const void *pixels);

exr_result exr_writer_finalize_to_memory(exr_writer *w, void **out_data,
                                         size_t *out_size);
exr_result exr_writer_finalize_to_file(exr_writer *w, const char *path);

/* ============================================================================
 * Streaming block I/O (bounded working memory)
 *
 * The calls above materialize a whole part at once. The block API lets a caller
 * process exactly one scanline-block or one tile at a time, so peak memory is a
 * single block rather than the entire image. A "block" is one offset-table
 * chunk: one scanline block for scanline parts, one tile (at one level) for
 * tiled parts; mipmap/ripmap levels and deep parts are covered too.
 * ========================================================================== */

/* Geometry of one block (chunk). Filled by exr_reader_block_info(). */
typedef struct exr_block_info {
    int32_t part;
    uint8_t is_tiled;          /* tiled or deep-tiled part */
    uint8_t is_deep;
    int32_t y0;                /* block first scanline (data-window y) */
    int32_t tile_x, tile_y;    /* tiled: tile indices within the level */
    int32_t level_x, level_y;  /* tiled: level (0,0 for ONE_LEVEL/scanline) */
    int32_t x0;                /* block origin x (absolute data-window coords) */
    int32_t width, height;     /* block pixel extent (height == #scanlines) */
    size_t  uncompressed_size; /* flat parts: bytes for the canonical buffer */
} exr_block_info;

/* ---- streaming decode ---- */

/* Number of chunks (offset-table entries) in a part, across all levels. */
exr_result exr_reader_num_blocks(exr_reader *r, int32_t part, uint32_t *out);

/* Geometry + buffer size for block `idx`. Derived from the offset-table / level
 * math; performs no pixel I/O and never returns EXR_WOULD_BLOCK. */
exr_result exr_reader_block_info(exr_reader *r, int32_t part, uint32_t idx,
                                 exr_block_info *out);

/* Flat parts: decode block `idx` into `dst` (>= info.uncompressed_size) in the
 * canonical layout (per scanline, then per channel in name-sorted order, sample
 * data only). May return EXR_WOULD_BLOCK in streaming mode (re-call after
 * supply). Use exr_block_extract_channel() to unpack a single channel. */
exr_result exr_reader_decode_block(exr_reader *r, int32_t part, uint32_t idx,
                                   void *dst, size_t dst_size);

/* Copy one channel out of a decoded canonical flat block into a tight planar
 * buffer: exr_num_samples(x0,x0+w-1,xs) * exr_num_samples(y0,y0+h-1,ys) elements
 * of that channel's pixel type, row-major. `channel` indexes the name-sorted
 * channel order (== header->channels order). */
exr_result exr_block_extract_channel(const exr_header *h,
                                     const exr_block_info *info,
                                     const void *block, size_t block_size,
                                     int32_t channel, void *dst);

/* Deep parts: counts must be known before sample buffers can be sized, so decode
 * is two-step. `counts` holds info.width*info.height per-pixel counts (block
 * row-major). After summing them, size chan_dst[c] to sum(counts) elements of
 * channel c's pixel type and call _decode_deep_samples. Both may WOULD_BLOCK. */
exr_result exr_reader_decode_deep_counts(exr_reader *r, int32_t part,
                                         uint32_t idx, int32_t *counts);
exr_result exr_reader_decode_deep_samples(exr_reader *r, int32_t part,
                                          uint32_t idx, void *const *chan_dst);

/* ---- streaming encode ---- */

/* Seekable output sink. write() appends `len` bytes; seek() repositions for the
 * offset-table backpatch at end_stream. close() is optional (may be NULL) and is
 * invoked exactly once when the stream finishes or the writer is destroyed — it
 * lets a sink release its resource (e.g. fclose) without the core touching libc.
 * write/seek/close return EXR_SUCCESS or an error. */
typedef struct exr_data_sink {
    void *user;
    exr_result (*write)(void *user, const void *data, size_t len);
    exr_result (*seek)(void *user, uint64_t off);
    exr_result (*close)(void *user); /* optional; may be NULL */
} exr_data_sink;

/* Begin a streaming encode. Parts must already be described via
 * exr_writer_add_part (channels, windows, tiling); do NOT call
 * exr_writer_set_channel. Writes magic+version+headers+zeroed offset tables to
 * the sink immediately, then expects one write call per block. `comp` overrides
 * every part's compression (use the part header's value by passing it). After
 * begin_stream the writer is in streaming mode until end_stream. */
exr_result exr_writer_begin_stream(exr_writer *w, const exr_data_sink *sink,
                                   exr_compression comp);
/* Convenience: stream straight to a file (fwrite + fseek backpatch). */
exr_result exr_writer_begin_stream_file(exr_writer *w, const char *path,
                                        exr_compression comp);

/* Feed one flat scanline block. y0 must be a block boundary. channel_rows[c]
 * points to this block's planar samples for channel c (header->channels order):
 * exr_num_samples(xmin,xmax,xs) * exr_num_samples(y0,y0+nlines-1,ys) elements. */
exr_result exr_writer_write_scanline_block(exr_writer *w, int32_t part,
                                           int32_t y0,
                                           const void *const *channel_rows);

/* Feed one flat tile at level (level_x,level_y) (0,0 for ONE_LEVEL). For
 * mipmap/ripmap the caller supplies every (tile,level) itself (no pyramid
 * auto-generation). channel_data[c] is the tile's planar samples for channel c. */
exr_result exr_writer_write_tile(exr_writer *w, int32_t part, int32_t tile_x,
                                 int32_t tile_y, int32_t level_x, int32_t level_y,
                                 const void *const *channel_data);

/* Deep variants. counts[] holds width*height per-pixel sample counts (block
 * row-major); chan_samp[c] holds the block's contiguous samples for channel c
 * in pixel row-major order. */
exr_result exr_writer_write_deep_scanline_block(exr_writer *w, int32_t part,
                                                int32_t y0, const int32_t *counts,
                                                const void *const *chan_samp);
exr_result exr_writer_write_deep_tile(exr_writer *w, int32_t part,
                                      int32_t tile_x, int32_t tile_y,
                                      int32_t level_x, int32_t level_y,
                                      const int32_t *counts,
                                      const void *const *chan_samp);

/* Backpatch every offset table (seek + write) and flush. For the file-backed
 * sink this also closes the file. */
exr_result exr_writer_end_stream(exr_writer *w);

/* ============================================================================
 * Utilities
 * ========================================================================== */

/* Identify an EXR by magic number. */
int exr_is_exr_memory(const void *data, size_t size);

/* Runtime SIMD capability bits (for diagnostics). */
typedef enum exr_simd_caps {
    EXR_SIMD_NONE = 0,
    EXR_SIMD_SSE2 = 1u << 0,
    EXR_SIMD_SSE41 = 1u << 1,
    EXR_SIMD_AVX2 = 1u << 2,
    EXR_SIMD_NEON = 1u << 3
} exr_simd_caps;

uint32_t exr_simd_capabilities(void);
const char *exr_simd_info(void);

/* Pixel-format conversion (runtime SIMD-dispatched). */
void exr_half_to_float(const uint16_t *src, float *dst, size_t count);
void exr_float_to_half(const float *src, uint16_t *dst, size_t count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TINYEXR_EXR_H_ */
