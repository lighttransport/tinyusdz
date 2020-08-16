#include "nanosg.h"

#include "simple-render.hh"

namespace example {

// Trianglate mesh
bool ConvertToRenderMesh(
  const tinyusdz::GeomMesh &mesh,
  DrawGeomMesh *dst)
{
  // vertex points
  // should be vec3f 
  dst->vertices = mesh.points.buffer.GetAsVec3fArray();
  if (dst->vertices.size() != (mesh.GetNumPoints() * 3)) {
    return false;
  }
   
  std::vector<float> facevarying_normals;
  if (!mesh.GetFacevaryingNormals(&facevarying_normals)) {
    return false;
  }

  // Triangulate mesh

  // Make facevarying indices
  // TODO(LTE): Make facevarying normal, uvs, ...
  {
    size_t face_offset = 0;
    for (size_t fid = 0; fid < mesh.faceVertexCounts.size(); fid++) {
      int f_count = mesh.faceVertexCounts[fid];

      assert(f_count >= 3);

      if (f_count == 3) {
        for (size_t f = 0; f < f_count; f++) {
          dst->facevarying_indices.push_back(mesh.faceVertexIndices[face_offset + f]);
        }
      } else {
        // Simple triangulation with triangle-fan decomposition
        for (size_t f = 0; f < f_count - 2; f++) {
          size_t f0 = 0;
          size_t f1 = f;
          size_t f2 = f+1;

          dst->facevarying_indices.push_back(mesh.faceVertexIndices[face_offset + f0]);
          dst->facevarying_indices.push_back(mesh.faceVertexIndices[face_offset + f1]);
          dst->facevarying_indices.push_back(mesh.faceVertexIndices[face_offset + f2]);
        }
      }
      
      face_offset += f_count;
    }
  }
}

} // namespace example
