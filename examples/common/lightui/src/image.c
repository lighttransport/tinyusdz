/*
 * image.c — Image loading via Wuffs
 *
 * Decodes PNG, JPEG, GIF, BMP (and other formats Wuffs supports) into
 * lvg_surface_t pixel buffers (ARGB 0xAARRGGBB format).
 *
 * On little-endian machines, Wuffs' BGRA_NONPREMUL byte order (B,G,R,A in
 * memory) is read as 0xAARRGGBB by uint32_t, which matches our surface format
 * exactly — no swizzle needed.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define WUFFS_IMPLEMENTATION

/* Only compile the modules we need for image decoding. */
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__BMP
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__GIF
#define WUFFS_CONFIG__MODULE__JPEG
#define WUFFS_CONFIG__MODULE__LZW
#define WUFFS_CONFIG__MODULE__NETPBM
#define WUFFS_CONFIG__MODULE__NIE
#define WUFFS_CONFIG__MODULE__PNG
#define WUFFS_CONFIG__MODULE__TARGA
#define WUFFS_CONFIG__MODULE__WBMP
#define WUFFS_CONFIG__MODULE__ZLIB

#include "../third_party/wuffs-v0.4.c"

#include <lightui/image.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Format detection --------------------------------------------------- */

lui_image_format_t lui_image_detect(const void *data, int len)
{
    if (!data || len < 4) return LUI_IMAGE_UNKNOWN;

    wuffs_base__slice_u8 prefix = wuffs_base__make_slice_u8(
        (uint8_t *)data, (size_t)(len > 64 ? 64 : len));
    int32_t fourcc = wuffs_base__magic_number_guess_fourcc(prefix, true);

    switch (fourcc) {
    case WUFFS_BASE__FOURCC__PNG:  return LUI_IMAGE_PNG;
    case WUFFS_BASE__FOURCC__JPEG: return LUI_IMAGE_JPEG;
    case WUFFS_BASE__FOURCC__GIF:  return LUI_IMAGE_GIF;
    case WUFFS_BASE__FOURCC__BMP:  return LUI_IMAGE_BMP;
    default:                       return LUI_IMAGE_UNKNOWN;
    }
}

/* ---- Decoder allocation by fourcc --------------------------------------- */

static wuffs_base__image_decoder *alloc_decoder(int32_t fourcc)
{
    switch (fourcc) {
    case WUFFS_BASE__FOURCC__PNG:
        return wuffs_png__decoder__alloc_as__wuffs_base__image_decoder();
    case WUFFS_BASE__FOURCC__JPEG:
        return wuffs_jpeg__decoder__alloc_as__wuffs_base__image_decoder();
    case WUFFS_BASE__FOURCC__GIF:
        return wuffs_gif__decoder__alloc_as__wuffs_base__image_decoder();
    case WUFFS_BASE__FOURCC__BMP:
        return wuffs_bmp__decoder__alloc_as__wuffs_base__image_decoder();
    case WUFFS_BASE__FOURCC__TGA:
        return wuffs_targa__decoder__alloc_as__wuffs_base__image_decoder();
    case WUFFS_BASE__FOURCC__NPBM:
        return wuffs_netpbm__decoder__alloc_as__wuffs_base__image_decoder();
    default:
        return NULL;
    }
}

/* ---- Core decode implementation ----------------------------------------- */

/*
 * Decode image from memory buffer.  If info_only is true, only fill
 * out_width/out_height and return NULL (success indicated by return code).
 *
 * Returns a new surface on success (when !info_only), NULL on failure or
 * info-only mode.  Sets *result to 0 on success, -1 on failure.
 */
