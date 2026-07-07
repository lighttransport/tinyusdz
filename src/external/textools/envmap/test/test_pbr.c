/*
 * TinyEXR envmap - PBR validation harness.
 *
 * Builds a float IBL from a synthetic env, then shades a set of surface samples
 * under it with SOURCE vs BC7-compressed-then-decoded material, and reports the
 * shaded-image PSNR plus normal-map angular error. This is the "does texture
 * compression hurt the final PBR image" gate.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "envmap.h"
#include "texcomp.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m)                                                            \
    do { if (!(c)) { printf("FAIL: %s (%s:%d)\n", (m), __FILE__, __LINE__); g_fail = 1; } } while (0)

/* Synthetic HDR env: dim ambient + two bright directional spots. */
static void make_env(em_image *eq) {
    int x, y;
    em_image_alloc(NULL, eq, EM_PROJ_EQUIRECT, 128, 64, 3);
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 128; ++x) {
            float u = (x + 0.5f) / 128.0f, v = (y + 0.5f) / 64.0f, d[3];
            float *t = em_image_texel(eq, 0, x, y);
            float val = 0.3f;
            em_uv_to_dir(EM_PROJ_EQUIRECT, 0, u, v, d);
            if (d[1] > 0.85f) val += 6.0f;             /* overhead light */
            if (d[0] > 0.9f) val += 3.0f;              /* side light */
            t[0] = val; t[1] = val * 0.95f; t[2] = val * 0.9f;
        }
}

static double psnr(const float *a, const float *b, int n) {
    double mse = 0.0;
    int i;
    for (i = 0; i < n; ++i) { double d = a[i] - b[i]; mse += d * d; }
    mse /= n;
    if (mse <= 0.0) return 1e9;
    return 10.0 * log10(1.0 / mse); /* signals ~[0,1]-ish */
}

/* Grid of sphere normals facing V=+Z. */
static int sphere_normals(float *N, int grid) {
    int c = 0, i, j;
    for (j = 0; j < grid; ++j)
        for (i = 0; i < grid; ++i) {
            float sx = (i + 0.5f) / grid * 2.0f - 1.0f;
            float sy = (j + 0.5f) / grid * 2.0f - 1.0f;
            float r2 = sx * sx + sy * sy;
            if (r2 > 1.0f) continue;
            N[c * 3 + 0] = sx; N[c * 3 + 1] = sy; N[c * 3 + 2] = sqrtf(1.0f - r2);
            ++c;
        }
    return c;
}

static void test_albedo_compression_shading(void) {
    em_image env, spec[8], irr;
    float *brdf;
    int levels = 6, lut = 32, i, np;
    /* material albedo texture (RGBA8) */
    int W = 64, H = 64;
    uint8_t *alb = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *dec = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *bc7 = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    float *Ns, *shade_src, *shade_dec;
    int x, y, grid = 48;
    double p;

    make_env(&env);
    CHECK(EM_OK(em_prefilter_specular(NULL, &env, 32, levels, 32, spec)), "prefilter");
    memset(&irr, 0, sizeof(irr));
    CHECK(EM_OK(em_irradiance_cube(NULL, &env, 16, 64, &irr)), "irradiance");
    brdf = (float *)malloc((size_t)lut * lut * 2 * sizeof(float));
    em_brdf_lut(lut, 128, brdf);

    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            uint8_t *p2 = alb + (y * W + x) * 4;
            p2[0] = (uint8_t)(40 + x * 3); p2[1] = (uint8_t)(30 + y * 3);
            p2[2] = (uint8_t)(120 + ((x + y) & 63)); p2[3] = 255;
        }
    CHECK(tc_bc7_compress_rgba8(alb, W, H, W * 4, NULL, bc7, tc_bc7_compressed_size(W, H)) == TC_SUCCESS, "bc7 encode albedo");
    CHECK(tc_bc7_decompress_rgba8(bc7, W, H, W * 4, dec, (size_t)W * H * 4) == TC_SUCCESS, "bc7 decode albedo");

    Ns = (float *)malloc((size_t)grid * grid * 3 * sizeof(float));
    np = sphere_normals(Ns, grid);
    shade_src = (float *)malloc((size_t)np * 3 * sizeof(float));
    shade_dec = (float *)malloc((size_t)np * 3 * sizeof(float));
    for (i = 0; i < np; ++i) {
        float V[3] = {0, 0, 1};
        const float *N = &Ns[i * 3];
        /* sample albedo at the normal's screen position */
        int tx = (int)((N[0] * 0.5f + 0.5f) * (W - 1));
        int ty = (int)((N[1] * 0.5f + 0.5f) * (H - 1));
        const uint8_t *as = alb + (ty * W + tx) * 4;
        const uint8_t *ad = dec + (ty * W + tx) * 4;
        float alb_s[3] = {as[0] / 255.0f, as[1] / 255.0f, as[2] / 255.0f};
        float alb_d[3] = {ad[0] / 255.0f, ad[1] / 255.0f, ad[2] / 255.0f};
        float rough = 0.35f, metal = (i & 8) ? 1.0f : 0.0f;
        em_shade_point(spec, levels, &irr, brdf, lut, N, V, alb_s, rough, metal, &shade_src[i * 3]);
        em_shade_point(spec, levels, &irr, brdf, lut, N, V, alb_d, rough, metal, &shade_dec[i * 3]);
    }
    p = psnr(shade_src, shade_dec, np * 3);
    printf("  albedo BC7 -> PBR shade PSNR = %.1f dB (%d samples)\n", p, np);
    CHECK(p >= 40.0, "BC7 albedo barely changes the shaded image (>=40 dB)");

    for (i = 0; i < levels; ++i) em_image_free(NULL, &spec[i]);
    em_image_free(NULL, &irr);
    em_image_free(NULL, &env);
    free(brdf); free(alb); free(dec); free(bc7); free(Ns); free(shade_src); free(shade_dec);
}

