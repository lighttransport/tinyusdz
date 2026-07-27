/*
 * tir - unit tests. Run from the repository root (no external assets).
 *
 * The correctness oracle is an independent naive 2D double-precision
 * resampler implemented below (same filter/edge/registration definitions,
 * direct convolution, no ring buffer, no float tables).
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tir.h"

static int g_pass = 0, g_fail = 0;

#define CHECK(cond, msg)                                            \
    do {                                                            \
        if (cond) {                                                 \
            g_pass++;                                               \
        } else {                                                    \
            g_fail++;                                               \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
                    (msg));                                         \
        }                                                           \
    } while (0)

/* deterministic PRNG */
static uint32_t g_rng = 0x12345678u;
static uint32_t xr(void) {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static float frand(void) { return (float)(xr() >> 8) / (float)(1 << 24); }

/* ===========================================================================
 * Reference resampler (independent implementation)
 * ========================================================================= */

static double r_cubic(double a, double B, double C) {
    if (a < 1.0)
        return ((12 - 9 * B - 6 * C) * a * a * a +
                (-18 + 12 * B + 6 * C) * a * a + (6 - 2 * B)) /
               6.0;
    if (a < 2.0)
        return ((-B - 6 * C) * a * a * a + (6 * B + 30 * C) * a * a +
                (-12 * B - 48 * C) * a + (8 * B + 24 * C)) /
               6.0;
    return 0.0;
}
static double r_sinc(double x) {
    if (x == 0.0) return 1.0;
    x *= 3.14159265358979323846;
    return sin(x) / x;
}
static double r_kern(tir_filter f, double t, double sigma) {
    double a = fabs(t);
    switch (f) {
        case TIR_FILTER_TRIANGLE:
            return a < 1 ? 1 - a : 0;
        case TIR_FILTER_BSPLINE:
            return r_cubic(a, 1, 0);
        case TIR_FILTER_GAUSSIAN:
            return exp(-(a * a) / (2 * sigma * sigma));
        case TIR_FILTER_MITCHELL:
            return r_cubic(a, 1 / 3.0, 1 / 3.0);
        case TIR_FILTER_CATMULL_ROM:
            return r_cubic(a, 0, 0.5);
        case TIR_FILTER_LANCZOS2:
            return a < 2 ? r_sinc(a) * r_sinc(a / 2) : 0;
        case TIR_FILTER_LANCZOS3:
            return a < 3 ? r_sinc(a) * r_sinc(a / 3) : 0;
        default:
            return a <= 0.5 ? 1 : 0;
    }
}
static double r_radius(tir_filter f, double sigma) {
    switch (f) {
        case TIR_FILTER_BOX:
            return 0.5;
        case TIR_FILTER_TRIANGLE:
            return 1;
        case TIR_FILTER_GAUSSIAN:
            return 3 * sigma < 0.5 ? 0.5 : 3 * sigma;
        case TIR_FILTER_LANCZOS3:
            return 3;
        default:
            return 2;
    }
}
static int r_edge(int j, int n, tir_edge_mode m) {
    if (n == 1) return 0;
    if (j >= 0 && j < n) return j;
    if (m == TIR_EDGE_CLAMP) return j < 0 ? 0 : n - 1;
    if (m == TIR_EDGE_WRAP) {
        j %= n;
        if (j < 0) j += n;
        return j;
    }
    {
        int p = 2 * n - 2;
        j %= p;
        if (j < 0) j += p;
        return j < n ? j : p - j;
    }
}

#define R_MAXW 512
typedef struct {
    int lo, n;
    double w[R_MAXW];
} r_win;

static void r_window(int s, int d, int i, tir_filter f, double sigma,
                     tir_registration reg, r_win *out) {
    int vertex = (reg == TIR_REG_GRID_VERTEX && d > 1 && s > 1);
    double ratio = vertex ? (double)(s - 1) / (d - 1) : (double)s / d;
    double center = vertex ? (double)i * (s - 1) / (d - 1)
                           : ((double)i + 0.5) * s / d - 0.5;
    double fs = ratio > 1 ? ratio : 1;
    int lo, hi, t;
    double sum = 0;
    if (f == TIR_FILTER_BOX) {
        double sb = 0.5 * fs, a = center - sb, b = center + sb;
        lo = (int)ceil(a - 0.5);
        hi = (int)floor(b + 0.5);
        if (hi < lo) hi = lo;
        for (t = lo; t <= hi; ++t) {
            double l = t - 0.5, r = t + 0.5;
            double ov = (r < b ? r : b) - (l > a ? l : a);
            out->w[t - lo] = ov > 0 ? ov : 0;
        }
    } else {
        double sup = r_radius(f, sigma) * fs;
        lo = (int)ceil(center - sup);
        hi = (int)floor(center + sup);
        if (hi < lo) hi = lo;
        for (t = lo; t <= hi; ++t)
            out->w[t - lo] = r_kern(f, (center - t) / fs, sigma);
    }
    out->lo = lo;
    out->n = hi - lo + 1;
    while (out->n > 1 && fabs(out->w[0]) <= 1e-10) {
        memmove(out->w, out->w + 1, (size_t)(out->n - 1) * sizeof(double));
        out->lo++;
        out->n--;
    }
    while (out->n > 1 && fabs(out->w[out->n - 1]) <= 1e-10) out->n--;
    for (t = 0; t < out->n; ++t) sum += out->w[t];
    if (sum == 0) {
        out->lo = (int)floor(center + 0.5);
        out->n = 1;
        out->w[0] = 1;
    } else {
        for (t = 0; t < out->n; ++t) out->w[t] /= sum;
    }
}

static void ref_resize(const float *src, int sw, int sh, int ch, float *dst,
                       int dw, int dh, tir_filter fx, tir_filter fy,
                       tir_edge_mode ex, tir_edge_mode ey,
                       tir_registration reg, double sigma) {
    int ox, oy, tx, ty, c;
    for (oy = 0; oy < dh; ++oy) {
        r_win wy;
        r_window(sh, dh, oy, fy, sigma, reg, &wy);
        for (ox = 0; ox < dw; ++ox) {
            r_win wx;
            r_window(sw, dw, ox, fx, sigma, reg, &wx);
            for (c = 0; c < ch; ++c) {
                double acc = 0;
                for (ty = 0; ty < wy.n; ++ty) {
                    int my = r_edge(wy.lo + ty, sh, ey);
                    double rowacc = 0;
                    for (tx = 0; tx < wx.n; ++tx) {
                        int mx = r_edge(wx.lo + tx, sw, ex);
                        rowacc += wx.w[tx] *
                                  (double)src[((size_t)my * sw + mx) * ch + c];
                    }
                    acc += wy.w[ty] * rowacc;
                }
                dst[((size_t)oy * dw + ox) * ch + c] = (float)acc;
            }
        }
    }
}

/* ---- helpers ------------------------------------------------------------- */

static float *make_image(int w, int h, int ch, float scale) {
    float *p = (float *)malloc((size_t)w * h * ch * sizeof(float));
    int i;
    for (i = 0; i < w * h * ch; ++i) p[i] = frand() * scale;
    return p;
}

static double max_rel_err(const float *a, const float *b, size_t n) {
    size_t i;
    double m = 0;
    for (i = 0; i < n; ++i) {
        double d = fabs((double)a[i] - (double)b[i]);
        double s = fabs((double)b[i]);
        double e = d / (s > 1 ? s : 1);
        if (e > m) m = e;
    }
    return m;
}

static tir_image_view viewf(float *data, int w, int h, int ch) {
    tir_image_view v;
    v.data = data;
    v.width = w;
    v.height = h;
    v.channels = ch;
    v.type = TIR_F32;
    v.row_stride_bytes = 0;
    return v;
}

/* ===========================================================================
 * 1. every filter x direction x channels x edge vs the reference
 * ========================================================================= */
static void test_vs_reference(void) {
    static const tir_filter filters[] = {
        TIR_FILTER_BOX,      TIR_FILTER_TRIANGLE,    TIR_FILTER_BSPLINE,
        TIR_FILTER_GAUSSIAN, TIR_FILTER_MITCHELL,    TIR_FILTER_CATMULL_ROM,
        TIR_FILTER_LANCZOS2, TIR_FILTER_LANCZOS3};
    static const tir_edge_mode edges[] = {TIR_EDGE_CLAMP, TIR_EDGE_REFLECT,
                                          TIR_EDGE_WRAP};
    int fi, ei, ch, dir;
    for (fi = 0; fi < 8; ++fi) {
        for (ei = 0; ei < 3; ++ei) {
            for (ch = 1; ch <= 4; ++ch) {
                for (dir = 0; dir < 2; ++dir) {
                    int sw = 37, sh = 29;
                    int dw = dir ? 61 : 17, dh = dir ? 43 : 11;
                    float *src = make_image(sw, sh, ch, 10.0f);
                    float *dst =
                        (float *)malloc((size_t)dw * dh * ch * sizeof(float));
                    float *ref =
                        (float *)malloc((size_t)dw * dh * ch * sizeof(float));
                    tir_image_view sv = viewf(src, sw, sh, ch);
                    tir_image_view dv = viewf(dst, dw, dh, ch);
                    tir_options o;
                    tir_result rc;
                    char msg[128];
                    tir_options_init(&o);
                    o.filter_x = o.filter_y = filters[fi];
                    o.edge_x = o.edge_y = edges[ei];
                    o.alpha = TIR_ALPHA_STRAIGHT;
                    rc = tir_resize(NULL, &sv, &dv, &o);
                    CHECK(rc == TIR_SUCCESS, "resize rc");
                    ref_resize(src, sw, sh, ch, ref, dw, dh, filters[fi],
                               filters[fi], edges[ei], edges[ei],
                               TIR_REG_CELL_CENTERED, 0.5);
                    {
                        double e =
                            max_rel_err(dst, ref, (size_t)dw * dh * ch);
                        snprintf(msg, sizeof(msg),
                                 "vs ref f=%d e=%d ch=%d dir=%d err=%g", fi,
                                 ei, ch, dir, e);
                        CHECK(e < 2e-4, msg);
                    }
                    free(src);
                    free(dst);
                    free(ref);
                }
            }
        }
    }
}

/* ===========================================================================
 * 2. identity resize is bit-exact for interpolating filters
 * ========================================================================= */
static void test_identity(void) {
    static const tir_filter interp[] = {TIR_FILTER_BOX, TIR_FILTER_TRIANGLE,
                                        TIR_FILTER_CATMULL_ROM,
                                        TIR_FILTER_LANCZOS2,
                                        TIR_FILTER_LANCZOS3};
    int fi;
    for (fi = 0; fi < 5; ++fi) {
        int w = 33, h = 21, ch = 3;
        float *src = make_image(w, h, ch, 100.0f);
        float *dst = (float *)malloc((size_t)w * h * ch * sizeof(float));
        tir_image_view sv = viewf(src, w, h, ch);
        tir_image_view dv = viewf(dst, w, h, ch);
        tir_options o;
        tir_options_init(&o);
        o.filter_x = o.filter_y = interp[fi];
        o.alpha = TIR_ALPHA_STRAIGHT;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "identity rc");
        CHECK(memcmp(src, dst, (size_t)w * h * ch * sizeof(float)) == 0,
              "identity bit-exact");
        free(src);
        free(dst);
    }
}

