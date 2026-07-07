/*
 * color.h - sRGB <-> linear conversion and tonemapping.
 *
 * The chess assets only need sRGB-texture decode and a linear->display encode,
 * so these are implemented with the exact sRGB piecewise curve. tinycolorio
 * ("tocio") is vendored under deps/tinyexr/tocio for full OCIO config-driven
 * transforms; wire it via color_ocio_* later if a config is supplied.
 */
#ifndef MTLXRENDER_COLOR_H_
#define MTLXRENDER_COLOR_H_

#include "vecmath.h"

/* scalar sRGB EOTF (encoded -> linear) and inverse (linear -> encoded). */
float srgb_to_linear_f(float c);
float linear_to_srgb_f(float c);

static inline v3 srgb_to_linear3(v3 c) {
    return v3_make(srgb_to_linear_f(c.x), srgb_to_linear_f(c.y), srgb_to_linear_f(c.z));
}
static inline v3 linear_to_srgb3(v3 c) {
    return v3_make(linear_to_srgb_f(c.x), linear_to_srgb_f(c.y), linear_to_srgb_f(c.z));
}

typedef enum { TONEMAP_NONE = 0, TONEMAP_REINHARD = 1, TONEMAP_ACES = 2 } tonemap_kind;

/* Map an HDR linear color to [0,1] display-linear via the chosen operator. */
v3 tonemap(v3 hdr, tonemap_kind kind, float exposure);

#endif /* MTLXRENDER_COLOR_H_ */
