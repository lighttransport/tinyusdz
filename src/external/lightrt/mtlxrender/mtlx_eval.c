#include "mtlx_eval.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- MtlxValue helpers ------------------------------------------------- */

static MtlxValue mv_zero(MtlxType t) { MtlxValue v; memset(&v, 0, sizeof(v)); v.type = t; return v; }
static MtlxValue mv_float(float f) { MtlxValue v = mv_zero(MV_FLOAT); v.v[0] = f; return v; }
static MtlxValue mv_color3(v3 c) { MtlxValue v = mv_zero(MV_COLOR3); v.v[0] = c.x; v.v[1] = c.y; v.v[2] = c.z; return v; }
static MtlxValue mv_vec3(v3 c) { MtlxValue v = mv_zero(MV_VEC3); v.v[0] = c.x; v.v[1] = c.y; v.v[2] = c.z; return v; }
static MtlxValue mv_vec2(float x, float y) { MtlxValue v = mv_zero(MV_VEC2); v.v[0] = x; v.v[1] = y; return v; }

static int ncomp_of(const MtlxValue *v) {
    switch (v->type) {
        case MV_VEC2: return 2;
        case MV_COLOR3: case MV_VEC3: return 3;
        case MV_COLOR4: case MV_VEC4: return 4;
        default: return 1;
    }
}
static float mv_as_float(const MtlxValue *v) {
    if (v->type == MV_FLOAT || v->type == MV_INT || v->type == MV_BOOL) return v->v[0];
    return (v->v[0] + v->v[1] + v->v[2]) * (1.0f / 3.0f);
}
static v3 mv_as_v3(const MtlxValue *v) {
    if (ncomp_of(v) == 1) return v3_splat(v->v[0]);
    return v3_make(v->v[0], v->v[1], v->v[2]);
}

/* component-wise binary/unary application preserving the type of `a`. */
static MtlxValue binop(MtlxValue a, MtlxValue b, float (*f)(float, float)) {
    int n = ncomp_of(&a), nb = ncomp_of(&b);
    for (int i = 0; i < n; i++) { float bv = (nb == 1) ? b.v[0] : b.v[i]; a.v[i] = f(a.v[i], bv); }
    return a;
}
static MtlxValue unop(MtlxValue a, float (*f)(float)) {
    int n = ncomp_of(&a);
    for (int i = 0; i < n; i++) a.v[i] = f(a.v[i]);
    return a;
}

static float f_add(float a, float b) { return a + b; }
static float f_sub(float a, float b) { return a - b; }
static float f_mul(float a, float b) { return a * b; }
static float f_div(float a, float b) { return b != 0.0f ? a / b : 0.0f; }
static float f_min(float a, float b) { return a < b ? a : b; }
static float f_max(float a, float b) { return a > b ? a : b; }
static float f_pow(float a, float b) { return powf(maxf(a, 0.0f), b); }
static float f_mod(float a, float b) { return b != 0.0f ? a - b * floorf(a / b) : 0.0f; }
static float f_atan2(float a, float b) { return atan2f(a, b); }
static float u_sin(float a) { return sinf(a); }
static float u_cos(float a) { return cosf(a); }
static float u_tan(float a) { return tanf(a); }
static float u_asin(float a) { return asinf(clampf(a, -1, 1)); }
static float u_acos(float a) { return acosf(clampf(a, -1, 1)); }
static float u_atan(float a) { return atanf(a); }
static float u_sqrt(float a) { return sqrtf(maxf(a, 0.0f)); }
static float u_ln(float a) { return logf(maxf(a, 1e-8f)); }
static float u_exp(float a) { return expf(a); }
static float u_abs(float a) { return fabsf(a); }
static float u_floor(float a) { return floorf(a); }
static float u_ceil(float a) { return ceilf(a); }
static float u_round(float a) { return roundf(a); }
static float u_sign(float a) { return a > 0 ? 1.0f : (a < 0 ? -1.0f : 0.0f); }

/* ---- color helpers ----------------------------------------------------- */
static v3 rgb_to_hsv(v3 c) {
    float r = c.x, g = c.y, b = c.z;
    float mx = maxf(r, maxf(g, b)), mn = minf(r, minf(g, b)), d = mx - mn;
    float h = 0, s = mx > 0 ? d / mx : 0, v = mx;
    if (d > 1e-8f) {
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    return v3_make(h, s, v);
}
static v3 hsv_to_rgb(v3 c) {
    float h = c.x * 6, s = c.y, v = c.z;
    int i = (int)floorf(h);
    float f = h - i, p = v * (1 - s), q = v * (1 - s * f), t = v * (1 - s * (1 - f));
    switch (((i % 6) + 6) % 6) {
        case 0: return v3_make(v, t, p);
        case 1: return v3_make(q, v, p);
        case 2: return v3_make(p, v, t);
        case 3: return v3_make(p, q, v);
        case 4: return v3_make(t, p, v);
        default: return v3_make(v, p, q);
    }
}

/* ---- MaterialX-compatible gradient (Perlin) noise ----------------------------
 * Ported bit-for-bit from MaterialX libraries/stdlib/genglsl/lib/mx_noise.glsl
 * (itself the OSL oslnoise): Bob Jenkins lookup3 hash + 3D gradient noise with
 * the 0.9820 normalization. Producing the same values as the reference renderers
 * is what makes procedural materials (e.g. standard_surface_marble_solid) match. */
static uint32_t mx_rotl32(uint32_t x, int k) { return (x << k) | (x >> (32 - k)); }
static uint32_t mx_bjfinal(uint32_t a, uint32_t b, uint32_t c) {
    c ^= b; c -= mx_rotl32(b, 14);
    a ^= c; a -= mx_rotl32(c, 11);
    b ^= a; b -= mx_rotl32(a, 25);
    c ^= b; c -= mx_rotl32(b, 16);
    a ^= c; a -= mx_rotl32(c, 4);
    b ^= a; b -= mx_rotl32(a, 14);
    c ^= b; c -= mx_rotl32(b, 24);
    return c;
}
static uint32_t mx_hash_int3(int x, int y, int z) {
    uint32_t a, b, c;
    a = b = c = 0xdeadbeefu + (3u << 2u) + 13u;
    a += (uint32_t)x; b += (uint32_t)y; c += (uint32_t)z;
    return mx_bjfinal(a, b, c);
}
static uint32_t mx_hash_int2(int x, int y) {
    uint32_t a, b, c;
    a = b = c = 0xdeadbeefu + (2u << 2u) + 13u;
    a += (uint32_t)x; b += (uint32_t)y;
    return mx_bjfinal(a, b, c);
}
/* dot product of (x,y,z) with one of 16 edge-of-cube gradient vectors */
static float mx_gradient(uint32_t hash, float x, float y, float z) {
    uint32_t h = hash & 15u;
    float u = (h < 8u) ? x : y;
    float v = (h < 4u) ? y : (((h == 12u) || (h == 14u)) ? x : z);
    float r = (h & 1u) ? -u : u;
    r += (h & 2u) ? -v : v;
    return r;
}
static float mx_fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }
static int mx_floorfrac(float x, float *f) { int i = (int)floorf(x); *f = x - (float)i; return i; }
static float mx_trilerp(float v0, float v1, float v2, float v3, float v4, float v5,
                        float v6, float v7, float s, float t, float r) {
    float s1 = 1.0f - s, t1 = 1.0f - t, r1 = 1.0f - r;
    return r1 * (t1 * (v0 * s1 + v1 * s) + t * (v2 * s1 + v3 * s)) +
           r  * (t1 * (v4 * s1 + v5 * s) + t * (v6 * s1 + v7 * s));
}
/* mx_perlin_noise_float(vec3): result in ~[-1,1] */
static float vnoise(v3 p) {
    float fx, fy, fz;
    int X = mx_floorfrac(p.x, &fx), Y = mx_floorfrac(p.y, &fy), Z = mx_floorfrac(p.z, &fz);
    float u = mx_fade(fx), v = mx_fade(fy), w = mx_fade(fz);
    float res = mx_trilerp(
        mx_gradient(mx_hash_int3(X,     Y,     Z    ), fx,        fy,        fz       ),
        mx_gradient(mx_hash_int3(X + 1, Y,     Z    ), fx - 1.0f, fy,        fz       ),
        mx_gradient(mx_hash_int3(X,     Y + 1, Z    ), fx,        fy - 1.0f, fz       ),
        mx_gradient(mx_hash_int3(X + 1, Y + 1, Z    ), fx - 1.0f, fy - 1.0f, fz       ),
        mx_gradient(mx_hash_int3(X,     Y,     Z + 1), fx,        fy,        fz - 1.0f),
        mx_gradient(mx_hash_int3(X + 1, Y,     Z + 1), fx - 1.0f, fy,        fz - 1.0f),
        mx_gradient(mx_hash_int3(X,     Y + 1, Z + 1), fx,        fy - 1.0f, fz - 1.0f),
        mx_gradient(mx_hash_int3(X + 1, Y + 1, Z + 1), fx - 1.0f, fy - 1.0f, fz - 1.0f),
        u, v, w);
    return 0.9820f * res;
}
static float fractal(v3 p, int octaves, float lacunarity, float diminish) {
    float sum = 0, amp = 1;
    for (int o = 0; o < octaves; o++) { sum += amp * vnoise(p); amp *= diminish; p = v3_scale(p, lacunarity); }
    return sum;
}
/* mx_cell_noise_float: hash of the integer lattice cell, mapped to [0,1] */
static float cellnoise(v3 p) {
    uint32_t h = mx_hash_int3((int)floorf(p.x), (int)floorf(p.y), (int)floorf(p.z));
    return (float)h / (float)0xffffffffu;
}
static float randomfloat_value(float input, int seed, float lo, float hi) {
    uint32_t h = mx_hash_int2((int)floorf(input * 4096.0f), seed);
    float q = (float)h / (float)0xffffffffu;
    return lo + q * (hi - lo);
}

