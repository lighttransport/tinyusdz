#include "color.h"

float srgb_to_linear_f(float c) {
    if (c <= 0.04045f) return c * (1.0f / 12.92f);
    return powf((c + 0.055f) * (1.0f / 1.055f), 2.4f);
}

float linear_to_srgb_f(float c) {
    c = clampf(c, 0.0f, 1.0f);
    if (c <= 0.0031308f) return c * 12.92f;
    return 1.055f * powf(c, 1.0f / 2.4f) - 0.055f;
}

/* Narkowicz 2015 ACES filmic approximation (operates per channel). */
static float aces_f(float x) {
    const float a = 2.51f, b = 0.03f, c = 2.43f, d = 0.59f, e = 0.14f;
    return clampf((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

v3 tonemap(v3 hdr, tonemap_kind kind, float exposure) {
    v3 c = v3_scale(hdr, exposure);
    switch (kind) {
        case TONEMAP_REINHARD:
            return v3_make(c.x / (1.0f + c.x), c.y / (1.0f + c.y), c.z / (1.0f + c.z));
        case TONEMAP_ACES:
            return v3_make(aces_f(c.x), aces_f(c.y), aces_f(c.z));
        case TONEMAP_NONE:
        default:
            return c;
    }
}