static void test_normal_bc7_angular_error(void) {
    /* Encode a UNORM normal map to BC7, decode, report max angular error. */
    int W = 64, H = 64, x, y;
    uint8_t *nm = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *dec = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *bc7 = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    double max_deg = 0.0, mean_deg = 0.0;
    int cnt = 0;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            float fx = (x / (float)(W - 1)) * 2 - 1, fy = (y / (float)(H - 1)) * 2 - 1;
            float nx = 0.6f * fx, ny = 0.6f * fy, nz = 1.0f, l = sqrtf(nx*nx+ny*ny+nz*nz);
            uint8_t *p = nm + (y * W + x) * 4;
            nx /= l; ny /= l; nz /= l;
            p[0] = (uint8_t)((nx * 0.5f + 0.5f) * 255); p[1] = (uint8_t)((ny * 0.5f + 0.5f) * 255);
            p[2] = (uint8_t)((nz * 0.5f + 0.5f) * 255); p[3] = 255;
        }
    tc_bc7_compress_rgba8(nm, W, H, W * 4, NULL, bc7, tc_bc7_compressed_size(W, H));
    tc_bc7_decompress_rgba8(bc7, W, H, W * 4, dec, (size_t)W * H * 4);
    for (y = 0; y < H * W; ++y) {
        float a[3], b[3], la, lb, d, ang;
        int k;
        for (k = 0; k < 3; ++k) { a[k] = nm[y*4+k]/255.0f*2-1; b[k] = dec[y*4+k]/255.0f*2-1; }
        la = sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]); lb = sqrtf(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
        if (la < 1e-4f || lb < 1e-4f) continue;
        d = (a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb);
        if (d > 1) d = 1;
        if (d < -1) d = -1;
        ang = acosf(d) * 180.0f / 3.14159265f;
        if (ang > max_deg) max_deg = ang;
        mean_deg += ang; ++cnt;
    }
    mean_deg /= cnt;
    printf("  normal BC7 angular error: mean=%.2f deg max=%.2f deg\n", mean_deg, max_deg);
    CHECK(max_deg < 10.0, "BC7 normal angular error bounded (informational; BC5 is better)");
    free(nm); free(dec); free(bc7);
}

/* BC4 block decode matching texcomp's encoder (endpoint0=max, endpoint1=min,
 * 8-value interpolation, 48 bits of 3-bit indices). */
static void bc4_decode(const uint8_t *blk, uint8_t out[16]) {
    int mx = blk[0], mn = blk[1], pal[8], i;
    uint64_t bits = 0;
    pal[0] = mx; pal[1] = mn;
    for (i = 1; i <= 6; ++i) pal[i + 1] = ((7 - i) * mx + i * mn + 3) / 7;
    for (i = 0; i < 6; ++i) bits |= (uint64_t)blk[2 + i] << (8 * i);
    for (i = 0; i < 16; ++i) out[i] = (uint8_t)pal[(bits >> (3 * i)) & 7];
}