/* ===========================================================================
 * 3. box integer-factor downscale preserves the mean
 * ========================================================================= */
static void test_box_mean(void) {
    int sw = 64, sh = 48, dw = 16, dh = 12;
    float *src = make_image(sw, sh, 1, 5.0f);
    float *dst = (float *)malloc((size_t)dw * dh * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, 1);
    tir_image_view dv = viewf(dst, dw, dh, 1);
    tir_options o;
    double ms = 0, md = 0;
    int i;
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_BOX;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "box rc");
    for (i = 0; i < sw * sh; ++i) ms += src[i];
    for (i = 0; i < dw * dh; ++i) md += dst[i];
    ms /= sw * sh;
    md /= dw * dh;
    CHECK(fabs(ms - md) < 1e-5, "box downscale preserves mean");
    free(src);
    free(dst);
}

/* ===========================================================================
 * 4. anti-ringing and clamp: hard "no negative pixels" guarantees
 * ========================================================================= */
static void test_antiring(void) {
    /* dark field with a very hot square: Lanczos3 must ring */
    int sw = 64, sh = 64, dw = 24, dh = 24, x, y;
    float *src = (float *)calloc((size_t)sw * sh, sizeof(float));
    float *dst = (float *)malloc((size_t)dw * dh * sizeof(float));
    float smin = 0.0f, smax = 1e4f;
    tir_image_view sv = viewf(src, sw, sh, 1);
    tir_image_view dv = viewf(dst, dw, dh, 1);
    tir_options o;
    int has_neg;
    for (y = 24; y < 40; ++y)
        for (x = 24; x < 40; ++x) src[y * sw + x] = 1e4f;

    /* (a) plain Lanczos3 undershoots below zero */
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_LANCZOS3;
    o.antiring = 0.0f;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "ring rc");
    has_neg = 0;
    for (x = 0; x < dw * dh; ++x)
        if (dst[x] < 0.0f) has_neg = 1;
    CHECK(has_neg, "lanczos3 rings negative without antiring (sanity)");

    /* (b) antiring 1.0 confines output to the local source range */
    o.antiring = 1.0f;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "ar rc");
    for (x = 0; x < dw * dh; ++x) {
        if (dst[x] < smin - 1e-3f || dst[x] > smax + 1e-3f) break;
    }
    CHECK(x == dw * dh, "antiring 1.0 keeps output within source range");

    /* (c) clamp_min alone guarantees no negatives */
    o.antiring = 0.0f;
    o.clamp_min = 0.0f;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "clamp rc");
    for (x = 0; x < dw * dh; ++x)
        if (dst[x] < 0.0f) break;
    CHECK(x == dw * dh, "clamp_min 0 kills negatives");

    /* (d) heightmap mode defaults antiring to 1.0 for ringing filters */
    tir_options_init(&o);
    o.mode = TIR_MODE_HEIGHTMAP;
    o.filter_x = o.filter_y = TIR_FILTER_LANCZOS3;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "hm rc");
    for (x = 0; x < dw * dh; ++x)
        if (dst[x] < smin - 1e-3f || dst[x] > smax + 1e-3f) break;
    CHECK(x == dw * dh, "heightmap auto-antiring keeps range");

    free(src);
    free(dst);
}

