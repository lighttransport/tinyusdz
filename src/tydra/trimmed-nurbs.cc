// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Trimmed NURBS Surface Tessellation Implementation

#include "trimmed-nurbs.hh"
#include <algorithm>
#include <cmath>

namespace tinyusdz {
namespace tydra {

///
/// Evaluate trim curve at parameter t
///
ParamPoint TrimCurve2D::Evaluate(double t) const {
  return EvaluateTrimCurve(*this, t);
}

///
/// Main tessellation entry point
///
bool TrimmedNurbsTessellator::Tessellate(
    const TrimmedNurbsSurface &trimmed_surface,
    const TrimmedNurbsTessellationOptions &options,
    RenderMesh &out_mesh,
    std::string *err) {

  // Validate input
  if (!trimmed_surface.Validate(err)) {
    if (err && err->empty()) {
      *err = "Trimmed NURBS surface validation failed";
    }
    return false;
  }

  out_mesh.Clear();

  std::vector<value::point3f> vertices;
  std::vector<value::normal3f> normals;
  std::vector<value::texcoord2f> uvs;
  std::vector<uint32_t> indices;

  // Start recursive subdivision from the full parameter domain
  bool success = SubdivideDomain(
      trimmed_surface.surface,
      trimmed_surface.trim_loops,
      options,
      trimmed_surface.surface.param_u_start,
      trimmed_surface.surface.param_u_end,
      trimmed_surface.surface.param_v_start,
      trimmed_surface.surface.param_v_end,
      0,  // initial depth
      vertices,
      normals,
      uvs,
      indices);

  if (!success) {
    if (err) *err = "Tessellation subdivision failed";
    return false;
  }

  // Fill output mesh
  out_mesh.points = vertices;
  if (options.generate_normals) {
    out_mesh.normals = normals;
  }
  if (options.generate_uvs) {
    out_mesh.uvs = uvs;
  }

  // Convert to triangle list
  out_mesh.faceVertexIndices = indices;
  out_mesh.faceVertexCounts.clear();
  for (size_t i = 0; i < indices.size(); i += 3) {
    out_mesh.faceVertexCounts.push_back(3);  // All triangles
  }

  return true;
}

///
/// Recursive adaptive subdivision of parametric domain
///
bool TrimmedNurbsTessellator::SubdivideDomain(
    const NurbsSurfaceData &surface,
    const std::vector<TrimLoop> &trim_loops,
    const TrimmedNurbsTessellationOptions &options,
    double u_min, double u_max, double v_min, double v_max,
    uint32_t depth,
    std::vector<value::point3f> &out_vertices,
    std::vector<value::normal3f> &out_normals,
    std::vector<value::texcoord2f> &out_uvs,
    std::vector<uint32_t> &out_indices) {

  // Check if we've reached max recursion depth
  if (depth > 10) {
    // Fallback: tessellate with minimum divisions
    GenerateQuadMesh(surface, trim_loops, options,
                     u_min, u_max, v_min, v_max,
                     options.min_u_divisions,
                     options.min_v_divisions,
                     out_vertices, out_normals, out_uvs, out_indices);
    return true;
  }

  // Check if domain is flat enough
  if (!options.adaptive || IsDomainFlat(surface, options, u_min, u_max, v_min, v_max)) {
    // Determine appropriate subdivision level
    uint32_t u_divs = options.min_u_divisions;
    uint32_t v_divs = options.min_v_divisions;

    // Scale divisions based on domain size and curve complexity
    if (options.adaptive) {
      double u_span = u_max - u_min;
      double v_span = v_max - v_min;

      // Increase divisions for larger spans
      u_divs = std::min(
          options.max_u_divisions,
          std::max(options.min_u_divisions,
                  static_cast<uint32_t>(4 * (1.0 / u_span))));
      v_divs = std::min(
          options.max_v_divisions,
          std::max(options.min_v_divisions,
                  static_cast<uint32_t>(4 * (1.0 / v_span))));
    }

    GenerateQuadMesh(surface, trim_loops, options,
                     u_min, u_max, v_min, v_max,
                     u_divs, v_divs,
                     out_vertices, out_normals, out_uvs, out_indices);
    return true;
  }

  // Domain is not flat: subdivide into 4 quadrants
  double u_mid = (u_min + u_max) * 0.5;
  double v_mid = (v_min + v_max) * 0.5;

  // Bottom-left quadrant
  if (!SubdivideDomain(surface, trim_loops, options,
                       u_min, u_mid, v_min, v_mid, depth + 1,
                       out_vertices, out_normals, out_uvs, out_indices)) {
    return false;
  }

  // Bottom-right quadrant
  if (!SubdivideDomain(surface, trim_loops, options,
                       u_mid, u_max, v_min, v_mid, depth + 1,
                       out_vertices, out_normals, out_uvs, out_indices)) {
    return false;
  }

  // Top-left quadrant
  if (!SubdivideDomain(surface, trim_loops, options,
                       u_min, u_mid, v_mid, v_max, depth + 1,
                       out_vertices, out_normals, out_uvs, out_indices)) {
    return false;
  }

  // Top-right quadrant
  if (!SubdivideDomain(surface, trim_loops, options,
                       u_mid, u_max, v_mid, v_max, depth + 1,
                       out_vertices, out_normals, out_uvs, out_indices)) {
    return false;
  }

  return true;
}

///
/// Check if a rectangular domain is flat enough
/// Uses curvature-based flatness criterion
///
bool TrimmedNurbsTessellator::IsDomainFlat(
    const NurbsSurfaceData &surface,
    const TrimmedNurbsTessellationOptions &options,
    double u_min, double u_max, double v_min, double v_max) {

  // Sample 5 points on the domain boundary and interior
  std::vector<std::pair<double, double>> sample_points = {
    {u_min, v_min}, {u_max, v_min}, {u_min, v_max}, {u_max, v_max},
    {(u_min + u_max) * 0.5, (v_min + v_max) * 0.5}
  };

  // Evaluate curvature at sample points
  float max_curvature = 0.0f;

  for (const auto &[u, v] : sample_points) {
    auto [du, dv] = ComputeNurbsSurfaceDerivatives(surface, u, v);

    // Approximate curvature from derivative magnitudes
    float du_len = std::sqrt(du.x * du.x + du.y * du.y + du.z * du.z);
    float dv_len = std::sqrt(dv.x * dv.x + dv.y * dv.y + dv.z * dv.z);

    // Cross product magnitude indicates "flatness"
    float cross_len = std::sqrt(
        std::pow(du.y * dv.z - du.z * dv.y, 2.0f) +
        std::pow(du.z * dv.x - du.x * dv.z, 2.0f) +
        std::pow(du.x * dv.y - du.y * dv.x, 2.0f));

    if (du_len > 1e-6f && dv_len > 1e-6f) {
      float curvature = cross_len / (du_len * dv_len);
      max_curvature = std::max(max_curvature, curvature);
    }
  }

  // Compare against tolerance
  // Larger domains or higher curvature require finer subdivision
  float domain_area = (u_max - u_min) * (v_max - v_min);
  float tolerance = options.max_edge_length / (domain_area * 10.0f);

  return max_curvature < tolerance;
}

///
/// Generate triangle mesh for a rectangular parametric domain
///
void TrimmedNurbsTessellator::GenerateQuadMesh(
    const NurbsSurfaceData &surface,
    const std::vector<TrimLoop> &trim_loops,
    const TrimmedNurbsTessellationOptions &options,
    double u_min, double u_max, double v_min, double v_max,
    uint32_t u_divs, uint32_t v_divs,
    std::vector<value::point3f> &out_vertices,
    std::vector<value::normal3f> &out_normals,
    std::vector<value::texcoord2f> &out_uvs,
    std::vector<uint32_t> &out_indices) {

  uint32_t start_vertex_idx = static_cast<uint32_t>(out_vertices.size());

  // Generate vertices at grid points
  std::vector<std::vector<uint32_t>> grid(u_divs + 1);
  for (size_t i = 0; i <= u_divs; ++i) {
    grid[i].resize(v_divs + 1);
  }

  // Create vertices only for points inside trim region
  for (uint32_t i = 0; i <= u_divs; ++i) {
    for (uint32_t j = 0; j <= v_divs; ++j) {
      double u = u_min + (static_cast<double>(i) / u_divs) * (u_max - u_min);
      double v = v_min + (static_cast<double>(j) / v_divs) * (v_max - v_min);

      ParamPoint param{u, v};

      // Check if point is in trim region
      bool inside = true;
      if (!trim_loops.empty()) {
        // Point is inside if it's inside the outer boundary
        // and outside all hole boundaries
        for (size_t loop_idx = 0; loop_idx < trim_loops.size(); ++loop_idx) {
          bool point_in_loop = IsPointInTrimRegion(param, trim_loops[loop_idx]);

          if (loop_idx == 0) {
            // First loop should be outer boundary
            inside = point_in_loop;
          } else {
            // Subsequent loops are holes
            if (point_in_loop) {
              inside = false;
            }
          }
        }
      }

      if (!inside) {
        grid[i][j] = UINT32_MAX;  // Mark as invalid
        continue;
      }

      // Evaluate surface at this point
      value::float3 pos = EvaluateNurbsSurface(surface, u, v);
      value::float3 normal = options.generate_normals ?
                            ComputeSurfaceNormal(surface, u, v) :
                            value::float3{0, 0, 1};

      grid[i][j] = start_vertex_idx + static_cast<uint32_t>(out_vertices.size() - start_vertex_idx);

      out_vertices.push_back(pos);
      if (options.generate_normals) {
        out_normals.push_back(value::normal3f{normal.x, normal.y, normal.z});
      }
      if (options.generate_uvs) {
        out_uvs.push_back(value::texcoord2f{
            static_cast<float>((u - u_min) / (u_max - u_min) * options.uv_scale_u),
            static_cast<float>((v - v_min) / (v_max - v_min) * options.uv_scale_v)});
      }
    }
  }

  // Generate triangles
  for (uint32_t i = 0; i < u_divs; ++i) {
    for (uint32_t j = 0; j < v_divs; ++j) {
      uint32_t idx00 = grid[i][j];
      uint32_t idx10 = grid[i + 1][j];
      uint32_t idx01 = grid[i][j + 1];
      uint32_t idx11 = grid[i + 1][j + 1];

      // Check if all four corners are valid
      if (idx00 == UINT32_MAX || idx10 == UINT32_MAX ||
          idx01 == UINT32_MAX || idx11 == UINT32_MAX) {
        continue;  // Skip trimmed quads
      }

      // Create two triangles for this quad
      // Triangle 1: (0,0), (1,0), (0,1)
      out_indices.push_back(idx00);
      out_indices.push_back(idx10);
      out_indices.push_back(idx01);

      // Triangle 2: (1,0), (1,1), (0,1)
      out_indices.push_back(idx10);
      out_indices.push_back(idx11);
      out_indices.push_back(idx01);
    }
  }
}

}  // namespace tydra
}  // namespace tinyusdz
