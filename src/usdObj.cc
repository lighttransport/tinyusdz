// SPDX-License-Identifier: MIT
//
// Built-in .obj import plugIn.
// Import only. Writing scene data as .obj is not supported.
//
// example usage
//
// def "mesh" (
//   prepend references = @bunny.obj@
// )
// {
//    ...
// }

#include <string>

#include "tinyusdz.hh"
#include "io-util.hh"
#include "usdObj.hh"

#include "math-util.inc"

#ifdef TINYUSDZ_USE_USDOBJ

// Assume implementation is done in external/tiny_obj_loader.cc
//#define TINYOBJLOADER_IMPLEMENTATION
#include "external/tiny_obj_loader.h"

#endif

namespace tinyusdz {

namespace usdObj {

namespace {



}

bool ReadObjFromFile(const std::string &filepath, tinyusdz::GPrim *prim, std::string *err)
{
#if !defined(TINYUSDZ_USE_USDOBJ)
  if (err) {
    (*err) = "usdObj is disabled in this build.\n";
  }
  return false;
#else

  std::vector<uint8_t> buf;
  if (!io::ReadWholeFile(&buf, err, filepath, /* filesize max */ 0,
                         /* user_ptr */ nullptr)) {
    return false;
  }

  std::string str(reinterpret_cast<const char *>(buf.data()), buf.size());

  return ReadObjFromString(str, prim, err);

#endif

}


bool ReadObjFromString(const std::string &str, tinyusdz::GPrim *prim, std::string *err)
{
#if !defined(TINYUSDZ_USE_USDOBJ)
  if (err) {
    (*err) = "usdObj is disabled in this build.\n";
  }
  return false;
#else

  tinyobj::ObjReaderConfig config;
  // do not triangulate
  config.triangulate = false;

  tinyobj::ObjReader reader;

  // ignore material
  const std::string mtl_text;

  bool ret = reader.ParseFromString(str, mtl_text, config);

  if (!ret) {
    if (err) {
      (*err) += reader.Warning();
      (*err) += reader.Error();
    }

    return false;
  }

  const auto &attrs = reader.GetAttrib();

  PrimAttrib pointsAttr;
  pointsAttr.type_name = "float3[]";
  pointsAttr.var = primvar::to_vec3(attrs.vertices); // std::vector<float> -> std::vector<Vec3f>
  prim->props["points"] = pointsAttr;

  const auto &shapes = reader.GetShapes();

  // Combine all shapes into single mesh.
  std::vector<int32_t> vertexIndices;
  std::vector<int32_t> vertexCounts;

  // normals and texcoords are facevarying
  std::vector<Vec2f> facevaryingTexcoords;
  std::vector<Vec3f> facevaryingNormals;

  {
    size_t index_offset = 0;

    for (size_t i = 0; i < shapes.size(); i++) {
      const tinyobj::shape_t &shape = shapes[i];

      for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
        size_t num_v = shape.mesh.num_face_vertices[f];

        vertexCounts.push_back(int32_t(num_v));

        if (num_v < 3) {
          if (err) {
            (*err) = "Degenerated face found.";
          }
          return false;
        }

        size_t num_fvn = 0;
        for (size_t v = 0; v < num_v; v++) {
          tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
          vertexIndices.push_back(int32_t(idx.vertex_index));

          if (idx.normal_index > -1) {
            Vec3f normal;
            normal[0] = attrs.normals[3 * size_t(idx.normal_index) + 0];
            normal[1] = attrs.normals[3 * size_t(idx.normal_index) + 1];
            normal[2] = attrs.normals[3 * size_t(idx.normal_index) + 2];
            facevaryingNormals.push_back(normal);
            num_fvn++;
          } else {
            facevaryingNormals.push_back({0.0f, 0.0f, 0.0f});
          }

          if (idx.texcoord_index > -1) {
            Vec2f texcoord;
            texcoord[0] = attrs.texcoords[2 * size_t(idx.texcoord_index) + 0];
            texcoord[1] = attrs.texcoords[2 * size_t(idx.texcoord_index) + 1];
          } else {
            facevaryingTexcoords.push_back({0.0f, 0.0f});
          }
        }

        if (num_fvn == 0) {
          // No per-vertex normal.
          // Compute geometric normal from p0, p1, p(N-1)
          // This won't give correct geometric normal for n-gons(n >= 4)
          Vec3f p0, p1, p2;

          uint32_t vidx0 = uint32_t(shape.mesh.indices[index_offset + 0].vertex_index);
          uint32_t vidx1 = uint32_t(shape.mesh.indices[index_offset + 1].vertex_index);
          uint32_t vidx2 = uint32_t(shape.mesh.indices[index_offset + (num_v - 1)].vertex_index);

          p0[0] = attrs.vertices[3 * vidx0 + 0];
          p0[1] = attrs.vertices[3 * vidx0 + 1];
          p0[2] = attrs.vertices[3 * vidx0 + 2];

          p1[0] = attrs.vertices[3 * vidx1 + 0];
          p1[1] = attrs.vertices[3 * vidx1 + 1];
          p1[2] = attrs.vertices[3 * vidx1 + 2];

          p2[0] = attrs.vertices[3 * vidx2 + 0];
          p2[1] = attrs.vertices[3 * vidx2 + 1];
          p2[2] = attrs.vertices[3 * vidx2 + 2];

          Vec3f n = math::geometric_normal(p0, p1, p2);

          for (size_t v = 0; v < num_v; v++) {
            facevaryingNormals.push_back(n);
          }
        }

        // TODO: normal, texcoords

        index_offset += num_v;
      }


    }

    // TODO: per-face material?
  }

  {
    PrimAttrib attr;
    attr.type_name = "int[]";
    attr.var = vertexIndices;
    prim->props["faceVertexIndices"] = attr;
  }

  {
    PrimAttrib attr;
    attr.type_name = "int[]";
    attr.var = vertexCounts;
    prim->props["faceVertexCounts"] = attr;
  }

  {
    PrimAttrib normalsAttr;
    normalsAttr.interpolation = Interpolation::FaceVarying;
    normalsAttr.variability = Variability::Varying;
    normalsAttr.type_name = "float3[]";
    normalsAttr.var = facevaryingNormals;
    prim->props["normals"] = normalsAttr;
  }

  {
    PrimAttrib texcoordsAttr;
    texcoordsAttr.interpolation = Interpolation::FaceVarying;
    texcoordsAttr.variability = Variability::Varying;
    texcoordsAttr.type_name = "float2[]";
    texcoordsAttr.var = facevaryingTexcoords;
    prim->props["prmvars:uv"] = texcoordsAttr;
  }

  // TODO: read skin weight/indices

  return true;
#endif
}

} // namespace usdObj

} // tinyusdz