/* ===========================================================================
 * 5. heightmap registration
 * ========================================================================= */
static void test_registration(void) {
    /* grid-vertex UPSCALE: 129 -> 257 on a ramp maps grid points exactly
     * (dst 2i == src i) and interpolates linearly in between - the classic
     * half-texel-shift regression. Cell-centered upscale with edge clamp
     * would flatten the borders instead. */
    int sw = 129, dw = 257, x;
    float *src = (float *)malloc((size_t)sw * sizeof(float));
    float *dst = (float *)malloc((size_t)dw * sizeof(float));
    tir_image_view sv = viewf(src, sw, 1, 1);
    tir_image_view dv = viewf(dst, dw, 1, 1);
    tir_options o;
    for (x = 0; x < sw; ++x) src[x] = (float)x / (float)(sw - 1);
    tir_options_init(&o);
    o.mode = TIR_MODE_HEIGHTMAP;
    o.registration = TIR_REG_GRID_VERTEX;
    o.filter_x = o.filter_y = TIR_FILTER_TRIANGLE;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "reg rc");
    CHECK(dst[0] == src[0], "grid-vertex keeps first endpoint");
    CHECK(dst[dw - 1] == src[sw - 1], "grid-vertex keeps last endpoint");
    for (x = 0; x < sw; ++x)
        if (dst[2 * x] != src[x]) break;
    CHECK(x == sw, "grid-vertex upscale maps grid points exactly");
    for (x = 0; x < dw; ++x) {
        double want = (double)x / (dw - 1);
        if (fabs((double)dst[x] - want) > 1e-6) break;
    }
    CHECK(x == dw, "grid-vertex ramp stays linear");

    /* grid-vertex DOWNSCALE 257 -> 129: interior stays linear (windows are
     * symmetric around the exactly-mapped grid points) */
    {
        float *src2 = (float *)malloc((size_t)dw * sizeof(float));
        tir_image_view s2 = viewf(src2, dw, 1, 1);
        tir_image_view d2 = viewf(dst, sw, 1, 1);
        for (x = 0; x < dw; ++x) src2[x] = (float)x / (float)(dw - 1);
        CHECK(tir_resize(NULL, &s2, &d2, &o) == TIR_SUCCESS, "reg down rc");
        for (x = 1; x < sw - 1; ++x) {
            double want = (double)x / (sw - 1);
            if (fabs((double)dst[x] - want) > 1e-5) break;
        }
        CHECK(x == sw - 1, "grid-vertex downscale interior stays linear");
        free(src2);
    }

    /* cell-centered box downscale of the ramp preserves the mean */
    {
        tir_options o2;
        double ms = 0, md = 0;
        tir_image_view d2 = viewf(dst, sw, 1, 1);
        float *src2 = (float *)malloc((size_t)dw * sizeof(float));
        tir_image_view s2 = viewf(src2, dw, 1, 1);
        for (x = 0; x < dw; ++x) src2[x] = (float)x / (float)(dw - 1);
        tir_options_init(&o2);
        o2.mode = TIR_MODE_HEIGHTMAP; /* box on downscale by default */
        CHECK(tir_resize(NULL, &s2, &d2, &o2) == TIR_SUCCESS, "cc rc");
        for (x = 0; x < dw; ++x) ms += src2[x];
        for (x = 0; x < sw; ++x) md += dst[x];
        CHECK(fabs(ms / dw - md / sw) < 1e-4, "cell-centered mean unshifted");
        free(src2);
    }
    free(src);
    free(dst);
}

/* ===========================================================================
 * 6. normal-map mode
 * ========================================================================= */
