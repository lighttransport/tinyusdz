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

#ifdef TINYUSDZ_USE_USDOBJ
#include "external/tiny_obj_loader.h"
#endif

namespace tinyusdz {

namespace usdObj {

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

  std::string str(buf.begin(), buf.begin() + buf.size());

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
  pointsAttr.type_name = "float3";
  pointsAttr.buffer.data.resize(sizeof(float) * attrs.vertices.size());
  memcpy(pointsAttr.buffer.data.data(), attrs.vertices.data(), sizeof(float) * attrs.vertices.size());
  pointsAttr.buffer.num_coords = 3;
  pointsAttr.buffer.data_type = tinyusdz::BufferData::BUFFER_DATA_TYPE_FLOAT;
  prim->props["points"] = pointsAttr;

  const auto &shapes = reader.GetShapes();

  // Combine all shapes into single mesh.
  std::vector<uint32_t> vertexIndices;
  std::vector<uint32_t> vertexCounts;

  // normals and texcoords are facevarying
  std::vector<Vec2f> facevaryingTexcoords;
  std::vector<Vec3f> facevaryingNormals;

  {
    size_t index_offset = 0;

    for (size_t i = 0; i < shapes.size(); i++) {
      const tinyobj::shape_t &shape = shapes[i];

      for (size_t f = 0; f < shape.mesh.num_face_vertices.size(); f++) {
        size_t num_v = shape.mesh.num_face_vertices[f];

        vertexCounts.push_back(uint32_t(num_v));

        size_t num_fvn = 0;
        for (size_t v = 0; v < num_v; v++) {
          tinyobj::index_t idx = shape.mesh.indices[index_offset + v];
          vertexIndices.push_back(uint32_t(idx.vertex_index));

          if (idx.normal_index > -1) {
            Vec3f normal;
            normal[0] = attrs.normals[3 * idx.normal_index + 0];
            normal[1] = attrs.normals[3 * idx.normal_index + 1];
            normal[2] = attrs.normals[3 * idx.normal_index + 2];
            facevaryingNormals.push_back(normal);
            num_fvn++;
          } else {
            facevaryingNormals.push_back({0.0f, 0.0f, 0.0f});
          }

          if (idx.texcoord_index > -1) {
            Vec2f texcoord;
            texcoord[0] = attrs.texcoords[2 * idx.texcoord_index + 0];
            texcoord[1] = attrs.texcoords[2 * idx.texcoord_index + 1];
          } else {
            facevaryingTexcoords.push_back({0.0f, 0.0f});
          }
        }

        if (num_fvn == 0) {
          // No per-vertex normal.
          // Compute geometric normal from p0, p1, p(N-1)
          // This won't give correct geometric normal for n-gons(n >= 4)
          
        }

        // TODO: normal, texcoords
        
        index_offset += num_v;
      }

      
    }

    // TODO: per-face material?
  }

  {
    PrimAttrib normalsAttr;
    normalsAttr.facevarying = true;
    normalsAttr.variability = VariabilityVarying;
    normalsAttr.type_name = "float3";
    normalsAttr.buffer.data.resize(sizeof(Vec3f) * facevaryingNormals.size());
    memcpy(normalsAttr.buffer.data.data(), facevaryingNormals.data(), sizeof(Vec3f) * facevaryingNormals.size());
    normalsAttr.buffer.num_coords = 3;
    normalsAttr.buffer.data_type = tinyusdz::BufferData::BUFFER_DATA_TYPE_FLOAT;
    prim->props["normals"] = normalsAttr;
  }

  {
    PrimAttrib texcoordsAttr;
    texcoordsAttr.facevarying = true;
    texcoordsAttr.variability = VariabilityVarying;
    texcoordsAttr.type_name = "float2";
    texcoordsAttr.buffer.data.resize(sizeof(Vec2f) * facevaryingTexcoords.size());
    memcpy(texcoordsAttr.buffer.data.data(), facevaryingTexcoords.data(), sizeof(Vec3f) * facevaryingTexcoords.size());
    texcoordsAttr.buffer.num_coords = 2;
    texcoordsAttr.buffer.data_type = tinyusdz::BufferData::BUFFER_DATA_TYPE_FLOAT;
    prim->props["texcoords"] = texcoordsAttr;
  }

  // TODO: read skin weight/indices

  return true;
#endif
}

} // namespace usdObj

} // tinyusdz
