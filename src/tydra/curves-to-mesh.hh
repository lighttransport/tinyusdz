// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Curves to Mesh conversion utilities for Tydra
// Converts BasisCurves and NurbsCurves primitives to triangle meshes
//

#pragma once

#include <vector>
#include <cmath>
#include <array>
#include <algorithm>

#include "../../src/math-util.inc"
#include "../../src/value-types.hh"
#include "../../src/usdGeom.hh"

namespace tinyusdz {
namespace tydra {

///
/// Curve tessellation mode for converting curves to mesh
///
enum class CurveTessellationMode {
  /// Simple line segments (for linear curves)
  /// Fastest, no width variation, creates degenerate triangles
  LineSegments,

  /// Cylindrical tubes around curve path
  /// Good for curves with widths, creates circular cross-sections
  /// Default for most use cases
  Cylinder,

  /// Ribbons oriented by normals
  /// Use when curves have normals attribute for ribbon rendering
  /// Creates flat rectangular cross-sections
  Ribbon,

  /// Cards/billboards (camera-facing rectangles)
  /// Optimized for rendering many thin curves (hair/fur)
  /// Requires view direction
  Cards
};

///
/// Configuration for curve-to-mesh tessellation
///
struct CurveTessellationOptions {
  /// Tessellation mode
  CurveTessellationMode mode = CurveTessellationMode::Cylinder;

  /// Number of radial subdivisions for cylindrical mode (4-32 recommended)
  /// Higher values = smoother tubes but more triangles
  int radial_subdivisions = 8;

  /// Number of longitudinal segments per curve segment
  /// Controls smoothness along curve length
  /// For bezier/bspline: segments between control points
  int segments_per_span = 4;

  /// Adaptive tessellation based on curvature
  /// Subdivides more where curve bends sharply
  bool adaptive = true;

  /// Maximum edge length for adaptive tessellation (in world units)
  /// Smaller = finer tessellation in high-curvature regions
  float max_edge_length = 0.1f;

  /// Minimum segments per curve (prevents over-simplification)
  int min_segments = 2;

  /// Maximum segments per curve (prevents explosion)
  int max_segments = 256;

  /// Default width when widths attribute is not present
  float default_width = 0.05f;

  /// Generate texture coordinates (U along curve, V around circumference)
  bool generate_uvs = true;

  /// Generate normals
  bool generate_normals = true;

