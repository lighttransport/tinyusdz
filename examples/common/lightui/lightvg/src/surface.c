/*
 * surface.c — Pixel-buffer surface allocation and writers
 *
 * Portability:
 *   - Core types + `lvg_surface_wrap` have zero dependencies.
 *   - `lvg_surface_create/destroy/resize` use the compile-time allocator
 *     macros below (default: malloc/free). Override with
 *         -DLVG_MALLOC=my_alloc -DLVG_FREE=my_free
 *     or via the `lvg_surface_create_ex()` callback form.
 *   - PPM encoding uses a user-supplied write callback; no stdio required.
 *   - PNG encoding is optional (compile with `LVG_ENABLE_PNG`), and
 *     pulls in the header-only `stb_image_write.h` plus its libm usage.
 *   - Path-based save helpers (`lvg_surface_save_*`) are compiled only when
 *     `LVG_NO_STDIO` is NOT defined.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <lightvg/surface.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#if !defined(LVG_MALLOC) || !defined(LVG_FREE)
#  include <stdlib.h>
#endif
#ifndef LVG_MALLOC
#  define LVG_MALLOC(sz) malloc(sz)
#endif
#ifndef LVG_FREE
#  define LVG_FREE(p)    free(p)
#endif

/* -------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------- */

static void *lvg__default_alloc(void *ud, size_t sz) {
    (void)ud; return LVG_MALLOC(sz);
}
static void lvg__default_free(void *ud, void *p) {
    (void)ud; LVG_FREE(p);
}

lvg_surface_t *lvg_surface_create_ex(int width, int height,
                                     lvg_alloc_fn alloc_cb,
                                     lvg_free_fn  free_cb,
                                     void *userdata)
{
    if (width <= 0 || height <= 0) return NULL;
    /* Reject sizes that would overflow size_t when multiplied by 4 bytes/pixel.
     * Matters on 32-bit hosts; on 64-bit the guard is effectively free. */
    if ((uint64_t)width * (uint64_t)height > (uint64_t)(SIZE_MAX / sizeof(uint32_t)))
        return NULL;
    if (!alloc_cb) alloc_cb = lvg__default_alloc;
    if (!free_cb)  free_cb  = lvg__default_free;

    lvg_surface_t *s = (lvg_surface_t *)alloc_cb(userdata, sizeof(lvg_surface_t));
    if (!s) return NULL;

    size_t bytes = (size_t)width * (size_t)height * sizeof(uint32_t);
    uint32_t *px = (uint32_t *)alloc_cb(userdata, bytes);
    if (!px) {
        free_cb(userdata, s);
        return NULL;
    }
    memset(px, 0, bytes);

    s->pixels    = px;
    s->width     = width;
    s->height    = height;
    s->stride    = width;
    s->dpi_scale = 1.0f;
    return s;
}

void lvg_surface_destroy_ex(lvg_surface_t *surface,
                            lvg_free_fn free_cb, void *userdata)
{
    if (!surface) return;
    if (!free_cb) free_cb = lvg__default_free;
    free_cb(userdata, surface->pixels);
    free_cb(userdata, surface);
}

lvg_surface_t *lvg_surface_create(int width, int height)
{
    return lvg_surface_create_ex(width, height, NULL, NULL, NULL);
}

void lvg_surface_destroy(lvg_surface_t *surface)
{
    lvg_surface_destroy_ex(surface, NULL, NULL);
}

lvg_surface_t lvg_surface_wrap(uint32_t *pixels,
                                int width, int height, int stride)
{
    lvg_surface_t s;
    s.pixels    = pixels;
    s.width     = width;
    s.height    = height;
    s.stride    = stride;
    s.dpi_scale = 1.0f;
    return s;
}

bool lvg_surface_resize(lvg_surface_t *surface, int new_width, int new_height)
{
    if (!surface || new_width <= 0 || new_height <= 0)
        return false;
    if ((uint64_t)new_width * (uint64_t)new_height > (uint64_t)(SIZE_MAX / sizeof(uint32_t)))
        return false;

    size_t bytes = (size_t)new_width * (size_t)new_height * sizeof(uint32_t);
    uint32_t *new_pixels = (uint32_t *)LVG_MALLOC(bytes);
    if (!new_pixels) return false;
    memset(new_pixels, 0, bytes);

    LVG_FREE(surface->pixels);
    surface->pixels = new_pixels;
    surface->width  = new_width;
    surface->height = new_height;
    surface->stride = new_width;
    return true;
}

/* -------------------------------------------------------------------------
 * PPM writer (callback-based, no stdio)
 * ------------------------------------------------------------------------- */

