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
/// Compute parallel transport frame
///
/// Computes a minimally-rotating reference frame along a curve using
/// parallel transport (rotation minimizing frames). This eliminates
/// twisting artifacts in tube geometry.
///
/// @param[in] prev_tangent Previous tangent vector
/// @param[in] curr_tangent Current tangent vector
/// @param[in] prev_normal Previous normal vector
/// @param[in] prev_binormal Previous binormal vector
/// @param[out] curr_normal Output current normal
/// @param[out] curr_binormal Output current binormal
///
inline void ParallelTransportFrame(
    const value::float3 &prev_tangent,
    const value::float3 &curr_tangent,
    const value::float3 &prev_normal,
    const value::float3 &prev_binormal,
    value::float3 &curr_normal,
    value::float3 &curr_binormal) {

  // Compute rotation axis and angle between tangents
  value::float3 axis = Cross(prev_tangent, curr_tangent);
  float axis_len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] + axis[2] * axis[2]);

  // If tangents are parallel, no rotation needed
  if (axis_len < 1e-6f) {
    curr_normal = prev_normal;
    curr_binormal = prev_binormal;
    return;
  }

  // Normalize axis
  axis[0] /= axis_len;
  axis[1] /= axis_len;
  axis[2] /= axis_len;

  // Compute rotation angle
  float cos_angle = Dot(prev_tangent, curr_tangent);
  cos_angle = std::max(-1.0f, std::min(1.0f, cos_angle));  // Clamp for safety
  float angle = std::acos(cos_angle);

  // Rotate previous normal and binormal around axis by angle
  // Using Rodrigues' rotation formula: v_rot = v*cos(θ) + (k×v)*sin(θ) + k*(k·v)*(1-cos(θ))
  float cos_a = std::cos(angle);
  float sin_a = std::sin(angle);

  // Rotate normal
  value::float3 k_cross_n = Cross(axis, prev_normal);
  float k_dot_n = Dot(axis, prev_normal);
  curr_normal = {
    prev_normal[0] * cos_a + k_cross_n[0] * sin_a + axis[0] * k_dot_n * (1.0f - cos_a),
    prev_normal[1] * cos_a + k_cross_n[1] * sin_a + axis[1] * k_dot_n * (1.0f - cos_a),
    prev_normal[2] * cos_a + k_cross_n[2] * sin_a + axis[2] * k_dot_n * (1.0f - cos_a)
  };

  // Rotate binormal
  value::float3 k_cross_b = Cross(axis, prev_binormal);
  float k_dot_b = Dot(axis, prev_binormal);
  curr_binormal = {
    prev_binormal[0] * cos_a + k_cross_b[0] * sin_a + axis[0] * k_dot_b * (1.0f - cos_a),
    prev_binormal[1] * cos_a + k_cross_b[1] * sin_a + axis[1] * k_dot_b * (1.0f - cos_a),
    prev_binormal[2] * cos_a + k_cross_b[2] * sin_a + axis[2] * k_dot_b * (1.0f - cos_a)
  };

  // Re-orthogonalize to ensure numerical stability
  curr_binormal = Normalize(Cross(curr_tangent, curr_normal));
  curr_normal = Normalize(Cross(curr_binormal, curr_tangent));
}

///
/// Helper: Distance between two points
///
inline float Distance(const value::float3 &a, const value::float3 &b) {
  float dx = b[0] - a[0];
  float dy = b[1] - a[1];
  float dz = b[2] - a[2];
  return std::sqrt(dx * dx + dy * dy + dz * dz);
}

///
/// Helper: Linear interpolation
///
inline value::float3 Lerp(const value::float3 &a, const value::float3 &b, float t) {
  return {
    a[0] + (b[0] - a[0]) * t,
    a[1] + (b[1] - a[1]) * t,
    a[2] + (b[2] - a[2]) * t
  };
}

