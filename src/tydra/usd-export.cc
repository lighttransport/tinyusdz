// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 - Present, Light Transport Entertainment Inc.
//
#include <sstream>
#include <numeric>
#include <unordered_set>

#include "usd-export.hh"
#include "common-macros.inc"
#include "tiny-format.hh"

namespace tinyusdz {
namespace tydra {

#define PushError(msg) { \
  if (err) { \
    (*err) += msg + "\n"; \
  } \
}

namespace detail {

static bool ToGeomMesh(const RenderMesh &rmesh, GeomMesh *dst, std::string *err) {
  std::vector<int> fvCounts(rmesh.faceVertexCounts().size());
  for (size_t i = 0; i < rmesh.faceVertexCounts().size(); i++) {
    fvCounts[i] = int(rmesh.faceVertexCounts()[i]);
  }
  dst->faceVertexCounts = fvCounts;

  std::vector<int> fvIndices(rmesh.faceVertexIndices().size());
  for (size_t i = 0; i < rmesh.faceVertexIndices().size(); i++) {
    fvCounts[i] = int(rmesh.faceVertexIndices()[i]);
  }
  dst->faceVertexIndices = fvIndices;

  std::vector<value::point3f> points(rmesh.points.size());
  for (size_t i = 0; i < rmesh.points.size(); i++) {
    points[i][0] = rmesh.points[i][0];
    points[i][1] = rmesh.points[i][1];
    points[i][2] = rmesh.points[i][2];
  }

  dst->points = points;

  dst->name = rmesh.prim_name;
  if (rmesh.display_name.size()) {
    dst->meta.displayName = rmesh.display_name;
  }

  if (!rmesh.normals.empty()) {
    // export as primvars:normals

    const auto &vattr = rmesh.normals;
    std::vector<value::normal3f> normals(vattr.vertex_count());
    const float *psrc = reinterpret_cast<const float *>(vattr.buffer());
    for (size_t i = 0; i < vattr.vertex_count(); i++) {
      normals[i][0] = psrc[3 * i + 0];
      normals[i][1] = psrc[3 * i + 1];
      normals[i][2] = psrc[3 * i + 2];
    }

    GeomPrimvar normalPvar;
    normalPvar.set_name("normals");
    normalPvar.set_value(normals);
    if (vattr.is_facevarying()) {
      normalPvar.set_interpolation(Interpolation::FaceVarying);
    } else if (vattr.is_vertex()) {
      normalPvar.set_interpolation(Interpolation::Vertex);
    } else if (vattr.is_uniform()) {
      normalPvar.set_interpolation(Interpolation::Uniform);
    } else if (vattr.is_constant()) {
      normalPvar.set_interpolation(Interpolation::Constant);
    } else {
      PUSH_ERROR_AND_RETURN("Invalid variability in RenderMesh.normals");
    }

    // primvar name is extracted from Primvar::name
    dst->set_primvar(normalPvar);
  }

  // Primary texcoord only.
  // TODO: Multi-texcoords support
  if (rmesh.texcoords.count(0)) {
    const auto &vattr = rmesh.texcoords.at(0);
    std::vector<value::texcoord2f> texcoords(vattr.vertex_count());
    const float *psrc = reinterpret_cast<const float *>(vattr.buffer());
    for (size_t i = 0; i < vattr.vertex_count(); i++) {
      texcoords[i][0] = psrc[2 * i + 0];
      texcoords[i][1] = psrc[2 * i + 1];
    }

    GeomPrimvar uvPvar;
    uvPvar.set_name(vattr.name);
    uvPvar.set_value(texcoords);
    if (vattr.is_facevarying()) {
      uvPvar.set_interpolation(Interpolation::FaceVarying);
    } else if (vattr.is_vertex()) {
      uvPvar.set_interpolation(Interpolation::Vertex);
    } else if (vattr.is_uniform()) {
      uvPvar.set_interpolation(Interpolation::Uniform);
    } else if (vattr.is_constant()) {
      uvPvar.set_interpolation(Interpolation::Constant);
    } else {
      PUSH_ERROR_AND_RETURN("Invalid variability in RenderMesh.texcoord0");
    }

    dst->set_primvar(uvPvar);
  }

  // TODO: Material assignment

  return true;
}

} // namespace

bool export_to_usda(const RenderScene &scene,
  std::string &usda_str, std::string *warn, std::string *err) {

  (void)warn;

  Stage stage;

  stage.metas().comment = "Exported from TinyUSDZ Tydra.";
  if (scene.upAxis == "X") {
    stage.metas().upAxis = Axis::X;
  } else if (scene.upAxis == "Y") {
    stage.metas().upAxis = Axis::Y;
  } else if (scene.upAxis == "Z") {
    stage.metas().upAxis = Axis::Z;
  } 

  // TODO: Node hierarchy

  for (size_t i = 0; i < scene.meshes.size(); i++) {
    GeomMesh mesh;
    if (!detail::ToGeomMesh(scene.meshes[i], &mesh, err)) {
      return false;
    }
    Prim prim(mesh);
    stage.add_root_prim(std::move(prim));
  }

  usda_str =stage.ExportToString();

  return true;
}


} // namespace tydra
} // namespace tinyusdz

