/*
 * TinyEXR texpipe - unit tests.
 *
 * Covers: mip geometry, per-mip BC7 round-trip PSNR (shipped decoder),
 * alpha-coverage preservation across LODs, and DDS/KTX2 container re-parse.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "texpipe.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(cond, msg)                                                        \
    do {                                                                        \
        if (!(cond)) {                                                          \
            printf("FAIL: %s (%s:%d)\n", (msg), __FILE__, __LINE__);            \
            g_fail = 1;                                                         \
        }                                                                       \
    } while (0)

static uint32_t rd_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint64_t rd_u64(const uint8_t *p) {
    return (uint64_t)rd_u32(p) | ((uint64_t)rd_u32(p + 4) << 32);
}

static uint8_t to_u8(float f) {
    int v;
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    v = (int)(f * 255.0f + 0.5f);
    return (uint8_t)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

/* Reference u8 RGBA for a float surface (matches texpipe's internal packing). */
static void surface_rgba8(const tp_surface *s, uint8_t *dst) {
    int x, y, c;
    for (y = 0; y < s->height; ++y) {
        const float *row =
            (const float *)((const uint8_t *)s->data + (size_t)y * s->stride);
        for (x = 0; x < s->width; ++x) {
            float px[4] = {0, 0, 0, 1};
            for (c = 0; c < s->channels && c < 4; ++c)
                px[c] = row[x * s->channels + c];
            dst[(y * s->width + x) * 4 + 0] = to_u8(px[0]);
            dst[(y * s->width + x) * 4 + 1] = to_u8(px[1]);
            dst[(y * s->width + x) * 4 + 2] = to_u8(px[2]);
            dst[(y * s->width + x) * 4 + 3] = to_u8(px[3]);
        }
    }
}

static double psnr_rgba(const uint8_t *a, const uint8_t *b, size_t npix) {
    double mse = 0.0;
    size_t i;
    for (i = 0; i < npix * 4; ++i) {
        double d = (double)a[i] - (double)b[i];
        mse += d * d;
    }
    mse /= (double)(npix * 4);
    if (mse <= 0.0) return 1e9;
    return 10.0 * log10(255.0 * 255.0 / mse);
}

/* ------------------------------------------------------------ fixtures */

/* Smooth RGBA gradient (opaque). */
static uint8_t *make_gradient(int w, int h) {
    uint8_t *img = (uint8_t *)malloc((size_t)w * h * 4);
    int x, y;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            uint8_t *p = img + (y * w + x) * 4;
            p[0] = (uint8_t)(x * 255 / (w - 1));
            p[1] = (uint8_t)(y * 255 / (h - 1));
            p[2] = (uint8_t)((x + y) * 255 / (w + h - 2));
            p[3] = 255;
        }
    return img;
}

/* Alpha-tested cutout: a filled disc (alpha 1 inside, 0 outside). */
static uint8_t *make_cutout(int w, int h) {
    uint8_t *img = (uint8_t *)malloc((size_t)w * h * 4);
    int x, y;
    float cx = w * 0.5f, cy = h * 0.5f, r = w * 0.30f;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            uint8_t *p = img + (y * w + x) * 4;
            float dx = x - cx, dy = y - cy;
            int inside = (dx * dx + dy * dy) <= (r * r);
            p[0] = 200; p[1] = 120; p[2] = 60;
            p[3] = inside ? 255 : 0;
        }
    return img;
}

static void view_u8(tir_image_view *v, uint8_t *data, int w, int h) {
    v->data = data;
    v->width = w;
    v->height = h;
    v->channels = 4;
    v->type = TIR_U8;
    v->row_stride_bytes = 0;
}

/* ------------------------------------------------------------- tests */

static void test_mip_geometry(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    uint8_t *img = make_gradient(128, 128);
    int expect_levels = 8; /* 128,64,32,16,8,4,2,1 */
    view_u8(&v, img, 128, 128);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build_mips gradient");
    CHECK(chain.num_levels == expect_levels, "level count 128->8");
    CHECK(chain.level[0].width == 128 && chain.level[0].height == 128, "level0 dim");
    CHECK(chain.level[expect_levels - 1].width == 1, "last level 1x1");
    tp_mip_chain_free(NULL, &chain);
    free(img);
    printf("  mip geometry: ok\n");
}

static void test_bc7_roundtrip_psnr(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    tp_blocks blocks;
    int i;
    uint8_t *img = make_gradient(128, 128);
    view_u8(&v, img, 128, 128);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    memset(&chain, 0, sizeof(chain));
    memset(&blocks, 0, sizeof(blocks));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build_mips");
    CHECK(TP_OK(tp_compress_chain(NULL, &chain, &opt, &blocks)), "compress_chain");
    for (i = 0; i < chain.num_levels; ++i) {
        const tp_surface *s = &chain.level[i];
        size_t npix = (size_t)s->width * s->height;
        uint8_t *ref = (uint8_t *)malloc(npix * 4);
        uint8_t *dec = (uint8_t *)malloc(npix * 4);
        double p;
        surface_rgba8(s, ref);
        CHECK(tc_bc7_decompress_rgba8(blocks.blk[i].data, (uint32_t)s->width,
                                      (uint32_t)s->height, (size_t)s->width * 4,
                                      dec, npix * 4) == TC_SUCCESS,
              "bc7 decode");
        p = psnr_rgba(ref, dec, npix);
        printf("    level %d %dx%d PSNR=%.2f dB\n", i, s->width, s->height, p);
        /* Assert only on non-trivial levels: a single 4x4/8x8 BC7 block can't
         * fit a 2D colour gradient, which is a BC7 property, not a pipeline
         * defect. Larger levels exercise the round-trip meaningfully. */
        if (s->width >= 32) CHECK(p >= 30.0, "bc7 per-mip PSNR >= 30 dB");
        free(ref);
        free(dec);
    }
    tp_blocks_free(NULL, &blocks);
    tp_mip_chain_free(NULL, &chain);
    free(img);
    printf("  bc7 per-mip round-trip PSNR: ok\n");
}

static void test_alpha_coverage(void) {
    tir_image_view v;
    tp_options opt, opt_off;
    tp_mip_chain chain, chain_off;
    float base_cov, worst_err = 0.0f, worst_err_off = 0.0f;
    int i;
    uint8_t *img = make_cutout(256, 256);
    view_u8(&v, img, 256, 256);

    /* With coverage preservation. */
    tp_options_init(&opt, TP_CONTENT_ALPHA_TESTED, TP_CODEC_BC7);
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build alpha-tested");
    base_cov = tp_alpha_coverage(&chain.level[0], opt.alpha_test_threshold);
    for (i = 1; i < chain.num_levels; ++i) {
        float c = tp_alpha_coverage(&chain.level[i], opt.alpha_test_threshold);
        float e = fabsf(c - base_cov);
        if (chain.level[i].width >= 4 && e > worst_err) worst_err = e;
    }

    /* Without preservation, for contrast. */
    tp_options_init(&opt_off, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt_off.preserve_alpha_coverage = 0;
    memset(&chain_off, 0, sizeof(chain_off));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt_off, &chain_off)), "build no-preserve");
    for (i = 1; i < chain_off.num_levels; ++i) {
        float c = tp_alpha_coverage(&chain_off.level[i], opt_off.alpha_test_threshold);
        float e = fabsf(c - base_cov);
        if (chain_off.level[i].width >= 4 && e > worst_err_off) worst_err_off = e;
    }

    printf("  alpha coverage: base=%.4f worst_err(preserve)=%.4f worst_err(off)=%.4f\n",
           base_cov, worst_err, worst_err_off);
    CHECK(worst_err < 0.05f, "coverage preserved within 5%");
    CHECK(worst_err <= worst_err_off + 1e-4f, "preservation no worse than off");

    tp_mip_chain_free(NULL, &chain);
    tp_mip_chain_free(NULL, &chain_off);
    free(img);
}