///
/// Flatness test for Bezier curve segment
///
/// Tests if a cubic Bezier curve is flat enough to be approximated by a line.
/// Uses the distance from middle control points to the line connecting endpoints.
///
/// @param[in] p0 First control point
/// @param[in] p1 Second control point
/// @param[in] p2 Third control point
/// @param[in] p3 Fourth control point
/// @param[in] tolerance Maximum allowed deviation from flatness
/// @return true if curve is flat enough (within tolerance)
///
inline bool IsBezierFlat(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float tolerance) {

  // Check distance of p1 from line p0-p3
  value::float3 line_dir = {p3[0] - p0[0], p3[1] - p0[1], p3[2] - p0[2]};
  float line_len = Distance(p0, p3);

  if (line_len < 1e-8f) {
    return true;  // Degenerate case - all points nearly coincident
  }

  // Normalize line direction
  line_dir[0] /= line_len;
  line_dir[1] /= line_len;
  line_dir[2] /= line_len;

  // Vector from p0 to p1
  value::float3 to_p1 = {p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2]};

  // Project onto line and compute perpendicular distance
  float proj1 = Dot(to_p1, line_dir);
  value::float3 closest1 = {
    p0[0] + line_dir[0] * proj1,
    p0[1] + line_dir[1] * proj1,
    p0[2] + line_dir[2] * proj1
  };
  float dist1 = Distance(p1, closest1);

  // Same for p2
  value::float3 to_p2 = {p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2]};
  float proj2 = Dot(to_p2, line_dir);
  value::float3 closest2 = {
    p0[0] + line_dir[0] * proj2,
    p0[1] + line_dir[1] * proj2,
    p0[2] + line_dir[2] * proj2
  };
  float dist2 = Distance(p2, closest2);

  // Curve is flat if both control points are within tolerance of the line
  return (dist1 < tolerance) && (dist2 < tolerance);
}

///
/// Adaptive subdivision of cubic Bezier curve
///
/// Recursively subdivides a Bezier curve until it's flat enough or max depth reached.
/// Uses de Casteljau's algorithm for stable subdivision.
///
/// @param[in] p0 First control point
/// @param[in] p1 Second control point
/// @param[in] p2 Third control point
/// @param[in] p3 Fourth control point
/// @param[in] tolerance Flatness tolerance
/// @param[in] max_depth Maximum recursion depth
/// @param[in] depth Current recursion depth
/// @param[out] points Output evaluated points along curve
///
inline void SubdivideBezierAdaptive(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float tolerance,
    int max_depth,
    int depth,
    std::vector<value::float3> &points) {

  // Check if flat enough or max depth reached
  if (depth >= max_depth || IsBezierFlat(p0, p1, p2, p3, tolerance)) {
    // Add endpoint (start point already added by previous recursion or caller)
    points.push_back(p3);
    return;
  }

  // de Casteljau subdivision at t=0.5
  value::float3 p01 = Lerp(p0, p1, 0.5f);
  value::float3 p12 = Lerp(p1, p2, 0.5f);
  value::float3 p23 = Lerp(p2, p3, 0.5f);
  value::float3 p012 = Lerp(p01, p12, 0.5f);
  value::float3 p123 = Lerp(p12, p23, 0.5f);
  value::float3 p0123 = Lerp(p012, p123, 0.5f);  // Midpoint on curve

  // Recursively subdivide left half
  SubdivideBezierAdaptive(p0, p01, p012, p0123, tolerance, max_depth, depth + 1, points);

  // Recursively subdivide right half
  SubdivideBezierAdaptive(p0123, p123, p23, p3, tolerance, max_depth, depth + 1, points);
}

///
/// Adaptive subdivision of B-spline curve segment
///
/// For B-splines, we convert the segment to Bezier and use adaptive Bezier subdivision.
///
inline void SubdivideBSplineAdaptive(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float tolerance,
    int max_depth,
    std::vector<value::float3> &points) {

  // Convert uniform B-spline segment to Bezier control points
  // Bezier control points for equivalent curve:
  value::float3 b0 = {
    (p0[0] + 4.0f * p1[0] + p2[0]) / 6.0f,
    (p0[1] + 4.0f * p1[1] + p2[1]) / 6.0f,
    (p0[2] + 4.0f * p1[2] + p2[2]) / 6.0f
  };
  value::float3 b1 = {
    (2.0f * p1[0] + p2[0]) / 3.0f,
    (2.0f * p1[1] + p2[1]) / 3.0f,
    (2.0f * p1[2] + p2[2]) / 3.0f
  };
  value::float3 b2 = {
    (p1[0] + 2.0f * p2[0]) / 3.0f,
    (p1[1] + 2.0f * p2[1]) / 3.0f,
    (p1[2] + 2.0f * p2[2]) / 3.0f
  };
  value::float3 b3 = {
    (p1[0] + 4.0f * p2[0] + p3[0]) / 6.0f,
    (p1[1] + 4.0f * p2[1] + p3[1]) / 6.0f,
    (p1[2] + 4.0f * p2[2] + p3[2]) / 6.0f
  };

  // Subdivide as Bezier
  SubdivideBezierAdaptive(b0, b1, b2, b3, tolerance, max_depth, 0, points);
}