/* ---- node dispatch ---------------------------------------------------- */
typedef enum {
    OP_UNKNOWN = 0,
    OP_IMAGE, OP_HEXTILEDIMAGE, OP_NORMALMAP, OP_TEXCOORD, OP_POSITION, OP_NORMAL, OP_TANGENT,
    OP_BITANGENT, OP_GEOMCOLOR, OP_PLACE2D, OP_CONSTANT,
    OP_ADD, OP_SUBTRACT, OP_MULTIPLY, OP_DIVIDE, OP_MODULO, OP_POWER,
    OP_MIN, OP_MAX, OP_ATAN2,
    OP_SIN, OP_COS, OP_TAN, OP_ASIN, OP_ACOS, OP_ATAN, OP_SQRT, OP_LN, OP_EXP,
    OP_ABS, OP_FLOOR, OP_CEIL, OP_FRACT, OP_STEP, OP_ROUND, OP_SIGN,
    OP_INVERT, OP_NORMALIZE,
    OP_MAGNITUDE, OP_DOTPRODUCT, OP_CROSSPRODUCT,
    OP_MIX, OP_SCREEN, OP_OVERLAY, OP_BURN, OP_DODGE,
    OP_CLAMP, OP_SMOOTHSTEP, OP_REMAP, OP_LUMINANCE, OP_RGBTOHSV,
    OP_HSVTORGB, OP_HSVADJUST, OP_SATURATE, OP_CONTRAST, OP_RANGE,
    OP_SEPARATE, OP_COMBINE2, OP_COMBINE3, OP_COMBINE4, OP_EXTRACT, OP_CONVERT,
    OP_SWIZZLE, OP_NOISE3D, OP_FRACTAL3D, OP_CELLNOISE2D, OP_CELLNOISE3D,
    OP_RAMPLR, OP_RAMPTB, OP_SPLITLR, OP_SPLITTB,
    OP_IFGREATER, OP_IFGREATEREQ, OP_IFEQUAL, OP_SWITCH, OP_DOT, OP_RAMP4,
    OP_ROTATE2D, OP_ONEMINUS, OP_DISTANCE, OP_REFLECT, OP_REFRACT,
    OP_PREMULT, OP_UNPREMULT, OP_MINCOMPONENT, OP_MAXCOMPONENT,
    OP_AND, OP_OR, OP_XOR, OP_NOT, OP_INSIDE, OP_OUTSIDE, OP_TRIANGLEWAVE,
    OP_CHECKERBOARD, OP_DIFFERENCE, OP_IN, OP_MASK, OP_MATTE, OP_OUT, OP_OVER,
    OP_DISJOINTOVER, OP_CIRCLE, OP_LINE, OP_COLORCORRECT, OP_RANDOMFLOAT,
    OP_RANDOMCOLOR
} NodeOp;

static NodeOp classify(const char *c) {
    /* textures */
    if (!strcmp(c, "hextiledimage")) return OP_HEXTILEDIMAGE;
    if (!strcmp(c, "image") || !strcmp(c, "tiledimage") ||
        !strcmp(c, "gltf_image") || !strcmp(c, "gltf_colorimage")) return OP_IMAGE;
    if (!strcmp(c, "normalmap") || !strcmp(c, "gltf_normalmap") ||
        !strcmp(c, "hextilednormalmap")) return OP_NORMALMAP;
    /* geometric */
    if (!strcmp(c, "texcoord") || !strcmp(c, "texcoord0") ||
        !strcmp(c, "texcoord1")) return OP_TEXCOORD;
    if (!strcmp(c, "position")) return OP_POSITION;
    if (!strcmp(c, "normal")) return OP_NORMAL;
    if (!strcmp(c, "tangent")) return OP_TANGENT;
    if (!strcmp(c, "bitangent")) return OP_BITANGENT;
    if (!strcmp(c, "geomcolor")) return OP_GEOMCOLOR;
    if (!strcmp(c, "place2d")) return OP_PLACE2D;
    if (!strcmp(c, "constant")) return OP_CONSTANT;
    /* math binary */
    if (!strcmp(c, "add") || !strcmp(c, "plus")) return OP_ADD;
    if (!strcmp(c, "subtract") || !strcmp(c, "minus")) return OP_SUBTRACT;
    if (!strcmp(c, "multiply")) return OP_MULTIPLY;
    if (!strcmp(c, "divide")) return OP_DIVIDE;
    if (!strcmp(c, "modulo")) return OP_MODULO;
    if (!strcmp(c, "power") || !strcmp(c, "safepower")) return OP_POWER;
    if (!strcmp(c, "min") || !strcmp(c, "minimum")) return OP_MIN;
    if (!strcmp(c, "max") || !strcmp(c, "maximum")) return OP_MAX;
    if (!strcmp(c, "atan2") || !strcmp(c, "arctan2")) return OP_ATAN2;
    /* math unary */
    if (!strcmp(c, "sin")) return OP_SIN;
    if (!strcmp(c, "cos")) return OP_COS;
    if (!strcmp(c, "tan")) return OP_TAN;
    if (!strcmp(c, "asin")) return OP_ASIN;
    if (!strcmp(c, "acos")) return OP_ACOS;
    if (!strcmp(c, "atan")) return OP_ATAN;
    if (!strcmp(c, "sqrt")) return OP_SQRT;
    if (!strcmp(c, "ln") || !strcmp(c, "log") || !strcmp(c, "logarithm")) return OP_LN;
    if (!strcmp(c, "exp")) return OP_EXP;
    if (!strcmp(c, "absval") || !strcmp(c, "abs")) return OP_ABS;
    if (!strcmp(c, "floor")) return OP_FLOOR;
    if (!strcmp(c, "ceil") || !strcmp(c, "ceiling")) return OP_CEIL;
    if (!strcmp(c, "fract") || !strcmp(c, "fraction")) return OP_FRACT;
    if (!strcmp(c, "step")) return OP_STEP;
    if (!strcmp(c, "round")) return OP_ROUND;
    if (!strcmp(c, "sign")) return OP_SIGN;
    if (!strcmp(c, "invert")) return OP_INVERT;
    if (!strcmp(c, "normalize")) return OP_NORMALIZE;
    if (!strcmp(c, "magnitude") || !strcmp(c, "length")) return OP_MAGNITUDE;
    if (!strcmp(c, "dotproduct") || !strcmp(c, "dot")) return OP_DOTPRODUCT;
    if (!strcmp(c, "crossproduct") || !strcmp(c, "cross")) return OP_CROSSPRODUCT;
    /* compositing / adjust */
    if (!strcmp(c, "mix")) return OP_MIX;
    if (!strcmp(c, "screen")) return OP_SCREEN;
    if (!strcmp(c, "overlay")) return OP_OVERLAY;
    if (!strcmp(c, "burn")) return OP_BURN;
    if (!strcmp(c, "dodge")) return OP_DODGE;
    if (!strcmp(c, "difference")) return OP_DIFFERENCE;
    if (!strcmp(c, "in")) return OP_IN;
    if (!strcmp(c, "mask")) return OP_MASK;
    if (!strcmp(c, "matte")) return OP_MATTE;
    if (!strcmp(c, "out")) return OP_OUT;
    if (!strcmp(c, "over")) return OP_OVER;
    if (!strcmp(c, "disjointover")) return OP_DISJOINTOVER;
    if (!strcmp(c, "clamp")) return OP_CLAMP;
    if (!strcmp(c, "smoothstep")) return OP_SMOOTHSTEP;
    if (!strcmp(c, "remap")) return OP_REMAP;
    if (!strcmp(c, "luminance")) return OP_LUMINANCE;
    if (!strcmp(c, "rgbtohsv")) return OP_RGBTOHSV;
    if (!strcmp(c, "hsvtorgb")) return OP_HSVTORGB;
    if (!strcmp(c, "hsvadjust")) return OP_HSVADJUST;
    if (!strcmp(c, "saturate")) return OP_SATURATE;
    if (!strcmp(c, "contrast")) return OP_CONTRAST;
    if (!strcmp(c, "range")) return OP_RANGE;
    /* channel */
    if (!strcmp(c, "separate2") || !strcmp(c, "separate3") || !strcmp(c, "separate4")) return OP_SEPARATE;
    if (!strcmp(c, "combine2")) return OP_COMBINE2;
    if (!strcmp(c, "combine3")) return OP_COMBINE3;
    if (!strcmp(c, "combine4")) return OP_COMBINE4;
    if (!strcmp(c, "extract")) return OP_EXTRACT;
    if (!strcmp(c, "convert") || !strncmp(c, "convert_", 8)) return OP_CONVERT;
    if (!strcmp(c, "swizzle")) return OP_SWIZZLE;
    /* procedural */
    if (!strcmp(c, "noise3d")) return OP_NOISE3D;
    if (!strcmp(c, "fractal3d")) return OP_FRACTAL3D;
    if (!strcmp(c, "cellnoise2d")) return OP_CELLNOISE2D;
    if (!strcmp(c, "cellnoise3d")) return OP_CELLNOISE3D;
    if (!strcmp(c, "ramplr")) return OP_RAMPLR;
    if (!strcmp(c, "ramptb")) return OP_RAMPTB;
    if (!strcmp(c, "splitlr")) return OP_SPLITLR;
    if (!strcmp(c, "splittb")) return OP_SPLITTB;
    if (!strcmp(c, "ramp4")) return OP_RAMP4;
    /* conditional / utility */
    if (!strcmp(c, "ifgreater")) return OP_IFGREATER;
    if (!strcmp(c, "ifgreatereq")) return OP_IFGREATEREQ;
    if (!strcmp(c, "ifequal")) return OP_IFEQUAL;
    if (!strcmp(c, "switch")) return OP_SWITCH;
    if (!strcmp(c, "dot")) return OP_DOT;
    if (!strcmp(c, "rotate2d")) return OP_ROTATE2D;
    if (!strcmp(c, "oneminus")) return OP_ONEMINUS;
    if (!strcmp(c, "distance")) return OP_DISTANCE;
    if (!strcmp(c, "reflect")) return OP_REFLECT;
    if (!strcmp(c, "refract")) return OP_REFRACT;
    if (!strcmp(c, "premult")) return OP_PREMULT;
    if (!strcmp(c, "unpremult")) return OP_UNPREMULT;
    if (!strcmp(c, "mincomponent")) return OP_MINCOMPONENT;
    if (!strcmp(c, "maxcomponent")) return OP_MAXCOMPONENT;
    if (!strcmp(c, "and")) return OP_AND;
    if (!strcmp(c, "or")) return OP_OR;
    if (!strcmp(c, "xor")) return OP_XOR;
    if (!strcmp(c, "not")) return OP_NOT;
    if (!strcmp(c, "inside")) return OP_INSIDE;
    if (!strcmp(c, "outside")) return OP_OUTSIDE;
    if (!strcmp(c, "trianglewave")) return OP_TRIANGLEWAVE;
    if (!strcmp(c, "checkerboard")) return OP_CHECKERBOARD;
    if (!strcmp(c, "circle")) return OP_CIRCLE;
    if (!strcmp(c, "line")) return OP_LINE;
    if (!strcmp(c, "colorcorrect")) return OP_COLORCORRECT;
    if (!strcmp(c, "randomfloat")) return OP_RANDOMFLOAT;
    if (!strcmp(c, "randomcolor")) return OP_RANDOMCOLOR;
    return OP_UNKNOWN;
}