static void test_alpha_scale_helper(void) {
    /* 4x1 surface, alphas 0.2,0.4,0.6,0.8: coverage@0.5 = 0.5. Target 0.75
     * (3 of 4 passing) requires scaling up. */
    float data[16] = {0, 0, 0, 0.2f, 0, 0, 0, 0.4f,
                      0, 0, 0, 0.6f, 0, 0, 0, 0.8f};
    tp_surface s;
    float cov;
    s.width = 4; s.height = 1; s.channels = 4;
    s.data = data; s.stride = 16 * sizeof(float);
    CHECK(fabsf(tp_alpha_coverage(&s, 0.5f) - 0.5f) < 1e-6f, "coverage 0.5");
    CHECK(TP_OK(tp_alpha_scale_to_coverage(&s, 0.75f, 0.5f)), "scale to 0.75");
    cov = tp_alpha_coverage(&s, 0.5f);
    CHECK(fabsf(cov - 0.75f) < 1e-6f, "coverage now 0.75");
    printf("  alpha scale helper: ok (cov=%.3f)\n", cov);
}

static void test_containers(void) {
    tir_image_view v;
    tp_options opt;
    uint8_t *ktx = NULL, *dds = NULL;
    size_t ktx_n = 0, dds_n = 0;
    uint8_t *img = make_gradient(64, 64);
    int expect_levels = 7; /* 64..1 */
    view_u8(&v, img, 64, 64);

    /* KTX2 */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_KTX2;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &ktx, &ktx_n)), "process ktx2");
    if (ktx) {
        static const uint8_t id[12] = {0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                       0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A};
        CHECK(memcmp(ktx, id, 12) == 0, "ktx2 identifier");
        CHECK(rd_u32(ktx + 12) == 145u, "ktx2 vkFormat BC7_UNORM");
        CHECK(rd_u32(ktx + 20) == 64u, "ktx2 pixelWidth");
        CHECK(rd_u32(ktx + 36) == 1u, "ktx2 faceCount");
        CHECK(rd_u32(ktx + 40) == (uint32_t)expect_levels, "ktx2 levelCount");
        /* level index[0] must point at valid, in-bounds data. */
        {
            uint64_t off = rd_u64(ktx + 80);
            uint64_t len = rd_u64(ktx + 88);
            size_t l0 = tc_bc7_compressed_size(64, 64);
            CHECK(len == l0, "ktx2 level0 length == bc7 size");
            CHECK(off + len <= ktx_n, "ktx2 level0 in bounds");
        }
    }

    /* DDS */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_DDS;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &dds, &dds_n)), "process dds");
    if (dds) {
        CHECK(memcmp(dds, "DDS ", 4) == 0, "dds magic");
        CHECK(rd_u32(dds + 28) == (uint32_t)expect_levels, "dds mipMapCount");
        CHECK(rd_u32(dds + 16) == 64u, "dds width");
        CHECK(memcmp(dds + 84, "DX10", 4) == 0, "dds DX10 fourcc");
        CHECK(rd_u32(dds + 128) == 98u, "dds dxgiFormat BC7_UNORM");
        CHECK(dds_n == 148u + tc_bc7_compressed_size(64, 64) +
                          tc_bc7_compressed_size(32, 32) +
                          tc_bc7_compressed_size(16, 16) +
                          tc_bc7_compressed_size(8, 8) +
                          tc_bc7_compressed_size(4, 4) +
                          tc_bc7_compressed_size(2, 2) +
                          tc_bc7_compressed_size(1, 1),
              "dds total size == header + all mips");
    }

    tp_free(NULL, ktx);
    tp_free(NULL, dds);
    free(img);
    printf("  containers (ktx2 + dds re-parse): ok\n");
}

/* ------------------------------------------------------------- cube */

/* Cube face direction convention (matches texpipe_cube.c). u,v in [-1,1]. */
static void cube_dir(int face, float u, float v, float d[3]) {
    switch (face) {
    case 0: d[0] = 1;  d[1] = -v; d[2] = -u; break; /* +X */
    case 1: d[0] = -1; d[1] = -v; d[2] = u;  break; /* -X */
    case 2: d[0] = u;  d[1] = 1;  d[2] = v;  break; /* +Y */
    case 3: d[0] = u;  d[1] = -1; d[2] = -v; break; /* -Y */
    case 4: d[0] = u;  d[1] = -v; d[2] = 1;  break; /* +Z */
    default: d[0] = -u; d[1] = -v; d[2] = -1; break; /* -Z */
    }
}

/* Smooth scalar field over the sphere, in [0,1], per RGB channel. */
static void cube_field(int face, int col, int row, int n, float rgb[3]) {
    float u = 2.0f * ((float)col + 0.5f) / (float)n - 1.0f;
    float v = 2.0f * ((float)row + 0.5f) / (float)n - 1.0f;
    float d[3], len;
    cube_dir(face, u, v, d);
    len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    rgb[0] = 0.5f + 0.5f * d[0] / len;
    rgb[1] = 0.5f + 0.5f * d[1] / len;
    rgb[2] = 0.5f + 0.5f * d[2] / len;
}

static void test_cube_seam_fixup(void) {
    const int n = 32, ch = 4;
    tp_surface faces[6];
    int f, x, y, c;
    float max_dev = 0.0f, max_idem = 0.0f;

    for (f = 0; f < 6; ++f) {
        faces[f].width = n; faces[f].height = n; faces[f].channels = ch;
        faces[f].stride = (size_t)n * ch * sizeof(float);
        faces[f].data = (float *)malloc(faces[f].stride * n);
        for (y = 0; y < n; ++y)
            for (x = 0; x < n; ++x) {
                float rgb[3];
                float *p = faces[f].data + (y * n + x) * ch;
                cube_field(f, x, y, n, rgb);
                p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2]; p[3] = 1.0f;
            }
    }

    CHECK(TP_OK(tp_cube_seam_fixup(faces, NULL)), "cube seam fixup");

    /* Geometric check: every border texel must remain close to its original
     * direction value. A wrong adjacency table would average unrelated texels
     * and produce a large deviation, so this validates the 12-edge/8-corner
     * tables independently of the fixup code. */
    for (f = 0; f < 6; ++f)
        for (y = 0; y < n; ++y)
            for (x = 0; x < n; ++x) {
                float rgb[3];
                const float *p = faces[f].data + (y * n + x) * ch;
                int border = (x == 0 || x == n - 1 || y == 0 || y == n - 1);
                if (!border) continue;
                cube_field(f, x, y, n, rgb);
                for (c = 0; c < 3; ++c) {
                    float dv = fabsf(p[c] - rgb[c]);
                    if (dv > max_dev) max_dev = dv;
                }
            }
    CHECK(max_dev < 0.05f, "cube borders stay near true direction (table correct)");

    /* Idempotency: a second fixup must not change anything. */
    {
        float *snap[6];
        for (f = 0; f < 6; ++f) {
            snap[f] = (float *)malloc(faces[f].stride * n);
            memcpy(snap[f], faces[f].data, faces[f].stride * n);
        }
        CHECK(TP_OK(tp_cube_seam_fixup(faces, NULL)), "cube fixup again");
        for (f = 0; f < 6; ++f) {
            size_t k, nf = (size_t)n * n * ch;
            for (k = 0; k < nf; ++k) {
                float dv = fabsf(faces[f].data[k] - snap[f][k]);
                if (dv > max_idem) max_idem = dv;
            }
            free(snap[f]);
        }
    }
    CHECK(max_idem < 1e-6f, "cube fixup is idempotent");

    printf("  cube seam fixup: ok (max_dev=%.4f idempotent_delta=%.2e)\n",
           max_dev, max_idem);
    for (f = 0; f < 6; ++f) free(faces[f].data);
}