///
/// Adaptive subdivision of Catmull-Rom curve segment
///
inline void SubdivideCatmullRomAdaptive(
    const value::float3 &p0,
    const value::float3 &p1,
    const value::float3 &p2,
    const value::float3 &p3,
    float tolerance,
    int max_depth,
    std::vector<value::float3> &points) {

  // Convert Catmull-Rom to Bezier control points
  value::float3 b0 = p1;
  value::float3 b1 = {
    p1[0] + (p2[0] - p0[0]) / 6.0f,
    p1[1] + (p2[1] - p0[1]) / 6.0f,
    p1[2] + (p2[2] - p0[2]) / 6.0f
  };
  value::float3 b2 = {
    p2[0] - (p3[0] - p1[0]) / 6.0f,
    p2[1] - (p3[1] - p1[1]) / 6.0f,
    p2[2] - (p3[2] - p1[2]) / 6.0f
  };
  value::float3 b3 = p2;

  // Subdivide as Bezier
  SubdivideBezierAdaptive(b0, b1, b2, b3, tolerance, max_depth, 0, points);
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

  // Storage for parallel transport frames
  value::float3 prev_tangent = {0, 0, 1};
  value::float3 prev_normal = {1, 0, 0};
  value::float3 prev_binormal = {0, 1, 0};

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

    // Compute perpendicular frame
    value::float3 normal, binormal;
    if (is_ribbon) {
      // Ribbon mode: use provided normals
      normal = curve_normals[static_cast<size_t>(i)];
      binormal = Normalize(Cross(tangent, normal));
      normal = Normalize(Cross(binormal, tangent));  // Re-orthogonalize
    } else {
      // Cylinder mode: use parallel transport frame for minimum rotation
      if (i == 0) {
        // Initialize first frame using fixed up vector
        value::float3 up = {0, 1, 0};
        if (std::abs(Dot(tangent, up)) > 0.99f) {
          up = {1, 0, 0};  // Tangent nearly parallel to Y, use X instead
        }
        binormal = Normalize(Cross(tangent, up));
        normal = Normalize(Cross(binormal, tangent));
      } else {
        // Use parallel transport to propagate frame from previous point
        ParallelTransportFrame(prev_tangent, tangent, prev_normal, prev_binormal,
                              normal, binormal);
      }

      // Store for next iteration
      prev_tangent = tangent;
      prev_normal = normal;
      prev_binormal = binormal;
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
      // Cubic curves - evaluate basis functions with adaptive tessellation
      auto basis = curves.basis.get_value();

      if (options.adaptive) {
        // Adaptive tessellation based on curvature
        int max_depth = 8; // Maximum recursion depth
        float tolerance = options.max_edge_length * 0.5f; // Flatness tolerance

        // Process curve segments based on basis type
        if (basis == GeomBasisCurves::Basis::Bezier) {
          // Bezier: segments are groups of 4 points (p0, p1, p2, p3)
          int num_segments = (num_cvs - 1) / 3; // For cubic Bezier
          for (int seg = 0; seg < num_segments; seg++) {
            int base_idx = seg * 3;
            if (base_idx + 3 >= num_cvs) break;

            value::float3 p0 = ToFloat3(curve_points[point_offset + base_idx]);
            value::float3 p1 = ToFloat3(curve_points[point_offset + base_idx + 1]);
            value::float3 p2 = ToFloat3(curve_points[point_offset + base_idx + 2]);
            value::float3 p3 = ToFloat3(curve_points[point_offset + base_idx + 3]);

            if (seg == 0) {
              eval_points.push_back(p0); // Add first point
            }
            SubdivideBezierAdaptive(p0, p1, p2, p3, tolerance, max_depth, 0, eval_points);
          }
        } else if (basis == GeomBasisCurves::Basis::Bspline) {
          // B-spline: sliding window of 4 points
          for (int i = 0; i + 3 < num_cvs; i++) {
            value::float3 p0 = ToFloat3(curve_points[point_offset + i]);
            value::float3 p1 = ToFloat3(curve_points[point_offset + i + 1]);
            value::float3 p2 = ToFloat3(curve_points[point_offset + i + 2]);
            value::float3 p3 = ToFloat3(curve_points[point_offset + i + 3]);

            if (i == 0) {
              eval_points.push_back(p1); // B-spline starts at second point
            }
            SubdivideBSplineAdaptive(p0, p1, p2, p3, tolerance, max_depth, eval_points);
          }
        } else if (basis == GeomBasisCurves::Basis::CatmullRom) {
          // Catmull-Rom: sliding window of 4 points
          for (int i = 0; i + 3 < num_cvs; i++) {
            value::float3 p0 = ToFloat3(curve_points[point_offset + i]);
            value::float3 p1 = ToFloat3(curve_points[point_offset + i + 1]);
            value::float3 p2 = ToFloat3(curve_points[point_offset + i + 2]);
            value::float3 p3 = ToFloat3(curve_points[point_offset + i + 3]);

            if (i == 0) {
              eval_points.push_back(p1); // Catmull-Rom starts at second point
            }
            SubdivideCatmullRomAdaptive(p0, p1, p2, p3, tolerance, max_depth, eval_points);
          }
        }

        // Generate widths along adaptively subdivided curve
        if (!widths.empty()) {
          int num_eval_points = static_cast<int>(eval_points.size());
          for (int i = 0; i < num_eval_points; i++) {
            // Map eval point index to control point parameter
            float t_global = static_cast<float>(i) / static_cast<float>(num_eval_points - 1);
            float t_idx = t_global * static_cast<float>(num_cvs - 1);
            int idx = static_cast<int>(std::floor(t_idx));
            idx = std::min(idx, num_cvs - 2);
            float t = t_idx - static_cast<float>(idx);

            float w0 = widths[point_offset + static_cast<size_t>(idx)];
            float w1 = widths[point_offset + static_cast<size_t>(idx + 1)];
            eval_widths.push_back(w0 + (w1 - w0) * t);
          }
        }
      } else {
        // Uniform tessellation (non-adaptive)
        int segments = std::max(options.min_segments, (num_cvs - 1) * options.segments_per_span);
        segments = std::min(segments, options.max_segments);

        for (int seg = 0; seg <= segments; seg++) {
          float t_global = static_cast<float>(seg) / static_cast<float>(segments);

          // Simple linear interpolation fallback for uniform mode
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

///
/// Find knot span index for parameter u in knot vector
///
/// Uses binary search to find the knot span containing parameter u.
/// This is used for NURBS curve evaluation.
///
/// @param[in] n Number of control points - 1
/// @param[in] p Degree of the curve
/// @param[in] u Parameter value to evaluate
/// @param[in] knots Knot vector
/// @return Knot span index
///
inline int FindKnotSpan(int n, int p, double u, const std::vector<double> &knots) {
  // Special case: u at end of knot vector
  if (u >= knots[n + 1]) {
    return n;
  }

  // Special case: u at start of knot vector
  if (u <= knots[p]) {
    return p;
  }

  // Binary search
  int low = p;
  int high = n + 1;
  int mid = (low + high) / 2;

  while (u < knots[mid] || u >= knots[mid + 1]) {
    if (u < knots[mid]) {
      high = mid;
    } else {
      low = mid;
    }
    mid = (low + high) / 2;
  }

  return mid;
}

///
/// Compute NURBS basis functions
///
/// Computes all non-zero basis functions N_{i,p}(u) using Cox-de Boor recursion.
///
/// @param[in] i Knot span index
/// @param[in] u Parameter value
/// @param[in] p Degree
/// @param[in] knots Knot vector
/// @param[out] N Basis function values (size p+1)
///
inline void BasisFunctions(int i, double u, int p, const std::vector<double> &knots,
                          std::vector<double> &N) {
  N.resize(p + 1);
  std::vector<double> left(p + 1);
  std::vector<double> right(p + 1);

  N[0] = 1.0;

  for (int j = 1; j <= p; j++) {
    left[j] = u - knots[i + 1 - j];
    right[j] = knots[i + j] - u;
    double saved = 0.0;

    for (int r = 0; r < j; r++) {
      double temp = N[r] / (right[r + 1] + left[j - r]);
      N[r] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }

    N[j] = saved;
  }
}

///
/// Compute NURBS basis function derivatives
///
/// Computes basis functions and their derivatives up to order n using
/// the algorithm from "The NURBS Book" by Piegl & Tiller.
///
/// @param[in] i Knot span index
/// @param[in] u Parameter value
/// @param[in] p Degree
/// @param[in] n Derivative order (typically 1 for tangents)
/// @param[in] knots Knot vector
/// @param[out] ders Basis function derivatives [derivative_order][function_index]
///
inline void BasisFunctionDerivatives(int i, double u, int p, int n,
                                     const std::vector<double> &knots,
                                     std::vector<std::vector<double>> &ders) {
  ders.resize(n + 1);
  for (int k = 0; k <= n; k++) {
    ders[k].resize(p + 1, 0.0);
  }

  std::vector<std::vector<double>> ndu(p + 1, std::vector<double>(p + 1, 0.0));
  std::vector<double> left(p + 1);
  std::vector<double> right(p + 1);

  ndu[0][0] = 1.0;

  for (int j = 1; j <= p; j++) {
    left[j] = u - knots[i + 1 - j];
    right[j] = knots[i + j] - u;
    double saved = 0.0;

    for (int r = 0; r < j; r++) {
      // Lower triangle
      ndu[j][r] = right[r + 1] + left[j - r];
      double temp = ndu[r][j - 1] / ndu[j][r];

      // Upper triangle
      ndu[r][j] = saved + right[r + 1] * temp;
      saved = left[j - r] * temp;
    }

    ndu[j][j] = saved;
  }

  // Load basis functions
  for (int j = 0; j <= p; j++) {
    ders[0][j] = ndu[j][p];
  }

  // Compute derivatives
  std::vector<std::vector<double>> a(2, std::vector<double>(p + 1));

  for (int r = 0; r <= p; r++) {
    int s1 = 0;
    int s2 = 1;
    a[0][0] = 1.0;

    // Compute kth derivative
    for (int k = 1; k <= n; k++) {
      double d = 0.0;
      int rk = r - k;
      int pk = p - k;

      if (r >= k) {
        a[s2][0] = a[s1][0] / ndu[pk + 1][rk];
        d = a[s2][0] * ndu[rk][pk];
      }

      int j1 = (rk >= -1) ? 1 : -rk;
      int j2 = (r - 1 <= pk) ? k - 1 : p - r;

      for (int j = j1; j <= j2; j++) {
        a[s2][j] = (a[s1][j] - a[s1][j - 1]) / ndu[pk + 1][rk + j];
        d += a[s2][j] * ndu[rk + j][pk];
      }

      if (r <= pk) {
        a[s2][k] = -a[s1][k - 1] / ndu[pk + 1][r];
        d += a[s2][k] * ndu[r][pk];
      }

      ders[k][r] = d;

      // Switch rows
      int temp_s = s1;
      s1 = s2;
      s2 = temp_s;
    }
  }

  // Multiply through by the correct factors
  double r_val = static_cast<double>(p);
  for (int k = 1; k <= n; k++) {
    for (int j = 0; j <= p; j++) {
      ders[k][j] *= r_val;
    }
    r_val *= static_cast<double>(p - k);
  }
}

///
/// Evaluate NURBS curve derivatives
///
/// Computes curve point and derivatives at parameter u.
///
/// @param[in] n Number of control points - 1
/// @param[in] p Degree
/// @param[in] knots Knot vector
/// @param[in] control_points Control points
/// @param[in] weights Control point weights
/// @param[in] u Parameter value
/// @param[in] deriv_order Derivative order (1 for tangent, 2 for curvature)
/// @param[out] curve_ders Output derivatives [order][xyz]
///
inline void EvaluateNURBSCurveDerivatives(
    int n, int p,
    const std::vector<double> &knots,
    const std::vector<value::float3> &control_points,
    const std::vector<double> &weights,
    double u,
    int deriv_order,
    std::vector<value::float3> &curve_ders) {

  curve_ders.resize(deriv_order + 1, {0.0f, 0.0f, 0.0f});

  // Find knot span
  int span = FindKnotSpan(n, p, u, knots);

  // Compute basis function derivatives
  std::vector<std::vector<double>> ders;
  BasisFunctionDerivatives(span, u, p, deriv_order, knots, ders);

  bool has_weights = !weights.empty();

  // Compute curve derivatives (rational or non-rational)
  for (int k = 0; k <= deriv_order; k++) {
    value::float3 point = {0.0f, 0.0f, 0.0f};
    double w = 0.0;

    for (int j = 0; j <= p; j++) {
      int idx = span - p + j;
      if (idx < 0 || idx > n) continue;

      double weight = has_weights ? weights[idx] : 1.0;
      double basis_deriv = ders[k][j];

      point[0] += static_cast<float>(basis_deriv * weight * control_points[idx][0]);
      point[1] += static_cast<float>(basis_deriv * weight * control_points[idx][1]);
      point[2] += static_cast<float>(basis_deriv * weight * control_points[idx][2]);

      if (k == 0) {
        w += basis_deriv * weight;
      }
    }

    // For rational curves, apply quotient rule
    if (has_weights && k == 0) {
      if (w > 1e-10) {
        curve_ders[0][0] = point[0] / static_cast<float>(w);
        curve_ders[0][1] = point[1] / static_cast<float>(w);
        curve_ders[0][2] = point[2] / static_cast<float>(w);
      }
    } else {
      curve_ders[k] = point;
    }
  }
}

///
/// Evaluate point on NURBS curve
///
/// Evaluates a point on a NURBS curve at parameter u using the NURBS algorithm.
/// Handles both non-rational (all weights = 1) and rational NURBS.
///
/// @param[in] n Number of control points - 1
/// @param[in] p Degree of the curve
/// @param[in] knots Knot vector
/// @param[in] control_points Control points
/// @param[in] weights Control point weights (empty for non-rational)
/// @param[in] u Parameter value to evaluate
/// @return Evaluated point on curve
///
inline value::float3 EvaluateNURBSCurve(
    int n, int p,
    const std::vector<double> &knots,
    const std::vector<value::float3> &control_points,
    const std::vector<double> &weights,
    double u) {

  // Find the knot span
  int span = FindKnotSpan(n, p, u, knots);

  // Compute basis functions
  std::vector<double> N;
  BasisFunctions(span, u, p, knots, N);

  // Compute curve point
  value::float3 point = {0.0f, 0.0f, 0.0f};
  double w = 0.0;

  bool has_weights = !weights.empty();

  for (int i = 0; i <= p; i++) {
    int idx = span - p + i;
    if (idx < 0 || idx > n) continue;

    double weight = has_weights ? weights[idx] : 1.0;
    double basis_weight = N[i] * weight;

    point[0] += static_cast<float>(basis_weight * control_points[idx][0]);
    point[1] += static_cast<float>(basis_weight * control_points[idx][1]);
    point[2] += static_cast<float>(basis_weight * control_points[idx][2]);
    w += basis_weight;
  }

  // Normalize by weight (for rational NURBS)
  if (w > 1e-10) {
    point[0] /= static_cast<float>(w);
    point[1] /= static_cast<float>(w);
    point[2] /= static_cast<float>(w);
  }

  return point;
}

///
/// Convert NurbsCurves primitive to triangle mesh
///
/// Converts USD NurbsCurves geometry to a triangle mesh suitable for rendering.
/// Supports arbitrary order NURBS with optional rational weights.
///
/// @param[in] curves NurbsCurves primitive to convert
/// @param[in] options Tessellation options
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts (all triangles = 3)
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output vertex normals
/// @param[out] uvs Output texture coordinates
/// @return true on success, false on error
///
inline bool NurbsCurvesToMesh(
    const GeomNurbsCurves &curves,
    const CurveTessellationOptions &options,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  // Get curve data
  auto curve_points = curves.get_points();
  auto curve_counts = curves.get_curveVertexCounts();
  auto widths = curves.get_widths();
  auto curve_normals = curves.get_normals();

  if (curve_points.empty() || curve_counts.empty()) {
    return false;
  }

  // Get NURBS-specific data
  std::vector<int> orders;
  std::vector<double> knots;
  std::vector<double> weights;

  // Get order attribute
  if (auto order_attr = curves.order.get_value()) {
    std::vector<int> order_vals;
    if (order_attr.value().get(value::TimeCode::Default(), &order_vals)) {
      orders = std::move(order_vals);
    }
  }

  // Get knots attribute
  if (auto knots_attr = curves.knots.get_value()) {
    std::vector<double> knots_vals;
    if (knots_attr.value().get(value::TimeCode::Default(), &knots_vals)) {
      knots = std::move(knots_vals);
    }
  }

  // Get pointWeights attribute (optional - for rational NURBS)
  if (auto weights_attr = curves.pointWeights.get_value()) {
    std::vector<double> weights_vals;
    if (weights_attr.value().get(value::TimeCode::Default(), &weights_vals)) {
      weights = std::move(weights_vals);
    }
  }

  if (orders.empty() || knots.empty()) {
    return false; // NURBS requires order and knots
  }

  // Process each curve
  size_t point_offset = 0;

  for (size_t curve_idx = 0; curve_idx < curve_counts.size(); curve_idx++) {
    int num_cvs = curve_counts[curve_idx];
    if (num_cvs < 2) {
      continue;
    }

    // Get order for this curve (uniform across all curves if only one order specified)
    int order = (orders.size() == 1) ? orders[0] :
                (curve_idx < orders.size() ? orders[curve_idx] : 4);
    int degree = order - 1;

    // Validate
    if (degree < 1 || degree >= num_cvs) {
      point_offset += static_cast<size_t>(num_cvs);
      continue;
    }

    // Extract control points for this curve
    std::vector<value::float3> control_points;
    for (int i = 0; i < num_cvs; i++) {
      control_points.push_back(ToFloat3(curve_points[point_offset + static_cast<size_t>(i)]));
    }

    // Extract weights for this curve (if rational)
    std::vector<double> curve_weights;
    if (!weights.empty() && weights.size() >= point_offset + static_cast<size_t>(num_cvs)) {
      for (int i = 0; i < num_cvs; i++) {
        curve_weights.push_back(weights[point_offset + static_cast<size_t>(i)]);
      }
    }

    // Evaluate NURBS curve to dense point samples
    std::vector<value::float3> eval_points;
    std::vector<float> eval_widths;

    // Determine parameter range
    // Knot vector size should be: num_cvs + order
    // Valid parameter range is from knots[degree] to knots[num_cvs]

    if (knots.size() < static_cast<size_t>(num_cvs + order)) {
      point_offset += static_cast<size_t>(num_cvs);
      continue; // Invalid knot vector
    }

    double u_start = knots[degree];
    double u_end = knots[num_cvs];

    if (options.adaptive) {
      // Adaptive tessellation based on curvature
      // Sample curve and compute curvature to determine subdivision
      int initial_samples = std::max(options.min_segments, num_cvs * 2);
      initial_samples = std::min(initial_samples, 64);

      std::vector<double> sample_params;
      std::vector<value::float3> sample_points;
      std::vector<double> sample_curvatures;

      // Initial uniform sampling to compute curvature
      for (int i = 0; i <= initial_samples; i++) {
        double t = static_cast<double>(i) / static_cast<double>(initial_samples);
        double u = u_start + t * (u_end - u_start);
        u = std::max(u_start, std::min(u, u_end));

        std::vector<value::float3> ders;
        EvaluateNURBSCurveDerivatives(num_cvs - 1, degree, knots, control_points,
                                     curve_weights, u, 2, ders);

        sample_params.push_back(u);
        sample_points.push_back(ders[0]);

        // Compute curvature: κ = |C'(u) × C''(u)| / |C'(u)|³
        value::float3 deriv1 = ders[1];
        value::float3 deriv2 = ders.size() > 2 ? ders[2] : value::float3{0, 0, 0};

        value::float3 cross = Cross(deriv1, deriv2);
        float cross_len = std::sqrt(cross[0] * cross[0] + cross[1] * cross[1] + cross[2] * cross[2]);
        float deriv1_len = std::sqrt(deriv1[0] * deriv1[0] + deriv1[1] * deriv1[1] + deriv1[2] * deriv1[2]);

        double curvature = 0.0;
        if (deriv1_len > 1e-8) {
          curvature = cross_len / (deriv1_len * deriv1_len * deriv1_len);
        }
        sample_curvatures.push_back(curvature);
      }

      // Refine segments with high curvature
      eval_points.push_back(sample_points[0]);

      for (size_t i = 0; i < sample_points.size() - 1; i++) {
        double max_curv = std::max(sample_curvatures[i], sample_curvatures[i + 1]);

        // Determine number of subdivisions based on curvature
        int subdivisions = 1;
        if (max_curv > 0.01) {
          subdivisions = std::min(8, std::max(2, static_cast<int>(max_curv * 100.0)));
        }

        // Subdivide segment
        for (int j = 1; j <= subdivisions; j++) {
          double t_seg = static_cast<double>(j) / static_cast<double>(subdivisions);
          double u = sample_params[i] + t_seg * (sample_params[i + 1] - sample_params[i]);

          value::float3 pt = EvaluateNURBSCurve(
              num_cvs - 1, degree, knots, control_points, curve_weights, u);
          eval_points.push_back(pt);
        }
      }

      // Generate widths for adaptive points
      if (!widths.empty() && widths.size() > point_offset) {
        for (size_t i = 0; i < eval_points.size(); i++) {
          double t = static_cast<double>(i) / static_cast<double>(eval_points.size() - 1);
          float t_idx = static_cast<float>(t) * static_cast<float>(num_cvs - 1);
          int idx = static_cast<int>(std::floor(t_idx));
          idx = std::min(idx, num_cvs - 2);
          float frac = t_idx - static_cast<float>(idx);

          float w0 = widths[point_offset + static_cast<size_t>(idx)];
          float w1 = widths[point_offset + static_cast<size_t>(std::min(idx + 1, num_cvs - 1))];
          eval_widths.push_back(w0 + (w1 - w0) * frac);
        }
      }

    } else {
      // Uniform tessellation
      int num_samples = std::max(options.min_segments, num_cvs * options.segments_per_span);
      num_samples = std::min(num_samples, options.max_segments);

      // Evaluate curve at uniform parameter intervals
      for (int i = 0; i <= num_samples; i++) {
        double t = static_cast<double>(i) / static_cast<double>(num_samples);
        double u = u_start + t * (u_end - u_start);

        // Clamp u to valid range
        u = std::max(u_start, std::min(u, u_end));

        value::float3 pt = EvaluateNURBSCurve(
            num_cvs - 1, degree, knots, control_points, curve_weights, u);

        eval_points.push_back(pt);

        // Interpolate width
        if (!widths.empty() && widths.size() > point_offset) {
          // Simple linear interpolation of widths based on parameter
          float t_idx = t * static_cast<float>(num_cvs - 1);
          int idx = static_cast<int>(std::floor(t_idx));
          idx = std::min(idx, num_cvs - 2);
          float frac = t_idx - static_cast<float>(idx);

          float w0 = widths[point_offset + static_cast<size_t>(idx)];
          float w1 = widths[point_offset + static_cast<size_t>(std::min(idx + 1, num_cvs - 1))];
          eval_widths.push_back(w0 + (w1 - w0) * frac);
        }
      }
    }

    // Generate mesh for this curve
    std::vector<value::float3> eval_normals; // NURBS normals not yet supported
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
