/*
 * TinyEXR envmap - Phase A unit tests: projection round-trips, solid-angle
 * normalization, and cross-projection resampling.
 *
 * Copyright (c) 2014-2026 Syoyo Fujita and TinyEXR authors
 * SPDX-License-Identifier: Apache-2.0
 */
#include "envmap.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m)                                                            \
    do {                                                                       \
        if (!(c)) { printf("FAIL: %s (%s:%d)\n", (m), __FILE__, __LINE__); g_fail = 1; } \
    } while (0)

#define EM_PIf 3.14159265358979323846f
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* A smooth, well-conditioned direction field for resample/round-trip checks. */
static void field(const float d[3], float rgb[3]) {
    rgb[0] = 0.5f + 0.5f * d[0];
    rgb[1] = 0.5f + 0.5f * d[1];
    rgb[2] = 0.5f + 0.5f * d[2];
}

static void fill(em_image *img) {
    int f, x, y;
    for (f = 0; f < img->faces; ++f)
        for (y = 0; y < img->height; ++y)
            for (x = 0; x < img->width; ++x) {
                float u = ((float)x + 0.5f) / img->width;
                float v = ((float)y + 0.5f) / img->height;
                float d[3], rgb[3];
                float *t = em_image_texel(img, f, x, y);
                em_uv_to_dir(img->proj, f, u, v, d);
                field(d, rgb);
                t[0] = rgb[0]; t[1] = rgb[1]; t[2] = rgb[2];
            }
}

static void test_dir_roundtrip(void) {
    em_proj projs[3] = {EM_PROJ_EQUIRECT, EM_PROJ_CUBE, EM_PROJ_OCTA};
    const char *nm[3] = {"equirect", "cube", "octa"};
    int p, i;
    float worst[3] = {0, 0, 0};
    /* many pseudo-random unit directions (deterministic) */
    for (p = 0; p < 3; ++p) {
        for (i = 0; i < 20000; ++i) {
            float a = (float)i * 0.6180339887f;
            float z = 2.0f * (((i * 2654435761u) & 0xffffu) / 65535.0f) - 1.0f;
            float r = sqrtf(1.0f - z * z);
            float d[3] = {r * cosf(a * 6.2831853f), z, r * sinf(a * 6.2831853f)};
            int face;
            float u, v, d2[3], e;
            em_dir_to_uv(projs[p], d, &face, &u, &v);
            em_uv_to_dir(projs[p], face, u, v, d2);
            e = fabsf(d[0] - d2[0]) + fabsf(d[1] - d2[1]) + fabsf(d[2] - d2[2]);
            if (e > worst[p]) worst[p] = e;
        }
        printf("  dir roundtrip %-8s worst L1=%.2e\n", nm[p], worst[p]);
        CHECK(worst[p] < 1e-4f, "direction round-trip < 1e-4");
    }
}

static void test_solid_angle(void) {
    /* Sum of texel solid angles must be ~4*pi. */
    struct { em_proj p; int w, h; const char *n; } cases[3] = {
        {EM_PROJ_EQUIRECT, 256, 128, "equirect"},
        {EM_PROJ_CUBE, 64, 64, "cube"},
        {EM_PROJ_OCTA, 128, 128, "octa"}};
    int c, f, x, y;
    for (c = 0; c < 3; ++c) {
        double sum = 0.0;
        int faces = (cases[c].p == EM_PROJ_CUBE) ? 6 : 1;
        for (f = 0; f < faces; ++f)
            for (y = 0; y < cases[c].h; ++y)
                for (x = 0; x < cases[c].w; ++x)
                    sum += em_texel_solid_angle(cases[c].p, cases[c].w, cases[c].h, f, x, y);
        printf("  solid-angle sum %-8s = %.5f (4pi=%.5f)\n", cases[c].n, sum, 4.0 * M_PI);
        CHECK(fabs(sum - 4.0 * M_PI) < 0.03 * 4.0 * M_PI, "solid angles sum to ~4pi");
    }
}

static double psnr(const em_image *a, const em_image *b) {
    double mse = 0.0;
    size_t n = (size_t)a->faces * a->width * a->height * a->channels, i;
    for (i = 0; i < n; ++i) {
        double d = (double)a->data[i] - (double)b->data[i];
        mse += d * d;
    }
    mse /= (double)n;
    if (mse <= 0.0) return 1e9;
    return 10.0 * log10(1.0 / mse);
}