static int lvg__emit(lvg_write_fn cb, void *ud, const void *buf, size_t n) {
    return (cb(ud, buf, n) == n) ? 0 : -1;
}

/* Minimal unsigned-int → decimal ASCII. */
static int lvg__emit_uint(lvg_write_fn cb, void *ud, unsigned v) {
    char buf[16]; int n = 0;
    if (v == 0) { buf[n++] = '0'; }
    else { char tmp[16]; int t = 0;
           while (v) { tmp[t++] = (char)('0' + (v % 10)); v /= 10; }
           while (t) buf[n++] = tmp[--t]; }
    return lvg__emit(cb, ud, buf, (size_t)n);
}

int lvg_surface_write_ppm(const lvg_surface_t *surface,
                          lvg_write_fn write_cb, void *userdata)
{
    if (!surface || !write_cb) return -1;
    int w = surface->width, h = surface->height;

    /* Header: "P6\n<w> <h>\n255\n" */
    if (lvg__emit(write_cb, userdata, "P6\n", 3) != 0) return -1;
    if (lvg__emit_uint(write_cb, userdata, (unsigned)w) != 0) return -1;
    if (lvg__emit(write_cb, userdata, " ", 1) != 0) return -1;
    if (lvg__emit_uint(write_cb, userdata, (unsigned)h) != 0) return -1;
    if (lvg__emit(write_cb, userdata, "\n255\n", 5) != 0) return -1;

    /* Chunked conversion keeps the stack buffer small (~3 KB) so this works
     * on constrained targets regardless of image width. */
    enum { CHUNK = 1024 };
    unsigned char rowbuf[CHUNK * 3];
    for (int y = 0; y < h; y++) {
        const uint32_t *row = surface->pixels + (size_t)y * surface->stride;
        int x = 0;
        while (x < w) {
            int n = (w - x) < CHUNK ? (w - x) : CHUNK;
            for (int i = 0; i < n; i++) {
                uint32_t px = row[x + i];
                rowbuf[i * 3 + 0] = (unsigned char)((px >> 16) & 0xFF);
                rowbuf[i * 3 + 1] = (unsigned char)((px >>  8) & 0xFF);
                rowbuf[i * 3 + 2] = (unsigned char)( px        & 0xFF);
            }
            if (lvg__emit(write_cb, userdata, rowbuf, (size_t)n * 3) != 0)
                return -1;
            x += n;
        }
    }
    return 0;
}

/* -------------------------------------------------------------------------
 * PNG / JPEG writers (optional; pull in stb_image_write + its libm usage)
 * ------------------------------------------------------------------------- */

#if defined(LVG_ENABLE_PNG) || defined(LVG_ENABLE_JPEG)

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Wunused-function"
#endif
#include "../../third_party/stb_image_write.h"
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

typedef struct {
    lvg_write_fn cb;
    void *ud;
    int failed;
} lvg__stbi_ctx;

static void lvg__stbi_write_thunk(void *context, void *data, int size) {
    lvg__stbi_ctx *c = (lvg__stbi_ctx *)context;
    if (c->failed) return;
    if (c->cb(c->ud, data, (size_t)size) != (size_t)size) c->failed = 1;
}

/* Convert ARGB32 surface → packed RGBA (4 bytes/pixel) in a fresh buffer. */
static unsigned char *lvg__surface_to_rgba(const lvg_surface_t *surface)
{
    int w = surface->width, h = surface->height;
    if (w <= 0 || h <= 0) return NULL;
    if ((uint64_t)w * (uint64_t)h > (uint64_t)(SIZE_MAX / 4)) return NULL;
    unsigned char *rgba = (unsigned char *)LVG_MALLOC((size_t)w * (size_t)h * 4);
    if (!rgba) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = surface->pixels[y * surface->stride + x];
            int off = (y * w + x) * 4;
            rgba[off + 0] = (unsigned char)((px >> 16) & 0xFF);
            rgba[off + 1] = (unsigned char)((px >>  8) & 0xFF);
            rgba[off + 2] = (unsigned char)( px        & 0xFF);
            rgba[off + 3] = (unsigned char)((px >> 24) & 0xFF);
        }
    }
    return rgba;
}

/* Convert ARGB32 surface → packed RGB (3 bytes/pixel) — for JPEG which
 * doesn't support alpha. Alpha is flattened against opaque black. */
