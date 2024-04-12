// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include <sstream>

#include "obj-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

bool export_to_obj(const RenderScene &scene, const int mesh_id,
  std::string &obj_str, std::string &mtl_str, std::string *warn, std::string *err) {

  (void)obj_str;
  (void)mtl_str;
  (void)warn;

  std::stringstream ss;

  if (mesh_id < 0) {
    PUSH_ERROR_AND_RETURN("Invalid mesh_id");
  } else if (size_t(mesh_id) < scene.meshes.size()) {
    PUSH_ERROR_AND_RETURN(fmt::format("mesh_id {} is out-of-range. scene.meshes.size {}", mesh_id, scene.meshes.size()));
  }

  const RenderMesh &mesh = scene.meshes[size_t(mesh_id)];

  ss << "# exported from TinyUSDZ Tydra.\n";
  ss << "mtllib " << mesh.prim_name + ".mtl";
  ss << "\n";
  
  for (size_t i = 0; i < mesh.points.size(); i++) {
    ss << "v " << mesh.points[i][0] << " " << mesh.points[i][1] << " " << mesh.points[i][2] << "\n";
  } 

  bool is_single_indexed = true;
  bool has_texcoord = false;
  bool has_normal = false;

  // primary texcoord only
  if (mesh.texcoords.count(0)) {
    const VertexAttribute &texcoord = mesh.texcoords.at(0);  
    if (texcoord.format == VertexAttributeFormat::Vec2) {
      const float *ptr = reinterpret_cast<const float *>(texcoord.buffer());
      for (size_t i = 0; i < texcoord.vertex_count(); i++) {
        ss << "vt " << ptr[2 * i + 0] << " " << ptr[2 * i + 1] << "\n";
      } 

      is_single_indexed &= texcoord.is_vertex();
      has_texcoord = true;
    }
  }

  if (!mesh.normals.empty()) {
    if (mesh.normals.format == VertexAttributeFormat::Vec3) {
      const float *ptr = reinterpret_cast<const float *>(mesh.normals.buffer());
      for (size_t i = 0; i < mesh.normals.vertex_count(); i++) {
        ss << "vn " << ptr[3 * i + 0] << " " << ptr[3 * i + 1] << " " << ptr[3 * i + 2] << "\n";
      } 
      is_single_indexed &= mesh.normals.is_vertex();
      has_normal = true;
    }
  }

  if (is_single_indexed) {
    size_t foffset = 0;
    for (size_t i = 0; i < mesh.faceVertexCounts.size(); i++) {

      ss << "f ";

      for (size_t f = 0; f < mesh.faceVertexCounts[i]; f++) {
        if (f > 0) {
          ss << " , ";
        }
        // obj's index starts with 1.
        uint32_t idx = mesh.faceVertexIndices[foffset + f] + 1;

        if (has_texcoord && has_normal) {
          ss << idx << "/" << idx << "/" << idx;
        } else if (has_texcoord) {
          ss << idx << "/" << idx;
        } else if (has_normal) {
          ss << idx << "//" << idx;
        } else {
          ss << idx;
        }
      }

      foffset += mesh.faceVertexCounts[i];
    }

  } else {
    PUSH_ERROR_AND_RETURN("different vertex indices are TODO");
  }

  obj_str = ss.str();

  return true;

}


} // namespace tydra
} // namespace tinyusdz

