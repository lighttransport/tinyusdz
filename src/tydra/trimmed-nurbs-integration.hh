// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Integration of Trimmed NURBS into Tydra tessellation pipeline
//
// This header provides conversion functions to bridge USD GeomNurbsSurface
// primitives with the modern trimmed NURBS tessellation algorithm.
//

#pragma once

#include "trimmed-nurbs.hh"
#include "../../src/usdGeom.hh"
#include "../../src/tinyusdz.hh"
#include <vector>
#include <string>

namespace tinyusdz {
namespace tydra {

///
/// Convert USD GeomNurbsSurface to internal TrimmedNurbsSurface representation
///
/// @param[in] nurbs_surface USD NURBS surface primitive
/// @param[out] trimmed_surface Output trimmed NURBS surface data
/// @param[out] err Error message on failure
/// @return true on success
///
inline bool ConvertGeomNurbsSurfaceToTrimmed(
    const GeomNurbsSurface &nurbs_surface,
    TrimmedNurbsSurface &trimmed_surface,
    std::string *err = nullptr) {

  // Extract surface data
  auto order = nurbs_surface.get_order();
  if (order.empty() || order.size() < 2) {
    if (err) *err = "Order must specify [order_u, order_v]";
    return false;
  }

  int order_u = order[0];
  int order_v = order[1];
  int degree_u = order_u - 1;
  int degree_v = order_v - 1;

  auto [num_u, num_v] = nurbs_surface.get_vertexCounts();
  if (num_u <= 0 || num_v <= 0) {
    if (err) *err = "Invalid vertex counts";
    return false;
  }

  // Validate control point count
  auto points = nurbs_surface.get_points();
  if (points.size() != static_cast<size_t>(num_u * num_v)) {
    if (err) {
      *err = "Point count mismatch: expected " +
             std::to_string(num_u * num_v) + ", got " + std::to_string(points.size());
    }
    return false;
  }

  // Extract knot vectors (combined [u_knots, v_knots])
  auto knots = nurbs_surface.get_knots();
  size_t knots_u_size = num_u + degree_u + 1;
  size_t knots_v_size = num_v + degree_v + 1;

  if (knots.size() < knots_u_size + knots_v_size) {
    if (err) *err = "Knot vector size mismatch";
    return false;
  }

  // Split knot vectors
  std::vector<double> knots_u(knots.begin(), knots.begin() + knots_u_size);
  std::vector<double> knots_v(knots.begin() + knots_u_size,
                             knots.begin() + knots_u_size + knots_v_size);

  // Fill surface data
  trimmed_surface.surface.control_points = points;
  trimmed_surface.surface.knots_u = knots_u;
  trimmed_surface.surface.knots_v = knots_v;
  trimmed_surface.surface.degree_u = degree_u;
  trimmed_surface.surface.degree_v = degree_v;
  trimmed_surface.surface.num_ctrl_u = num_u;
  trimmed_surface.surface.num_ctrl_v = num_v;

  // Extract weights if rational NURBS
  auto weights = nurbs_surface.get_pointWeights();
  if (!weights.empty()) {
    trimmed_surface.surface.weights = weights;
  }

  // TODO: Extract trim curves from nurbs_surface.trimCurvePoints, etc.
  // This requires parsing the USD trim curve data structure

  return trimmed_surface.Validate(err);
}

///
/// Tessellate a USD GeomNurbsSurface to a triangle mesh
///
/// @param[in] nurbs_surface USD NURBS surface primitive
/// @param[in] options Tessellation configuration
/// @param[out] out_mesh Output triangle mesh
/// @param[out] err Error message on failure
/// @return true on success
///
inline bool TessellateGeomNurbsSurface(
    const GeomNurbsSurface &nurbs_surface,
    const TrimmedNurbsTessellationOptions &options,
    RenderMesh &out_mesh,
    std::string *err = nullptr) {

  TrimmedNurbsSurface trimmed_surface;

  // Convert USD format to internal format
  if (!ConvertGeomNurbsSurfaceToTrimmed(nurbs_surface, trimmed_surface, err)) {
    return false;
  }

  // Tessellate
  TrimmedNurbsTessellator tessellator;
  return tessellator.Tessellate(trimmed_surface, options, out_mesh, err);
}

///
/// Extended tessellation options for NURBS surfaces in Tydra context
///
struct GeomNurbsSurfaceTessellationOptions : public TrimmedNurbsTessellationOptions {
  // Additional Tydra-specific options

  /// Apply material shading during conversion
  bool apply_materials = true;

  /// Apply transformation matrices
  bool apply_transforms = true;

  /// Merge adjacent surfaces into a single mesh
  bool merge_surfaces = false;

  /// Generate additional vertex data (tangents, bitangents)
  bool generate_tangents = false;
};

///
/// High-level function for tessellating GeomNurbsSurface in rendering context
///
inline bool TessellateNurbsSurfaceForRendering(
    const GeomNurbsSurface &nurbs_surface,
    const GeomNurbsSurfaceTessellationOptions &options,
    RenderMesh &out_mesh,
    std::string *err = nullptr) {

  TrimmedNurbsTessellationOptions base_options;
  base_options.adaptive = options.adaptive;
  base_options.screen_space_error = options.screen_space_error;
  base_options.max_edge_length = options.max_edge_length;
  base_options.generate_normals = options.generate_normals;
  base_options.generate_uvs = options.generate_uvs;
  base_options.camera_distance = options.camera_distance;
  base_options.field_of_view = options.field_of_view;

  return TessellateGeomNurbsSurface(nurbs_surface, base_options, out_mesh, err);
}

}  // namespace tydra
}  // namespace tinyusdz