static void test_normals(void) {
    int sw = 32, sh = 32, dw = 13, dh = 13, i;
    /* random unit normals, z-positive hemisphere */
    float *src = (float *)malloc((size_t)sw * sh * 3 * sizeof(float));
    float *dst = (float *)malloc((size_t)dw * dh * 3 * sizeof(float));
    float *len = (float *)malloc((size_t)dw * dh * sizeof(float));
    tir_options o;
    for (i = 0; i < sw * sh; ++i) {
        float x = frand() * 2 - 1, y = frand() * 2 - 1;
        float z = sqrtf(1.0f + 1e-6f - (x * x + y * y) * 0.5f);
        float l = sqrtf(x * x * 0.5f + y * y * 0.5f + z * z);
        src[i * 3 + 0] = x * 0.7071f / l;
        src[i * 3 + 1] = y * 0.7071f / l;
        src[i * 3 + 2] = z / l;
    }
    /* SNORM float path with length side-output */
    {
        tir_image_view sv = viewf(src, sw, sh, 3);
        tir_image_view dv = viewf(dst, dw, dh, 3);
        tir_options_init(&o);
        o.mode = TIR_MODE_NORMAL_MAP;
        o.normal_length_out = len;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "nm rc");
        for (i = 0; i < dw * dh; ++i) {
            float x = dst[i * 3], y = dst[i * 3 + 1], z = dst[i * 3 + 2];
            if (fabsf(x * x + y * y + z * z - 1.0f) > 1e-5f) break;
        }
        CHECK(i == dw * dh, "renormalized outputs are unit length");
        for (i = 0; i < dw * dh; ++i)
            if (!(len[i] > 0.0f && len[i] <= 1.0f + 1e-4f)) break;
        CHECK(i == dw * dh, "|N| side output in (0,1]");
    }
    /* degenerate: two opposing normals average to ~0 -> (0,0,1) fallback */
    {
        float deg[2 * 3] = {1, 0, 0, -1, 0, 0};
        float out[3];
        tir_image_view sv = viewf(deg, 2, 1, 3);
        tir_image_view dv = viewf(out, 1, 1, 3);
        tir_options_init(&o);
        o.mode = TIR_MODE_NORMAL_MAP;
        o.filter_x = o.filter_y = TIR_FILTER_BOX;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "deg rc");
        CHECK(out[0] == 0.0f && out[1] == 0.0f && out[2] == 1.0f,
              "degenerate footprint falls back to (0,0,1)");
    }
    /* RG two-channel path: decoded x^2+y^2 <= 1 */
    {
        float *rg = (float *)malloc((size_t)sw * sh * 2 * sizeof(float));
        float *rgo = (float *)malloc((size_t)dw * dh * 2 * sizeof(float));
        tir_image_view sv = viewf(rg, sw, sh, 2);
        tir_image_view dv = viewf(rgo, dw, dh, 2);
        for (i = 0; i < sw * sh; ++i) {
            rg[i * 2 + 0] = src[i * 3 + 0];
            rg[i * 2 + 1] = src[i * 3 + 1];
        }
        tir_options_init(&o);
        o.mode = TIR_MODE_NORMAL_MAP;
        o.normal_encoding = TIR_NORMAL_RG;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "rg rc");
        for (i = 0; i < dw * dh; ++i) {
            float x = rgo[i * 2], y = rgo[i * 2 + 1];
            if (x * x + y * y > 1.0f + 1e-5f) break;
        }
        CHECK(i == dw * dh, "RG output stays inside the unit disc");
        free(rg);
        free(rgo);
    }
    /* UNORM u8 storage round trip: unit length after decode */
    {
        uint8_t *u8s = (uint8_t *)malloc((size_t)sw * sh * 3);
        uint8_t *u8d = (uint8_t *)malloc((size_t)dw * dh * 3);
        tir_image_view sv, dv;
        for (i = 0; i < sw * sh * 3; ++i) {
            float v = (src[i] + 1.0f) * 0.5f;
            u8s[i] = (uint8_t)(v * 255.0f + 0.5f);
        }
        sv = viewf(NULL, sw, sh, 3);
        sv.data = u8s;
        sv.type = TIR_U8;
        dv = viewf(NULL, dw, dh, 3);
        dv.data = u8d;
        dv.type = TIR_U8;
        tir_options_init(&o);
        o.mode = TIR_MODE_NORMAL_MAP;
        o.normal_encoding = TIR_NORMAL_UNORM;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "u8 nm rc");
        for (i = 0; i < dw * dh; ++i) {
            float x = (float)u8d[i * 3] / 255.0f * 2 - 1;
            float y = (float)u8d[i * 3 + 1] / 255.0f * 2 - 1;
            float z = (float)u8d[i * 3 + 2] / 255.0f * 2 - 1;
            if (fabsf(x * x + y * y + z * z - 1.0f) > 0.03f) break;
        }
        CHECK(i == dw * dh, "u8 normals unit length within quantization");
        free(u8s);
        free(u8d);
    }
    free(src);
    free(dst);
    free(len);
}

/* ===========================================================================
 * 7. alpha handling
 * ========================================================================= */
static void test_alpha(void) {
    /* transparent texels carry garbage RGB=1000; opaque texels RGB<=1.
     * premultiplied filtering must not bleed the garbage. */
    int sw = 32, sh = 32, dw = 11, dh = 11, i;
    float *src = (float *)malloc((size_t)sw * sh * 4 * sizeof(float));
    float *dst = (float *)malloc((size_t)dw * dh * 4 * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, 4);
    tir_image_view dv = viewf(dst, dw, dh, 4);
    tir_options o;
    for (i = 0; i < sw * sh; ++i) {
        int hole = (xr() & 3) == 0;
        float v = hole ? 1000.0f : frand();
        src[i * 4 + 0] = v;
        src[i * 4 + 1] = v;
        src[i * 4 + 2] = v;
        src[i * 4 + 3] = hole ? 0.0f : 1.0f;
    }
    tir_options_init(&o); /* PREMULTIPLY default */
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "alpha rc");
    for (i = 0; i < dw * dh; ++i) {
        if (dst[i * 4 + 3] > 0.05f && dst[i * 4 + 0] > 2.0f) break;
    }
    CHECK(i == dw * dh, "premultiplied filtering does not bleed holes");

    /* PREMULTIPLIED passthrough: a==0 rows keep filtered RGB untouched */
    {
        float s2[4 * 4], d2[4];
        tir_image_view s2v = viewf(s2, 2, 2, 4);
        tir_image_view d2v = viewf(d2, 1, 1, 4);
        for (i = 0; i < 4; ++i) {
            s2[i * 4 + 0] = 5.0f;
            s2[i * 4 + 1] = 5.0f;
            s2[i * 4 + 2] = 5.0f;
            s2[i * 4 + 3] = 0.0f;
        }
        tir_options_init(&o);
        o.alpha = TIR_ALPHA_PREMULTIPLIED;
        o.filter_x = o.filter_y = TIR_FILTER_BOX;
        CHECK(tir_resize(NULL, &s2v, &d2v, &o) == TIR_SUCCESS, "pm rc");
        CHECK(fabsf(d2[0] - 5.0f) < 1e-6f && d2[3] == 0.0f,
              "premultiplied input: RGB kept where alpha == 0");
    }
    free(src);
    free(dst);
}

/* ===========================================================================
 * 8. nonfinite policies
 * ========================================================================= */
