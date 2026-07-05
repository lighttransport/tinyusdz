/*
 * bsdf.h - OpenPBR-style layered BSDF (metallic-roughness core + dielectric
 * specular + smooth glass transmission + subsurface-as-tinted-diffuse).
 *
 * All directions are world-space; wo points from the surface toward the viewer.
 * The shading normal N is oriented to the wo side by the caller helpers.
 */
#ifndef MTLXRENDER_BSDF_H_
#define MTLXRENDER_BSDF_H_

#include "mtlx_eval.h"
#include "vecmath.h"

typedef struct {
    v3    wi;          /* sampled incident direction (world) */
    v3    throughput;  /* f * |cos| / pdf */
    float pdf;
    int   specular;    /* delta lobe: skip NEE/MIS for this bounce */
    int   transmission;/* glass refraction lobe (ray enters/exits medium) */
    int   crossed;     /* transmission actually refracted across the interface
                          (vs reflected/TIR); toggles the caller's medium state */
    int   subsurface;  /* diffuse lobe flagged as subsurface entry */
} BsdfSample;

/* Importance-sample the BSDF. Returns 0 if no valid sample. */
int bsdf_sample(const OpenPBRParams *p, v3 N, v3 wo, pcg32 *rng, BsdfSample *out);

/* Homogeneous interior medium of a transmissive material (OpenPBR semantics):
 *   sigma_t = -ln(transmission_color)/transmission_depth        (extinction)
 *   sigma_s = min(transmission_scatter/transmission_depth, sigma_t) (scattering)
 *   sigma_a = sigma_t - sigma_s                                  (absorption)
 * g is the Henyey-Greenstein anisotropy. All zero when transmission_depth <= 0. */
typedef struct {
    v3    sigma_t;   /* extinction coefficient (per channel) */
    v3    sigma_s;   /* scattering coefficient (per channel) */
    float g;         /* phase anisotropy */
} VolumeMedium;

VolumeMedium transmission_medium(const OpenPBRParams *p);

/* Evaluate the non-delta BSDF (diffuse + rough specular) for NEE/MIS.
 * Returns f (rgb); *pdf_out gets the mixture pdf for sampling wi. */
v3 bsdf_eval(const OpenPBRParams *p, v3 N, v3 wo, v3 wi, float *pdf_out);

#endif /* MTLXRENDER_BSDF_H_ */