static MtlxValue eval_node(ShadeContext *ctx, int node_id);

static const MtlxInput *find_input(const MtlxNode *n, const char *name) {
    for (int i = 0; i < n->ninput; i++)
        if (!strcmp(n->inputs[i].name, name)) return &n->inputs[i];
    return NULL;
}

/* Apply a sub-output channel selection (separate3.outr, etc.). */
static MtlxValue swizzle_out(MtlxValue v, const char *out) {
    if (!out || !out[0] || !strcmp(out, "out")) return v;
    int idx = -1;
    if (!strcmp(out, "outr") || !strcmp(out, "outx")) idx = 0;
    else if (!strcmp(out, "outg") || !strcmp(out, "outy")) idx = 1;
    else if (!strcmp(out, "outb") || !strcmp(out, "outz")) idx = 2;
    else if (!strcmp(out, "outa") || !strcmp(out, "outw")) idx = 3;
    if (idx >= 0) return mv_float(v.v[idx]);
    return v;
}

static int channel_index(char c) {
    switch (c) {
        case 'r': case 'x': return 0;
        case 'g': case 'y': return 1;
        case 'b': case 'z': return 2;
        case 'a': case 'w': return 3;
        default: return -1;
    }
}

static MtlxValue swizzle_channels(MtlxValue v, const char *channels, MtlxType type) {
    if (!channels || !channels[0]) return v;

    float out[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    int n = 0;
    for (; n < 4 && channels[n]; n++) {
        int idx = channel_index(channels[n]);
        out[n] = (idx >= 0) ? v.v[idx] : 0.0f;
    }
    if (n <= 1) return mv_float(out[0]);
    if (n == 2) return mv_vec2(out[0], out[1]);
    if (n == 3) {
        return (type == MV_VEC3) ? mv_vec3(v3_make(out[0], out[1], out[2]))
                                 : mv_color3(v3_make(out[0], out[1], out[2]));
    }
    {
        MtlxValue r = mv_zero((type == MV_VEC4) ? MV_VEC4 : MV_COLOR4);
        r.v[0] = out[0]; r.v[1] = out[1]; r.v[2] = out[2]; r.v[3] = out[3];
        return r;
    }
}

static MtlxValue eval_input(ShadeContext *ctx, const MtlxInput *in) {
    MtlxValue r;
    if (!in) return mv_zero(MV_NONE);
    if (in->src_node >= 0) r = swizzle_out(eval_node(ctx, in->src_node), in->src_output);
    else if (in->has_value) r = in->value;
    else r = mv_zero(in->type);
    return swizzle_channels(r, in->channels, in->type);
}
static MtlxValue in_or(ShadeContext *ctx, const MtlxNode *n, const char *name, MtlxValue dflt) {
    const MtlxInput *in = find_input(n, name);
    return in ? eval_input(ctx, in) : dflt;
}

static MtlxValue eval_image(ShadeContext *ctx, const MtlxNode *n) {
    const MtlxInput *file = find_input(n, "file");
    int srgb = file ? file->colorspace_srgb : 0;
    const char *path = (file && file->value.s) ? file->value.s : NULL;
    /* honor an explicit texcoord input (e.g. from place2d) */
    float u = ctx->uv[0], v = ctx->uv[1];
    const MtlxInput *tc = find_input(n, "texcoord");
    if (tc && tc->src_node >= 0) { MtlxValue t = eval_input(ctx, tc); u = t.v[0]; v = t.v[1]; }
    int id = path ? texcache_get(ctx->tex, path, srgb) : -1;
    float s[4];
    if (id >= 0) texcache_sample(ctx->tex, id, u, v, s);
    else { MtlxValue d = in_or(ctx, n, "default", mv_zero(n->type)); v3 dc = mv_as_v3(&d); s[0]=dc.x; s[1]=dc.y; s[2]=dc.z; s[3]=1; }
    switch (n->type) {
        case MV_FLOAT: return mv_float(s[0]);
        case MV_VEC2: return mv_vec2(s[0], s[1]);
        case MV_VEC3: return mv_vec3(v3_make(s[0], s[1], s[2]));
        case MV_COLOR4: { MtlxValue r = mv_zero(MV_COLOR4); r.v[0]=s[0];r.v[1]=s[1];r.v[2]=s[2];r.v[3]=s[3]; return r; }
        default: return mv_color3(v3_make(s[0], s[1], s[2]));
    }
}

/* ---- hex-tiling (Mikkelsen, Practical Real-Time Hex-Tiling, JCGT 2022) ---
 * Blend three randomly rotated/scaled/offset lookups of a tiled texture by the
 * simplex-grid barycentric weights, modulated by per-tap luminance, so the
 * obvious repetition of a plain tiled texture is hidden. Ported from MaterialX
 * mx_hextile.glsl / mx_hextiledimage.glsl. Derivatives (textureGrad) are dropped
 * -- they only affect mip selection, not the blend. */
static float hex_fract(float x) { return x - floorf(x); }
static void hex_hash(float px, float py, float *ox, float *oy) {
    float p3x = hex_fract(px * 0.1031f), p3y = hex_fract(py * 0.1030f), p3z = hex_fract(px * 0.0973f);
    float d = p3x * (p3y + 33.33f) + p3y * (p3z + 33.33f) + p3z * (p3x + 33.33f);
    p3x += d; p3y += d; p3z += d;
    *ox = hex_fract((p3x + p3y) * p3z);
    *oy = hex_fract((p3x + p3z) * p3y);
}
static float hex_schlick_gain(float x, float r) {
    float rr = clampf(r, 0.001f, 0.999f);
    float a = (1.0f / rr - 2.0f) * (1.0f - 2.0f * x);
    return (x < 0.5f) ? x / (a + 1.0f) : (a - x) / (a - 1.0f);
}
static float hin_f(ShadeContext *ctx, const MtlxNode *n, const char *nm, float fb) {
    MtlxValue v = in_or(ctx, n, nm, mv_float(fb));
    return mv_as_float(&v);
}
static MtlxValue eval_hextiledimage(ShadeContext *ctx, const MtlxNode *n) {
    const MtlxInput *file = find_input(n, "file");
    int srgb = file ? file->colorspace_srgb : 0;
    const char *path = (file && file->value.s) ? file->value.s : NULL;
    int id = path ? texcache_get(ctx->tex, path, srgb) : -1;
    if (id < 0) return in_or(ctx, n, "default", mv_zero(n->type));

    MtlxValue tv = in_or(ctx, n, "tiling", mv_vec2(1, 1));
    float tile_x = tv.v[0], tile_y = (tv.v[1] != 0.0f ? tv.v[1] : tv.v[0]);
    MtlxValue rr = in_or(ctx, n, "rotationrange", mv_vec2(0.0f, 360.0f));
    MtlxValue sr = in_or(ctx, n, "scalerange", mv_vec2(0.5f, 2.0f));
    MtlxValue orr = in_or(ctx, n, "offsetrange", mv_vec2(0.0f, 1.0f));
    float rotation = hin_f(ctx, n, "rotation", 1.0f);
    float scale = hin_f(ctx, n, "scale", 1.0f);
    float offset = hin_f(ctx, n, "offset", 1.0f);
    float falloff = hin_f(ctx, n, "falloff", 0.5f);
    float fcontrast = hin_f(ctx, n, "falloffcontrast", 0.5f);
    MtlxValue lv = in_or(ctx, n, "lumacoeffs", mv_color3(v3_make(0.2722287f, 0.6740818f, 0.0536895f)));
    v3 luma = mv_as_v3(&lv);

    float coord_x = ctx->uv[0] * tile_x, coord_y = ctx->uv[1] * tile_y;
    const float sqrt3_2 = 3.46410162f;
    /* skew into the simplex grid: to_skewed * (coord*sqrt3_2) */
    float stx = coord_x * sqrt3_2, sty = coord_y * sqrt3_2;
    float skx = stx - 0.57735027f * sty, sky = 1.15470054f * sty;
    float fracx = hex_fract(skx), fracy = hex_fract(sky);
    float tz = 1.0f - fracx - fracy;
    float s = (tz <= 0.0f) ? 1.0f : 0.0f, s2 = 2.0f * s - 1.0f;
    float w[3];
    w[0] = -tz * s2; w[1] = s - fracy * s2; w[2] = s - fracx * s2;
    int basex = (int)floorf(skx), basey = (int)floorf(sky), si = (int)s;
    int idx[3] = { basex + si, basex + si, basex + (1 - si) };
    int idy[3] = { basey + si, basey + (1 - si), basey + si };
    float cx[3], cy[3];
    for (int i = 0; i < 3; i++) {
        /* tile center: inv_skewed * (id / sqrt3_2) */
        float ax = idx[i] / sqrt3_2, ay = idy[i] / sqrt3_2;
        float ctrx = ax + 0.5f * ay, ctry = 0.866025404f * ay;
        float rx, ry; hex_hash((float)idx[i] + 0.12345f, (float)idy[i] + 0.12345f, &rx, &ry);
        float rad0 = rr.v[0] * (float)MTLX_PI / 180.0f, rad1 = rr.v[1] * (float)MTLX_PI / 180.0f;
        float rot = rad0 + (rad1 - rad0) * (rx * rotation);
        float sc = 1.0f + ((sr.v[0] + (sr.v[1] - sr.v[0]) * ry) - 1.0f) * scale;
        float offx = orr.v[0] + (orr.v[1] - orr.v[0]) * (rx * offset);
        float offy = orr.v[0] + (orr.v[1] - orr.v[0]) * (ry * offset);
        float dx = coord_x - ctrx, dy = coord_y - ctry;
        float cr = cosf(rot), srn = sinf(rot);
        cx[i] = (dx * cr - dy * srn) / sc + ctrx + offx;
        cy[i] = (dx * srn + dy * cr) / sc + ctry + offy;
    }
    float c[3][4];
    for (int i = 0; i < 3; i++) texcache_sample(ctx->tex, id, cx[i], cy[i], c[i]);
    /* blend weights: luminance(mix to 1 by contrast) * barycentric^7, normalized */
    float bw[3], sum = 1e-8f;
    for (int i = 0; i < 3; i++) {
        float cwi = c[i][0] * luma.x + c[i][1] * luma.y + c[i][2] * luma.z;
        cwi = 1.0f + (cwi - 1.0f) * fcontrast;
        bw[i] = cwi * powf(maxf(w[i], 0.0f), 7.0f);
        sum += bw[i];
    }
    for (int i = 0; i < 3; i++) bw[i] /= sum;
    if (falloff != 0.5f) {
        float s3 = 1e-8f;
        for (int i = 0; i < 3; i++) { bw[i] = hex_schlick_gain(bw[i], falloff); s3 += bw[i]; }
        for (int i = 0; i < 3; i++) bw[i] /= s3;
    }
    float o[4];
    for (int k = 0; k < 4; k++) o[k] = bw[0] * c[0][k] + bw[1] * c[1][k] + bw[2] * c[2][k];
    switch (n->type) {
        case MV_FLOAT: return mv_float(o[0]);
        case MV_VEC2: return mv_vec2(o[0], o[1]);
        case MV_VEC3: return mv_vec3(v3_make(o[0], o[1], o[2]));
        case MV_COLOR4: { MtlxValue r = mv_zero(MV_COLOR4); r.v[0]=o[0];r.v[1]=o[1];r.v[2]=o[2];r.v[3]=o[3]; return r; }
        default: return mv_color3(v3_make(o[0], o[1], o[2]));
    }
}

static MtlxValue eval_normalmap(ShadeContext *ctx, const MtlxNode *n) {
    /* `normalmap` takes an `in` tangent-space vector (usually from an image),
     * but the gltf convenience node `gltf_normalmap` embeds the texture `file`
     * directly. Sample it like an image node in that case, else no normal map
     * is ever applied for gltf assets. */
    const MtlxInput *inp = find_input(n, "in");
    MtlxValue in;
    if (inp && (inp->src_node >= 0 || inp->has_value)) in = eval_input(ctx, inp);
    else if (find_input(n, "file")) in = eval_image(ctx, n);
    else in = mv_vec3(v3_make(0.5f, 0.5f, 1.0f)); /* flat tangent-space normal */
    v3 t = mv_as_v3(&in);
    v3 ts = v3_make(2 * t.x - 1, 2 * t.y - 1, 2 * t.z - 1);
    const MtlxInput *scin = find_input(n, "scale");
    if (scin) { MtlxValue s = eval_input(ctx, scin); float sc = mv_as_float(&s); ts.x *= sc; ts.y *= sc; }
    v3 N = v3_normalize(ctx->Ns);
    v3 T = v3_sub(ctx->dpdu, v3_scale(N, v3_dot(N, ctx->dpdu))), B;
    if (!v3_is_finite(T) || v3_len(T) < 1e-6f) onb(N, &T, &B);
    else { T = v3_normalize(T); B = v3_cross(N, T); }
    v3 w = v3_normalize(v3_add(v3_add(v3_scale(T, ts.x), v3_scale(B, ts.y)), v3_scale(N, ts.z)));
    return mv_vec3(v3_is_finite(w) ? w : N);
}

static MtlxValue eval_node(ShadeContext *ctx, int node_id) {
    if (node_id < 0 || node_id >= ctx->doc->nnode) return mv_zero(MV_NONE);
    if (ctx->memo_done[node_id]) return ctx->memo[node_id];
    ctx->memo_done[node_id] = 1;
    ctx->memo[node_id] = mv_zero(MV_NONE); /* cycle sentinel */

    const MtlxNode *n = &ctx->doc->nodes[node_id];
    MtlxValue a, b, r = mv_zero(n->type);
    /* nodegraph interface input (category "input"): forward its single
     * self-input (a literal value or an upstream connection). */
    if (!strcmp(n->category, "input")) {
        r = (n->ninput > 0) ? eval_input(ctx, &n->inputs[0]) : mv_zero(n->type);
        ctx->memo[node_id] = r;
        return r;
    }
    switch (classify(n->category)) {
        case OP_IMAGE: r = eval_image(ctx, n); break;
        case OP_HEXTILEDIMAGE: r = eval_hextiledimage(ctx, n); break;
        case OP_NORMALMAP: r = eval_normalmap(ctx, n); break;
        case OP_TEXCOORD: r = mv_vec2(ctx->uv[0], ctx->uv[1]); break;
        case OP_POSITION: r = mv_vec3(ctx->P); break;
        case OP_NORMAL: r = mv_vec3(ctx->Ns); break;
        case OP_TANGENT: r = mv_vec3(v3_normalize(ctx->dpdu)); break;
        case OP_BITANGENT: r = mv_vec3(v3_normalize(ctx->dpdv)); break;
        case OP_GEOMCOLOR: r = mv_color3(v3_splat(1.0f)); break;
        case OP_CONSTANT: r = in_or(ctx, n, "value", mv_zero(n->type)); break;
        case OP_PLACE2D: {
            MtlxValue tc = in_or(ctx, n, "texcoord", mv_vec2(ctx->uv[0], ctx->uv[1]));
            MtlxValue piv = in_or(ctx, n, "pivot", mv_vec2(0, 0));
            MtlxValue scl = in_or(ctx, n, "scale", mv_vec2(1, 1));
            MtlxValue off = in_or(ctx, n, "offset", mv_vec2(0, 0));
            MtlxValue rot = in_or(ctx, n, "rotate", mv_float(0));
            float x = tc.v[0] - piv.v[0], y = tc.v[1] - piv.v[1];
            float ang = rot.v[0] * (MTLX_PI / 180.0f), cs = cosf(ang), sn = sinf(ang);
            float rx = x * cs - y * sn, ry = x * sn + y * cs;
            rx = rx / (scl.v[0] != 0 ? scl.v[0] : 1) + piv.v[0] + off.v[0];
            ry = ry / (scl.v[1] != 0 ? scl.v[1] : 1) + piv.v[1] + off.v[1];
            r = mv_vec2(rx, ry); break;
        }
        case OP_ADD: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(0)); r = binop(a,b,f_add); break;
        case OP_SUBTRACT: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(0)); r = binop(a,b,f_sub); break;
        case OP_MULTIPLY: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(1)); r = binop(a,b,f_mul); break;
        case OP_DIVIDE: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(1)); r = binop(a,b,f_div); break;
        case OP_MODULO: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(1)); r = binop(a,b,f_mod); break;
        case OP_POWER: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(1)); r = binop(a,b,f_pow); break;
        case OP_MIN: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(0)); r = binop(a,b,f_min); break;
        case OP_MAX: a = in_or(ctx,n,"in1",mv_zero(n->type)); b = in_or(ctx,n,"in2",mv_float(0)); r = binop(a,b,f_max); break;
        case OP_ATAN2: a = in_or(ctx,n,"in1",mv_float(0)); b = in_or(ctx,n,"in2",mv_float(1)); r = binop(a,b,f_atan2); break;
        case OP_SIN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_sin); break;
        case OP_COS: r = unop(in_or(ctx,n,"in",mv_float(0)), u_cos); break;
        case OP_TAN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_tan); break;
        case OP_ASIN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_asin); break;
        case OP_ACOS: r = unop(in_or(ctx,n,"in",mv_float(0)), u_acos); break;
        case OP_ATAN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_atan); break;
        case OP_SQRT: r = unop(in_or(ctx,n,"in",mv_float(0)), u_sqrt); break;
        case OP_LN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_ln); break;
        case OP_EXP: r = unop(in_or(ctx,n,"in",mv_float(0)), u_exp); break;
        case OP_ABS: r = unop(in_or(ctx,n,"in",mv_float(0)), u_abs); break;
        case OP_FLOOR: r = unop(in_or(ctx,n,"in",mv_float(0)), u_floor); break;
        case OP_CEIL: r = unop(in_or(ctx,n,"in",mv_float(0)), u_ceil); break;
        case OP_FRACT: { a=in_or(ctx,n,"in",mv_float(0)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++) r.v[i]=a.v[i]-floorf(a.v[i]); break; }
        case OP_STEP: { a=in_or(ctx,n,"in",mv_float(0)); b=in_or(ctx,n,"edge",mv_float(0)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++){float edge=ncomp_of(&b)==1?b.v[0]:b.v[i];r.v[i]=a.v[i]<edge?0.0f:1.0f;} break; }
        case OP_ROUND: r = unop(in_or(ctx,n,"in",mv_float(0)), u_round); break;
        case OP_SIGN: r = unop(in_or(ctx,n,"in",mv_float(0)), u_sign); break;
        case OP_INVERT: { a = in_or(ctx,n,"in",mv_float(0)); MtlxValue amt = in_or(ctx,n,"amount",mv_float(1)); int nc=ncomp_of(&a); for(int i=0;i<nc;i++) a.v[i]=(nc==ncomp_of(&amt)?amt.v[i]:amt.v[0])-a.v[i]; r=a; break; }
        case OP_NORMALIZE: a = in_or(ctx,n,"in",mv_zero(MV_VEC3)); r = mv_vec3(v3_normalize(mv_as_v3(&a))); break;
        case OP_MAGNITUDE: a = in_or(ctx,n,"in",mv_zero(MV_VEC3)); r = mv_float(v3_len(mv_as_v3(&a))); break;
        case OP_DOTPRODUCT: a = in_or(ctx,n,"in1",mv_zero(MV_VEC3)); b = in_or(ctx,n,"in2",mv_zero(MV_VEC3)); r = mv_float(v3_dot(mv_as_v3(&a), mv_as_v3(&b))); break;
        case OP_CROSSPRODUCT: a = in_or(ctx,n,"in1",mv_zero(MV_VEC3)); b = in_or(ctx,n,"in2",mv_zero(MV_VEC3)); r = mv_vec3(v3_cross(mv_as_v3(&a), mv_as_v3(&b))); break;
        case OP_MIX: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)), bg=in_or(ctx,n,"bg",mv_zero(n->type)), m=in_or(ctx,n,"mix",mv_float(0)); int nc=ncomp_of(&fg); r=fg; for(int i=0;i<nc;i++){ float t=(ncomp_of(&m)==1)?m.v[0]:m.v[i]; r.v[i]=bg.v[i]*(1-t)+fg.v[i]*t; } break; }
        case OP_SCREEN: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)),bg=in_or(ctx,n,"bg",mv_zero(n->type)),m=in_or(ctx,n,"mix",mv_float(1)); int nc=ncomp_of(&fg); r=bg; for(int i=0;i<nc;i++){ float t=ncomp_of(&m)==1?m.v[0]:m.v[i],v=1-(1-fg.v[i])*(1-bg.v[i]); r.v[i]=bg.v[i]+t*(v-bg.v[i]); } break; }
        case OP_OVERLAY: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)),bg=in_or(ctx,n,"bg",mv_zero(n->type)),m=in_or(ctx,n,"mix",mv_float(1)); int nc=ncomp_of(&fg); r=bg; for(int i=0;i<nc;i++){ float t=ncomp_of(&m)==1?m.v[0]:m.v[i],v=bg.v[i]>=0.5f?1-2*(1-fg.v[i])*(1-bg.v[i]):2*fg.v[i]*bg.v[i]; r.v[i]=bg.v[i]+t*(v-bg.v[i]); } break; }
        case OP_BURN: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)),bg=in_or(ctx,n,"bg",mv_zero(n->type)),m=in_or(ctx,n,"mix",mv_float(1)); int nc=ncomp_of(&fg); r=bg; for(int i=0;i<nc;i++){ float t=ncomp_of(&m)==1?m.v[0]:m.v[i]; r.v[i]=fabsf(fg.v[i])<1e-6f?0:t*(1-(1-bg.v[i])/fg.v[i])+(1-t)*bg.v[i]; } break; }
        case OP_DODGE: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)),bg=in_or(ctx,n,"bg",mv_zero(n->type)),m=in_or(ctx,n,"mix",mv_float(1)); int nc=ncomp_of(&fg); r=bg; for(int i=0;i<nc;i++){ float t=ncomp_of(&m)==1?m.v[0]:m.v[i]; r.v[i]=fabsf(1-fg.v[i])<1e-6f?0:t*(bg.v[i]/(1-fg.v[i]))+(1-t)*bg.v[i]; } break; }
        case OP_DIFFERENCE: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(n->type)),bg=in_or(ctx,n,"bg",mv_zero(n->type)),m=in_or(ctx,n,"mix",mv_float(1));int nc=ncomp_of(&fg);r=bg;for(int i=0;i<nc;i++){float t=ncomp_of(&m)==1?m.v[0]:m.v[i],q=fabsf(fg.v[i]-bg.v[i]);r.v[i]=bg.v[i]+t*(q-bg.v[i]);}break; }
        case OP_IN: case OP_MASK: case OP_MATTE: case OP_OUT: case OP_OVER: case OP_DISJOINTOVER: { MtlxValue fg=in_or(ctx,n,"fg",mv_zero(MV_COLOR4)),bg=in_or(ctx,n,"bg",mv_zero(MV_COLOR4)),m=in_or(ctx,n,"mix",mv_float(1)),q=mv_zero(MV_COLOR4);NodeOp op=classify(n->category);if(op==OP_IN){for(int i=0;i<4;i++)q.v[i]=fg.v[i]*bg.v[3];}else if(op==OP_MASK){for(int i=0;i<4;i++)q.v[i]=bg.v[i]*fg.v[3];}else if(op==OP_OUT){for(int i=0;i<4;i++)q.v[i]=fg.v[i]*(1-bg.v[3]);}else if(op==OP_DISJOINTOVER){float s=fg.v[3]+bg.v[3];if(s<=1){for(int i=0;i<3;i++)q.v[i]=fg.v[i]+bg.v[i];}else if(fabsf(bg.v[3])>=1e-6f){float x=(1-fg.v[3])/bg.v[3];for(int i=0;i<3;i++)q.v[i]=fg.v[i]+bg.v[i]*x;}q.v[3]=fminf(s,1);}else{for(int i=0;i<3;i++)q.v[i]=(op==OP_MATTE?fg.v[i]*fg.v[3]:fg.v[i])+bg.v[i]*(1-fg.v[3]);q.v[3]=fg.v[3]+bg.v[3]*(1-fg.v[3]);}r=bg;for(int i=0;i<4;i++){float t=ncomp_of(&m)==1?m.v[0]:m.v[i];r.v[i]=bg.v[i]+t*(q.v[i]-bg.v[i]);}break; }
        case OP_CLAMP: { a=in_or(ctx,n,"in",mv_float(0)); MtlxValue lo=in_or(ctx,n,"low",mv_float(0)), hi=in_or(ctx,n,"high",mv_float(1)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++){ float l=(ncomp_of(&lo)==1)?lo.v[0]:lo.v[i], h=(ncomp_of(&hi)==1)?hi.v[0]:hi.v[i]; r.v[i]=clampf(a.v[i],l,h);} break; }
        case OP_SMOOTHSTEP: { a=in_or(ctx,n,"in",mv_float(0)); MtlxValue lo=in_or(ctx,n,"low",mv_float(0)), hi=in_or(ctx,n,"high",mv_float(1)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++){ float l=(ncomp_of(&lo)==1)?lo.v[0]:lo.v[i], h=(ncomp_of(&hi)==1)?hi.v[0]:hi.v[i]; float t=clampf((a.v[i]-l)/(h-l!=0?h-l:1),0,1); r.v[i]=t*t*(3-2*t);} break; }
        case OP_REMAP: { a=in_or(ctx,n,"in",mv_float(0)); MtlxValue il=in_or(ctx,n,"inlow",mv_float(0)),ih=in_or(ctx,n,"inhigh",mv_float(1)),ol=in_or(ctx,n,"outlow",mv_float(0)),oh=in_or(ctx,n,"outhigh",mv_float(1)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++){ float t=(a.v[i]-il.v[0])/((ih.v[0]-il.v[0])!=0?ih.v[0]-il.v[0]:1); r.v[i]=ol.v[0]+t*(oh.v[0]-ol.v[0]);} break; }
        case OP_LUMINANCE: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); float l=luminance(mv_as_v3(&a)); r=mv_color3(v3_splat(l)); break; }
        case OP_RGBTOHSV: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); r=mv_color3(rgb_to_hsv(mv_as_v3(&a))); break; }
        case OP_HSVTORGB: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); r=mv_color3(hsv_to_rgb(mv_as_v3(&a))); break; }
        case OP_HSVADJUST: { a=in_or(ctx,n,"in",mv_zero(n->type));MtlxValue amount=in_or(ctx,n,"amount",mv_vec3(v3_make(0,1,1)));v3 hsv=rgb_to_hsv(mv_as_v3(&a));hsv.x=hsv.x+amount.v[0]-floorf(hsv.x+amount.v[0]);hsv.y=clampf(hsv.y*amount.v[1],0,1);hsv.z=fmaxf(hsv.z*amount.v[2],0);v3 rgb=hsv_to_rgb(hsv);r=a;r.v[0]=rgb.x;r.v[1]=rgb.y;r.v[2]=rgb.z;break; }
        case OP_SATURATE: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); MtlxValue am=in_or(ctx,n,"amount",mv_float(1)); v3 c=mv_as_v3(&a); float l=luminance(c); r=mv_color3(v3_lerp(v3_splat(l),c,am.v[0])); break; }
        case OP_CONTRAST: { a=in_or(ctx,n,"in",mv_zero(n->type)); MtlxValue am=in_or(ctx,n,"amount",mv_float(1)),pv=in_or(ctx,n,"pivot",mv_float(0.5f)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++) r.v[i]=(a.v[i]-pv.v[0])*am.v[0]+pv.v[0]; break; }
        case OP_RANGE: { a=in_or(ctx,n,"in",mv_zero(n->type)); MtlxValue il=in_or(ctx,n,"inlow",mv_float(0)),ih=in_or(ctx,n,"inhigh",mv_float(1)),ol=in_or(ctx,n,"outlow",mv_float(0)),oh=in_or(ctx,n,"outhigh",mv_float(1)),g=in_or(ctx,n,"gamma",mv_float(1)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++){ float t=clampf((a.v[i]-il.v[0])/((ih.v[0]-il.v[0])!=0?ih.v[0]-il.v[0]:1),0,1); t=powf(t,g.v[0]); r.v[i]=ol.v[0]+t*(oh.v[0]-ol.v[0]);} break; }
        case OP_SEPARATE: r = in_or(ctx, n, "in", mv_zero(MV_VEC3)); break; /* consumer swizzles */
        case OP_COMBINE2: { a=in_or(ctx,n,"in1",mv_float(0)); b=in_or(ctx,n,"in2",mv_float(0)); r=mv_vec2(a.v[0],b.v[0]); break; }
        case OP_COMBINE3: { MtlxValue i1=in_or(ctx,n,"in1",mv_float(0)),i2=in_or(ctx,n,"in2",mv_float(0)),i3=in_or(ctx,n,"in3",mv_float(0)); r=(n->type==MV_VEC3)?mv_vec3(v3_make(i1.v[0],i2.v[0],i3.v[0])):mv_color3(v3_make(i1.v[0],i2.v[0],i3.v[0])); break; }
        case OP_COMBINE4: { MtlxValue i1=in_or(ctx,n,"in1",mv_float(0)),i2=in_or(ctx,n,"in2",mv_float(0)),i3=in_or(ctx,n,"in3",mv_float(0)),i4=in_or(ctx,n,"in4",mv_float(0)); r=mv_zero(MV_COLOR4); r.v[0]=i1.v[0];r.v[1]=i2.v[0];r.v[2]=i3.v[0];r.v[3]=i4.v[0]; break; }
        case OP_EXTRACT: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); MtlxValue idx=in_or(ctx,n,"index",mv_float(0)); int i=(int)(idx.v[0]+0.5f); r=mv_float(a.v[i&3]); break; }
        case OP_CONVERT: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3)); r=a; r.type=n->type; if(ncomp_of(&a)==1){ r.v[1]=r.v[2]=a.v[0]; } break; }
        case OP_SWIZZLE: { const MtlxInput *ch=find_input(n,"channels"); const char *s=(ch && ch->has_value && ch->value.s)?ch->value.s:NULL; a=in_or(ctx,n,"in",mv_zero(n->type)); r=swizzle_channels(a,s,n->type); break; }
        case OP_NOISE3D: { MtlxValue pos=in_or(ctx,n,"position",mv_vec3(ctx->P)); MtlxValue amp=in_or(ctx,n,"amplitude",mv_float(1)),piv=in_or(ctx,n,"pivot",mv_float(0)); float val=piv.v[0]+vnoise(mv_as_v3(&pos))*amp.v[0]; r=(n->type==MV_FLOAT)?mv_float(val):mv_color3(v3_splat(val)); break; }
        case OP_FRACTAL3D: { MtlxValue pos=in_or(ctx,n,"position",mv_vec3(ctx->P)); MtlxValue amp=in_or(ctx,n,"amplitude",mv_float(1)),oc=in_or(ctx,n,"octaves",mv_float(3)),lac=in_or(ctx,n,"lacunarity",mv_float(2)),dim=in_or(ctx,n,"diminish",mv_float(0.5f)); float f=fractal(mv_as_v3(&pos),(int)oc.v[0],lac.v[0],dim.v[0])*amp.v[0]; r=(n->type==MV_FLOAT)?mv_float(f):mv_color3(v3_splat(f)); break; }
        case OP_CELLNOISE2D: { MtlxValue tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1]));uint32_t h=mx_hash_int2((int)floorf(tc.v[0]),(int)floorf(tc.v[1]));r=mv_float((float)h/(float)0xffffffffu);break; }
        case OP_CELLNOISE3D: { MtlxValue pos=in_or(ctx,n,"position",mv_vec3(ctx->P)); float c=cellnoise(mv_as_v3(&pos)); r=(n->type==MV_FLOAT)?mv_float(c):mv_color3(v3_splat(c)); break; }
        case OP_RAMPLR: { MtlxValue l=in_or(ctx,n,"valuel",mv_zero(n->type)),rr=in_or(ctx,n,"valuer",mv_zero(n->type)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])); float t=clampf(tc.v[0],0,1); int nc=ncomp_of(&l); r=l; for(int i=0;i<nc;i++) r.v[i]=l.v[i]*(1-t)+rr.v[i]*t; break; }
        case OP_RAMPTB: { MtlxValue tval=in_or(ctx,n,"valuet",mv_zero(n->type)),bval=in_or(ctx,n,"valueb",mv_zero(n->type)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])); float t=clampf(tc.v[1],0,1); int nc=ncomp_of(&tval); r=tval; for(int i=0;i<nc;i++) r.v[i]=tval.v[i]*(1-t)+bval.v[i]*t; break; }
        case OP_SPLITLR: { MtlxValue l=in_or(ctx,n,"valuel",mv_zero(n->type)),rr=in_or(ctx,n,"valuer",mv_zero(n->type)),ct=in_or(ctx,n,"center",mv_float(0.5f)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])); r=(tc.v[0]<ct.v[0])?l:rr; break; }
        case OP_SPLITTB: { MtlxValue tval=in_or(ctx,n,"valuet",mv_zero(n->type)),bval=in_or(ctx,n,"valueb",mv_zero(n->type)),ct=in_or(ctx,n,"center",mv_float(0.5f)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])); r=(tc.v[1]<ct.v[0])?tval:bval; break; }
        case OP_RAMP4: { MtlxValue tl=in_or(ctx,n,"valuetl",mv_zero(n->type)),tr=in_or(ctx,n,"valuetr",mv_zero(n->type)),bl=in_or(ctx,n,"valuebl",mv_zero(n->type)),br=in_or(ctx,n,"valuebr",mv_zero(n->type)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])); float u=clampf(tc.v[0],0,1),vv=clampf(tc.v[1],0,1); int nc=ncomp_of(&tl); r=tl; for(int i=0;i<nc;i++){ float top=tl.v[i]*(1-u)+tr.v[i]*u, bot=bl.v[i]*(1-u)+br.v[i]*u; r.v[i]=top*(1-vv)+bot*vv; } break; }
        case OP_CIRCLE: { MtlxValue tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])),center=in_or(ctx,n,"center",mv_vec2(0,0)),radius=in_or(ctx,n,"radius",mv_float(0.5f));float x=tc.v[0]-center.v[0],y=tc.v[1]-center.v[1];r=mv_float(x*x+y*y>radius.v[0]*radius.v[0]?0.0f:1.0f);break; }
        case OP_LINE: { MtlxValue tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1])),center=in_or(ctx,n,"center",mv_vec2(0,0)),radius=in_or(ctx,n,"radius",mv_float(0.1f)),p1=in_or(ctx,n,"point1",mv_vec2(0.25f,0.25f)),p2=in_or(ctx,n,"point2",mv_vec2(0.75f,0.75f));float px=tc.v[0]-center.v[0]-p1.v[0],py=tc.v[1]-center.v[1]-p1.v[1],bx=p2.v[0]-p1.v[0],by=p2.v[1]-p1.v[1],bb=bx*bx+by*by,t=clampf(bb>1e-12f?(px*bx+py*by)/bb:0,0,1),dx=px-bx*t,dy=py-by*t;r=mv_float(sqrtf(dx*dx+dy*dy)>radius.v[0]?0.0f:1.0f);break; }
        case OP_COLORCORRECT: { a=in_or(ctx,n,"in",mv_zero(n->type));MtlxValue hue=in_or(ctx,n,"hue",mv_float(0)),sat=in_or(ctx,n,"saturation",mv_float(1)),gamma=in_or(ctx,n,"gamma",mv_float(1)),lift=in_or(ctx,n,"lift",mv_float(0)),gain=in_or(ctx,n,"gain",mv_float(1)),contrast=in_or(ctx,n,"contrast",mv_float(1)),pivot=in_or(ctx,n,"contrastpivot",mv_float(0.5f)),exposure=in_or(ctx,n,"exposure",mv_float(0));v3 hsv=rgb_to_hsv(mv_as_v3(&a));hsv.x=hsv.x+hue.v[0]-floorf(hsv.x+hue.v[0]);v3 rgb=hsv_to_rgb(hsv);float lum=luminance(rgb),scale=powf(2.0f,exposure.v[0]),recip=fabsf(gamma.v[0])>1e-6f?1.0f/gamma.v[0]:0.0f;r=a;for(int i=0;i<3;i++){float x=lum+sat.v[0]*((i==0?rgb.x:(i==1?rgb.y:rgb.z))-lum);x=(x<0?-1.0f:1.0f)*powf(fabsf(x),recip);x=(x*(1-lift.v[0])+lift.v[0])*gain.v[0];r.v[i]=((x-pivot.v[0])*contrast.v[0]+pivot.v[0])*scale;}break; }
        case OP_RANDOMFLOAT: { a=in_or(ctx,n,"in",mv_float(0));MtlxValue seed=in_or(ctx,n,"seed",mv_zero(MV_INT)),lo=in_or(ctx,n,"min",mv_float(0)),hi=in_or(ctx,n,"max",mv_float(1));int x=(int)floorf(a.v[0]*(a.type==MV_INT?1.0f:4096.0f)),y=(int)floorf(seed.v[0]);float q=(float)mx_hash_int2(x,y)/(float)0xffffffffu;r=mv_float(lo.v[0]+q*(hi.v[0]-lo.v[0]));break; }
        case OP_RANDOMCOLOR: { a=in_or(ctx,n,"in",mv_float(0));MtlxValue seed=in_or(ctx,n,"seed",mv_zero(MV_INT)),hl=in_or(ctx,n,"huelow",mv_float(0)),hh=in_or(ctx,n,"huehigh",mv_float(1)),sl=in_or(ctx,n,"saturationlow",mv_float(0.825f)),sh=in_or(ctx,n,"saturationhigh",mv_float(1)),bl=in_or(ctx,n,"brightnesslow",mv_float(1)),bh=in_or(ctx,n,"brightnesshigh",mv_float(1));int s=(int)floorf(seed.v[0]);float h=randomfloat_value(a.v[0],(int)ceilf((float)s+413.3f),hl.v[0],hh.v[0]),ss=randomfloat_value(a.v[0],(int)ceilf((float)s+1522.4f),sl.v[0],sh.v[0]),v=randomfloat_value(a.v[0],(int)ceilf((float)s+1813.8f),bl.v[0],bh.v[0]);r=mv_color3(hsv_to_rgb(v3_make(h,ss,v)));break; }
        case OP_IFGREATER: { MtlxValue v1=in_or(ctx,n,"value1",mv_float(0)),v2=in_or(ctx,n,"value2",mv_float(0)); r=(mv_as_float(&v1)>mv_as_float(&v2))?in_or(ctx,n,"in1",mv_zero(n->type)):in_or(ctx,n,"in2",mv_zero(n->type)); break; }
        case OP_IFGREATEREQ: { MtlxValue v1=in_or(ctx,n,"value1",mv_float(0)),v2=in_or(ctx,n,"value2",mv_float(0)); r=(mv_as_float(&v1)>=mv_as_float(&v2))?in_or(ctx,n,"in1",mv_zero(n->type)):in_or(ctx,n,"in2",mv_zero(n->type)); break; }
        case OP_IFEQUAL: { MtlxValue v1=in_or(ctx,n,"value1",mv_float(0)),v2=in_or(ctx,n,"value2",mv_float(0)); r=(mv_as_float(&v1)==mv_as_float(&v2))?in_or(ctx,n,"in1",mv_zero(n->type)):in_or(ctx,n,"in2",mv_zero(n->type)); break; }
        case OP_SWITCH: { MtlxValue w=in_or(ctx,n,"which",mv_float(0)); int k=(int)(mv_as_float(&w)+0.5f); if(k<0)k=0; if(k>9)k=9; char nm[8]; snprintf(nm,sizeof(nm),"in%d",k+1); r=in_or(ctx,n,nm,mv_zero(n->type)); break; }
        case OP_DOT: r=in_or(ctx,n,"in",mv_zero(n->type)); break;
        case OP_ONEMINUS: { a=in_or(ctx,n,"in",mv_zero(n->type)); int nc=ncomp_of(&a); r=a; for(int i=0;i<nc;i++) r.v[i]=1.0f-a.v[i]; break; }
        case OP_ROTATE2D: { a=in_or(ctx,n,"in",mv_vec2(0,0)); MtlxValue am=in_or(ctx,n,"amount",mv_float(0)); float ang=am.v[0]*(MTLX_PI/180.0f),cs=cosf(ang),sn=sinf(ang); r=mv_vec2(a.v[0]*cs-a.v[1]*sn, a.v[0]*sn+a.v[1]*cs); break; }
        case OP_DISTANCE: { a=in_or(ctx,n,"in1",mv_zero(MV_VEC3));b=in_or(ctx,n,"in2",mv_zero(MV_VEC3));r=mv_float(v3_len(v3_sub(mv_as_v3(&a),mv_as_v3(&b))));break; }
        case OP_REFLECT: { a=in_or(ctx,n,"in",mv_zero(MV_VEC3));b=in_or(ctx,n,"normal",mv_vec3(v3_make(0,0,1)));v3 av=mv_as_v3(&a),bv=mv_as_v3(&b);r=mv_vec3(v3_sub(av,v3_scale(bv,2*v3_dot(av,bv))));break; }
        case OP_REFRACT: { a=in_or(ctx,n,"in",mv_zero(MV_VEC3));b=in_or(ctx,n,"normal",mv_vec3(v3_make(0,0,1)));MtlxValue et=in_or(ctx,n,"ior",mv_float(1));v3 av=mv_as_v3(&a),bv=mv_as_v3(&b);float d=v3_dot(av,bv),e=et.v[0],k=1-e*e*(1-d*d);r=mv_vec3(k<0?v3_make(0,0,0):v3_sub(v3_scale(av,e),v3_scale(bv,e*d+sqrtf(k))));break; }
        case OP_PREMULT: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR4));r=a;r.v[0]*=r.v[3];r.v[1]*=r.v[3];r.v[2]*=r.v[3];break; }
        case OP_UNPREMULT: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR4));r=a;float q=fabsf(r.v[3])>1e-6f?1/r.v[3]:0;r.v[0]*=q;r.v[1]*=q;r.v[2]*=q;break; }
        case OP_MINCOMPONENT: case OP_MAXCOMPONENT: { a=in_or(ctx,n,"in",mv_zero(MV_COLOR3));int nc=ncomp_of(&a);float q=a.v[0];NodeOp op=classify(n->category);for(int i=1;i<nc;i++)q=op==OP_MINCOMPONENT?fminf(q,a.v[i]):fmaxf(q,a.v[i]);r=mv_float(q);break; }
        case OP_AND: case OP_OR: case OP_XOR: { a=in_or(ctx,n,"in1",mv_float(0));b=in_or(ctx,n,"in2",mv_float(0));int av=mv_as_float(&a)!=0,bv=mv_as_float(&b)!=0;NodeOp op=classify(n->category);int q=op==OP_AND?(av&&bv):(op==OP_OR?(av||bv):(av!=bv));r=mv_float((float)q);break; }
        case OP_NOT: { a=in_or(ctx,n,"in",mv_float(0));r=mv_float(mv_as_float(&a)==0?1:0);break; }
        case OP_INSIDE: case OP_OUTSIDE: { a=in_or(ctx,n,"in",mv_zero(n->type));b=in_or(ctx,n,"mask",mv_float(0));float q=classify(n->category)==OP_INSIDE?b.v[0]:1-b.v[0];r=a;for(int i=0;i<ncomp_of(&r);i++)r.v[i]*=q;break; }
        case OP_TRIANGLEWAVE: { a=in_or(ctx,n,"in",mv_float(0));float q=fmodf(fabsf(a.v[0]),1.0f);r=mv_float(0.5f-fabsf(q-0.5f));break; }
        case OP_CHECKERBOARD: { MtlxValue c1=in_or(ctx,n,"color1",mv_color3(v3_splat(1))),c2=in_or(ctx,n,"color2",mv_color3(v3_splat(0))),tile=in_or(ctx,n,"uvtiling",mv_vec2(8,8)),off=in_or(ctx,n,"uvoffset",mv_vec2(0,0)),tc=in_or(ctx,n,"texcoord",mv_vec2(ctx->uv[0],ctx->uv[1]));int q=((int)floorf(tc.v[0]*tile.v[0]+off.v[0])+(int)floorf(tc.v[1]*tile.v[1]+off.v[1]))&1;r=q?c2:c1;break; }
        case OP_UNKNOWN:
        default:
            if (n->ninput > 0) r = eval_input(ctx, &n->inputs[0]);
            break;
    }
    ctx->memo[node_id] = r;
    return r;
}