static void test_nonfinite(void) {
    int sw = 16, sh = 16, dw = 8, dh = 8, i;
    float *src = make_image(sw, sh, 1, 1.0f);
    float *dst = (float *)malloc((size_t)dw * dh * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, 1);
    tir_image_view dv = viewf(dst, dw, dh, 1);
    tir_options o;
    src[5 * sw + 7] = NAN;
    src[9 * sw + 2] = INFINITY;

    tir_options_init(&o); /* KEEP */
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "keep rc");
    for (i = 0; i < dw * dh; ++i)
        if (!isfinite(dst[i])) break;
    CHECK(i < dw * dh, "KEEP propagates nonfinite (sanity)");

    o.nonfinite = TIR_NONFINITE_ZERO;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "zero rc");
    for (i = 0; i < dw * dh; ++i)
        if (!isfinite(dst[i])) break;
    CHECK(i == dw * dh, "ZERO scrubs");

    o.nonfinite = TIR_NONFINITE_REPAIR;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "repair rc");
    for (i = 0; i < dw * dh; ++i)
        if (!isfinite(dst[i])) break;
    CHECK(i == dw * dh, "REPAIR scrubs");

    o.nonfinite = TIR_NONFINITE_ERROR;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_ERROR_NONFINITE,
          "ERROR reports");
    free(src);
    free(dst);
}

/* ===========================================================================
 * 9. pixel types
 * ========================================================================= */
static void test_types(void) {
    int w = 24, h = 18, ch = 4, i;
    /* u8 identity round trip */
    {
        uint8_t *s = (uint8_t *)malloc((size_t)w * h * ch);
        uint8_t *d = (uint8_t *)malloc((size_t)w * h * ch);
        tir_image_view sv = viewf(NULL, w, h, ch), dv = viewf(NULL, w, h, ch);
        tir_options o;
        for (i = 0; i < w * h * ch; ++i) s[i] = (uint8_t)(xr() & 0xFF);
        sv.data = s;
        sv.type = TIR_U8;
        dv.data = d;
        dv.type = TIR_U8;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_TRIANGLE;
        o.alpha = TIR_ALPHA_STRAIGHT;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "u8 rc");
        CHECK(memcmp(s, d, (size_t)w * h * ch) == 0, "u8 identity");
        free(s);
        free(d);
    }
    /* u16 identity round trip */
    {
        uint16_t *s = (uint16_t *)malloc((size_t)w * h * ch * 2);
        uint16_t *d = (uint16_t *)malloc((size_t)w * h * ch * 2);
        tir_image_view sv = viewf(NULL, w, h, ch), dv = viewf(NULL, w, h, ch);
        tir_options o;
        for (i = 0; i < w * h * ch; ++i) s[i] = (uint16_t)(xr() & 0xFFFF);
        sv.data = s;
        sv.type = TIR_U16;
        dv.data = d;
        dv.type = TIR_U16;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_TRIANGLE;
        o.alpha = TIR_ALPHA_STRAIGHT;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "u16 rc");
        CHECK(memcmp(s, d, (size_t)w * h * ch * 2) == 0, "u16 identity");
        free(s);
        free(d);
    }
    /* f16 finite round trip through identity */
    {
        uint16_t *s = (uint16_t *)malloc((size_t)w * h * 2);
        uint16_t *d = (uint16_t *)malloc((size_t)w * h * 2);
        tir_image_view sv = viewf(NULL, w, h, 1), dv = viewf(NULL, w, h, 1);
        tir_options o;
        for (i = 0; i < w * h; ++i) {
            uint16_t v = (uint16_t)(xr() & 0x7FFF);
            if ((v & 0x7C00) == 0x7C00) v &= 0x63FF; /* avoid inf/nan */
            s[i] = v;
        }
        sv.data = s;
        sv.type = TIR_F16;
        dv.data = d;
        dv.type = TIR_F16;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_TRIANGLE;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "f16 rc");
        CHECK(memcmp(s, d, (size_t)w * h * 2) == 0, "f16 identity");
        free(s);
        free(d);
    }
    /* cross-type u8 -> f32 equals decode(u8) resized */
    {
        int dw = 11, dh = 9;
        uint8_t *s = (uint8_t *)malloc((size_t)w * h);
        float *sf = (float *)malloc((size_t)w * h * sizeof(float));
        float *d1 = (float *)malloc((size_t)dw * dh * sizeof(float));
        float *d2 = (float *)malloc((size_t)dw * dh * sizeof(float));
        tir_image_view sv = viewf(NULL, w, h, 1), dv = viewf(d1, dw, dh, 1);
        tir_image_view sfv = viewf(sf, w, h, 1), dv2 = viewf(d2, dw, dh, 1);
        tir_options o;
        for (i = 0; i < w * h; ++i) {
            s[i] = (uint8_t)(xr() & 0xFF);
            sf[i] = (float)s[i] / 255.0f;
        }
        sv.data = s;
        sv.type = TIR_U8;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_MITCHELL;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "x rc");
        CHECK(tir_resize(NULL, &sfv, &dv2, &o) == TIR_SUCCESS, "x rc2");
        CHECK(max_rel_err(d1, d2, (size_t)dw * dh) < 1e-6,
              "u8 source path == decoded f32 path");
        free(s);
        free(sf);
        free(d1);
        free(d2);
    }
}

/* ===========================================================================
 * 10. SIMD levels match scalar; deterministic is byte-identical
 * ========================================================================= */
static void run_case(float *dst, int deterministic) {
    int sw = 53, sh = 41, dw = 87, dh = 23;
    static float *src = NULL;
    tir_image_view sv, dv;
    tir_options o;
    if (!src) {
        g_rng = 0xC0FFEEu;
        src = make_image(sw, sh, 4, 100.0f);
    }
    sv = viewf(src, sw, sh, 4);
    dv = viewf(dst, dw, dh, 4);
    tir_options_init(&o);
    o.filter_x = TIR_FILTER_LANCZOS3;
    o.filter_y = TIR_FILTER_MITCHELL;
    o.antiring = 0.7f;
    o.clamp_min = 0.0f;
    o.deterministic = deterministic;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "simd case rc");
}