static double normal_angle_deg(const float a[3], const float b[3]) {
    float la = sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);
    float lb = sqrtf(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]), d;
    if (la < 1e-6f || lb < 1e-6f) return 0.0;
    d = (a[0]*b[0]+a[1]*b[1]+a[2]*b[2])/(la*lb);
    if (d > 1) d = 1;
    if (d < -1) d = -1;
    return acosf(d) * 180.0 / 3.14159265;
}

/* PBR-tuned compression: BC5 (2-channel, reconstruct Z) should beat BC7 for
 * normal maps. Encode a normal map both ways, decode, compare angular error. */
static void test_normal_bc5_vs_bc7(void) {
    int W = 64, H = 64, x, y, bx, by;
    uint8_t *nm = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *rg = (uint8_t *)malloc((size_t)W * H * 2);
    uint8_t *bc5 = (uint8_t *)malloc(tc_bc5_compressed_size(W, H));
    uint8_t *bc7 = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    uint8_t *dec7 = (uint8_t *)malloc((size_t)W * H * 4);
    double bc5_mean = 0, bc7_mean = 0, bc5_max = 0, bc7_max = 0;
    int cnt = 0;
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            float fx = (x / (float)(W - 1)) * 2 - 1, fy = (y / (float)(H - 1)) * 2 - 1;
            float nx = 0.7f * sinf(fx * 3.0f), ny = 0.7f * sinf(fy * 3.0f), nz = 1.0f;
            float l = sqrtf(nx*nx+ny*ny+nz*nz);
            uint8_t *p = nm + (y * W + x) * 4;
            nx /= l; ny /= l; nz /= l;
            p[0] = (uint8_t)((nx*0.5f+0.5f)*255); p[1] = (uint8_t)((ny*0.5f+0.5f)*255);
            p[2] = (uint8_t)((nz*0.5f+0.5f)*255); p[3] = 255;
            rg[(y * W + x) * 2 + 0] = p[0];
            rg[(y * W + x) * 2 + 1] = p[1];
        }
    tc_bc5_compress_rg8(rg, W, H, W * 2, NULL, bc5, tc_bc5_compressed_size(W, H));
    tc_bc7_compress_rgba8(nm, W, H, W * 4, NULL, bc7, tc_bc7_compressed_size(W, H));
    tc_bc7_decompress_rgba8(bc7, W, H, W * 4, dec7, (size_t)W * H * 4);
    for (by = 0; by < H; by += 4)
        for (bx = 0; bx < W; bx += 4) {
            uint8_t r[16], g[16];
            const uint8_t *blk = bc5 + (size_t)((by / 4) * (W / 4) + bx / 4) * 16;
            int i;
            bc4_decode(blk, r); bc4_decode(blk + 8, g);
            for (i = 0; i < 16; ++i) {
                int lx = bx + (i % 4), ly = by + (i / 4);
                const uint8_t *src = nm + (ly * W + lx) * 4;
                const uint8_t *d7 = dec7 + (ly * W + lx) * 4;
                float ns[3] = {src[0]/255.f*2-1, src[1]/255.f*2-1, src[2]/255.f*2-1};
                float n5[3], n7[3], zx, zy;
                zx = r[i]/255.f*2-1; zy = g[i]/255.f*2-1;
                n5[0]=zx; n5[1]=zy; n5[2]=sqrtf(1-zx*zx-zy*zy>0?1-zx*zx-zy*zy:0);
                n7[0]=d7[0]/255.f*2-1; n7[1]=d7[1]/255.f*2-1; n7[2]=d7[2]/255.f*2-1;
                {
                    double a5 = normal_angle_deg(ns, n5), a7 = normal_angle_deg(ns, n7);
                    bc5_mean += a5; bc7_mean += a7;
                    if (a5 > bc5_max) bc5_max = a5;
                    if (a7 > bc7_max) bc7_max = a7;
                    ++cnt;
                }
            }
        }
    bc5_mean /= cnt; bc7_mean /= cnt;
    printf("  normal BC5 vs BC7 angular error: BC5 mean=%.2f max=%.2f | BC7 mean=%.2f max=%.2f\n",
           bc5_mean, bc5_max, bc7_mean, bc7_max);
    CHECK(bc5_mean < bc7_mean, "BC5 beats BC7 for normal maps (PBR-tuned format choice)");
    free(nm); free(rg); free(bc5); free(bc7); free(dec7);
}