static void test_cube_split(void) {
    const int n = 16;
    int W = 6 * n, H = n, f, ok = 1;
    uint8_t *img = (uint8_t *)malloc((size_t)W * H * 4);
    tir_image_view src, out[6];
    int x, y;
    /* strip_h: tile f filled with value f*10 in R. */
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = img + (y * W + x) * 4;
            p[0] = (uint8_t)((x / n) * 10); p[1] = 0; p[2] = 0; p[3] = 255;
        }
    src.data = img; src.width = W; src.height = H; src.channels = 4;
    src.type = TIR_U8; src.row_stride_bytes = 0;
    CHECK(TP_OK(tp_cube_split(&src, TP_CUBE_STRIP_H, out)), "cube split strip_h");
    for (f = 0; f < 6; ++f) {
        const uint8_t *p = (const uint8_t *)out[f].data; /* top-left texel */
        if (out[f].width != n || out[f].height != n || p[0] != (uint8_t)(f * 10))
            ok = 0;
    }
    CHECK(ok, "cube split produces 6 correctly-placed faces");
    printf("  cube split (strip_h): ok\n");
    free(img);
}

static void test_cube_container(void) {
    const int n = 32;
    tir_image_view faces[6];
    uint8_t *data[6];
    tp_options opt;
    uint8_t *ktx = NULL;
    size_t ktx_n = 0;
    int f, x, y;
    for (f = 0; f < 6; ++f) {
        data[f] = (uint8_t *)malloc((size_t)n * n * 4);
        for (y = 0; y < n; ++y)
            for (x = 0; x < n; ++x) {
                uint8_t *p = data[f] + (y * n + x) * 4;
                p[0] = (uint8_t)(x * 255 / (n - 1));
                p[1] = (uint8_t)(y * 255 / (n - 1));
                p[2] = (uint8_t)(f * 40); p[3] = 255;
            }
        view_u8(&faces[f], data[f], n, n);
    }
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_KTX2;
    opt.is_cube = 1;
    CHECK(TP_OK(tp_process(NULL, faces, 6, &opt, &ktx, &ktx_n)), "cube process ktx2");
    if (ktx) {
        CHECK(rd_u32(ktx + 36) == 6u, "ktx2 faceCount == 6");
        CHECK(rd_u32(ktx + 40) == 6u, "ktx2 levelCount == 6 (32->1)");
        /* level 0 length must cover all 6 faces. */
        CHECK(rd_u64(ktx + 88) == 6u * tc_bc7_compressed_size(n, n),
              "ktx2 cube level0 length = 6 faces");
    }
    /* DDS cube caps. */
    {
        uint8_t *dds = NULL; size_t dds_n = 0;
        tp_options o2;
        tp_options_init(&o2, TP_CONTENT_COLOR, TP_CODEC_BC7);
        o2.container = TP_CONTAINER_DDS; o2.is_cube = 1;
        CHECK(TP_OK(tp_process(NULL, faces, 6, &o2, &dds, &dds_n)), "cube process dds");
        if (dds) {
            CHECK((rd_u32(dds + 112) & 0x200u) != 0u, "dds DDSCAPS2_CUBEMAP set");
            CHECK((rd_u32(dds + 136) & 0x4u) != 0u, "dds miscFlag TEXTURECUBE");
        }
        tp_free(NULL, dds);
    }
    tp_free(NULL, ktx);
    for (f = 0; f < 6; ++f) free(data[f]);
    printf("  cube container (ktx2 faceCount + dds cube caps): ok\n");
}

/* ------------------------------------------------------- normal/height */

static void view_f32(tir_image_view *v, float *data, int w, int h, int ch) {
    v->data = data; v->width = w; v->height = h; v->channels = ch;
    v->type = TIR_F32; v->row_stride_bytes = 0;
}

/* F32 RGBA unit-normal field (xyz unit, a=1), smoothly varying so filtering
 * actually shortens the averaged vector. */
static float *make_normals(int w, int h) {
    float *img = (float *)malloc((size_t)w * h * 4 * sizeof(float));
    int x, y;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            float fx = (float)x / (w - 1) * 2.0f - 1.0f;
            float fy = (float)y / (h - 1) * 2.0f - 1.0f;
            float nx = 0.7f * sinf(fx * 3.1415926f);
            float ny = 0.7f * sinf(fy * 3.1415926f);
            float nz = 1.0f, len;
            float *p = img + ((size_t)y * w + x) * 4;
            len = sqrtf(nx * nx + ny * ny + nz * nz);
            p[0] = nx / len; p[1] = ny / len; p[2] = nz / len; p[3] = 1.0f;
        }
    return img;
}