  /// View direction for Cards mode (normalized)
  value::float3 view_direction = {0, 0, 1};
};

///
/// Helper: Convert point3f to float3
///
inline value::float3 ToFloat3(const value::point3f &p) {
  return {p.x, p.y, p.z};
}

///
/// Helper: Convert normal3f to float3
///
inline value::float3 ToFloat3(const value::normal3f &n) {
  return {n.x, n.y, n.z};
}

///
/// Helper: Evaluate cubic Bezier curve at parameter t [0,1]
///
inline value::float3 EvaluateBezier(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1.0f - t;
  float mt2 = mt * mt;
  float mt3 = mt2 * mt;

  return {
    mt3 * p0[0] + 3.0f * mt2 * t * p1[0] + 3.0f * mt * t2 * p2[0] + t3 * p3[0],
    mt3 * p0[1] + 3.0f * mt2 * t * p1[1] + 3.0f * mt * t2 * p2[1] + t3 * p3[1],
    mt3 * p0[2] + 3.0f * mt2 * t * p1[2] + 3.0f * mt * t2 * p2[2] + t3 * p3[2]
  };
}

///
/// Helper: Evaluate cubic Bezier tangent at parameter t [0,1]
///
inline value::float3 EvaluateBezierTangent(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float t) {
  float t2 = t * t;
  float mt = 1.0f - t;
  float mt2 = mt * mt;

  return {
    -3.0f * mt2 * p0[0] + 3.0f * mt2 * p1[0] - 6.0f * mt * t * p1[0] - 3.0f * t2 * p2[0] + 6.0f * mt * t * p2[0] + 3.0f * t2 * p3[0],
    -3.0f * mt2 * p0[1] + 3.0f * mt2 * p1[1] - 6.0f * mt * t * p1[1] - 3.0f * t2 * p2[1] + 6.0f * mt * t * p2[1] + 3.0f * t2 * p3[1],
    -3.0f * mt2 * p0[2] + 3.0f * mt2 * p1[2] - 6.0f * mt * t * p1[2] - 3.0f * t2 * p2[2] + 6.0f * mt * t * p2[2] + 3.0f * t2 * p3[2]
  };
}

///
/// Helper: Evaluate cubic B-spline at parameter t [0,1] within a segment
///
inline value::float3 EvaluateBSpline(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float t) {
  float t2 = t * t;
  float t3 = t2 * t;
  float mt = 1.0f - t;
  float mt2 = mt * mt;
  float mt3 = mt2 * mt;

  // B-spline basis functions
  float b0 = mt3 / 6.0f;
  float b1 = (3.0f * t3 - 6.0f * t2 + 4.0f) / 6.0f;
  float b2 = (-3.0f * t3 + 3.0f * t2 + 3.0f * t + 1.0f) / 6.0f;
  float b3 = t3 / 6.0f;

  return {
    b0 * p0[0] + b1 * p1[0] + b2 * p2[0] + b3 * p3[0],
    b0 * p0[1] + b1 * p1[1] + b2 * p2[1] + b3 * p3[1],
    b0 * p0[2] + b1 * p2[1] + b2 * p2[2] + b3 * p3[2]
  };
}

///
/// Helper: Evaluate Catmull-Rom spline at parameter t [0,1]
///
inline value::float3 EvaluateCatmullRom(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float t) {
  float t2 = t * t;
  float t3 = t2 * t;

  // Catmull-Rom matrix
  value::float3 result;
  for (int i = 0; i < 3; i++) {
    result[i] = 0.5f * (
      (2.0f * p1[i]) +
      (-p0[i] + p2[i]) * t +
      (2.0f * p0[i] - 5.0f * p1[i] + 4.0f * p2[i] - p3[i]) * t2 +
      (-p0[i] + 3.0f * p1[i] - 3.0f * p2[i] + p3[i]) * t3
    );
  }
  return result;
}

///
/// Helper: Normalize vector
///
inline value::float3 Normalize(const value::float3 &v) {
  float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
  if (len < 1e-8f) {
    return {0, 0, 1};  // Default up vector
  }
  return {v[0] / len, v[1] / len, v[2] / len};
}

///
/// Helper: Cross product
///
inline value::float3 Cross(const value::float3 &a, const value::float3 &b) {
  return {
    a[1] * b[2] - a[2] * b[1],
    a[2] * b[0] - a[0] * b[2],
    a[0] * b[1] - a[1] * b[0]
  };
}

///
/// Helper: Dot product
///
inline float Dot(const value::float3 &a, const value::float3 &b) {
  return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

///
/// Generate cylindrical tube mesh around a curve
///
/// @param[in] curve_points Evaluated points along the curve spine
/// @param[in] curve_widths Radius at each point (half of width attribute)
/// @param[in] curve_normals Optional normals for ribbon mode
/// @param[in] options Tessellation options
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output per-face-vertex normals
/// @param[out] uvs Output per-face-vertex texture coordinates
///
inline void GenerateCylindricalCurveMesh(
    const std::vector<value::float3> &curve_points,
    const std::vector<float> &curve_widths,
    const std::vector<value::float3> &curve_normals,  // Empty for cylinder mode
    const CurveTessellationOptions &options,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  if (curve_points.size() < 2) {
    return;  // Need at least 2 points
  }

  const int num_points = static_cast<int>(curve_points.size());
  const int num_radial = options.radial_subdivisions;
  const bool is_ribbon = (options.mode == CurveTessellationMode::Ribbon && !curve_normals.empty());

  points.clear();
  faceVertexIndices.clear();
  faceVertexCounts.clear();
  if (options.generate_normals) normals.clear();
  if (options.generate_uvs) uvs.clear();

  // Generate vertices
  for (int i = 0; i < num_points; i++) {
    const value::float3 &pos = curve_points[static_cast<size_t>(i)];
    float radius = curve_widths.empty() ? options.default_width * 0.5f :
                   curve_widths[static_cast<size_t>(i)] * 0.5f;

    // Compute tangent
    value::float3 tangent;
    if (i == 0) {
      tangent = Normalize({
        curve_points[1][0] - pos[0],
        curve_points[1][1] - pos[1],
        curve_points[1][2] - pos[2]
      });
    } else if (i == num_points - 1) {
      tangent = Normalize({
        pos[0] - curve_points[static_cast<size_t>(i - 1)][0],
        pos[1] - curve_points[static_cast<size_t>(i - 1)][1],
        pos[2] - curve_points[static_cast<size_t>(i - 1)][2]
      });
    } else {
      tangent = Normalize({
        curve_points[static_cast<size_t>(i + 1)][0] - curve_points[static_cast<size_t>(i - 1)][0],
        curve_points[static_cast<size_t>(i + 1)][1] - curve_points[static_cast<size_t>(i - 1)][1],
        curve_points[static_cast<size_t>(i + 1)][2] - curve_points[static_cast<size_t>(i - 1)][2]
      });
    }

    // Compute perpendicular frame (Frenet frame or ribbon normal)
    value::float3 normal, binormal;
    if (is_ribbon) {
      normal = curve_normals[static_cast<size_t>(i)];
      binormal = Normalize(Cross(tangent, normal));
      normal = Normalize(Cross(binormal, tangent));  // Re-orthogonalize
    } else {
      // Use minimum rotation frame (parallel transport)
      // For simplicity, use fixed up vector approach
      value::float3 up = {0, 1, 0};
      if (std::abs(Dot(tangent, up)) > 0.99f) {
        up = {1, 0, 0};  // Tangent nearly parallel to Y, use X instead
      }
      binormal = Normalize(Cross(tangent, up));
      normal = Normalize(Cross(binormal, tangent));
    }

    // Generate ring of vertices around this spine point
    float u = static_cast<float>(i) / static_cast<float>(num_points - 1);
    for (int j = 0; j < num_radial; j++) {
      float angle = 2.0f * static_cast<float>(M_PI) * static_cast<float>(j) / static_cast<float>(num_radial);
      float cos_a = std::cos(angle);
      float sin_a = std::sin(angle);

      // Position on circle
      value::float3 offset = {
        (normal[0] * cos_a + binormal[0] * sin_a) * radius,
        (normal[1] * cos_a + binormal[1] * sin_a) * radius,
        (normal[2] * cos_a + binormal[2] * sin_a) * radius
      };

      points.push_back({pos[0] + offset[0], pos[1] + offset[1], pos[2] + offset[2]});

      // Store normal for this vertex (if needed)
      if (options.generate_normals) {
        value::float3 n = Normalize(offset);
        normals.push_back(n);
      }

      if (options.generate_uvs) {
        float v = static_cast<float>(j) / static_cast<float>(num_radial);
        uvs.push_back({u, v});
      }
    }
  }

  // Generate quad faces connecting rings
  for (int i = 0; i < num_points - 1; i++) {
    for (int j = 0; j < num_radial; j++) {
      int curr = i * num_radial + j;
      int next = i * num_radial + (j + 1) % num_radial;
      int curr_next_ring = (i + 1) * num_radial + j;
      int next_next_ring = (i + 1) * num_radial + (j + 1) % num_radial;

      // Quad (or two triangles)
      faceVertexIndices.push_back(curr);
      faceVertexIndices.push_back(curr_next_ring);
      faceVertexIndices.push_back(next_next_ring);
      faceVertexIndices.push_back(next);

      faceVertexCounts.push_back(4);
    }
  }
}

///
/// Convert BasisCurves to mesh with cylindrical tessellation
///
/// @param[in] curves BasisCurves primitive
/// @param[in] options Tessellation options
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output per-face-vertex normals
/// @param[out] uvs Output per-face-vertex texture coordinates
/// @return true on success
///
inline bool BasisCurvesToMesh(
    const GeomBasisCurves &curves,
    const CurveTessellationOptions &options,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  // Get curve data
  auto curve_points = curves.get_points();
  auto counts = curves.get_curveVertexCounts();
  auto widths = curves.get_widths();
  auto curve_normals = curves.get_normals();

  if (curve_points.empty() || counts.empty()) {
    return false;
  }

  // Get basis and type
  auto basis = curves.basis.get_value();
  auto type = curves.type.get_value();

  // Process each curve strand
  size_t point_offset = 0;
  for (size_t curve_idx = 0; curve_idx < counts.size(); curve_idx++) {
    int num_cvs = counts[curve_idx];
    if (num_cvs < 2) {
      point_offset += static_cast<size_t>(num_cvs);
      continue;
    }

    // Evaluate curve to dense point samples
    std::vector<value::float3> eval_points;
    std::vector<float> eval_widths;
    std::vector<value::float3> eval_normals;

    // For linear curves, just use the control points directly
    if (type == GeomBasisCurves::Type::Linear) {
      for (int i = 0; i < num_cvs; i++) {
        eval_points.push_back(ToFloat3(curve_points[point_offset + static_cast<size_t>(i)]));
        if (!widths.empty()) {
          eval_widths.push_back(widths[point_offset + static_cast<size_t>(i)]);
        }
        if (!curve_normals.empty()) {
          eval_normals.push_back(ToFloat3(curve_normals[point_offset + static_cast<size_t>(i)]));
        }
      }
    } else {
      // Cubic curves - evaluate basis functions
      int segments = std::max(options.min_segments, (num_cvs - 1) * options.segments_per_span);
      segments = std::min(segments, options.max_segments);

      for (int seg = 0; seg <= segments; seg++) {
        float t_global = static_cast<float>(seg) / static_cast<float>(segments);

        // TODO: Implement proper basis evaluation for bezier/bspline/catmullRom
        // For now, use linear interpolation as placeholder
        float t_idx = t_global * static_cast<float>(num_cvs - 1);
        int idx = static_cast<int>(std::floor(t_idx));
        idx = std::min(idx, num_cvs - 2);
        float t = t_idx - static_cast<float>(idx);

        const auto &p0 = curve_points[point_offset + static_cast<size_t>(idx)];
        const auto &p1 = curve_points[point_offset + static_cast<size_t>(idx + 1)];

        eval_points.push_back({
          p0.x + (p1.x - p0.x) * t,
          p0.y + (p1.y - p0.y) * t,
          p0.z + (p1.z - p0.z) * t
        });

        if (!widths.empty()) {
          float w0 = widths[point_offset + static_cast<size_t>(idx)];
          float w1 = widths[point_offset + static_cast<size_t>(idx + 1)];
          eval_widths.push_back(w0 + (w1 - w0) * t);
        }
      }
    }

    // Generate mesh for this curve
    GenerateCylindricalCurveMesh(
      eval_points, eval_widths, eval_normals, options,
      points, faceVertexCounts, faceVertexIndices, normals, uvs
    );

    point_offset += static_cast<size_t>(num_cvs);
  }

  return true;
}

}  // namespace tydra
}  // namespace tinyusdz