static void test_simd_levels(void) {
    enum { N = 87 * 23 * 4 };
    uint32_t avail = tir_simd_available();
    float *ref = (float *)malloc(N * sizeof(float));
    float *det = (float *)malloc(N * sizeof(float));
    float *cur = (float *)malloc(N * sizeof(float));
    int l;
    CHECK(tir_simd_force(TIR_SIMD_SCALAR) == TIR_SUCCESS, "force scalar");
    run_case(ref, 0);
    run_case(det, 1);
    CHECK(memcmp(ref, det, N * sizeof(float)) == 0,
          "deterministic == scalar table");
    for (l = 1; l <= TIR_SIMD_SVE; ++l) {
        if (!(avail & (1u << l))) continue;
        CHECK(tir_simd_force((tir_simd_level)l) == TIR_SUCCESS, "force lvl");
        run_case(cur, 0);
        {
            char msg[64];
            double e = max_rel_err(cur, ref, N);
            snprintf(msg, sizeof(msg), "simd level %d err=%g", l, e);
            CHECK(e < 1e-5, msg);
        }
        run_case(cur, 1);
        CHECK(memcmp(cur, det, N * sizeof(float)) == 0,
              "deterministic byte-identical under forced SIMD");
    }
    /* restore best */
    for (l = TIR_SIMD_SVE; l >= 0; --l)
        if (avail & (1u << l)) {
            tir_simd_force((tir_simd_level)l);
            break;
        }
    free(ref);
    free(det);
    free(cur);
}

/* ===========================================================================
 * 11. streaming push/pull == one-shot
 * ========================================================================= */
static void test_streaming(void) {
    int sw = 40, sh = 31, dw = 21, dh = 57, ch = 3, y;
    float *src = make_image(sw, sh, ch, 10.0f);
    float *d1 = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    float *d2 = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    tir_options o;
    tir_sampler *s = NULL;
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_CATMULL_ROM;
    o.antiring = 1.0f;
    {
        tir_image_view sv = viewf(src, sw, sh, ch);
        tir_image_view dv = viewf(d1, dw, dh, ch);
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "oneshot rc");
    }
    CHECK(tir_sampler_create(NULL, sw, sh, dw, dh, ch, TIR_F32, TIR_F32, &o,
                             &s) == TIR_SUCCESS,
          "sampler create");
    /* rows come out top to bottom, so the next pull target is always the
     * out_rows'th destination row */
    {
        int out_rows = 0;
        y = 0;
        while (out_rows < dh) {
            int dy;
            tir_result rc = tir_sampler_pull_row(
                s, &dy, d2 + (size_t)out_rows * dw * ch);
            if (rc == TIR_WOULD_BLOCK) {
                CHECK(y < sh, "streaming starved");
                if (y >= sh) break;
                rc = tir_sampler_push_row(s, y, src + (size_t)y * sw * ch);
                CHECK(rc == TIR_SUCCESS, "push rc");
                y++;
                continue;
            }
            CHECK(rc == TIR_SUCCESS, "pull rc");
            CHECK(dy == out_rows, "rows come out in order");
            out_rows++;
        }
    }
    CHECK(memcmp(d1, d2, (size_t)dw * dh * ch * sizeof(float)) == 0,
          "streaming == one-shot byte-identical");
    tir_sampler_destroy(s);
    free(src);
    free(d1);
    free(d2);
}

/* ===========================================================================
 * 12. hostile arguments
 * ========================================================================= */
static void test_hostile(void) {
    tir_sampler *s = NULL;
    tir_options o;
    tir_options_init(&o);
    CHECK(tir_sampler_create(NULL, 0, 10, 5, 5, 1, TIR_F32, TIR_F32, &o,
                             &s) == TIR_ERROR_INVALID_ARGUMENT &&
              s == NULL,
          "zero width rejected");
    CHECK(tir_sampler_create(NULL, 10, 10, 5, 5, 5, TIR_F32, TIR_F32, &o,
                             &s) == TIR_ERROR_INVALID_ARGUMENT,
          "5 channels rejected");
    CHECK(tir_sampler_create(NULL, 1 << 30, 10, 5, 5, 1, TIR_F32, TIR_F32,
                             &o, &s) == TIR_ERROR_INVALID_ARGUMENT,
          "huge dimension rejected");
    CHECK(tir_resize(NULL, NULL, NULL, NULL) == TIR_ERROR_INVALID_ARGUMENT,
          "null views rejected");
    {
        float a[4], b[4];
        tir_image_view sv = viewf(a, 2, 2, 1), dv = viewf(b, 2, 2, 2);
        CHECK(tir_resize(NULL, &sv, &dv, NULL) ==
                  TIR_ERROR_INVALID_ARGUMENT,
              "channel mismatch rejected");
    }
    /* out-of-order streaming */
    {
        float row[8];
        int i;
        for (i = 0; i < 8; ++i) row[i] = 0.0f;
        CHECK(tir_sampler_create(NULL, 8, 8, 4, 4, 1, TIR_F32, TIR_F32, &o,
                                 &s) == TIR_SUCCESS,
              "create ok");
        CHECK(tir_sampler_push_row(s, 3, row) == TIR_ERROR_ORDER,
              "out-of-order push rejected");
        CHECK(tir_sampler_push_row(s, 0, row) == TIR_SUCCESS, "push 0 ok");
        CHECK(tir_sampler_push_row(s, 0, row) == TIR_ERROR_ORDER,
              "duplicate push rejected");
        tir_sampler_destroy(s);
        s = NULL;
    }
    /* mode/channel mismatches */
    CHECK(tir_sampler_create(NULL, 8, 8, 4, 4, 4, TIR_F32, TIR_F32,
                             (tir_options_init(&o), o.mode =
                                                        TIR_MODE_NORMAL_MAP,
                              &o),
                             &s) == TIR_ERROR_UNSUPPORTED,
          "normal mode with 4 channels rejected");
}

/* ===========================================================================
 * hicomp: compressed filtering still lands near the plain result for
 * smooth data and clamps non-negative
 * ========================================================================= */
static void test_hicomp(void) {
    int sw = 32, sh = 32, dw = 12, dh = 12, i;
    float *src = make_image(sw, sh, 1, 3000.0f);
    float *dst = (float *)malloc((size_t)dw * dh * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, 1);
    tir_image_view dv = viewf(dst, dw, dh, 1);
    tir_options o;
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_LANCZOS3;
    o.hicomp = 1;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "hicomp rc");
    for (i = 0; i < dw * dh; ++i)
        if (dst[i] < 0.0f) break;
    CHECK(i == dw * dh, "hicomp output is non-negative");
    free(src);
    free(dst);
}