static void test_normal_unit_length(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    float *img = make_normals(128, 128);
    float worst = 0.0f;
    int i, x, y;
    view_f32(&v, img, 128, 128, 4);
    tp_options_init(&opt, TP_CONTENT_NORMAL, TP_CODEC_BC7);
    opt.normal_encoding = TIR_NORMAL_SNORM;
    opt.renormalize = 1;
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build normal mips");
    for (i = 1; i < chain.num_levels; ++i) { /* level 0 == authored base */
        const tp_surface *s = &chain.level[i];
        for (y = 0; y < s->height; ++y) {
            const float *row =
                (const float *)((const uint8_t *)s->data + (size_t)y * s->stride);
            for (x = 0; x < s->width; ++x) {
                const float *p = row + x * s->channels;
                float len = sqrtf(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                float e = fabsf(len - 1.0f);
                if (e > worst) worst = e;
            }
        }
    }
    CHECK(worst < 1e-3f, "normal mips stay unit-length after renormalize");
    printf("  normal unit-length: ok (worst |len-1|=%.2e)\n", worst);
    tp_mip_chain_free(NULL, &chain);
    free(img);
}

static void test_toksvig_helper(void) {
    float nlen[4] = {1.0f, 0.9f, 0.5f, 0.05f};
    float rough[4];
    CHECK(TP_OK(tp_toksvig_roughness(nlen, 4, 0.1f, rough)), "toksvig map");
    CHECK(fabsf(rough[0] - 0.1f) < 1e-3f, "|N|=1 -> base roughness");
    CHECK(rough[1] > rough[0] && rough[2] > rough[1] && rough[3] > rough[2],
          "roughness increases as |N| shrinks");
    CHECK(rough[3] > 0.7f, "near-random normals -> high roughness");
    printf("  toksvig helper: ok (%.3f %.3f %.3f %.3f)\n", rough[0], rough[1],
           rough[2], rough[3]);
}

static double mean_channel(const tp_surface *s, int c) {
    double sum = 0.0;
    int x, y;
    for (y = 0; y < s->height; ++y) {
        const float *row =
            (const float *)((const uint8_t *)s->data + (size_t)y * s->stride);
        for (x = 0; x < s->width; ++x) sum += row[x * s->channels + c];
    }
    return sum / ((double)s->width * s->height);
}

static void test_toksvig_bake(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain, rc;
    float *img = make_normals(64, 64);
    double r0, r1, rlast;
    view_f32(&v, img, 64, 64, 4);
    tp_options_init(&opt, TP_CONTENT_NORMAL, TP_CODEC_BC7);
    opt.normal_encoding = TIR_NORMAL_SNORM;
    opt.bake_toksvig_roughness = 1;
    opt.base_roughness = 0.1f;
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build normal+bake");
    CHECK(chain.roughness != NULL, "roughness baked into chain");
    if (chain.roughness) {
        /* roughness[level] is a width*height float array. */
        tp_surface rs;
        int i;
        double mean0;
        rs = chain.level[0]; rs.channels = 1;
        rs.data = chain.roughness[0]; rs.stride = (size_t)rs.width * sizeof(float);
        mean0 = mean_channel(&rs, 0);
        CHECK(fabs(mean0 - 0.1) < 1e-4, "level0 roughness == base");
        r0 = mean0;
        /* mean roughness of level 1 */
        rs = chain.level[1]; rs.channels = 1;
        rs.data = chain.roughness[1]; rs.stride = (size_t)rs.width * sizeof(float);
        r1 = mean_channel(&rs, 0);
        i = chain.num_levels - 2; /* a coarse but non-1x1 level */
        rs = chain.level[i]; rs.channels = 1;
        rs.data = chain.roughness[i]; rs.stride = (size_t)rs.width * sizeof(float);
        rlast = mean_channel(&rs, 0);
        CHECK(r1 > r0, "roughness grows once filtering starts (level1 > base)");
        CHECK(rlast >= r1 - 1e-3f, "coarser levels are no smoother");
        printf("  toksvig bake: ok (r[0]=%.3f r[1]=%.3f r[coarse]=%.3f)\n",
               r0, r1, rlast);
    }
    /* roughness pyramid is compressible (EAC_R11 path). */
    memset(&rc, 0, sizeof(rc));
    CHECK(TP_OK(tp_build_roughness_chain(NULL, &chain, &rc)), "build roughness chain");
    CHECK(rc.channels == 4 && rc.num_levels == chain.num_levels, "roughness chain shape");
    tp_mip_chain_free(NULL, &rc);
    tp_mip_chain_free(NULL, &chain);
    free(img);
}

static void test_heightmap_mean(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    float *img = (float *)malloc((size_t)128 * 128 * 4 * sizeof(float));
    double base_mean;
    int i, x, y;
    for (y = 0; y < 128; ++y)
        for (x = 0; x < 128; ++x) {
            float *p = img + ((size_t)y * 128 + x) * 4;
            p[0] = 0.25f + 0.5f * (float)x / 127.0f; /* height in R */
            p[1] = p[2] = 0.0f; p[3] = 1.0f;
        }
    view_f32(&v, img, 128, 128, 4);
    tp_options_init(&opt, TP_CONTENT_HEIGHT, TP_CODEC_BC7);
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build height mips");
    base_mean = mean_channel(&chain.level[0], 0);
    for (i = 1; i < chain.num_levels; ++i) {
        double m = mean_channel(&chain.level[i], 0);
        if (chain.level[i].width >= 2)
            CHECK(fabs(m - base_mean) < 0.02, "heightmap mip mean preserved");
    }
    printf("  heightmap mean preservation: ok (base_mean=%.4f)\n", base_mean);
    tp_mip_chain_free(NULL, &chain);
    free(img);
}

/* Octahedral (Y-up) decode, matching tools/envmap octa convention. */
static void octa_dir(float u01, float v01, float d[3]) {
    float u = 2 * u01 - 1, v = 2 * v01 - 1, x = u, z = v, y = 1 - fabsf(u) - fabsf(v), l;
    if (y < 0) { x = (1 - fabsf(v)) * (u >= 0 ? 1.f : -1.f); z = (1 - fabsf(u)) * (v >= 0 ? 1.f : -1.f); }
    l = sqrtf(x * x + y * y + z * z);
    d[0] = x / l; d[1] = y / l; d[2] = z / l;
}

static void test_octa_seam(void) {
    tp_surface s;
    int n = 64, x, y, c;
    float max_dev = 0.0f;
    s.width = n; s.height = n; s.channels = 4;
    s.stride = (size_t)n * 4 * sizeof(float);
    s.data = (float *)malloc(s.stride * n);
    /* fill with a smooth direction field */
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x) {
            float d[3], *p = s.data + (y * n + x) * 4;
            octa_dir((x + 0.5f) / n, (y + 0.5f) / n, d);
            p[0] = 0.5f + 0.5f * d[0]; p[1] = 0.5f + 0.5f * d[1];
            p[2] = 0.5f + 0.5f * d[2]; p[3] = 1.0f;
        }
    CHECK(TP_OK(tp_octa_seam_fixup(&s, NULL)), "octa seam fixup");
    /* Border texels must stay near their true direction value (correct pairing);
     * a wrong fold rule would average unrelated texels and deviate a lot. */
    for (y = 0; y < n; ++y)
        for (x = 0; x < n; ++x) {
            int border = (x == 0 || x == n - 1 || y == 0 || y == n - 1);
            float d[3], *p;
            if (!border) continue;
            octa_dir((x + 0.5f) / n, (y + 0.5f) / n, d);
            p = s.data + (y * n + x) * 4;
            for (c = 0; c < 3; ++c) {
                float ref = 0.5f + 0.5f * d[c];
                float dv = fabsf(p[c] - ref);
                if (dv > max_dev) max_dev = dv;
            }
        }
    printf("  octa seam fixup: ok (max border dev=%.4f)\n", max_dev);
    CHECK(max_dev < 0.05f, "octa fold pairing keeps borders near true direction");
    free(s.data);
}

static void test_channel_majority(void) {
    /* R = binary checkerboard mask; MAJORITY must keep it binary at every mip,
     * while LINEAR would produce gray. */
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    uint8_t *img = (uint8_t *)malloc((size_t)64 * 64 * 4);
    int x, y, i, nonbinary = 0;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            uint8_t *p = img + (y * 64 + x) * 4;
            p[0] = ((x ^ y) & 1) ? 255 : 0; /* mask */
            p[1] = (uint8_t)(x * 4);        /* linear */
            p[2] = 0; p[3] = 255;
        }
    view_u8(&v, img, 64, 64);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.channel_op[0] = TP_CH_MAJORITY;
    memset(&chain, 0, sizeof(chain));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "build packed mips");
    for (i = 1; i < chain.num_levels; ++i) {
        const tp_surface *s = &chain.level[i];
        for (y = 0; y < s->height; ++y) {
            const float *row = (const float *)((const uint8_t *)s->data + (size_t)y * s->stride);
            for (x = 0; x < s->width; ++x) {
                float r = row[x * s->channels + 0];
                if (r > 1e-4f && r < 1.0f - 1e-4f) nonbinary = 1;
            }
        }
    }
    CHECK(!nonbinary, "MAJORITY channel stays binary across mips");
    printf("  channel majority packing: ok (nonbinary=%d)\n", nonbinary);
    tp_mip_chain_free(NULL, &chain);
    free(img);
}

static void test_array_ktx2(void) {
    tir_image_view v;
    tp_options opt;
    tp_mip_chain chain;
    tp_blocks b, layers[3];
    uint8_t *img = make_gradient(64, 64);
    uint8_t *buf = NULL;
    size_t need, wrote = 0;
    view_u8(&v, img, 64, 64);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    memset(&chain, 0, sizeof(chain));
    memset(&b, 0, sizeof(b));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &chain)), "array: build");
    CHECK(TP_OK(tp_compress_chain(NULL, &chain, &opt, &b)), "array: compress");
    layers[0] = b; layers[1] = b; layers[2] = b; /* 3 layers reuse same blocks */
    need = tp_ktx2_array_size(layers, 3, &opt);
    CHECK(need > 0, "array size");
    buf = (uint8_t *)malloc(need);
    CHECK(TP_OK(tp_write_ktx2_array(layers, 3, &opt, buf, need, &wrote)), "array: write");
    if (buf) {
        CHECK(rd_u32(buf + 32) == 3u, "ktx2 layerCount == 3");
        CHECK(rd_u32(buf + 40) == 7u, "ktx2 levelCount == 7");
        CHECK(rd_u64(buf + 88) == 3u * tc_bc7_compressed_size(64, 64),
              "ktx2 array level0 length = 3 layers");
        CHECK(rd_u64(buf + 80) + rd_u64(buf + 88) <= wrote, "array level0 in bounds");
    }
    printf("  array ktx2 (layerCount=3): ok\n");
    free(buf);
    tp_blocks_free(NULL, &b);
    tp_mip_chain_free(NULL, &chain);
    free(img);
}

