/*
 * env.h - environment lighting: constant / vertical-gradient dome, or an
 * importance-sampled lat-long HDRI (EXR). This is the scene's only light.
 */
#ifndef MTLXRENDER_ENV_H_
#define MTLXRENDER_ENV_H_

#include "vecmath.h"

typedef enum { ENV_CONSTANT = 0, ENV_GRADIENT = 1, ENV_HDRI = 2 } env_kind;

typedef struct Env Env;

/* Constant dome of the given radiance. */
Env *env_constant(v3 color);
/* Vertical gradient: ground..sky by direction.y, scaled by intensity. */
Env *env_gradient(v3 ground, v3 sky, float intensity);
/* Load an equirectangular HDRI from EXR; falls back to NULL on failure. */
Env *env_hdri(const char *exr_path, float intensity, float rotation_deg);
void env_free(Env *e);

/* Radiance arriving from direction `dir` (unit, world). */
v3 env_eval(const Env *e, v3 dir);

/* Sample a direction; fills *wi (unit) and *pdf (solid-angle). Returns radiance. */
v3 env_sample(const Env *e, float u1, float u2, v3 *wi, float *pdf);

/* PDF (solid angle) of sampling direction `dir` under env_sample. */
float env_pdf(const Env *e, v3 dir);

#endif /* MTLXRENDER_ENV_H_ */
