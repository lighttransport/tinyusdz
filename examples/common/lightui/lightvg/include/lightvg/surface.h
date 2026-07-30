/*
 * lightvg/surface.h — Pixel-buffer surface
 *
 * A surface is a plain 32-bit ARGB pixel buffer (0xAARRGGBB per pixel).
 * On HiDPI displays the physical buffer is larger than the logical size;
 * dpi_scale reflects the ratio (e.g. 2.0 for Apple Retina).
 *
 * Portability notes
 * -----------------
 * The core rasterizer never allocates.  Only the convenience constructors
 * `lvg_surface_create` / `lvg_surface_resize` call a malloc-style allocator.
 * To run in environments without libc heap, either:
 *   (a) use `lvg_surface_wrap()` on caller-owned memory and skip create/destroy;
 *   (b) pass allocator callbacks to `lvg_surface_create_ex()`;
 *   (c) compile the module with `-DLVG_MALLOC=... -DLVG_FREE=...` macros
 *       to override the default malloc/free globally.
 *
 * Similarly, PNG/PPM writers are exposed in both "write-callback" and "save-
 * to-path" flavors.  The callback flavor has no stdio dependency.  The path
 * flavor is compiled only when `LVG_NO_STDIO` is not defined (default on).
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTVG_SURFACE_H
#define LIGHTVG_SURFACE_H

#include <stddef.h>
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * lvg_surface_t — an owned or borrowed pixel buffer.
 *
 * Layout: row-major, top-to-bottom.
 * pixels[y * stride + x] is the pixel at logical coordinate (x, y).
 * stride >= width (in uint32_t units, not bytes).
 */
typedef struct {
    uint32_t  *pixels;      /* pixel data: ARGB packed as 0xAARRGGBB  */
    int        width;       /* logical width  (columns)                */
    int        height;      /* logical height (rows)                   */
    int        stride;      /* row stride in uint32_t units            */
    float      dpi_scale;   /* HiDPI backing-store scale (1.0 = 1x)   */
} lvg_surface_t;

/**
 * lvg_surface_view_t — frozen POD snapshot of a surface's pixel storage.
 *
 * The long-term plan is to make `lvg_surface_t` opaque; tight hot loops
 * that need raw access to `pixels` / `stride` / dimensions should read
 * them through this view so they keep compiling when that flip happens.
 *
 * Layout of this struct is guaranteed stable across minor versions.
 * Obtain via `lvg_surface_as_view()`.
 */
typedef struct {
    uint32_t  *pixels;
    int        width;
    int        height;
    int        stride;
    float      dpi_scale;
} lvg_surface_view_t;

/* -------------------------------------------------------------------------
 * Allocator and writer callbacks
 * ------------------------------------------------------------------------- */

/** Allocator: returns `size` bytes (zero-initialised semantics NOT required).
 *  Must return NULL on failure. `userdata` is passed through unchanged. */
typedef void *(*lvg_alloc_fn)(void *userdata, size_t size);

/** Deallocator paired with lvg_alloc_fn. Passing NULL `ptr` is a no-op. */
typedef void  (*lvg_free_fn)(void *userdata, void *ptr);

/** Byte-stream writer: called with each chunk emitted by PPM/PNG encoders.
 *  Return value: number of bytes actually consumed (usually `len`); any
 *  short-write is treated as an error and aborts encoding. */
typedef size_t (*lvg_write_fn)(void *userdata, const void *data, size_t len);

/* -------------------------------------------------------------------------
 * Surface lifecycle
 * ------------------------------------------------------------------------- */

/**
 * Allocate a new surface of the given dimensions using the default allocator
 * (malloc/free, overridable at compile time via LVG_MALLOC / LVG_FREE).
 * Pixels are zero-initialised (fully transparent black).
 * Returns NULL on allocation failure or invalid dimensions.
 */
lvg_surface_t *lvg_surface_create(int width, int height);

/**
 * Same as lvg_surface_create() but uses caller-supplied allocator callbacks.
 * The same free_cb/userdata must later be passed to lvg_surface_destroy_ex().
 * `alloc_cb` / `free_cb` may be NULL to use the compile-time defaults.
 */
lvg_surface_t *lvg_surface_create_ex(int width, int height,
                                     lvg_alloc_fn alloc_cb,
                                     lvg_free_fn  free_cb,
                                     void *userdata);

/**
 * Destroy a surface allocated with lvg_surface_create().
 * Passing NULL is a no-op.
 */
void lvg_surface_destroy(lvg_surface_t *surface);

