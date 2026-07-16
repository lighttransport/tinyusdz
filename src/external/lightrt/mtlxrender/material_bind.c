#include "material_bind.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Normalize a name for fuzzy matching: lowercase, drop a trailing ".00N". */
static void normalize(const char *in, char *out, size_t cap) {
    size_t n = strlen(in);
    /* strip ".0NN" exporter suffix */
    if (n > 4 && in[n - 4] == '.' && in[n - 3] >= '0' && in[n - 3] <= '9') n -= 4;
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char c = in[i];
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        out[o++] = c;
    }
    out[o] = '\0';
}

MaterialBinding material_bind(const Scene *scene, const MtlxDoc *doc) {
    MaterialBinding b;
    b.ngeom = scene->ngeom;
    b.geom_to_surface = (int *)malloc(sizeof(int) * (size_t)(scene->ngeom > 0 ? scene->ngeom : 1));

    /* Fallback for single-material example files (no <materialassign>): if the
     * doc has exactly one surface shader, assign it to all geometry. */
    int only_surface = -1;
    if (doc->nassign == 0) {
        int count = 0;
        for (int m = 0; m < doc->nmat; m++)
            if (doc->mats[m].surface_node >= 0) { only_surface = doc->mats[m].surface_node; count++; }
        if (count != 1) only_surface = (doc->nmat > 0 ? doc->mats[0].surface_node : -1);
    }

    int unmatched = 0;
    for (int g = 0; g < scene->ngeom; g++) {
        const char *gname = scene->geom_names[g];
        int surface = (doc->nassign == 0) ? only_surface : -1;

        /* 1. exact geom -> materialassign -> surfacematerial -> surface node */
        for (int a = 0; a < doc->nassign; a++) {
            if (strcmp(doc->assigns[a].geom, gname) == 0) {
                int mi = mtlx_find_material(doc, doc->assigns[a].material);
                if (mi >= 0) surface = doc->mats[mi].surface_node;
                break;
            }
        }

        /* 2. normalized match */
        if (surface < 0) {
            char gn[256]; normalize(gname, gn, sizeof(gn));
            for (int a = 0; a < doc->nassign && surface < 0; a++) {
                char an[256]; normalize(doc->assigns[a].geom, an, sizeof(an));
                if (strcmp(gn, an) == 0) {
                    int mi = mtlx_find_material(doc, doc->assigns[a].material);
                    if (mi >= 0) surface = doc->mats[mi].surface_node;
                }
            }
        }

        b.geom_to_surface[g] = surface;
        if (surface < 0) {
            unmatched++;
            fprintf(stderr, "bind: geom '%s' -> (no material)\n", gname);
        } else {
            fprintf(stderr, "bind: geom '%s' -> surface node #%d (%s)\n",
                    gname, surface, doc->nodes[surface].category);
        }
    }
    if (unmatched) fprintf(stderr, "bind: %d/%d geometry groups unmatched\n", unmatched, scene->ngeom);
    return b;
}

void material_binding_free(MaterialBinding *b) {
    if (!b) return;
    free(b->geom_to_surface);
    b->geom_to_surface = NULL;
    b->ngeom = 0;
}