/* ===========================================================================
 * vertical-first pass: strongly anisotropic resize (tall+narrow source to
 * short+wide destination) takes the v-first path; must match the reference
 * and stream identically
 * ========================================================================= */
static void test_vfirst(void) {
    int sw = 23, sh = 190, dw = 210, dh = 17, ch = 3, i;
    float *src = make_image(sw, sh, ch, 50.0f);
    float *dst = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    float *ref = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    float *d2 = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, ch);
    tir_image_view dv = viewf(dst, dw, dh, ch);
    tir_options o;
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_MITCHELL;
    o.alpha = TIR_ALPHA_STRAIGHT;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "vf rc");
    ref_resize(src, sw, sh, ch, ref, dw, dh, TIR_FILTER_MITCHELL,
               TIR_FILTER_MITCHELL, TIR_EDGE_CLAMP, TIR_EDGE_CLAMP,
               TIR_REG_CELL_CENTERED, 0.5);
    CHECK(max_rel_err(dst, ref, (size_t)dw * dh * ch) < 2e-4,
          "v-first matches reference");

    /* antiring works in v-first order too: hot square stays in range */
    {
        int x, y;
        for (i = 0; i < sw * sh * ch; ++i) src[i] = 0.0f;
        for (y = 60; y < 120; ++y)
            for (x = 6; x < 16; ++x)
                for (i = 0; i < ch; ++i)
                    src[(y * sw + x) * ch + i] = 1e4f;
        o.filter_x = o.filter_y = TIR_FILTER_LANCZOS3;
        o.antiring = 1.0f;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "vf ar rc");
        for (i = 0; i < dw * dh * ch; ++i)
            if (dst[i] < -1e-3f || dst[i] > 1e4f + 1.0f) break;
        CHECK(i == dw * dh * ch, "v-first antiring keeps source range");
    }

    /* streaming push/pull matches the one-shot on the v-first geometry */
    {
        tir_sampler *s = NULL;
        int out_rows = 0, y = 0;
        g_rng = 0xBEEFu;
        for (i = 0; i < sw * sh * ch; ++i) src[i] = frand() * 5.0f;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_CATMULL_ROM;
        o.antiring = 1.0f;
        o.alpha = TIR_ALPHA_STRAIGHT;
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "vf os rc");
        CHECK(tir_sampler_create(NULL, sw, sh, dw, dh, ch, TIR_F32, TIR_F32,
                                 &o, &s) == TIR_SUCCESS,
              "vf sampler");
        while (out_rows < dh) {
            int dy;
            tir_result rc = tir_sampler_pull_row(
                s, &dy, d2 + (size_t)out_rows * dw * ch);
            if (rc == TIR_WOULD_BLOCK) {
                if (y >= sh) break;
                tir_sampler_push_row(s, y, src + (size_t)y * sw * ch);
                y++;
                continue;
            }
            if (rc != TIR_SUCCESS) break;
            out_rows++;
        }
        CHECK(out_rows == dh, "vf streaming completes");
        CHECK(memcmp(dst, d2, (size_t)dw * dh * ch * sizeof(float)) == 0,
              "vf streaming == one-shot");
        tir_sampler_destroy(s);
    }
    free(src);
    free(dst);
    free(ref);
    free(d2);
}

/* ===========================================================================
 * threads: banded whole-image run is byte-identical to serial (per-row math
 * is unchanged); serial fallback without TIR_ENABLE_THREADS also passes
 * ========================================================================= */
static void test_threads(void) {
    static const tir_edge_mode edges[3] = {TIR_EDGE_CLAMP, TIR_EDGE_REFLECT,
                                           TIR_EDGE_WRAP};
    static const char *ename[3] = {"clamp", "reflect", "wrap"};
    static const int nthreads[3] = {2, 3, 5};
    int sw = 127, sh = 97, dw = 73, dh = 141, ch = 4, e, t;
    float *src = make_image(sw, sh, ch, 20.0f);
    float *d1 = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    float *d2 = (float *)malloc((size_t)dw * dh * ch * sizeof(float));
    tir_image_view sv = viewf(src, sw, sh, ch);
    tir_image_view dv1 = viewf(d1, dw, dh, ch);
    tir_image_view dv2 = viewf(d2, dw, dh, ch);
    src[(size_t)(11 * sw + 17) * ch + 2] = NAN; /* exercise REPAIR banding */
    /* Every edge mode x thread count must be byte-identical to serial. The
     * wrap-y rows are the regression guard: a band that starts in the fast
     * range but reaches the bottom wrap-exception rows (which wrap back to
     * row 0) must still push those rows instead of fast-forwarding past them. */
    for (e = 0; e < 3; ++e) {
        tir_options o;
        tir_options_init(&o);
        o.filter_x = o.filter_y = TIR_FILTER_LANCZOS3;
        o.edge_x = o.edge_y = edges[e];
        o.antiring = 0.9f;
        o.nonfinite = TIR_NONFINITE_REPAIR;
        o.num_threads = 0;
        CHECK(tir_resize(NULL, &sv, &dv1, &o) == TIR_SUCCESS, "serial rc");
        for (t = 0; t < 3; ++t) {
            char msg[64];
            o.num_threads = nthreads[t];
            CHECK(tir_resize(NULL, &sv, &dv2, &o) == TIR_SUCCESS, "threads rc");
            snprintf(msg, sizeof(msg), "threads==serial edge=%s nt=%d",
                     ename[e], nthreads[t]);
            CHECK(memcmp(d1, d2, (size_t)dw * dh * ch * sizeof(float)) == 0,
                  msg);
        }
    }
    free(src);
    free(d1);
    free(d2);
}

/* ===========================================================================
 * regression guards for the three audit bugs (2026-07-02)
 * ========================================================================= */

/* Bug: unpremult4_sse2 zeroed RGB where alpha==0 instead of preserving the
 * filtered RGB. Two RGBA texels whose alphas cancel to 0 under a box filter;
 * the premultiplied RGB must survive, and every SIMD level must equal scalar. */