static lvg_surface_t *decode_mem(const void *data, int len,
                                 int *out_width, int *out_height,
                                 int info_only, int *result)
{
    *result = -1;

    if (!data || len <= 0) return NULL;

    /* Detect format */
    wuffs_base__slice_u8 prefix = wuffs_base__make_slice_u8(
        (uint8_t *)data, (size_t)(len > 64 ? 64 : len));
    int32_t fourcc = wuffs_base__magic_number_guess_fourcc(prefix, true);
    if (fourcc <= 0) return NULL;

    /* Allocate decoder */
    wuffs_base__image_decoder *dec = alloc_decoder(fourcc);
    if (!dec) return NULL;

    /* Set up IO buffer (entire input in memory, already closed) */
    wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(
        (uint8_t *)data, (size_t)len, true);

    /* Decode image config (dimensions, pixel format) */
    wuffs_base__image_config ic;
    memset(&ic, 0, sizeof(ic));

    wuffs_base__status status =
        wuffs_base__image_decoder__decode_image_config(dec, &ic, &src);
    if (status.repr) {
        free(dec);
        return NULL;
    }

    uint32_t w = wuffs_base__pixel_config__width(&ic.pixcfg);
    uint32_t h = wuffs_base__pixel_config__height(&ic.pixcfg);

    if (out_width)  *out_width  = (int)w;
    if (out_height) *out_height = (int)h;

    if (info_only) {
        free(dec);
        *result = 0;
        return NULL;
    }

    /* Sanity check dimensions */
    if (w == 0 || h == 0 || w > 32768 || h > 32768) {
        free(dec);
        return NULL;
    }

    /*
     * Set up pixel buffer in BGRA_NONPREMUL format.
     * On little-endian, bytes in memory are B,G,R,A which when read as
     * uint32_t gives 0xAARRGGBB — exactly our ARGB surface format.
     */
    wuffs_base__pixel_config pc;
    memset(&pc, 0, sizeof(pc));
    wuffs_base__pixel_config__set(&pc,
                                  WUFFS_BASE__PIXEL_FORMAT__BGRA_NONPREMUL,
                                  WUFFS_BASE__PIXEL_SUBSAMPLING__NONE, w, h);

    uint64_t pixbuf_len = (uint64_t)w * (uint64_t)h * 4;
    void *pixbuf_ptr = malloc((size_t)pixbuf_len);
    if (!pixbuf_ptr) {
        free(dec);
        return NULL;
    }

    wuffs_base__pixel_buffer pb;
    memset(&pb, 0, sizeof(pb));
    status = wuffs_base__pixel_buffer__set_from_slice(
        &pb, &pc,
        wuffs_base__make_slice_u8((uint8_t *)pixbuf_ptr, (size_t)pixbuf_len));
    if (status.repr) {
        free(pixbuf_ptr);
        free(dec);
        return NULL;
    }

    /* Allocate work buffer */
    uint64_t workbuf_len =
        wuffs_base__image_decoder__workbuf_len(dec).max_incl;
    void *workbuf_ptr = NULL;
    if (workbuf_len > 0) {
        workbuf_ptr = malloc((size_t)workbuf_len);
        if (!workbuf_ptr) {
            free(pixbuf_ptr);
            free(dec);
            return NULL;
        }
    }
    wuffs_base__slice_u8 workbuf = wuffs_base__make_slice_u8(
        (uint8_t *)workbuf_ptr, (size_t)workbuf_len);

    /* Decode first frame */
    status = wuffs_base__image_decoder__decode_frame(
        dec, &pb, &src, WUFFS_BASE__PIXEL_BLEND__SRC, workbuf, NULL);
    free(workbuf_ptr);
    free(dec);

    if (status.repr) {
        free(pixbuf_ptr);
        return NULL;
    }

    /* Wrap decoded pixels in a lvg_surface_t.
     * We transfer ownership of pixbuf_ptr to the surface. */
    lvg_surface_t *surf = (lvg_surface_t *)calloc(1, sizeof(lvg_surface_t));
    if (!surf) {
        free(pixbuf_ptr);
        return NULL;
    }
    surf->pixels = (uint32_t *)pixbuf_ptr;
    surf->width  = (int)w;
    surf->height = (int)h;
    surf->stride = (int)w;
    surf->dpi_scale = 1.0f;

    *result = 0;
    return surf;
}

/* ---- File reading helper ------------------------------------------------ */

static void *read_file(const char *path, int *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize <= 0 || fsize > 256 * 1024 * 1024) { /* 256 MB limit */
        fclose(f);
        return NULL;
    }

    void *buf = malloc((size_t)fsize);
    if (!buf) { fclose(f); return NULL; }

    size_t nread = fread(buf, 1, (size_t)fsize, f);
    fclose(f);

    if ((long)nread != fsize) {
        free(buf);
        return NULL;
    }

    *out_len = (int)fsize;
    return buf;
}

/* ---- Public API --------------------------------------------------------- */

lvg_surface_t *lui_image_load_mem(const void *data, int len)
{
    int result = -1;
    return decode_mem(data, len, NULL, NULL, 0, &result);
}

int lui_image_info_mem(const void *data, int len,
                       int *out_width, int *out_height)
{
    int result = -1;
    decode_mem(data, len, out_width, out_height, 1, &result);
    return result;
}

lvg_surface_t *lui_image_load_file(const char *path)
{
    if (!path) return NULL;
    int len = 0;
    void *data = read_file(path, &len);
    if (!data) return NULL;

    lvg_surface_t *surf = lui_image_load_mem(data, len);
    free(data);
    return surf;
}

int lui_image_info_file(const char *path, int *out_width, int *out_height)
{
    if (!path) return -1;
    int len = 0;
    void *data = read_file(path, &len);
    if (!data) return -1;

    int result = lui_image_info_mem(data, len, out_width, out_height);
    free(data);
    return result;
}