/**
 * Destroy a surface allocated with lvg_surface_create_ex(), using the
 * matching free callback.
 */
void lvg_surface_destroy_ex(lvg_surface_t *surface,
                            lvg_free_fn free_cb, void *userdata);

/**
 * Wrap an existing pixel buffer — no ownership is taken.
 * The caller is responsible for keeping the buffer alive as long as the
 * returned surface value is used.
 */
lvg_surface_t lvg_surface_wrap(uint32_t *pixels,
                                int width, int height, int stride);

/**
 * Take a POD snapshot of the surface's pixel storage. The returned view
 * is valid for as long as the surface's backing buffer is alive.
 * Returns a zero-initialised view when `surface` is NULL.
 */
static inline lvg_surface_view_t lvg_surface_as_view(const lvg_surface_t *surface)
{
    lvg_surface_view_t v;
    if (!surface) {
        v.pixels = 0; v.width = 0; v.height = 0; v.stride = 0; v.dpi_scale = 0.0f;
        return v;
    }
    v.pixels    = surface->pixels;
    v.width     = surface->width;
    v.height    = surface->height;
    v.stride    = surface->stride;
    v.dpi_scale = surface->dpi_scale;
    return v;
}

/**
 * Resize an owned surface (re-allocates the pixel buffer via the default
 * allocator). Pixel contents after resize are undefined.
 * Returns false on allocation failure; the surface is not modified.
 */
bool lvg_surface_resize(lvg_surface_t *surface, int new_width, int new_height);

/* -------------------------------------------------------------------------
 * Writers — callback flavor (no stdio dependency)
 * ------------------------------------------------------------------------- */

/**
 * Encode the surface as binary PPM (P6, RGB, no alpha) and stream it through
 * `write_cb`. Returns 0 on success, -1 on I/O (short-write) or argument error.
 */
int lvg_surface_write_ppm(const lvg_surface_t *surface,
                          lvg_write_fn write_cb, void *userdata);

/**
 * Encode the surface as PNG (RGBA) and stream it through `write_cb`.
 * Requires the optional PNG backend (compile with LVG_ENABLE_PNG).
 * Returns 0 on success, -1 on error. When PNG support is disabled at
 * compile time this always returns -1.
 */
int lvg_surface_write_png(const lvg_surface_t *surface,
                          lvg_write_fn write_cb, void *userdata);

/**
 * Encode the surface as JPEG and stream it through `write_cb`.
 * Alpha is discarded (JPEG does not support an alpha channel).
 * `quality` is clamped to [1, 100]; higher = better quality / larger file.
 * Typical values: 75 (web default), 90 (visually lossless).
 * Requires the optional JPEG backend (compile with LVG_ENABLE_JPEG).
 * Returns 0 on success, -1 on error. When JPEG support is disabled at
 * compile time this always returns -1.
 */
int lvg_surface_write_jpeg(const lvg_surface_t *surface,
                           lvg_write_fn write_cb, void *userdata,
                           int quality);

/* -------------------------------------------------------------------------
 * Writers — file path flavor (uses stdio; compiled when LVG_NO_STDIO
 * is NOT defined)
 * ------------------------------------------------------------------------- */

#ifndef LVG_NO_STDIO
/**
 * Save surface to a PPM file (binary P6 format, no alpha).
 * Returns 0 on success, -1 on error.
 */
int lvg_surface_save_ppm(const lvg_surface_t *surface, const char *path);

/**
 * Save surface to a PNG file (RGBA, lossless).
 * Requires LVG_ENABLE_PNG at compile time.
 * Returns 0 on success, -1 on error.
 */
int lvg_surface_save_png(const lvg_surface_t *surface, const char *path);

/**
 * Save surface to a JPEG file (RGB, lossy). `quality` is clamped to [1, 100].
 * Requires LVG_ENABLE_JPEG at compile time.
 * Returns 0 on success, -1 on error.
 */
int lvg_surface_save_jpeg(const lvg_surface_t *surface, const char *path,
                          int quality);

/**
 * Save surface to a file, choosing format by extension
 * (.png, .ppm, .jpg, .jpeg). JPEG uses a default quality of 90; use
 * lvg_surface_save_jpeg() directly to control it. Unknown extensions fall
 * back to PPM.
 * Returns 0 on success, -1 on error.
 */
int lvg_surface_save(const lvg_surface_t *surface, const char *path);
#endif /* LVG_NO_STDIO */

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* LIGHTVG_SURFACE_H */