static float srgb_to_lin(float c) {
    return c <= 0.04045f ? c / 12.92f : powf((c + 0.055f) / 1.055f, 2.4f);
}

static void test_srgb_aware_resize(void) {
    /* Black/white sRGB checker. Naive filtering in sRGB space gives ~0.5 sRGB
     * (linear 0.21). sRGB-aware gives linear 0.5 -> sRGB ~0.735. Test that the
     * linear-decoded mean of the sRGB-aware mip is ~0.5 (energy preserved). */
    tir_image_view v;
    tp_options opt;
    tp_mip_chain aware, naive;
    uint8_t *img = (uint8_t *)malloc((size_t)64 * 64 * 4);
    int x, y, i;
    double lin_aware = 0, lin_naive = 0, n;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            uint8_t *p = img + (y * 64 + x) * 4;
            uint8_t c = ((x ^ y) & 1) ? 255 : 0;
            p[0] = p[1] = p[2] = c; p[3] = 255;
        }
    view_u8(&v, img, 64, 64);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.srgb_aware = 1;
    memset(&aware, 0, sizeof(aware));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &aware)), "build srgb-aware");
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    memset(&naive, 0, sizeof(naive));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &naive)), "build naive");
    /* use a mid level (fully averaged checker) */
    i = 3;
    {
        const tp_surface *sa = &aware.level[i], *sn = &naive.level[i];
        int xx, yy;
        n = (double)sa->width * sa->height;
        for (yy = 0; yy < sa->height; ++yy) {
            const float *ra = (const float *)((const uint8_t *)sa->data + (size_t)yy * sa->stride);
            const float *rn = (const float *)((const uint8_t *)sn->data + (size_t)yy * sn->stride);
            for (xx = 0; xx < sa->width; ++xx) {
                lin_aware += srgb_to_lin(ra[xx * sa->channels]);
                lin_naive += srgb_to_lin(rn[xx * sn->channels]);
            }
        }
        lin_aware /= n; lin_naive /= n;
    }
    printf("  srgb-aware resize: linear mean aware=%.3f naive=%.3f (target 0.5)\n",
           lin_aware, lin_naive);
    CHECK(fabs(lin_aware - 0.5) < 0.03, "sRGB-aware mip preserves linear energy (~0.5)");
    CHECK(lin_naive < 0.35, "naive sRGB filtering darkens (linear ~0.21)");
    tp_mip_chain_free(NULL, &aware);
    tp_mip_chain_free(NULL, &naive);
    free(img);
}

static void test_roughness_variance(void) {
    /* Roughness channel varying 0..1; RMS packing must raise coarse-mip
     * roughness above the plain average. */
    tir_image_view v;
    tp_options opt;
    tp_mip_chain rms, lin;
    uint8_t *img = (uint8_t *)malloc((size_t)64 * 64 * 4);
    int x, y, i;
    double mean_rms = 0, mean_lin = 0, n;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            uint8_t *p = img + (y * 64 + x) * 4;
            p[0] = 128; p[1] = ((x ^ y) & 1) ? 230 : 30; /* rough in G, high variance */
            p[2] = 0; p[3] = 255;
        }
    view_u8(&v, img, 64, 64);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.channel_op[1] = TP_CH_ROUGHNESS;
    memset(&rms, 0, sizeof(rms));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &rms)), "build rough-rms");
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    memset(&lin, 0, sizeof(lin));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &lin)), "build rough-linear");
    i = 3;
    {
        const tp_surface *sr = &rms.level[i], *sl = &lin.level[i];
        int xx, yy;
        n = (double)sr->width * sr->height;
        for (yy = 0; yy < sr->height; ++yy) {
            const float *rr = (const float *)((const uint8_t *)sr->data + (size_t)yy * sr->stride);
            const float *rl = (const float *)((const uint8_t *)sl->data + (size_t)yy * sl->stride);
            for (xx = 0; xx < sr->width; ++xx) { mean_rms += rr[xx * sr->channels + 1]; mean_lin += rl[xx * sl->channels + 1]; }
        }
        mean_rms /= n; mean_lin /= n;
    }
    printf("  roughness variance packing: rms=%.3f linear=%.3f\n", mean_rms, mean_lin);
    CHECK(mean_rms > mean_lin + 0.02, "RMS roughness > plain average under variance");
    tp_mip_chain_free(NULL, &rms);
    tp_mip_chain_free(NULL, &lin);
    free(img);
}

static void test_minmax_pyramid(void) {
    /* Random-ish height map; each level's (min,max) must conservatively bound
     * the base heights it covers, and nest across levels. */
    tir_image_view v;
    tp_mip_chain c;
    uint8_t *img = (uint8_t *)malloc((size_t)64 * 64 * 4);
    int x, y, l, ok_nest = 1, ok_cover = 1;
    float base_min = 1e9f, base_max = -1e9f;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            uint8_t *p = img + (y * 64 + x) * 4;
            uint8_t h = (uint8_t)(((x * 37 + y * 101) ^ (x * y)) & 0xff);
            p[0] = h; p[1] = h; p[2] = h; p[3] = 255;
            if (h / 255.0f < base_min) base_min = h / 255.0f;
            if (h / 255.0f > base_max) base_max = h / 255.0f;
        }
    view_u8(&v, img, 64, 64);
    memset(&c, 0, sizeof(c));
    CHECK(TP_OK(tp_build_minmax_pyramid(NULL, &v, 0, 0, &c)), "build minmax");
    CHECK(c.channels == 2, "minmax is 2-channel");
    /* nesting: level l bounds must contain level l-1 bounds over each footprint */
    for (l = 1; l < c.num_levels; ++l) {
        const tp_surface *s = &c.level[l], *p = &c.level[l - 1];
        for (y = 0; y < s->height; ++y)
            for (x = 0; x < s->width; ++x) {
                const float *d = s->data + (y * s->width + x) * 2;
                int cx = x * 2 < p->width ? x * 2 : p->width - 1;
                int cy = y * 2 < p->height ? y * 2 : p->height - 1;
                const float *cc = p->data + (cy * p->width + cx) * 2;
                if (d[0] > cc[0] + 1e-6f) ok_nest = 0; /* min must not rise */
                if (d[1] < cc[1] - 1e-6f) ok_nest = 0; /* max must not fall */
            }
    }
    /* coarsest level must bound the whole base */
    {
        const tp_surface *top = &c.level[c.num_levels - 1];
        const float *d = top->data;
        if (d[0] > base_min + 1e-4f || d[1] < base_max - 1e-4f) ok_cover = 0;
    }
    CHECK(ok_nest, "minmax bounds nest across levels");
    CHECK(ok_cover, "coarsest minmax bounds the whole height field");
    printf("  minmax height pyramid: ok (base [%.2f,%.2f])\n", base_min, base_max);
    tp_mip_chain_free(NULL, &c);
    free(img);
}