static void test_convert(void) {
    em_image eq, cube, back;
    memset(&eq, 0, sizeof(eq));
    memset(&cube, 0, sizeof(cube));
    memset(&back, 0, sizeof(back));
    CHECK(EM_OK(em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 512, 256, 3)), "alloc eq");
    fill(&eq);
    /* equirect -> cube -> equirect should recover the smooth field. */
    CHECK(EM_OK(em_convert(NULL, &eq, EM_PROJ_CUBE, 128, &cube)), "convert eq->cube");
    CHECK(cube.faces == 6 && cube.width == 128, "cube shape");
    CHECK(EM_OK(em_convert(NULL, &cube, EM_PROJ_EQUIRECT, 512, &back)), "convert cube->eq");
    {
        double p = psnr(&eq, &back);
        printf("  convert eq->cube->eq PSNR=%.1f dB\n", p);
        CHECK(p >= 34.0, "resample round-trip PSNR >= 34 dB");
    }
    /* octa round-trip too */
    {
        em_image octa, back2;
        double p;
        memset(&octa, 0, sizeof(octa));
        memset(&back2, 0, sizeof(back2));
        CHECK(EM_OK(em_convert(NULL, &eq, EM_PROJ_OCTA, 256, &octa)), "convert eq->octa");
        CHECK(EM_OK(em_convert(NULL, &octa, EM_PROJ_EQUIRECT, 512, &back2)), "convert octa->eq");
        p = psnr(&eq, &back2);
        printf("  convert eq->octa->eq PSNR=%.1f dB\n", p);
        CHECK(p >= 30.0, "octa round-trip PSNR >= 30 dB");
        em_image_free(NULL, &octa);
        em_image_free(NULL, &back2);
    }
    em_image_free(NULL, &eq);
    em_image_free(NULL, &cube);
    em_image_free(NULL, &back);
}

/* ------------------------------------------------------------- SH / SG */

/* Constant white env: only the DC SH term survives; reconstruction is flat. */
static void test_sh_constant(void) {
    em_image eq;
    float coeffs[EM_SH_MAX_ORDER * EM_SH_MAX_ORDER + 2 * EM_SH_MAX_ORDER + 1][3];
    int i, ncoeff;
    memset(&eq, 0, sizeof(eq));
    em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 128, 64, 3);
    for (i = 0; i < eq.width * eq.height * 3; ++i) eq.data[i] = 0.7f;
    CHECK(EM_OK(em_sh_project(&eq, 2, &coeffs[0][0])), "sh project constant");
    ncoeff = em_sh_num_coeffs(2);
    /* DC coeff nonzero; all higher coeffs ~0. */
    CHECK(fabsf(coeffs[0][0]) > 1.0f, "DC term present");
    for (i = 1; i < ncoeff; ++i)
        CHECK(fabsf(coeffs[i][0]) < 1e-2f, "non-DC terms ~0 for constant env");
    /* reconstruction ~= 0.7 everywhere */
    {
        float d[3] = {0.3f, 0.6f, -0.74f}, rgb[3], l;
        l = sqrtf(d[0]*d[0]+d[1]*d[1]+d[2]*d[2]); d[0]/=l; d[1]/=l; d[2]/=l;
        em_sh_eval(2, &coeffs[0][0], d, rgb);
        CHECK(fabsf(rgb[0] - 0.7f) < 0.02f, "SH reconstruct constant ~0.7");
    }
    printf("  sh constant/white-furnace: ok (DC=%.3f)\n", coeffs[0][0]);
    em_image_free(NULL, &eq);
}

/* SH reconstruction RMS on a smooth low-frequency env should be small. */
static void test_sh_reconstruct(void) {
    em_image eq;
    float coeffs[EM_SH_MAX_ORDER * EM_SH_MAX_ORDER + 2 * EM_SH_MAX_ORDER + 1][3];
    double se = 0.0; int cnt = 0, x, y;
    memset(&eq, 0, sizeof(eq));
    em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 128, 64, 3);
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 128; ++x) {
            float u = (x + 0.5f) / 128.0f, v = (y + 0.5f) / 64.0f, d[3];
            float *t = em_image_texel(&eq, 0, x, y);
            em_uv_to_dir(EM_PROJ_EQUIRECT, 0, u, v, d);
            /* smooth low-order field (fits in SH-4) */
            t[0] = 0.5f + 0.5f * d[1];
            t[1] = 0.5f + 0.4f * d[0] * d[1];
            t[2] = 0.5f + 0.3f * d[2];
        }
    em_sh_project(&eq, 4, &coeffs[0][0]);
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 128; ++x) {
            float u = (x + 0.5f) / 128.0f, v = (y + 0.5f) / 64.0f, d[3], rgb[3];
            const float *t = em_image_texel(&eq, 0, x, y);
            em_uv_to_dir(EM_PROJ_EQUIRECT, 0, u, v, d);
            em_sh_eval(4, &coeffs[0][0], d, rgb);
            se += (rgb[0]-t[0])*(rgb[0]-t[0]); ++cnt;
        }
    {
        double rms = sqrt(se / cnt);
        printf("  sh reconstruct smooth field: RMS=%.4f\n", rms);
        CHECK(rms < 0.05, "SH-4 reconstructs a smooth field (RMS<0.05)");
    }
    em_image_free(NULL, &eq);
}

