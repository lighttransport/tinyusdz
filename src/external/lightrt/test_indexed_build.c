/* Phase-1 byte-identity check: indexed build vs de-indexed soup build must
 * produce identical ray hits (prim_id, t, u, v) for every ray. */
#include "lightrt_c_tri.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

int main(void) {
    /* Tessellated grid in z=0 plane, N x N quads -> 2*N*N triangles, with a
     * SHARED vertex grid (so indexing actually dedups, exercising the gather). */
    const int N = 37;
    const int VW = N + 1;
    const size_t nverts = (size_t)VW * VW;
    float *verts = malloc(nverts * 3 * sizeof(float));
    for (int y = 0; y <= N; y++)
        for (int x = 0; x <= N; x++) {
            float *v = &verts[((size_t)y * VW + x) * 3];
            v[0] = (float)x / N * 2.0f - 1.0f;
            v[1] = (float)y / N * 2.0f - 1.0f;
            /* a little height variation so triangles aren't coplanar-degenerate */
            v[2] = 0.15f * sinf((float)x * 0.7f) * cosf((float)y * 0.5f);
        }
    const size_t ntris = (size_t)N * N * 2;
    uint32_t *indices = malloc(ntris * 3 * sizeof(uint32_t));
    float *soup = malloc(ntris * 9 * sizeof(float));
    size_t t = 0;
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            uint32_t a = (uint32_t)(y * VW + x), b = a + 1;
            uint32_t c = a + VW, d = c + 1;
            uint32_t tri[2][3] = {{a, b, d}, {a, d, c}};
            for (int k = 0; k < 2; k++) {
                for (int j = 0; j < 3; j++) {
                    indices[t * 3 + j] = tri[k][j];
                    memcpy(&soup[t * 9 + j * 3], &verts[(size_t)tri[k][j] * 3],
                           3 * sizeof(float));
                }
                t++;
            }
        }

    lrt_tri_build_options opt;
    memset(&opt, 0, sizeof(opt));
    opt.quality = LRT_TRI_BUILD_FAST;   /* the mode tusdrender defaults to */
    opt.layout = LRT_TRI_LAYOUT_AUTO;
    opt.num_threads = 4;

    lrt_result e1 = LRT_RESULT_OK, e2 = LRT_RESULT_OK;
    lrt_tri_scene *s_soup = lrt_tri_scene_build(soup, ntris, &opt, &e1);
    lrt_tri_scene *s_idx =
        lrt_tri_scene_build_indexed(verts, nverts, indices, ntris, &opt, &e2);
    if (!s_soup || !s_idx) {
        printf("BUILD FAIL soup=%p(%d) idx=%p(%d)\n", (void *)s_soup, e1,
               (void *)s_idx, e2);
        return 2;
    }

    /* Trace a dense ray grid straight down -Z; compare every hit field. */
    const int R = 256;
    size_t diffs = 0, hits = 0;
    for (int j = 0; j < R; j++)
        for (int i = 0; i < R; i++) {
            lrt_ray ray;
            ray.org[0] = (float)i / (R - 1) * 2.0f - 1.0f;
            ray.org[1] = (float)j / (R - 1) * 2.0f - 1.0f;
            ray.org[2] = 5.0f;
            ray.dir[0] = 0; ray.dir[1] = 0; ray.dir[2] = -1.0f;
            ray.tmin = 0.0f; ray.tmax = 100.0f;
            lrt_hit h1, h2;
            int r1 = lrt_tri_intersect1(s_soup, &ray, &h1);
            int r2 = lrt_tri_intersect1(s_idx, &ray, &h2);
            if (r1 != r2) { diffs++; continue; }
            if (!r1) continue;
            hits++;
            if (h1.prim_id != h2.prim_id ||
                memcmp(&h1.t, &h2.t, sizeof(float)) != 0 ||
                memcmp(&h1.u, &h2.u, sizeof(float)) != 0 ||
                memcmp(&h1.v, &h2.v, sizeof(float)) != 0) {
                if (diffs < 5)
                    printf("DIFF ray(%d,%d): soup prim=%u t=%.9g u=%.9g v=%.9g | "
                           "idx prim=%u t=%.9g u=%.9g v=%.9g\n",
                           i, j, h1.prim_id, h1.t, h1.u, h1.v, h2.prim_id, h2.t,
                           h2.u, h2.v);
                diffs++;
            }
        }
    printf("ntris=%zu nverts=%zu  rays_hit=%zu  DIFFS=%zu  -> %s\n", ntris,
           nverts, hits, diffs, diffs == 0 ? "BYTE-IDENTICAL" : "MISMATCH");
    lrt_tri_scene_free(s_soup);
    lrt_tri_scene_free(s_idx);
    free(verts); free(indices); free(soup);
    return diffs == 0 ? 0 : 1;
}
