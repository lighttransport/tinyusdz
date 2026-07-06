#include "env.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "exr.h"
#include "stb_image.h"

/* ---- 2D piecewise-constant distribution (pbrt-style) ------------------- */
typedef struct {
    int    w, h;
    float *cond_cdf;  /* (w+1) per row */
    float *marg_cdf;  /* h+1 */
    float *func;      /* w*h (luminance * sin(theta)) */
    float  integral;
} Dist2D;

static void dist2d_build(Dist2D *d, const float *func, int w, int h) {
    d->w = w; d->h = h;
    d->func = (float *)malloc(sizeof(float) * (size_t)w * h);
    memcpy(d->func, func, sizeof(float) * (size_t)w * h);
    d->cond_cdf = (float *)malloc(sizeof(float) * (size_t)(w + 1) * h);
    d->marg_cdf = (float *)malloc(sizeof(float) * (size_t)(h + 1));
    float *row_int = (float *)malloc(sizeof(float) * h);
    for (int y = 0; y < h; y++) {
        float *cdf = &d->cond_cdf[(size_t)(w + 1) * y];
        cdf[0] = 0.0f;
        for (int x = 0; x < w; x++) cdf[x + 1] = cdf[x] + func[y * w + x] / w;
        row_int[y] = cdf[w];
        if (cdf[w] > 0.0f) for (int x = 1; x <= w; x++) cdf[x] /= cdf[w];
        else for (int x = 1; x <= w; x++) cdf[x] = (float)x / w;
    }
    d->marg_cdf[0] = 0.0f;
    for (int y = 0; y < h; y++) d->marg_cdf[y + 1] = d->marg_cdf[y] + row_int[y] / h;
    d->integral = d->marg_cdf[h];
    if (d->integral > 0.0f) for (int y = 1; y <= h; y++) d->marg_cdf[y] /= d->marg_cdf[h];
    else for (int y = 1; y <= h; y++) d->marg_cdf[y] = (float)y / h;
    free(row_int);
}

static void dist2d_free(Dist2D *d) {
    if (!d->func) return;
    free(d->func); free(d->cond_cdf); free(d->marg_cdf);
    memset(d, 0, sizeof(*d));
}

static int upper_bound(const float *cdf, int n, float u) {
    int lo = 0, hi = n;
    while (lo < hi) { int m = (lo + hi) / 2; if (cdf[m] <= u) lo = m + 1; else hi = m; }
    return lo - 1 < 0 ? 0 : lo - 1;
}

/* sample (u,v) in [0,1]^2 + pdf in uv-space. */
static void dist2d_sample(const Dist2D *d, float u1, float u2, float *u, float *v, float *pdf) {
    int y = upper_bound(d->marg_cdf, d->h + 1, u2);
    if (y >= d->h) y = d->h - 1;
    float dy = (d->marg_cdf[y + 1] - d->marg_cdf[y]);
    float fy = dy > 0 ? (u2 - d->marg_cdf[y]) / dy : 0.0f;
    const float *cdf = &d->cond_cdf[(size_t)(d->w + 1) * y];
    int x = upper_bound(cdf, d->w + 1, u1);
    if (x >= d->w) x = d->w - 1;
    float dx = (cdf[x + 1] - cdf[x]);
    float fx = dx > 0 ? (u1 - cdf[x]) / dx : 0.0f;
    *u = (x + fx) / d->w;
    *v = (y + fy) / d->h;
    float fval = d->func[y * d->w + x];
    *pdf = (d->integral > 0.0f) ? fval / d->integral : 0.0f; /* pdf over [0,1]^2 */
}

static float dist2d_pdf(const Dist2D *d, float u, float v) {
    int x = (int)(u * d->w); if (x < 0) x = 0; if (x >= d->w) x = d->w - 1;
    int y = (int)(v * d->h); if (y < 0) y = 0; if (y >= d->h) y = d->h - 1;
    float fval = d->func[y * d->w + x];
    return (d->integral > 0.0f) ? fval / d->integral : 0.0f;
}