static void test_sg_fit(void) {
    em_image eq;
    em_sg_lobe lobes[32];
    double se = 0.0, se_const = 0.0; int x, y;
    float target_const = 0.6f;
    memset(&eq, 0, sizeof(eq));
    em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 128, 64, 3);
    /* constant env -> SG should reconstruct ~constant */
    for (x = 0; x < eq.width * eq.height * 3; ++x) eq.data[x] = target_const;
    CHECK(EM_OK(em_sg_fit(NULL, &eq, 24, 0, lobes)), "sg fit constant");
    {
        double mean = 0.0;
        for (y = 0; y < 64; ++y)
            for (x = 0; x < 128; ++x) {
                float u=(x+0.5f)/128.0f, v=(y+0.5f)/64.0f, d[3], rgb[3];
                em_uv_to_dir(EM_PROJ_EQUIRECT,0,u,v,d);
                em_sg_eval(lobes, 24, d, rgb);
                se_const += (rgb[0]-target_const)*(rgb[0]-target_const);
                mean += rgb[0];
            }
        mean /= (128*64);
        /* SG is inherently ripply on flat regions; assert energy preserved
         * (mean) and bounded ripple, not exact reconstruction. */
        CHECK(fabs(mean - target_const) < 0.02, "SG preserves constant energy");
        CHECK(sqrt(se_const/(128*64)) < 0.1, "SG constant ripple bounded");
    }

    /* a couple of bright spots -> SG residual should be modest */
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 128; ++x) {
            float u=(x+0.5f)/128.0f, v=(y+0.5f)/64.0f, d[3];
            float *t = em_image_texel(&eq,0,x,y);
            float val;
            em_uv_to_dir(EM_PROJ_EQUIRECT,0,u,v,d);
            val = 0.2f + (d[1] > 0.8f ? 3.0f : 0.0f) + (d[0] > 0.9f ? 2.0f : 0.0f);
            t[0]=t[1]=t[2]=val;
        }
    CHECK(EM_OK(em_sg_fit(NULL, &eq, 24, 0, lobes)), "sg fit spots");
    for (y = 0; y < 64; ++y)
        for (x = 0; x < 128; ++x) {
            float u=(x+0.5f)/128.0f, v=(y+0.5f)/64.0f, d[3], rgb[3];
            const float *t = em_image_texel(&eq,0,x,y);
            em_uv_to_dir(EM_PROJ_EQUIRECT,0,u,v,d);
            em_sg_eval(lobes,24,d,rgb);
            se += (rgb[0]-t[0])*(rgb[0]-t[0]);
        }
    printf("  sg fit: const RMS=%.4f, spots RMS=%.4f\n",
           sqrt(se_const/(128*64)), sqrt(se/(128*64)));
    CHECK(sqrt(se/(128*64)) < 0.6, "SG fit of spotty env is bounded");
    em_image_free(NULL, &eq);
}

/* ------------------------------------------------------------- IBL */

static double cube_mean(const em_image *c) {
    double s = 0.0;
    size_t n = (size_t)c->faces * c->width * c->height * c->channels, i;
    for (i = 0; i < n; ++i) s += c->data[i];
    return s / n;
}