MtlxValue mtlx_eval_node_test(ShadeContext *ctx, int node_id) {
    memset(ctx->memo_done, 0, (size_t)ctx->doc->nnode);
    return eval_node(ctx, node_id);
}

/* ---- public surface evaluation ---------------------------------------- */

void openpbr_defaults(OpenPBRParams *p) {
    memset(p, 0, sizeof(*p));
    p->base_weight = 1.0f; p->base_color = v3_make(0.8f, 0.8f, 0.8f);
    p->specular_weight = 1.0f; p->specular_color = v3_splat(1.0f);
    p->specular_roughness = 0.3f; p->specular_ior = 1.5f;
    p->transmission_color = v3_splat(1.0f);
    p->subsurface_color = v3_splat(0.8f); p->subsurface_radius = v3_splat(1.0f); p->subsurface_scale = 1.0f;
    p->coat_color = v3_splat(1.0f); p->coat_roughness = 0.1f; p->coat_ior = 1.5f;
    p->sheen_color = v3_splat(1.0f); p->sheen_roughness = 0.3f;
    p->thin_film_ior = 1.5f; /* weight/thickness default to 0 (off) via memset */
    p->emission_color = v3_splat(1.0f); p->opacity = 1.0f;
}

static float in_float(ShadeContext *ctx, const MtlxNode *n, const char *name, float fb) {
    const MtlxInput *in = find_input(n, name);
    if (!in) return fb;
    MtlxValue v = eval_input(ctx, in);
    return mv_as_float(&v);
}
static v3 in_color(ShadeContext *ctx, const MtlxNode *n, const char *name, v3 fb) {
    const MtlxInput *in = find_input(n, name);
    if (!in) return fb;
    MtlxValue v = eval_input(ctx, in);
    return mv_as_v3(&v);
}