/* ---- Env --------------------------------------------------------------- */
struct Env {
    env_kind kind;
    v3       c0, c1;     /* constant color / gradient ground+sky */
    float    intensity;
    /* HDRI */
    float   *pixels;     /* w*h*3 interleaved RGB */
    int      w, h;
    float    rot;        /* radians */
    Dist2D   dist;
};

/* direction <-> equirect uv. u in [0,1) wraps azimuth, v in [0,1] is theta. */
static void dir_to_uv(v3 d, float rot, float *u, float *v) {
    float theta = acosf(clampf(d.y, -1.0f, 1.0f));
    float phi = atan2f(d.x, -d.z) + rot;
    *u = phi * (0.5f * MTLX_INV_PI) + 0.5f;
    *u -= floorf(*u);
    *v = theta * MTLX_INV_PI;
}
static v3 uv_to_dir(float u, float v, float rot) {
    float theta = v * MTLX_PI;
    float phi = (u - 0.5f) * MTLX_TWO_PI - rot;
    float st = sinf(theta);
    return v3_make(st * sinf(phi), cosf(theta), -st * cosf(phi));
}

Env *env_constant(v3 color) {
    Env *e = (Env *)calloc(1, sizeof(Env));
    e->kind = ENV_CONSTANT; e->c0 = color; e->intensity = 1.0f;
    return e;
}

Env *env_gradient(v3 ground, v3 sky, float intensity) {
    Env *e = (Env *)calloc(1, sizeof(Env));
    e->kind = ENV_GRADIENT; e->c0 = ground; e->c1 = sky; e->intensity = intensity;
    return e;
}

static float lum3(const float *p) { return 0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2]; }