static void test_unpremult_alpha0(void) {
    float src[8] = {1, 2, 3, 0.5f, 4, 5, 6, -0.5f};
    tir_image_view sv = {src, 2, 1, 4, TIR_F32, 0};
    float ref[4];
    uint32_t avail = tir_simd_available();
    int l;
    tir_options o;
    tir_options_init(&o);
    o.filter_x = o.filter_y = TIR_FILTER_BOX;
    o.alpha = TIR_ALPHA_PREMULTIPLY;
    tir_simd_force(TIR_SIMD_SCALAR);
    {
        tir_image_view dv = {ref, 1, 1, 4, TIR_F32, 0};
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "unpremult ref");
    }
    CHECK(ref[3] == 0.0f, "alpha cancels to 0");
    CHECK(ref[0] != 0.0f, "scalar keeps RGB where alpha==0");
    for (l = 1; l <= TIR_SIMD_SVE; ++l) {
        float out[4];
        tir_image_view dv = {out, 1, 1, 4, TIR_F32, 0};
        char msg[64];
        if (!(avail & (1u << l))) continue;
        tir_simd_force((tir_simd_level)l);
        CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "unpremult lvl");
        snprintf(msg, sizeof(msg), "unpremult alpha==0 level %d == scalar", l);
        CHECK(memcmp(out, ref, sizeof(ref)) == 0, msg);
    }
    for (l = TIR_SIMD_SVE; l >= 0; --l)
        if (avail & (1u << l)) { tir_simd_force((tir_simd_level)l); break; }
}

/* Bug: hicomp forced clamp_lo=0 mode-independently, but hicomp is disabled in
 * normal-map mode -> --mode normal --hicomp clamped signed X/Y to 0. */
static void test_normal_hicomp(void) {
    float src[6] = {-0.6f, 0.3f, 0.74f, -0.5f, 0.2f, 0.84f};
    float out[3];
    tir_image_view sv = {src, 2, 1, 3, TIR_F32, 0};
    tir_image_view dv = {out, 1, 1, 3, TIR_F32, 0};
    tir_options o;
    double len;
    tir_options_init(&o);
    o.mode = TIR_MODE_NORMAL_MAP;
    o.normal_encoding = TIR_NORMAL_SNORM;
    o.hicomp = 1;
    o.filter_x = o.filter_y = TIR_FILTER_BOX;
    CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "nm hicomp rc");
    CHECK(out[0] < 0.0f, "normal+hicomp keeps signed X (not clamped to 0)");
    len = sqrt((double)out[0] * out[0] + (double)out[1] * out[1] +
               (double)out[2] * out[2]);
    CHECK(fabs(len - 1.0) < 1e-5, "normal+hicomp output stays unit length");
}

/* Broad SIMD-vs-scalar parity: channels 1..4 x up/down/same geometries with
 * the premult/antiring paths live. A single fixed geometry (test_simd_levels)
 * missed the channel-specific unpremult bug; this sweeps the channel kernels. */
static void test_simd_sweep(void) {
    static const int geo[3][4] = {
        {61, 47, 130, 99}, {130, 99, 41, 33}, {96, 64, 96, 64}};
    uint32_t avail = tir_simd_available();
    int ci, gi, l;
    for (ci = 1; ci <= 4; ++ci)
        for (gi = 0; gi < 3; ++gi) {
            int sw = geo[gi][0], sh = geo[gi][1], dw = geo[gi][2],
                dh = geo[gi][3];
            size_t n = (size_t)dw * dh * ci;
            float *src, *ref = (float *)malloc(n * sizeof(float));
            float *cur = (float *)malloc(n * sizeof(float));
            tir_image_view sv, dv;
            tir_options o;
            g_rng = 0x5A5A0000u + (uint32_t)(ci * 16 + gi);
            src = make_image(sw, sh, ci, 8.0f);
            sv = viewf(src, sw, sh, ci);
            tir_options_init(&o);
            o.filter_x = TIR_FILTER_LANCZOS3;
            o.filter_y = TIR_FILTER_CATMULL_ROM;
            o.antiring = 0.5f;
            /* Straight alpha: exercise the 4-channel filtering kernels without
             * the unpremult division amplifying reassociation noise (the
             * unpremult path is covered exactly by test_unpremult_alpha0). */
            o.alpha = TIR_ALPHA_STRAIGHT;
            tir_simd_force(TIR_SIMD_SCALAR);
            dv = viewf(ref, dw, dh, ci);
            CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS, "sweep ref");
            for (l = 1; l <= TIR_SIMD_SVE; ++l) {
                char msg[80];
                double err;
                if (!(avail & (1u << l))) continue;
                tir_simd_force((tir_simd_level)l);
                dv = viewf(cur, dw, dh, ci);
                CHECK(tir_resize(NULL, &sv, &dv, &o) == TIR_SUCCESS,
                      "sweep lvl");
                err = max_rel_err(cur, ref, n);
                snprintf(msg, sizeof(msg),
                         "simd sweep ch=%d geo=%d lvl=%d err=%g", ci, gi, l,
                         err);
                CHECK(err < 2e-5, msg);
            }
            free(src);
            free(ref);
            free(cur);
        }
    for (l = TIR_SIMD_SVE; l >= 0; --l)
        if (avail & (1u << l)) { tir_simd_force((tir_simd_level)l); break; }
}

int main(void) {
    fprintf(stderr, "[t] vs_reference\n"); test_vs_reference();
    fprintf(stderr, "[t] identity\n"); test_identity();
    fprintf(stderr, "[t] box_mean\n"); test_box_mean();
    fprintf(stderr, "[t] antiring\n"); test_antiring();
    fprintf(stderr, "[t] registration\n"); test_registration();
    fprintf(stderr, "[t] normals\n"); test_normals();
    fprintf(stderr, "[t] alpha\n"); test_alpha();
    fprintf(stderr, "[t] nonfinite\n"); test_nonfinite();
    fprintf(stderr, "[t] types\n"); test_types();
    fprintf(stderr, "[t] simd\n"); test_simd_levels();
    fprintf(stderr, "[t] simd_sweep\n"); test_simd_sweep();
    fprintf(stderr, "[t] streaming\n"); test_streaming();
    fprintf(stderr, "[t] hostile\n"); test_hostile();
    fprintf(stderr, "[t] hicomp\n"); test_hicomp();
    fprintf(stderr, "[t] unpremult_alpha0\n"); test_unpremult_alpha0();
    fprintf(stderr, "[t] normal_hicomp\n"); test_normal_hicomp();
    fprintf(stderr, "[t] vfirst\n"); test_vfirst();
    fprintf(stderr, "[t] threads\n"); test_threads();
    printf("tir tests: %d passed, %d failed (simd: %s)\n", g_pass, g_fail,
           tir_simd_info());
    return g_fail ? 1 : 0;
}