static unsigned char *lvg__surface_to_rgb(const lvg_surface_t *surface)
{
    int w = surface->width, h = surface->height;
    if (w <= 0 || h <= 0) return NULL;
    if ((uint64_t)w * (uint64_t)h > (uint64_t)(SIZE_MAX / 3)) return NULL;
    unsigned char *rgb = (unsigned char *)LVG_MALLOC((size_t)w * (size_t)h * 3);
    if (!rgb) return NULL;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            uint32_t px = surface->pixels[y * surface->stride + x];
            int off = (y * w + x) * 3;
            rgb[off + 0] = (unsigned char)((px >> 16) & 0xFF);
            rgb[off + 1] = (unsigned char)((px >>  8) & 0xFF);
            rgb[off + 2] = (unsigned char)( px        & 0xFF);
        }
    }
    return rgb;
}

#endif /* stb backend enabled */

#ifdef LVG_ENABLE_PNG
int lvg_surface_write_png(const lvg_surface_t *surface,
                          lvg_write_fn write_cb, void *userdata)
{
    if (!surface || !write_cb) return -1;
    unsigned char *rgba = lvg__surface_to_rgba(surface);
    if (!rgba) return -1;
    lvg__stbi_ctx ctx = { write_cb, userdata, 0 };
    int ok = stbi_write_png_to_func(lvg__stbi_write_thunk, &ctx,
                                    surface->width, surface->height,
                                    4, rgba, surface->width * 4);
    LVG_FREE(rgba);
    return (ok && !ctx.failed) ? 0 : -1;
}
#else
int lvg_surface_write_png(const lvg_surface_t *surface,
                          lvg_write_fn write_cb, void *userdata)
{
    (void)surface; (void)write_cb; (void)userdata;
    return -1;
}
#endif /* LVG_ENABLE_PNG */

#ifdef LVG_ENABLE_JPEG
int lvg_surface_write_jpeg(const lvg_surface_t *surface,
                           lvg_write_fn write_cb, void *userdata,
                           int quality)
{
    if (!surface || !write_cb) return -1;
    if (quality < 1)   quality = 1;
    if (quality > 100) quality = 100;
    unsigned char *rgb = lvg__surface_to_rgb(surface);
    if (!rgb) return -1;
    lvg__stbi_ctx ctx = { write_cb, userdata, 0 };
    int ok = stbi_write_jpg_to_func(lvg__stbi_write_thunk, &ctx,
                                    surface->width, surface->height,
                                    3, rgb, quality);
    LVG_FREE(rgb);
    return (ok && !ctx.failed) ? 0 : -1;
}
#else
int lvg_surface_write_jpeg(const lvg_surface_t *surface,
                           lvg_write_fn write_cb, void *userdata,
                           int quality)
{
    (void)surface; (void)write_cb; (void)userdata; (void)quality;
    return -1;
}
#endif /* LVG_ENABLE_JPEG */

/* -------------------------------------------------------------------------
 * Path-based save helpers (stdio)
 * ------------------------------------------------------------------------- */

#ifndef LVG_NO_STDIO
#include <stdio.h>

static size_t lvg__stdio_write(void *ud, const void *data, size_t len) {
    return fwrite(data, 1, len, (FILE *)ud);
}

int lvg_surface_save_ppm(const lvg_surface_t *surface, const char *path)
{
    if (!surface || !path) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    int rc = lvg_surface_write_ppm(surface, lvg__stdio_write, fp);
    fclose(fp);
    return rc;
}

int lvg_surface_save_png(const lvg_surface_t *surface, const char *path)
{
    if (!surface || !path) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    int rc = lvg_surface_write_png(surface, lvg__stdio_write, fp);
    fclose(fp);
    return rc;
}

int lvg_surface_save_jpeg(const lvg_surface_t *surface, const char *path,
                          int quality)
{
    if (!surface || !path) return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp) return -1;
    int rc = lvg_surface_write_jpeg(surface, lvg__stdio_write, fp, quality);
    fclose(fp);
    return rc;
}

static int lvg__ext_eq(const char *path, size_t len, const char *ext, size_t elen) {
    if (len < elen) return 0;
    const char *s = path + len - elen;
    for (size_t i = 0; i < elen; i++) {
        char c = s[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
        if (c != ext[i]) return 0;
    }
    return 1;
}

int lvg_surface_save(const lvg_surface_t *surface, const char *path)
{
    if (!surface || !path) return -1;
    size_t len = strlen(path);
    if (lvg__ext_eq(path, len, ".png",  4)) return lvg_surface_save_png(surface, path);
    if (lvg__ext_eq(path, len, ".ppm",  4)) return lvg_surface_save_ppm(surface, path);
    if (lvg__ext_eq(path, len, ".jpg",  4)) return lvg_surface_save_jpeg(surface, path, 90);
    if (lvg__ext_eq(path, len, ".jpeg", 5)) return lvg_surface_save_jpeg(surface, path, 90);
    return lvg_surface_save_ppm(surface, path);
}

#endif /* !LVG_NO_STDIO */