static void apply_normal_input(ShadeContext *ctx, const MtlxNode *n, const char *name, OpenPBRParams *out) {
    const MtlxInput *nin = find_input(n, name);
    if (nin && nin->src_node >= 0) {
        MtlxValue nv = eval_node(ctx, nin->src_node);
        v3 wn = mv_as_v3(&nv);
        if (v3_is_finite(wn) && v3_len(wn) > 0.1f) out->normal = v3_normalize(wn);
    }
}

int mtlx_eval_surface(ShadeContext *ctx, int surface_node, OpenPBRParams *out) {
    openpbr_defaults(out);
    out->normal = v3_normalize(ctx->Ns);
    if (surface_node < 0 || surface_node >= ctx->doc->nnode) return 1;
    memset(ctx->memo_done, 0, (size_t)ctx->doc->nnode);

    const MtlxNode *n = &ctx->doc->nodes[surface_node];
    const char *cat = n->category;

    if (!strcmp(cat, "open_pbr_surface")) {
        out->base_weight = in_float(ctx, n, "base_weight", 1.0f);
        out->base_color = in_color(ctx, n, "base_color", out->base_color);
        out->diffuse_roughness = in_float(ctx, n, "base_diffuse_roughness", 0.0f);
        out->metalness = in_float(ctx, n, "base_metalness", 0.0f);
        out->specular_weight = in_float(ctx, n, "specular_weight", 1.0f);
        out->specular_color = in_color(ctx, n, "specular_color", out->specular_color);
        out->specular_roughness =
            in_float(ctx, n, "specular_roughness",
                     in_float(ctx, n, "base_roughness", 0.3f));
        out->specular_ior = in_float(ctx, n, "specular_ior", 1.5f);
        out->transmission = in_float(ctx, n, "transmission_weight", 0.0f);
        out->transmission_color = in_color(ctx, n, "transmission_color", out->transmission_color);
        out->transmission_depth = in_float(ctx, n, "transmission_depth", 0.0f);
        out->transmission_scatter = in_color(ctx, n, "transmission_scatter", v3_splat(0.0f));
        out->transmission_scatter_anisotropy = in_float(ctx, n, "transmission_scatter_anisotropy", 0.0f);
        out->subsurface = in_float(ctx, n, "subsurface_weight", 0.0f);
        out->subsurface_color = in_color(ctx, n, "subsurface_color", out->subsurface_color);
        out->subsurface_radius = v3_splat(in_float(ctx, n, "subsurface_radius", 1.0f));
        out->subsurface_scale = in_float(ctx, n, "subsurface_radius_scale", 1.0f);
        out->coat_weight = in_float(ctx, n, "coat_weight", 0.0f);
        out->coat_color = in_color(ctx, n, "coat_color", out->coat_color);
        out->coat_roughness = in_float(ctx, n, "coat_roughness", 0.0f);
        out->coat_ior = in_float(ctx, n, "coat_ior", 1.6f);
        out->sheen_weight = in_float(ctx, n, "fuzz_weight", 0.0f);
        out->sheen_color = in_color(ctx, n, "fuzz_color", out->sheen_color);
        out->sheen_roughness = in_float(ctx, n, "fuzz_roughness", 0.3f);
        out->thin_film_weight = in_float(ctx, n, "thin_film_weight", 0.0f);
        out->thin_film_thickness = in_float(ctx, n, "thin_film_thickness", 0.0f) * 1000.0f; /* um -> nm */
        out->thin_film_ior = in_float(ctx, n, "thin_film_ior", 1.4f);
        out->emission = in_float(ctx, n, "emission_luminance", 0.0f);
        out->emission_color = in_color(ctx, n, "emission_color", out->emission_color);
        out->opacity = in_float(ctx, n, "geometry_opacity", 1.0f);
        apply_normal_input(ctx, n, "geometry_normal", out);
    } else if (!strcmp(cat, "gltf_pbr")) {
        out->base_color = in_color(ctx, n, "base_color", out->base_color);
        out->metalness = in_float(ctx, n, "metallic", 1.0f);
        out->specular_roughness = in_float(ctx, n, "roughness", 0.5f);
        out->specular_weight = in_float(ctx, n, "specular", 1.0f);
        out->specular_color = in_color(ctx, n, "specular_color", out->specular_color);
        out->specular_ior = in_float(ctx, n, "ior", 1.5f);
        out->transmission = in_float(ctx, n, "transmission", 0.0f);
        out->coat_weight = in_float(ctx, n, "clearcoat", 0.0f);
        out->coat_roughness = in_float(ctx, n, "clearcoat_roughness", 0.0f);
        out->sheen_weight = in_float(ctx, n, "sheen_color", 0.0f) > 0 ? 1.0f : 0.0f;
        out->sheen_color = in_color(ctx, n, "sheen_color", out->sheen_color);
        { float es = in_float(ctx, n, "emissive_strength", 1.0f);
          v3 em = in_color(ctx, n, "emissive", v3_splat(0.0f));
          out->emission_color = em; out->emission = (luminance(em) > 0 ? es : 0.0f); }
        apply_normal_input(ctx, n, "normal", out);
    } else if (!strcmp(cat, "UsdPreviewSurface")) {
        out->base_color = in_color(ctx, n, "diffuseColor", out->base_color);
        out->metalness = in_float(ctx, n, "metallic", 0.0f);
        out->specular_roughness = in_float(ctx, n, "roughness", 0.5f);
        out->specular_color = in_color(ctx, n, "specularColor", out->specular_color);
        out->specular_ior = in_float(ctx, n, "ior", 1.5f);
        out->coat_weight = in_float(ctx, n, "clearcoat", 0.0f);
        out->coat_roughness = in_float(ctx, n, "clearcoatRoughness", 0.01f);
        out->opacity = in_float(ctx, n, "opacity", 1.0f);
        { v3 em = in_color(ctx, n, "emissiveColor", v3_splat(0.0f));
          out->emission_color = em; out->emission = (luminance(em) > 0 ? 1.0f : 0.0f); }
        apply_normal_input(ctx, n, "normal", out);
    } else if (!strcmp(cat, "disney_principled")) {
        out->base_color = in_color(ctx, n, "baseColor", out->base_color);
        out->metalness = in_float(ctx, n, "metallic", 0.0f);
        out->specular_roughness = in_float(ctx, n, "roughness", 0.5f);
        out->specular_ior = in_float(ctx, n, "ior", 1.5f);
        out->transmission = in_float(ctx, n, "specTrans", 0.0f);
        out->coat_weight = in_float(ctx, n, "clearcoat", 0.0f);
        out->coat_roughness = 1.0f - in_float(ctx, n, "clearcoatGloss", 0.9f);
        out->sheen_weight = in_float(ctx, n, "sheen", 0.0f);
        out->subsurface = in_float(ctx, n, "subsurface", 0.0f);
        apply_normal_input(ctx, n, "normal", out);
    } else { /* standard_surface (default) */
        out->base_weight = in_float(ctx, n, "base", 1.0f);
        out->base_color = in_color(ctx, n, "base_color", out->base_color);
        out->metalness = in_float(ctx, n, "metalness", 0.0f);
        out->diffuse_roughness = in_float(ctx, n, "diffuse_roughness", 0.0f);
        out->specular_weight = in_float(ctx, n, "specular", 1.0f);
        out->specular_color = in_color(ctx, n, "specular_color", out->specular_color);
        out->specular_roughness =
            in_float(ctx, n, "specular_roughness",
                     in_float(ctx, n, "roughness", 0.2f));
        out->specular_ior =
            in_float(ctx, n, "specular_IOR",
                     in_float(ctx, n, "specular_ior", 1.5f));
        out->transmission = in_float(ctx, n, "transmission", 0.0f);
        out->transmission_color = in_color(ctx, n, "transmission_color", out->transmission_color);
        out->transmission_depth = in_float(ctx, n, "transmission_depth", 0.0f);
        out->transmission_scatter = in_color(ctx, n, "transmission_scatter", v3_splat(0.0f));
        out->transmission_scatter_anisotropy = in_float(ctx, n, "transmission_scatter_anisotropy", 0.0f);
        out->subsurface = in_float(ctx, n, "subsurface", 0.0f);
        out->subsurface_color = in_color(ctx, n, "subsurface_color", out->subsurface_color);
        out->subsurface_radius = in_color(ctx, n, "subsurface_radius", v3_splat(1.0f));
        out->subsurface_scale = in_float(ctx, n, "subsurface_scale", 1.0f);
        out->coat_weight = in_float(ctx, n, "coat", 0.0f);
        out->coat_color = in_color(ctx, n, "coat_color", out->coat_color);
        out->coat_roughness = in_float(ctx, n, "coat_roughness", 0.1f);
        out->coat_ior =
            in_float(ctx, n, "coat_IOR",
                     in_float(ctx, n, "coat_ior", 1.5f));
        out->sheen_weight = in_float(ctx, n, "sheen", 0.0f);
        out->sheen_color = in_color(ctx, n, "sheen_color", out->sheen_color);
        out->sheen_roughness = in_float(ctx, n, "sheen_roughness", 0.3f);
        out->thin_film_thickness = in_float(ctx, n, "thin_film_thickness", 0.0f); /* already nm */
        out->thin_film_ior = in_float(ctx, n, "thin_film_IOR", 1.5f);
        out->thin_film_weight = (out->thin_film_thickness > 0.0f) ? 1.0f : 0.0f;
        out->emission = in_float(ctx, n, "emission", 0.0f);
        out->emission_color = in_color(ctx, n, "emission_color", out->emission_color);
        out->opacity = in_float(ctx, n, "opacity", 1.0f);
        apply_normal_input(ctx, n, "normal", out);
    }

    out->specular_roughness = clampf(out->specular_roughness, 0.01f, 1.0f);
    out->coat_roughness = clampf(out->coat_roughness, 0.01f, 1.0f);
    out->metalness = clampf(out->metalness, 0.0f, 1.0f);
    return 0;
}
