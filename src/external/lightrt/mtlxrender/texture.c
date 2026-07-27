#include "texture.h"
#include "color.h"
#include "mtlx_doc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stb_image.h"

typedef struct {
    char         *key;     /* rel_path + srgb flag */
    unsigned char *pixels; /* 8-bit, comp channels */
    int           w, h, comp;
    int           srgb;
} Texture;

struct TextureCache {
    char    *base_dir;
    Texture *tex;
    int      ntex, cap;
    int      frozen; /* when set, texcache_get never mutates (render-safe) */
};

TextureCache *texcache_create(const char *base_dir) {
    TextureCache *tc = (TextureCache *)calloc(1, sizeof(TextureCache));
    tc->base_dir = strdup(base_dir ? base_dir : ".");
    return tc;
}

void texcache_free(TextureCache *tc) {
    if (!tc) return;
    for (int i = 0; i < tc->ntex; i++) {
        free(tc->tex[i].key);
        stbi_image_free(tc->tex[i].pixels);
    }
    free(tc->tex);
    free(tc->base_dir);
    free(tc);
}

/* Resolve a texture filename the way MaterialX does: it is given relative to a
 * search path, not just the document directory. Example materials reference
 * images by bare name (e.g. "brick_normal.jpg") that live in resources/Images/,
 * several levels above the .mtlx. Walk up from base_dir and try both <dir>/<rel>
 * and <dir>/Images/<rel> at each ancestor level. */
static unsigned char *load_searchpath(const char *base_dir, const char *rel,
                                      int *w, int *h, int *comp) {
    char dir[1024];
    snprintf(dir, sizeof(dir), "%s", (base_dir && base_dir[0]) ? base_dir : ".");
    for (int up = 0; up < 12; up++) {
        char path[1100];
        snprintf(path, sizeof(path), "%s/%s", dir, rel);
        unsigned char *px = stbi_load(path, w, h, comp, 0);
        if (px) return px;
        snprintf(path, sizeof(path), "%s/Images/%s", dir, rel);
        px = stbi_load(path, w, h, comp, 0);
        if (px) return px;
        char *slash = strrchr(dir, '/');
        if (!slash) break;
        *slash = '\0';
        if (!dir[0]) break;
    }
    return NULL;
}

int texcache_get(TextureCache *tc, const char *rel_path, int srgb) {
    if (!rel_path || !rel_path[0]) return -1;
    char key[1024];
    snprintf(key, sizeof(key), "%s|%d", rel_path, srgb ? 1 : 0);
    for (int i = 0; i < tc->ntex; i++)
        if (strcmp(tc->tex[i].key, key) == 0) return i;

    if (tc->frozen) return -1; /* render-time miss: never mutate the cache */

    int w, h, comp;
    unsigned char *px = load_searchpath(tc->base_dir, rel_path, &w, &h, &comp);
    if (!px) { fprintf(stderr, "texture: failed to load '%s' (searched from '%s')\n", rel_path, tc->base_dir); return -1; }

    if (tc->ntex >= tc->cap) {
        tc->cap = tc->cap ? tc->cap * 2 : 16;
        tc->tex = (Texture *)realloc(tc->tex, sizeof(Texture) * tc->cap);
    }
    Texture *t = &tc->tex[tc->ntex];
    t->key = strdup(key);
    t->pixels = px;
    t->w = w; t->h = h; t->comp = comp; t->srgb = srgb;
    return tc->ntex++;
}

void texcache_preload(TextureCache *tc, const MtlxDoc *doc) {
    for (int i = 0; i < doc->nnode; i++) {
        const MtlxNode *n = &doc->nodes[i];
        /* match every image variant the evaluator treats as OP_IMAGE, else its
         * file never enters the cache and the frozen render-time lookup misses. */
        const char *c = n->category;
        if (strcmp(c, "image") && strcmp(c, "tiledimage") && strcmp(c, "hextiledimage") &&
            strcmp(c, "gltf_image") && strcmp(c, "gltf_colorimage") &&
            strcmp(c, "normalmap") && strcmp(c, "gltf_normalmap")) continue;
        for (int j = 0; j < n->ninput; j++) {
            const MtlxInput *in = &n->inputs[j];
            if (strcmp(in->name, "file") == 0 && in->value.s)
                texcache_get(tc, in->value.s, in->colorspace_srgb);
        }
    }
    tc->frozen = 1;
    fprintf(stderr, "texture: preloaded %d textures\n", tc->ntex);
}

static int wrap(int x, int n) {
    x %= n;
    if (x < 0) x += n;
    return x;
}

static void fetch_texel(const Texture *t, int x, int y, float out[4]) {
    x = wrap(x, t->w);
    y = wrap(y, t->h);
    const unsigned char *p = t->pixels + (size_t)(y * t->w + x) * t->comp;
    float r, g, b, a = 1.0f;
    if (t->comp >= 3) { r = p[0] / 255.0f; g = p[1] / 255.0f; b = p[2] / 255.0f; if (t->comp == 4) a = p[3] / 255.0f; }
    else { r = g = b = p[0] / 255.0f; if (t->comp == 2) a = p[1] / 255.0f; }
    if (t->srgb) { r = srgb_to_linear_f(r); g = srgb_to_linear_f(g); b = srgb_to_linear_f(b); }
    out[0] = r; out[1] = g; out[2] = b; out[3] = a;
}

void texcache_sample(TextureCache *tc, int id, float u, float v, float out[4]) {
    if (id < 0 || id >= tc->ntex) { out[0] = out[1] = out[2] = 0.0f; out[3] = 1.0f; return; }
    const Texture *t = &tc->tex[id];
    /* flip V to match image-space (OpenEXR/glTF: V grows downward in texel space) */
    float fu = u * t->w - 0.5f;
    float fv = (1.0f - v) * t->h - 0.5f;
    int x0 = (int)floorf(fu), y0 = (int)floorf(fv);
    float dx = fu - x0, dy = fv - y0;
    float c00[4], c10[4], c01[4], c11[4];
    fetch_texel(t, x0, y0, c00);
    fetch_texel(t, x0 + 1, y0, c10);
    fetch_texel(t, x0, y0 + 1, c01);
    fetch_texel(t, x0 + 1, y0 + 1, c11);
    for (int c = 0; c < 4; c++) {
        float a = c00[c] * (1 - dx) + c10[c] * dx;
        float b = c01[c] * (1 - dx) + c11[c] * dx;
        out[c] = a * (1 - dy) + b * dy;
    }
}