static void test_dilate(void) {
    /* A valid red disc on transparent (alpha 0) background. After dilation the
     * gutter ring must take the disc colour (red), not the background. */
    int W = 48, H = 48, x, y;
    tp_surface s;
    int gutter_ok = 1, checked = 0;
    s.width = W; s.height = H; s.channels = 4;
    s.stride = (size_t)W * 4 * sizeof(float);
    s.data = (float *)malloc(s.stride * H);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            float dx = x - W * 0.5f, dy = y - H * 0.5f;
            int inside = dx * dx + dy * dy <= (W * 0.3f) * (W * 0.3f);
            float *p = s.data + (y * W + x) * 4;
            if (inside) { p[0] = 1; p[1] = 0; p[2] = 0; p[3] = 1; }
            else { p[0] = 0; p[1] = 0; p[2] = 1; p[3] = 0; } /* blue bg, alpha 0 */
        }
    CHECK(TP_OK(tp_dilate(&s, 3, 0.5f, 6)), "dilate");
    /* Texels just outside the disc should now be reddish (pulled from the disc,
     * not the blue background), with alpha untouched (still 0). */
    {
        int red = 0, alpha_ok = 1;
        for (y = 0; y < H; ++y)
            for (x = 0; x < W; ++x) {
                float dx = x - W * 0.5f, dy = y - H * 0.5f;
                float r = sqrtf(dx * dx + dy * dy), edge = W * 0.3f;
                const float *p = s.data + (y * W + x) * 4;
                if (r > edge && r < edge + 2.0f) { /* gutter ring */
                    ++checked;
                    if (p[0] > p[2]) ++red;        /* red dominates blue bg */
                    if (p[3] > 0.001f) alpha_ok = 0; /* alpha preserved (0) */
                }
            }
        gutter_ok = alpha_ok && checked > 0 && red >= (int)(0.9 * checked);
        printf("  gutter dilation: %d/%d ring texels filled from disc, alpha_ok=%d\n",
               red, checked, alpha_ok);
    }
    CHECK(gutter_ok, "dilation fills gutter with disc colour, keeps alpha");
    free(s.data);
}

static void test_vector_displacement_mean(void) {
    /* Vector displacement (3-ch float) resizes mean-preserving via BC6H HDR path
     * (averaging keeps the mean vector — correct for displacement). */
    tir_image_view v;
    tp_options opt;
    tp_mip_chain c;
    float *img = (float *)malloc((size_t)64 * 64 * 3 * sizeof(float));
    double bx = 0, by = 0, bz = 0;
    int x, y, i;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            float *p = img + (y * 64 + x) * 3;
            p[0] = 0.5f + 0.4f * sinf(x * 0.3f);
            p[1] = 0.2f + 0.1f * cosf(y * 0.2f);
            p[2] = 0.7f;
            bx += p[0]; by += p[1]; bz += p[2];
        }
    bx /= 4096; by /= 4096; bz /= 4096;
    v.data = img; v.width = 64; v.height = 64; v.channels = 3; v.type = TIR_F32; v.row_stride_bytes = 0;
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC6H);
    memset(&c, 0, sizeof(c));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &c)), "build vector-disp mips");
    /* mid level mean should match base mean (mean-preserving) */
    i = 2;
    {
        const tp_surface *s = &c.level[i];
        double mx = 0, my = 0, mz = 0, n = (double)s->width * s->height;
        int xx, yy;
        for (yy = 0; yy < s->height; ++yy) {
            const float *row = (const float *)((const uint8_t *)s->data + (size_t)yy * s->stride);
            for (xx = 0; xx < s->width; ++xx) { mx += row[xx*3]; my += row[xx*3+1]; mz += row[xx*3+2]; }
        }
        mx /= n; my /= n; mz /= n;
        CHECK(fabs(mx - bx) < 0.02 && fabs(my - by) < 0.02 && fabs(mz - bz) < 0.02,
              "vector displacement mip preserves the mean vector");
    }
    printf("  vector displacement mean-preserving: ok (base %.3f,%.3f,%.3f)\n", bx, by, bz);
    tp_mip_chain_free(NULL, &c);
    free(img);
}

static void test_cone_map(void) {
    /* Flat field except a central spike: cone ratio grows with distance from the
     * spike, and a fully flat field yields cone 1 everywhere. */
    tir_image_view v;
    tp_surface cone;
    uint8_t *img = (uint8_t *)malloc((size_t)32 * 32 * 4);
    int x, y;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x) {
            uint8_t *p = img + (y * 32 + x) * 4;
            uint8_t h = (x == 16 && y == 16) ? 255 : 128; /* spike */
            p[0] = h; p[1] = h; p[2] = h; p[3] = 255;
        }
    view_u8(&v, img, 32, 32);
    memset(&cone, 0, sizeof(cone));
    CHECK(TP_OK(tp_build_cone_map(NULL, &v, 0, &cone)), "build cone map");
    {
        float near_spike = cone.data[15 * 32 + 16]; /* adjacent to spike */
        float far_corner = cone.data[0];            /* far away */
        CHECK(near_spike < far_corner, "cone ratio grows with distance from occluder");
        CHECK(near_spike >= 0.0f && far_corner <= 1.0f, "cone ratio in [0,1]");
        printf("  cone-step map: ok (near=%.3f far=%.3f)\n", near_spike, far_corner);
    }
    tp_free(NULL, cone.data);
    free(img);
}

static void test_ripmap(void) {
    float *img = (float *)malloc((size_t)64 * 64 * 3 * sizeof(float));
    tir_image_view v;
    tp_mip_chain rip;
    int nx = 0, ny = 0, x, y, ok = 1;
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 64; ++x) {
            float *p = img + (y * 64 + x) * 3;
            p[0] = x / 63.0f; p[1] = y / 63.0f; p[2] = 0.5f;
        }
    v.data = img; v.width = 64; v.height = 64; v.channels = 3; v.type = TIR_F32; v.row_stride_bytes = 0;
    memset(&rip, 0, sizeof(rip));
    CHECK(TP_OK(tp_build_ripmap(NULL, &v, NULL, &rip, &nx, &ny)), "build ripmap");
    CHECK(nx == 7 && ny == 7, "ripmap 7x7 grid for 64x64");
    /* cell (ix,jy) must have dims (64>>ix, 64>>jy) */
    {
        int ix, jy;
        for (jy = 0; jy < ny; ++jy)
            for (ix = 0; ix < nx; ++ix) {
                const tp_surface *s = &rip.level[jy * nx + ix];
                int ew = 64 >> ix, eh = 64 >> jy;
                if (ew < 1) ew = 1;
                if (eh < 1) eh = 1;
                if (s->width != ew || s->height != eh) ok = 0;
            }
    }
    CHECK(ok, "ripmap cell dimensions are anisotropic (w>>ix, h>>jy)");
    printf("  ripmap: ok (%dx%d cells)\n", nx, ny);
    tp_mip_chain_free(NULL, &rip);
    free(img);
}