/* Reconstruct a unit normal from decoded X,Y (Z from XY), like a BC5 workflow. */
static void recon_xy(const uint8_t *p, float n[3]) {
    float x = p[0]/255.f*2-1, y = p[1]/255.f*2-1, zz = 1 - x*x - y*y;
    n[0] = x; n[1] = y; n[2] = sqrtf(zz > 0 ? zz : 0);
}

/* Error-weighted BC7: when Z is reconstructed from X,Y (the standard normal-map
 * workflow), de-weighting Z (and alpha) frees bits for X,Y and lowers the
 * post-reconstruction angular error. Also checks the safety guarantee: uniform
 * weights are byte-identical to no weighting. */
static void test_bc7_weighted_normal(void) {
    int W = 64, H = 64, x, y, i, identical = 1;
    uint8_t *nm = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *b0 = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    uint8_t *b1 = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    uint8_t *bw = (uint8_t *)malloc(tc_bc7_compressed_size(W, H));
    uint8_t *du = (uint8_t *)malloc((size_t)W * H * 4);
    uint8_t *dw = (uint8_t *)malloc((size_t)W * H * 4);
    tc_bc7_options ou, o1, ow;
    double eu = 0, ew = 0;
    size_t bsz = tc_bc7_compressed_size(W, H);
    for (y = 0; y < H; ++y)
        for (x = 0; x < W; ++x) {
            float fx = (x/(float)(W-1))*2-1, fy = (y/(float)(H-1))*2-1;
            float nx = 0.8f*sinf(fx*3.4f), ny = 0.8f*sinf(fy*3.4f), nz = 1, l = sqrtf(nx*nx+ny*ny+nz*nz);
            uint8_t *p = nm + (y*W+x)*4;
            nx/=l; ny/=l; nz/=l;
            p[0]=(uint8_t)((nx*.5f+.5f)*255); p[1]=(uint8_t)((ny*.5f+.5f)*255);
            p[2]=(uint8_t)((nz*.5f+.5f)*255); p[3]=255;
        }
    tc_bc7_options_init(&ou);
    tc_bc7_options_init(&o1);
    o1.channel_weights[0]=1; o1.channel_weights[1]=1; o1.channel_weights[2]=1; o1.channel_weights[3]=1;
    tc_bc7_options_init(&ow);
    ow.channel_weights[0]=6; ow.channel_weights[1]=6; /* X,Y */
    ow.channel_weights[2]=1; ow.channel_weights[3]=1; /* Z reconstructed, alpha unused */
    /* safety: uniform explicit weights == default */
    tc_bc7_compress_rgba8(nm, W, H, W*4, &ou, b0, bsz);
    tc_bc7_compress_rgba8(nm, W, H, W*4, &o1, b1, bsz);
    if (memcmp(b0, b1, bsz) != 0) identical = 0;
    CHECK(identical, "uniform channel_weights are byte-identical to unweighted");
    /* weighted encode */
    tc_bc7_compress_rgba8(nm, W, H, W*4, &ow, bw, bsz);
    tc_bc7_decompress_rgba8(b0, W, H, W*4, du, (size_t)W*H*4);
    tc_bc7_decompress_rgba8(bw, W, H, W*4, dw, (size_t)W*H*4);
    for (i = 0; i < W*H; ++i) {
        float ns[3], nu[3], nw[3];
        recon_xy(nm + i*4, ns); recon_xy(du + i*4, nu); recon_xy(dw + i*4, nw);
        eu += normal_angle_deg(ns, nu);
        ew += normal_angle_deg(ns, nw);
    }
    eu /= (W*H); ew /= (W*H);
    printf("  error-weighted BC7 normal (Z reconstructed): unweighted=%.3f deg, XY-weighted=%.3f deg\n", eu, ew);
    CHECK(ew <= eu, "XY-weighted BC7 lowers reconstructed-normal angular error");
    free(nm); free(b0); free(b1); free(bw); free(du); free(dw);
}

int main(void) {
    printf("envmap PBR validation harness\n");
    test_albedo_compression_shading();
    test_normal_bc7_angular_error();
    test_normal_bc5_vs_bc7();
    test_bc7_weighted_normal();
    if (g_fail) { printf("SOME TESTS FAILED\n"); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