/* Has the path the given (case-insensitive) extension? */
static int has_ext(const char *path, const char *ext) {
    size_t lp = strlen(path), le = strlen(ext);
    if (lp < le) return 0;
    const char *p = path + lp - le;
    for (size_t i = 0; i < le; i++) {
        char a = p[i], b = ext[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

static void copy_exr_channel_as_float(float *dst, const void *src,
                                      exr_pixel_type pt, size_t n) {
    if (pt == EXR_PIXEL_FLOAT) {
        memcpy(dst, src, sizeof(float) * n);
    } else if (pt == EXR_PIXEL_HALF) {
        exr_half_to_float((const uint16_t *)src, dst, n);
    } else {
        const uint32_t *u = (const uint32_t *)src;
        for (size_t i = 0; i < n; i++) dst[i] = (float)u[i];
    }
}

/* Load an equirect HDRI into interleaved float RGB. EXR via tinyexr, everything
 * else (Radiance .hdr, etc.) via stb_image's float loader. NULL on failure. */
static float *load_hdri_pixels(const char *path, int *W, int *H) {
    if (has_ext(path, ".exr")) {
        exr_image img;
        memset(&img, 0, sizeof(img));
        exr_result r = exr_load_from_file(path, NULL, &img);
        if (!EXR_OK(r) || img.num_parts < 1) {
            fprintf(stderr, "env: EXR load failed '%s': %s\n", path, exr_result_string(r));
            return NULL;
        }
        exr_part *part = &img.parts[0];
        int w = part->width, h = part->height;
        int ci[3] = {-1, -1, -1};
        for (int c = 0; c < part->header.num_channels; c++) {
            const char *nm = part->header.channels[c].name;
            if (!strcmp(nm, "R")) ci[0] = c;
            else if (!strcmp(nm, "G")) ci[1] = c;
            else if (!strcmp(nm, "B")) ci[2] = c;
        }
        if (ci[0] < 0) ci[0] = 0;
        if (ci[1] < 0) ci[1] = ci[0];
        if (ci[2] < 0) ci[2] = ci[0];
        size_t npx = (size_t)w * h;
        float *rgb = (float *)malloc(sizeof(float) * npx * 3);
        float *tmp = (float *)malloc(sizeof(float) * npx);
        for (int k = 0; k < 3; k++) {
            exr_pixel_type pt = part->header.channels[ci[k]].pixel_type;
            copy_exr_channel_as_float(tmp, part->images[ci[k]], pt, npx);
            for (size_t i = 0; i < npx; i++) rgb[i * 3 + k] = tmp[i];
        }
        free(tmp);
        exr_image_free(&img);
        *W = w; *H = h;
        return rgb;
    }
    /* Radiance .hdr / other float-capable formats */
    int w, h, comp;
    float *px = stbi_loadf(path, &w, &h, &comp, 3);
    if (!px) { fprintf(stderr, "env: HDRI load failed '%s'\n", path); return NULL; }
    *W = w; *H = h;
    return px; /* stb already interleaved RGB (comp forced to 3) */
}

Env *env_hdri(const char *path, float intensity, float rotation_deg) {
    int W = 0, H = 0;
    float *pixels = load_hdri_pixels(path, &W, &H);
    if (!pixels) return NULL;

    Env *e = (Env *)calloc(1, sizeof(Env));
    e->kind = ENV_HDRI; e->w = W; e->h = H; e->intensity = intensity;
    e->rot = rotation_deg * (MTLX_PI / 180.0f);
    e->pixels = pixels;
    size_t npx = (size_t)W * H;

    /* build importance distribution over luminance * sin(theta) */
    float *func = (float *)malloc(sizeof(float) * npx);
    for (int y = 0; y < H; y++) {
        float sin_t = sinf(MTLX_PI * (y + 0.5f) / H);
        for (int x = 0; x < W; x++)
            func[y * W + x] = lum3(&e->pixels[(y * W + x) * 3]) * sin_t;
    }
    dist2d_build(&e->dist, func, W, H);
    free(func);
    fprintf(stderr, "env: loaded HDRI %dx%d from %s\n", W, H, path);
    return e;
}

void env_free(Env *e) {
    if (!e) return;
    free(e->pixels);
    dist2d_free(&e->dist);
    free(e);
}

static v3 hdri_texel(const Env *e, float u, float v) {
    int x = (int)(u * e->w); if (x < 0) x = 0; if (x >= e->w) x = e->w - 1;
    int y = (int)(v * e->h); if (y < 0) y = 0; if (y >= e->h) y = e->h - 1;
    const float *p = &e->pixels[(y * e->w + x) * 3];
    return v3_scale(v3_make(p[0], p[1], p[2]), e->intensity);
}

v3 env_eval(const Env *e, v3 dir) {
    switch (e->kind) {
        case ENV_CONSTANT: return v3_scale(e->c0, e->intensity);
        case ENV_GRADIENT: {
            float t = clampf(0.5f * (dir.y + 1.0f), 0.0f, 1.0f);
            return v3_scale(v3_lerp(e->c0, e->c1, t), e->intensity);
        }
        case ENV_HDRI: {
            float u, v; dir_to_uv(dir, e->rot, &u, &v);
            return hdri_texel(e, u, v);
        }
    }
    return v3_splat(0.0f);
}

v3 env_sample(const Env *e, float u1, float u2, v3 *wi, float *pdf) {
    if (e->kind != ENV_HDRI) {
        *wi = sample_uniform_sphere(u1, u2);
        *pdf = 1.0f / (4.0f * MTLX_PI);
        return env_eval(e, *wi);
    }
    float u, v, pdf_uv;
    dist2d_sample(&e->dist, u1, u2, &u, &v, &pdf_uv);
    v3 dir = uv_to_dir(u, v, e->rot);
    float theta = v * MTLX_PI;
    float sin_t = sinf(theta);
    *wi = dir;
    *pdf = (sin_t > 1e-6f) ? pdf_uv / (2.0f * MTLX_PI * MTLX_PI * sin_t) : 0.0f;
    return hdri_texel(e, u, v);
}

float env_pdf(const Env *e, v3 dir) {
    if (e->kind != ENV_HDRI) return 1.0f / (4.0f * MTLX_PI);
    float u, v; dir_to_uv(dir, e->rot, &u, &v);
    float theta = v * MTLX_PI;
    float sin_t = sinf(theta);
    if (sin_t <= 1e-6f) return 0.0f;
    return dist2d_pdf(&e->dist, u, v) / (2.0f * MTLX_PI * MTLX_PI * sin_t);
}