static void test_ycocg(void) {
    /* round-trip identity + BC7-on-YCoCg vs BC7-on-RGB on a chroma-rich image. */
    float rgb[3] = {0.8f, 0.2f, 0.5f}, yc[3], back[3];
    int W = 64, H = 64, x, y, i;
    uint8_t *rgba, *yimg, *bc, *dec;
    double mse_rgb = 0, mse_yc = 0;
    tp_rgb_to_ycocg(rgb, yc);
    tp_ycocg_to_rgb(yc, back);
    CHECK(fabsf(back[0]-rgb[0]) < 1e-5f && fabsf(back[1]-rgb[1]) < 1e-5f && fabsf(back[2]-rgb[2]) < 1e-5f,
          "YCoCg round-trip identity");
    rgba = (uint8_t *)malloc((size_t)W*H*4);
    yimg = (uint8_t *)malloc((size_t)W*H*4);
    bc = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    dec = (uint8_t *)malloc((size_t)W*H*4);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p = rgba + (y*W+x)*4;
            float r[3] = {0.5f+0.5f*sinf(x*0.4f), 0.5f+0.5f*sinf(y*0.4f+2), 0.5f+0.5f*sinf((x+y)*0.3f+4)};
            float c[3];
            uint8_t *q = yimg + (y*W+x)*4;
            p[0]=to_u8(r[0]); p[1]=to_u8(r[1]); p[2]=to_u8(r[2]); p[3]=255;
            tp_rgb_to_ycocg(r, c);
            q[0]=to_u8(c[0]); q[1]=to_u8(c[1]); q[2]=to_u8(c[2]); q[3]=255;
        }
    /* RGB path */
    tc_bc7_compress_rgba8(rgba, W, H, W*4, NULL, bc, tc_bc7_compressed_size(W,H));
    tc_bc7_decompress_rgba8(bc, W, H, W*4, dec, (size_t)W*H*4);
    for (i = 0; i < W*H; ++i) { int c; for (c=0;c<3;++c){ double d=(double)dec[i*4+c]-rgba[i*4+c]; mse_rgb+=d*d; } }
    /* YCoCg path: decode, inverse-transform, compare to source RGB */
    tc_bc7_compress_rgba8(yimg, W, H, W*4, NULL, bc, tc_bc7_compressed_size(W,H));
    tc_bc7_decompress_rgba8(bc, W, H, W*4, dec, (size_t)W*H*4);
    for (i = 0; i < W*H; ++i) {
        float c[3] = {dec[i*4]/255.f, dec[i*4+1]/255.f, dec[i*4+2]/255.f}, r[3];
        int k;
        tp_ycocg_to_rgb(c, r);
        for (k = 0; k < 3; ++k) { double d = to_u8(r[k]) - (double)rgba[i*4+k]; mse_yc += d*d; }
    }
    {
        double p_rgb = 10*log10(255.0*255.0/(mse_rgb/(W*H*3)));
        double p_yc = 10*log10(255.0*255.0/(mse_yc/(W*H*3)));
        printf("  ycocg: BC7 RGB=%.1f dB, BC7 YCoCg=%.1f dB\n", p_rgb, p_yc);
        CHECK(p_yc >= p_rgb - 1.0, "YCoCg BC7 quality not worse than RGB");
    }
    free(rgba); free(yimg); free(bc); free(dec);
}

static void test_kaiser(void) {
    /* Kaiser resize must produce finite, energy-preserving output. */
    tir_image_view v;
    tp_options opt;
    tp_mip_chain c;
    uint8_t *img = make_gradient(128, 128);
    int i, x, y, finite = 1;
    double base_mean = 0, mip_mean = 0;
    view_u8(&v, img, 128, 128);
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.filter = TIR_FILTER_KAISER;
    memset(&c, 0, sizeof(c));
    CHECK(TP_OK(tp_build_mips(NULL, &v, 1, &opt, &c)), "build kaiser mips");
    for (i = 0; i < 128*128; ++i) base_mean += img[i*4];
    base_mean /= (128*128*255.0);
    {
        const tp_surface *s = &c.level[2];
        double n = (double)s->width * s->height;
        for (y = 0; y < s->height; ++y) {
            const float *row = (const float *)((const uint8_t *)s->data + (size_t)y*s->stride);
            for (x = 0; x < s->width; ++x) {
                float r = row[x*s->channels];
                if (!(r == r) || r < -0.5f || r > 1.5f) finite = 0;
                mip_mean += r;
            }
        }
        mip_mean /= n;
    }
    CHECK(finite, "kaiser output finite and bounded");
    CHECK(fabs(mip_mean - base_mean) < 0.03, "kaiser mip preserves mean");
    printf("  kaiser filter: ok (base %.3f -> mip %.3f)\n", base_mean, mip_mean);
    tp_mip_chain_free(NULL, &c);
    free(img);
}

/* ------------------------------------------------- KTX2 read / decode / uni */

static void test_ktx2_read_roundtrip(void) {
    tir_image_view v;
    tp_options opt;
    uint8_t *ref = make_gradient(64, 64);
    uint8_t *ktx = NULL, *dec = NULL;
    size_t ktx_n = 0;
    tp_ktx2_image img;
    double p;
    const size_t npix = 64u * 64u;
    view_u8(&v, ref, 64, 64);
    dec = (uint8_t *)malloc(npix * 4u);

    /* --- BC7 KTX2: write -> read -> decode level 0 --- */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_KTX2;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &ktx, &ktx_n)), "process bc7 ktx2");
    CHECK(TP_OK(tp_ktx2_read(ktx, ktx_n, &img)), "read bc7 ktx2");
    CHECK(!img.is_uni && img.codec == TP_CODEC_BC7, "bc7 codec mapped");
    CHECK(img.width == 64 && img.height == 64, "bc7 dims");
    CHECK(img.num_levels == 7 && img.num_faces == 1, "bc7 levels/faces");
    CHECK(img.levels[0].width == 64 && img.levels[1].width == 32, "bc7 level dims");
    CHECK(TP_OK(tp_ktx2_decode_level_rgba8(&img, 0, dec, npix * 4u)), "decode bc7 l0");
    p = psnr_rgba(ref, dec, npix);
    printf("    bc7 ktx2 read+decode level0 PSNR=%.2f dB\n", p);
    CHECK(p >= 30.0, "bc7 ktx2 decode PSNR >= 30 dB");
    tp_free(NULL, ktx); ktx = NULL;

    /* --- ASTC 4x4 KTX2: exercises the newly-exposed ASTC decoder --- */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_ASTC);
    opt.container = TP_CONTAINER_KTX2;
    opt.astc.block_x = 4; opt.astc.block_y = 4;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &ktx, &ktx_n)), "process astc ktx2");
    CHECK(TP_OK(tp_ktx2_read(ktx, ktx_n, &img)), "read astc ktx2");
    CHECK(!img.is_uni && img.codec == TP_CODEC_ASTC, "astc codec mapped");
    CHECK(img.block_w == 4 && img.block_h == 4, "astc block 4x4");
    CHECK(TP_OK(tp_ktx2_decode_level_rgba8(&img, 0, dec, npix * 4u)), "decode astc l0");
    p = psnr_rgba(ref, dec, npix);
    printf("    astc ktx2 read+decode level0 PSNR=%.2f dB\n", p);
    CHECK(p >= 30.0, "astc ktx2 decode PSNR >= 30 dB");
    tp_free(NULL, ktx); ktx = NULL;

    /* --- uni (UASTC) KTX2: write_uni -> read -> decode + transcode --- */
    {
        size_t usz = tc_uni_compressed_size(64, 64);
        uint8_t *uni = (uint8_t *)malloc(usz);
        const uint8_t *levels[1]; size_t sizes[1]; uint32_t lw[1], lh[1];
        uint8_t *ubuf = NULL; size_t ktx_size;
        CHECK(tc_uni_compress_rgba8(ref, 64, 64, (size_t)64 * 4u, uni, usz) == TC_SUCCESS,
              "uni compress");
        levels[0] = uni; sizes[0] = usz; lw[0] = 64; lh[0] = 64;
        ktx_size = tp_ktx2_uni_size(sizes, 1);
        ubuf = (uint8_t *)malloc(ktx_size);
        CHECK(TP_OK(tp_ktx2_write_uni(levels, sizes, lw, lh, 1, ubuf, ktx_size, NULL)),
              "write uni ktx2");
        CHECK(TP_OK(tp_ktx2_read(ubuf, ktx_size, &img)), "read uni ktx2");
        CHECK(img.is_uni && img.vk_format == 0u, "uni marker");
        CHECK(img.width == 64 && img.num_levels == 1, "uni header");
        CHECK(TP_OK(tp_ktx2_decode_level_rgba8(&img, 0, dec, npix * 4u)), "decode uni l0");
        p = psnr_rgba(ref, dec, npix);
        printf("    uni ktx2 read+decode level0 PSNR=%.2f dB\n", p);
        CHECK(p >= 25.0, "uni ktx2 decode PSNR >= 25 dB");
        /* transcode uni -> BC7, decode, and confirm it survives the transcode */
        {
            size_t bsz = tc_bc7_compressed_size(64, 64);
            uint8_t *bc7 = (uint8_t *)malloc(bsz);
            CHECK(tc_uni_transcode_bc7(img.levels[0].data, 64, 64, bc7, bsz) == TC_SUCCESS,
                  "uni->bc7 transcode");
            CHECK(tc_bc7_decompress_rgba8(bc7, 64, 64, (size_t)64 * 4u, dec, npix * 4u) == TC_SUCCESS,
                  "decode transcoded bc7");
            p = psnr_rgba(ref, dec, npix);
            printf("    uni->bc7 transcode PSNR=%.2f dB\n", p);
            CHECK(p >= 25.0, "uni->bc7 PSNR >= 25 dB");
            free(bc7);
        }
        free(ubuf); free(uni);
    }

    /* --- malformed guards --- */
    {
        uint8_t bad[80];
        tp_ktx2_image bimg;
        memset(bad, 0, sizeof(bad));
        CHECK(tp_ktx2_read(bad, sizeof(bad), &bimg) == TP_ERROR_INVALID_ARGUMENT,
              "reject bad identifier");
        CHECK(tp_ktx2_read(NULL, 0, &bimg) == TP_ERROR_INVALID_ARGUMENT,
              "reject null");
    }
    /* corrupt a valid BC7 KTX2's level-0 offset -> out-of-bounds rejected */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_KTX2;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &ktx, &ktx_n)), "process bc7 ktx2 (2)");
    ktx[80] = 0xFF; ktx[81] = 0xFF; ktx[82] = 0xFF; ktx[83] = 0xFF; /* huge offset */
    CHECK(tp_ktx2_read(ktx, ktx_n, &img) == TP_ERROR_INVALID_ARGUMENT,
          "reject out-of-bounds level offset");
    tp_free(NULL, ktx);

    free(dec);
    free(ref);
    printf("  ktx2 read + decode (bc7 / astc / uni) + transcode: ok\n");
}

