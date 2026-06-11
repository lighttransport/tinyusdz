// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv <-> tinyusdz adapter. The only tsd file that includes
// tinyusdz types.

#include "tsd-tinyusdz.hh"

#include "usdGeom.hh"

namespace tinyusdz {
namespace tsd {

namespace {

// Reads the default value of an authored vector attribute (subdiv tags are
// not animatable in practice); absent attributes yield an empty vector.
template <typename T>
std::vector<T> GetTagValues(
    const TypedAttribute<Animatable<std::vector<T>>> &attr) {
  std::vector<T> values;
  if (attr.has_value()) {
    auto animatable = attr.get_value();
    if (animatable && animatable->has_default()) {
      animatable->get_default(&values);
    }
  }
  return values;
}

}  // namespace

bool RefineGeomMesh(const GeomMesh &mesh, int32_t level,
                    const std::vector<float> &points_xyz,
                    const std::vector<uint32_t> &faceVertexCounts,
                    const std::vector<uint32_t> &faceVertexIndices,
                    RefinedMesh *out, std::string *err) {
  Options options;
  options.level = level;

  switch (mesh.subdivisionScheme.get_value()) {
    case GeomMesh::SubdivisionScheme::CatmullClark:
      options.scheme = Scheme::CatmullClark;
      break;
    case GeomMesh::SubdivisionScheme::Loop:
      options.scheme = Scheme::Loop;
      break;
    case GeomMesh::SubdivisionScheme::Bilinear:
      options.scheme = Scheme::Bilinear;
      break;
    case GeomMesh::SubdivisionScheme::SubdivisionSchemeNone:
      if (err) {
        (*err) +=
            "subdivisionScheme is 'none'; skip subdivision on the caller "
            "side.\n";
      }
      return false;
  }

  options.boundary = BoundaryInterpolation::EdgeAndCorner;
  {
    GeomMesh::InterpolateBoundary boundary =
        GeomMesh::InterpolateBoundary::EdgeAndCorner;
    const auto &animatable = mesh.interpolateBoundary.get_value();
    if (animatable.has_default()) {
      animatable.get_default(&boundary);
    }
    switch (boundary) {
      case GeomMesh::InterpolateBoundary::EdgeAndCorner:
        options.boundary = BoundaryInterpolation::EdgeAndCorner;
        break;
      case GeomMesh::InterpolateBoundary::EdgeOnly:
        options.boundary = BoundaryInterpolation::EdgeOnly;
        break;
      case GeomMesh::InterpolateBoundary::InterpolateBoundaryNone:
        options.boundary = BoundaryInterpolation::None;
        break;
    }
  }

  const std::vector<int32_t> corner_indices = GetTagValues(mesh.cornerIndices);
  const std::vector<float> corner_sharpnesses =
      GetTagValues(mesh.cornerSharpnesses);
  const std::vector<int32_t> crease_indices = GetTagValues(mesh.creaseIndices);
  const std::vector<int32_t> crease_lengths = GetTagValues(mesh.creaseLengths);
  const std::vector<float> crease_sharpnesses =
      GetTagValues(mesh.creaseSharpnesses);
  const std::vector<int32_t> hole_indices = GetTagValues(mesh.holeIndices);

  MeshView view;
  view.points = points_xyz.data();
  view.num_points = uint32_t(points_xyz.size() / 3);
  view.face_vertex_counts = faceVertexCounts.data();
  view.num_faces = uint32_t(faceVertexCounts.size());
  view.face_vertex_indices = faceVertexIndices.data();
  view.num_face_vertex_indices = uint32_t(faceVertexIndices.size());

  if (!corner_indices.empty() &&
      corner_indices.size() == corner_sharpnesses.size()) {
    view.corner_indices = corner_indices.data();
    view.num_corners = uint32_t(corner_indices.size());
    view.corner_sharpnesses = corner_sharpnesses.data();
  }
  if (!crease_lengths.empty()) {
    view.crease_indices = crease_indices.data();
    view.num_crease_indices = uint32_t(crease_indices.size());
    view.crease_lengths = crease_lengths.data();
    view.num_crease_lengths = uint32_t(crease_lengths.size());
    view.crease_sharpnesses = crease_sharpnesses.data();
    view.num_crease_sharpnesses = uint32_t(crease_sharpnesses.size());
  }
  if (!hole_indices.empty()) {
    view.hole_indices = hole_indices.data();
    view.num_holes = uint32_t(hole_indices.size());
  }

  std::string tsd_err;
  const Result r = Refine(view, options, out, &tsd_err);
  if (r != Result::Success) {
    if (err) {
      (*err) += "tinysubdiv refinement failed (";
      (*err) += to_string(r);
      (*err) += "): " + tsd_err;
    }
    return false;
  }
  return true;
}

}  // namespace tsd
}  // namespace tinyusdz
