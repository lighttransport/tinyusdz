/*
 * material_bind.h - bind glTF geometry names to MaterialX surface shaders.
 *
 * The chess .mtlx assigns materials by <materialassign geom="..."> where geom
 * matches the glTF node/mesh name. We resolve geom-name -> surfacematerial ->
 * surface shader node, producing a table indexed by Scene.tri_geom.
 */
#ifndef MTLXRENDER_MATERIAL_BIND_H_
#define MTLXRENDER_MATERIAL_BIND_H_

#include "gltf_load.h"
#include "mtlx_doc.h"

typedef struct {
    int *geom_to_surface; /* size scene->ngeom; surface node id or -1 */
    int  ngeom;
} MaterialBinding;

/* Build the binding; logs unmatched geometry to stderr. */
MaterialBinding material_bind(const Scene *scene, const MtlxDoc *doc);
void material_binding_free(MaterialBinding *b);

#endif /* MTLXRENDER_MATERIAL_BIND_H_ */