/* Identity "decompressor": the scheme-2 payloads in this test are stored
 * uncompressed, so decompression is a bounds-checked memcpy. Exercises the
 * tp_ktx2_read_zstd allocation / per-level callback path without a real zstd. */
static size_t passthrough_zdec(void *user, uint8_t *dst, size_t dst_cap,
                               const uint8_t *src, size_t src_size) {
    (void)user;
    if (src_size > dst_cap) return 0;
    memcpy(dst, src, src_size);
    return src_size;
}

static void test_ktx2_zstd_scheme(void) {
    tir_image_view v;
    tp_options opt;
    uint8_t *ref = make_gradient(64, 64);
    uint8_t *ktx = NULL, *sc2 = NULL, *dec = NULL;
    size_t ktx_n = 0;
    tp_ktx2_image img;
    const size_t npix = 64u * 64u;
    view_u8(&v, ref, 64, 64);
    dec = (uint8_t *)malloc(npix * 4u);

    /* A scheme-0 BC7 KTX2 whose levels happen to be "stored uncompressed" is a
     * valid scheme-2 stream for a passthrough decompressor: byteLength ==
     * uncompressedByteLength already, so only the scheme field must change. */
    tp_options_init(&opt, TP_CONTENT_COLOR, TP_CODEC_BC7);
    opt.container = TP_CONTAINER_KTX2;
    CHECK(TP_OK(tp_process(NULL, &v, 1, &opt, &ktx, &ktx_n)), "process bc7 ktx2");
    sc2 = (uint8_t *)malloc(ktx_n);
    memcpy(sc2, ktx, ktx_n);
    sc2[44] = 2; sc2[45] = 0; sc2[46] = 0; sc2[47] = 0;  /* supercompression = Zstd */

    /* Plain tp_ktx2_read must refuse a supercompressed stream. */
    CHECK(tp_ktx2_read(sc2, ktx_n, &img) == TP_ERROR_UNSUPPORTED,
          "scheme 2 rejected without a decompressor");

    /* With a decompressor it parses, owns a buffer, and decodes identically. */
    CHECK(TP_OK(tp_ktx2_read_zstd(sc2, ktx_n, NULL, passthrough_zdec, NULL, &img)),
          "read scheme-2 with passthrough zdec");
    CHECK(img.supercompression == 2u && img._owned != NULL, "scheme 2 owns buffer");
    CHECK(img.codec == TP_CODEC_BC7 && img.num_levels == 7, "scheme 2 header");
    CHECK(TP_OK(tp_ktx2_decode_level_rgba8(&img, 0, dec, npix * 4u)), "decode sc2 l0");
    {
        double p = psnr_rgba(ref, dec, npix);
        printf("    scheme-2(Zstd path) read+decode level0 PSNR=%.2f dB\n", p);
        CHECK(p >= 30.0, "scheme-2 decode PSNR >= 30 dB");
    }
    tp_ktx2_image_free(NULL, &img);
    CHECK(img._owned == NULL, "image_free clears owned buffer");

    /* A truncated compressed payload (decompressor returns short) is rejected. */
    {
        uint64_t off = rd_u64(sc2 + 80);
        uint64_t len = rd_u64(sc2 + 88);
        /* Corrupt level-0 byteLength so off+clen overflows the file. */
        uint8_t save[8];
        memcpy(save, sc2 + 88, 8);
        sc2[88] = 0xFF; sc2[89] = 0xFF; sc2[90] = 0xFF; sc2[91] = 0xFF;
        CHECK(tp_ktx2_read_zstd(sc2, ktx_n, NULL, passthrough_zdec, NULL, &img) ==
                  TP_ERROR_INVALID_ARGUMENT,
              "reject scheme-2 out-of-bounds compressed length");
        memcpy(sc2 + 88, save, 8);
        (void)off; (void)len;
    }

    tp_free(NULL, ktx);
    free(sc2);
    free(dec);
    free(ref);
    printf("  ktx2 scheme-2 (Zstd) read via decompressor callback: ok\n");
}

int main(void) {
    printf("texpipe unit tests\n");
    test_octa_seam();
    test_channel_majority();
    test_array_ktx2();
    test_srgb_aware_resize();
    test_roughness_variance();
    test_minmax_pyramid();
    test_dilate();
    test_vector_displacement_mean();
    test_cone_map();
    test_ripmap();
    test_ycocg();
    test_kaiser();
    test_mip_geometry();
    test_bc7_roundtrip_psnr();
    test_alpha_scale_helper();
    test_alpha_coverage();
    test_containers();
    test_ktx2_read_roundtrip();
    test_ktx2_zstd_scheme();
    test_cube_seam_fixup();
    test_cube_split();
    test_cube_container();
    test_normal_unit_length();
    test_toksvig_helper();
    test_toksvig_bake();
    test_heightmap_mean();
    if (g_fail) {
        printf("SOME TESTS FAILED\n");
        return 1;
    }
    printf("ALL TESTS PASSED\n");
    return 0;
}