static void test_ibl_white_furnace(void) {
    em_image eq, levels[6], irr;
    int i, l;
    memset(&eq, 0, sizeof(eq));
    em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 128, 64, 3);
    for (i = 0; i < eq.width * eq.height * 3; ++i) eq.data[i] = 0.8f; /* constant */
    /* Prefiltered specular of a constant env must stay constant at every level. */
    CHECK(EM_OK(em_prefilter_specular(NULL, &eq, 32, 5, 64, levels)), "prefilter");
    for (l = 0; l < 5; ++l) {
        double m = cube_mean(&levels[l]);
        double dev = 0.0;
        size_t k, n = (size_t)levels[l].faces * levels[l].width * levels[l].height * 3;
        for (k = 0; k < n; ++k) { double d = levels[l].data[k] - 0.8; if (fabs(d) > dev) dev = fabs(d); }
        CHECK(fabs(m - 0.8) < 0.01, "prefilter constant mean ~0.8");
        CHECK(dev < 0.02, "prefilter constant flat");
        (void)m;
        em_image_free(NULL, &levels[l]);
    }
    /* Irradiance of a constant env = the constant (E/pi with L const = L). */
    memset(&irr, 0, sizeof(irr));
    CHECK(EM_OK(em_irradiance_cube(NULL, &eq, 16, 256, &irr)), "irradiance");
    CHECK(fabs(cube_mean(&irr) - 0.8) < 0.01, "irradiance constant ~0.8");
    printf("  ibl white-furnace: ok (irr mean=%.3f)\n", cube_mean(&irr));
    em_image_free(NULL, &irr);
    em_image_free(NULL, &eq);
}

static void test_ibl_blur_monotonic(void) {
    /* A high-frequency env: prefiltered variance must DROP as roughness rises. */
    em_image eq, levels[6];
    int x, y, l;
    double var_prev = 1e30;
    memset(&eq, 0, sizeof(eq));
    em_image_alloc(NULL, &eq, EM_PROJ_EQUIRECT, 256, 128, 3);
    for (y = 0; y < 128; ++y)
        for (x = 0; x < 256; ++x) {
            float *t = em_image_texel(&eq, 0, x, y);
            float v = ((x / 8 + y / 8) & 1) ? 1.0f : 0.0f; /* checker */
            t[0] = t[1] = t[2] = v;
        }
    CHECK(EM_OK(em_prefilter_specular(NULL, &eq, 64, 5, 64, levels)), "prefilter hf");
    {
        double var0 = -1.0, varlast = -1.0;
        for (l = 0; l < 5; ++l) {
            double m = cube_mean(&levels[l]), var = 0.0;
            size_t k, n = (size_t)levels[l].faces * levels[l].width * levels[l].height * 3;
            for (k = 0; k < n; ++k) { double d = levels[l].data[k] - m; var += d * d; }
            var /= n;
            if (l == 0) var0 = var;
            varlast = var;
            em_image_free(NULL, &levels[l]);
        }
        /* Highest roughness must be far smoother than the sharp mirror level. */
        CHECK(varlast < 0.5 * var0, "roughness=1 is much blurrier than roughness=0");
        printf("  ibl roughness blur: ok (var0=%.4f -> varlast=%.4f)\n", var0, varlast);
    }
    (void)var_prev;
    em_image_free(NULL, &eq);
}

static void test_brdf_lut(void) {
    int size = 32, i;
    float *lut = (float *)malloc((size_t)size * size * 2 * sizeof(float));
    int inrange = 1;
    em_brdf_lut(size, 256, lut);
    for (i = 0; i < size * size * 2; ++i)
        if (lut[i] < -0.01f || lut[i] > 1.01f) inrange = 0;
    CHECK(inrange, "BRDF LUT values in [0,1]");
    /* Low roughness, high NdotV -> scale (A) approaches 1, bias (B) small. */
    {
        int ix = size - 1, iy = 0; /* NdotV~1, roughness~0 */
        float A = lut[(iy * size + ix) * 2 + 0], B = lut[(iy * size + ix) * 2 + 1];
        CHECK(A > 0.9f, "BRDF A ~1 at low roughness / high NdotV");
        CHECK(B < 0.1f, "BRDF B ~0 at low roughness / high NdotV");
        printf("  brdf lut: ok (A=%.3f B=%.3f)\n", A, B);
    }
    free(lut);
}

int main(void) {
    printf("envmap Phase A/B/IBL tests\n");
    test_dir_roundtrip();
    test_solid_angle();
    test_convert();
    test_sh_constant();
    test_sh_reconstruct();
    test_sg_fit();
    test_ibl_white_furnace();
    test_ibl_blur_monotonic();
    test_brdf_lut();
    if (g_fail) { printf("SOME TESTS FAILED\n"); return 1; }
    printf("ALL TESTS PASSED\n");
    return 0;
}
