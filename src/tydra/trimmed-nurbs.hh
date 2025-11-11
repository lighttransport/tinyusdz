// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Trimmed NURBS Surface Tessellation
//
// Modern GPU-friendly tessellation algorithm for trimmed NURBS surfaces:
// - Adaptive domain griding with screen-space error control
// - Efficient point-in-trim-region testing using raycasting
// - Curvature-adaptive tessellation for smooth surfaces
// - Parallel-ready architecture for GPU implementation
//
// References:
// [1] "Efficient trimmed NURBS tessellation" - Adaptive domain griding approach
// [2] "GPU-based trimming and tessellation" - Parallel evaluation strategies
// [3] "Direct NURBS evaluation on GPU" - Cox-de Boor in shader-friendly form
//

#pragma once

#include <vector>
#include <cmath>
#include <array>
#include <algorithm>
#include <cstring>

#include "../../src/value-types.hh"
#include "render-data.hh"

namespace tinyusdz {
namespace tydra {

///
/// 2D parametric point (u, v) in the parameter domain
///
struct ParamPoint {
  double u;
  double v;

  ParamPoint() : u(0.0), v(0.0) {}
  ParamPoint(double u_, double v_) : u(u_), v(v_) {}

  ParamPoint operator+(const ParamPoint &other) const {
    return ParamPoint(u + other.u, v + other.v);
  }

  ParamPoint operator-(const ParamPoint &other) const {
    return ParamPoint(u - other.u, v - other.v);
  }

  ParamPoint operator*(double t) const {
    return ParamPoint(u * t, v * t);
  }

  double Dot(const ParamPoint &other) const {
    return u * other.u + v * other.v;
  }

  double Length() const {
    return std::sqrt(u * u + v * v);
  }

  ParamPoint Normalized() const {
    double len = Length();
    if (len < 1e-10) return ParamPoint(0, 0);
    return ParamPoint(u / len, v / len);
  }
};

///
/// NURBS Surface Control Points and Definition
/// Represents a non-rational or rational B-spline surface
///
struct NurbsSurfaceData {
  // Control points in 3D space (flattened: size = num_u * num_v)
  std::vector<value::point3f> control_points;

  // Optional weights for rational NURBS (size = num_u * num_v, or empty for non-rational)
  std::vector<double> weights;

  // Knot vectors
  std::vector<double> knots_u;  // U direction knots
  std::vector<double> knots_v;  // V direction knots

  // Surface topology
  uint32_t degree_u = 3;  // U direction degree (cubic by default)
  uint32_t degree_v = 3;  // V direction degree
  uint32_t num_ctrl_u = 0;  // Number of control points in U
  uint32_t num_ctrl_v = 0;  // Number of control points in V

  // Parameter ranges (subset of full knot range)
  double param_u_start = 0.0;
  double param_u_end = 1.0;
  double param_v_start = 0.0;
  double param_v_end = 1.0;

  // Validate surface data
  bool Validate(std::string *err = nullptr) const {
    if (control_points.empty()) {
      if (err) *err = "Control points are empty";
      return false;
    }

    uint32_t expected_cpts = num_ctrl_u * num_ctrl_v;
    if (static_cast<uint32_t>(control_points.size()) != expected_cpts) {
      if (err) {
        *err = "Control points size mismatch: expected " +
               std::to_string(expected_cpts) + ", got " +
               std::to_string(control_points.size());
      }
      return false;
    }

    if (!weights.empty()) {
      if (static_cast<uint32_t>(weights.size()) != expected_cpts) {
        if (err) {
          *err = "Weights size mismatch: expected " +
                 std::to_string(expected_cpts) + ", got " +
                 std::to_string(weights.size());
        }
        return false;
      }
    }

    if (knots_u.size() != static_cast<size_t>(num_ctrl_u + degree_u + 1)) {
      if (err) *err = "Knots U size mismatch";
      return false;
    }

    if (knots_v.size() != static_cast<size_t>(num_ctrl_v + degree_v + 1)) {
      if (err) *err = "Knots V size mismatch";
      return false;
    }

    return true;
  }

  // Get control point by (i, j) index
  const value::point3f &GetControlPoint(uint32_t i, uint32_t j) const {
    return control_points[j * num_ctrl_u + i];
  }

  // Get weight by (i, j) index
  double GetWeight(uint32_t i, uint32_t j) const {
    if (weights.empty()) return 1.0;
    return weights[j * num_ctrl_u + i];
  }
};

///
/// 2D Trim Curve in parametric domain
/// Represents a single curve segment used for trimming
///
struct TrimCurve2D {
  // Control points in 2D parametric space
  std::vector<ParamPoint> control_points;

  // Knot vector (for NURBS trim curves)
  std::vector<double> knots;

  // Degree of the curve
  uint32_t degree = 3;

  // Weights for rational curves (empty = non-rational)
  std::vector<double> weights;

  // Type of curve
  enum class CurveType {
    Linear,      // Straight line segment
    BSpline,     // B-spline (possibly non-uniform)
    CircleArc,   // Circular arc
  } type = CurveType::BSpline;

  // For circular arcs
  ParamPoint circle_center;
  double circle_radius = 0.0;

  // Parameter range [0, 1]
  double param_start = 0.0;
  double param_end = 1.0;

  bool IsValid() const {
    return !control_points.empty();
  }

  // Evaluate curve at parameter t ∈ [0, 1]
  ParamPoint Evaluate(double t) const;
};

///
/// Trim Loop - closed sequence of trim curves
///
struct TrimLoop {
  std::vector<TrimCurve2D> curves;  // Curves forming the loop
  bool outer_boundary = true;        // true for outer boundary, false for holes

  bool IsValid() const {
    return !curves.empty() && std::all_of(
        curves.begin(), curves.end(),
        [](const TrimCurve2D &c) { return c.IsValid(); });
  }

  // Number of trim curves in this loop
  size_t NumCurves() const { return curves.size(); }
};

///
/// Complete Trimmed NURBS Surface Definition
///
struct TrimmedNurbsSurface {
  NurbsSurfaceData surface;
  std::vector<TrimLoop> trim_loops;

  bool Validate(std::string *err = nullptr) const {
    if (!surface.Validate(err)) return false;

    for (const auto &loop : trim_loops) {
      if (!loop.IsValid()) {
        if (err) *err = "Invalid trim loop";
        return false;
      }
    }

    return true;
  }

  bool HasTrim() const { return !trim_loops.empty(); }

  size_t NumTrimLoops() const { return trim_loops.size(); }
};

///
/// Configuration for tessellation of trimmed NURBS
///
struct TrimmedNurbsTessellationOptions {
  // Adaptive tessellation based on screen-space error
  bool adaptive = true;

  // Target screen-space error in pixels
  float screen_space_error = 1.0f;

  // Maximum edge length in world space (fallback if no screen info)
  float max_edge_length = 0.1f;

  // Minimum and maximum tessellation levels
  uint32_t min_u_divisions = 2;
  uint32_t max_u_divisions = 64;
  uint32_t min_v_divisions = 2;
  uint32_t max_v_divisions = 64;

  // Trim curve tessellation
  uint32_t trim_curve_divisions = 32;

  // Generate normals
  bool generate_normals = true;

  // Generate texture coordinates
  bool generate_uvs = true;

  // Texture coordinate scaling
  float uv_scale_u = 1.0f;
  float uv_scale_v = 1.0f;

  // Camera distance for screen-space error calculation
  float camera_distance = 1.0f;

  // FOV for screen-space error (radians)
  float field_of_view = 1.0f;  // ~57 degrees

  // Screen resolution height
  uint32_t screen_height = 720;
};

///
/// Cox-de Boor algorithm: Evaluate B-spline basis function
/// Returns N_{i,p}(u) for the given knot span and parameter
///
inline double BSplineBasis(int i, int p, double u,
                           const std::vector<double> &knots) {
  if (p == 0) {
    return (u >= knots[i] && u < knots[i + 1]) ? 1.0 : 0.0;
  }

  double left = u - knots[i];
  double right = knots[i + p + 1] - u;
  double left_denom = knots[i + p] - knots[i];
  double right_denom = knots[i + p + 1] - knots[i + 1];

  double result = 0.0;
  if (left_denom > 1e-10) {
    result += (left / left_denom) * BSplineBasis(i, p - 1, u, knots);
  }
  if (right_denom > 1e-10) {
    result += (right / right_denom) * BSplineBasis(i + 1, p - 1, u, knots);
  }

  return result;
}

///
/// Find knot span for parameter u
///
inline int FindKnotSpan(int n, int degree, double u,
                        const std::vector<double> &knots) {
  if (u >= knots[n + 1]) return n;

  int low = degree;
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
/// Evaluate NURBS surface at (u, v) in 3D space
/// Uses Cox-de Boor algorithm with rational weights
///
inline value::float3 EvaluateNurbsSurface(
    const NurbsSurfaceData &surface,
    double u, double v) {
  int span_u = FindKnotSpan(static_cast<int>(surface.num_ctrl_u) - 1,
                            surface.degree_u, u, surface.knots_u);
  int span_v = FindKnotSpan(static_cast<int>(surface.num_ctrl_v) - 1,
                            surface.degree_v, v, surface.knots_v);

  double sum_x = 0.0, sum_y = 0.0, sum_z = 0.0, sum_w = 0.0;

  for (int l = 0; l <= static_cast<int>(surface.degree_v); ++l) {
    int v_idx = span_v - surface.degree_v + l;
    double N_v = BSplineBasis(v_idx, surface.degree_v, v, surface.knots_v);

    for (int k = 0; k <= static_cast<int>(surface.degree_u); ++k) {
      int u_idx = span_u - surface.degree_u + k;
      double N_u = BSplineBasis(u_idx, surface.degree_u, u, surface.knots_u);

      double weight = surface.GetWeight(u_idx, v_idx);
      const auto &cp = surface.GetControlPoint(u_idx, v_idx);

      double basis = N_u * N_v * weight;

      sum_x += basis * cp.x;
      sum_y += basis * cp.y;
      sum_z += basis * cp.z;
      sum_w += basis;
    }
  }

  if (sum_w < 1e-10) {
    return value::float3{0, 0, 0};
  }

  return value::float3{
      static_cast<float>(sum_x / sum_w),
      static_cast<float>(sum_y / sum_w),
      static_cast<float>(sum_z / sum_w)};
}

///
/// Compute partial derivatives of NURBS surface
/// Returns S_u (du component) and S_v (dv component)
///
inline std::pair<value::float3, value::float3>
ComputeNurbsSurfaceDerivatives(
    const NurbsSurfaceData &surface,
    double u, double v) {
  // Simplified finite difference approach for stability
  // In production, implement analytical derivatives using NURBS derivative formulas
  const double delta = 1e-5;

  auto surf_u_plus = EvaluateNurbsSurface(surface, u + delta, v);
  auto surf_u_minus = EvaluateNurbsSurface(surface, u - delta, v);
  auto surf_v_plus = EvaluateNurbsSurface(surface, u, v + delta);
  auto surf_v_minus = EvaluateNurbsSurface(surface, u, v - delta);

  value::float3 du{
      (surf_u_plus.x - surf_u_minus.x) / (2.0f * delta),
      (surf_u_plus.y - surf_u_minus.y) / (2.0f * delta),
      (surf_u_plus.z - surf_u_minus.z) / (2.0f * delta)};

  value::float3 dv{
      (surf_v_plus.x - surf_v_minus.x) / (2.0f * delta),
      (surf_v_plus.y - surf_v_minus.y) / (2.0f * delta),
      (surf_v_plus.z - surf_v_minus.z) / (2.0f * delta)};

  return {du, dv};
}

///
/// Compute surface normal via cross product of derivatives
///
inline value::float3 ComputeSurfaceNormal(
    const NurbsSurfaceData &surface,
    double u, double v) {
  auto [du, dv] = ComputeNurbsSurfaceDerivatives(surface, u, v);

  // Normal = du × dv
  value::float3 normal{
      du.y * dv.z - du.z * dv.y,
      du.z * dv.x - du.x * dv.z,
      du.x * dv.y - du.y * dv.x};

  // Normalize
  float len = std::sqrt(normal.x * normal.x +
                        normal.y * normal.y +
                        normal.z * normal.z);
  if (len > 1e-6f) {
    normal.x /= len;
    normal.y /= len;
    normal.z /= len;
  }

  return normal;
}

///
/// Evaluate 2D trim curve using Cox-de Boor
///
inline ParamPoint EvaluateTrimCurve(
    const TrimCurve2D &curve,
    double t) {
  if (curve.type == TrimCurve2D::CurveType::Linear) {
    // Simple linear interpolation
    if (curve.control_points.size() >= 2) {
      double s = (t - curve.param_start) / (curve.param_end - curve.param_start);
      s = std::max(0.0, std::min(1.0, s));
      const auto &p0 = curve.control_points.front();
      const auto &p1 = curve.control_points.back();
      return ParamPoint{
          p0.u + s * (p1.u - p0.u),
          p0.v + s * (p1.v - p0.v)};
    }
    return curve.control_points.front();
  }

  if (curve.type == TrimCurve2D::CurveType::CircleArc) {
    // Evaluate circular arc
    double s = (t - curve.param_start) / (curve.param_end - curve.param_start);
    s = std::max(0.0, std::min(1.0, s));

    // Assuming control points define start and end points of arc
    if (curve.control_points.size() >= 2) {
      const auto &p0 = curve.control_points.front();
      const auto &p1 = curve.control_points.back();

      // Simple arc interpolation
      double angle0 = std::atan2(p0.v - curve.circle_center.v,
                                 p0.u - curve.circle_center.u);
      double angle1 = std::atan2(p1.v - curve.circle_center.v,
                                 p1.u - curve.circle_center.u);

      double angle = angle0 + s * (angle1 - angle0);
      return ParamPoint{
          curve.circle_center.u + curve.circle_radius * std::cos(angle),
          curve.circle_center.v + curve.circle_radius * std::sin(angle)};
    }
    return curve.circle_center;
  }

  // BSpline/NURBS case
  if (curve.knots.empty() || curve.control_points.empty()) {
    return ParamPoint(0, 0);
  }

  int n = static_cast<int>(curve.control_points.size()) - 1;
  double u = curve.param_start + (t - curve.param_start) /
             (curve.param_end - curve.param_start) * (curve.knots[n + curve.degree + 1] - curve.knots[0]);

  double sum_u = 0.0, sum_v = 0.0, sum_w = 0.0;

  for (int i = 0; i <= n; ++i) {
    double N = BSplineBasis(i, curve.degree, u, curve.knots);
    double weight = curve.weights.empty() ? 1.0 : curve.weights[i];
    double basis = N * weight;

    sum_u += basis * curve.control_points[i].u;
    sum_v += basis * curve.control_points[i].v;
    sum_w += basis;
  }

  if (sum_w < 1e-10) {
    return curve.control_points[0];
  }

  return ParamPoint{sum_u / sum_w, sum_v / sum_w};
}

///
/// Point-in-trim-region test using ray casting algorithm
/// Returns true if (u, v) is inside the trimmed region
///
inline bool IsPointInTrimRegion(
    const ParamPoint &pt,
    const TrimLoop &trim_loop) {
  // Raycast from point to infinity in +u direction
  // Count intersections with trim curves
  int crossing_count = 0;

  for (const auto &curve : trim_loop.curves) {
    // Sample curve and check for ray crossings
    const int samples = 32;  // Tessellate trim curve for intersection testing

    ParamPoint prev_pt = EvaluateTrimCurve(curve, 0.0);

    for (int i = 1; i <= samples; ++i) {
      double t = static_cast<double>(i) / samples;
      ParamPoint curr_pt = EvaluateTrimCurve(curve, t);

      // Check if segment crosses the horizontal ray from pt
      if ((prev_pt.v <= pt.v && curr_pt.v > pt.v) ||
          (curr_pt.v <= pt.v && prev_pt.v > pt.v)) {
        // Intersection occurs; check if it's to the right of pt
        double x_intersect =
            prev_pt.u + (pt.v - prev_pt.v) /
            (curr_pt.v - prev_pt.v) * (curr_pt.u - prev_pt.u);

        if (x_intersect > pt.u) {
          crossing_count++;
        }
      }

      prev_pt = curr_pt;
    }
  }

  // Odd crossing count means inside
  return (crossing_count % 2) == 1;
}

///
/// Tessellator for Trimmed NURBS surfaces
/// Converts trimmed NURBS to triangle mesh
///
class TrimmedNurbsTessellator {
public:
  ///
  /// Tessellate a trimmed NURBS surface into triangular mesh
  ///
  /// @param[in] trimmed_surface The trimmed NURBS surface
  /// @param[in] options Tessellation configuration
  /// @param[out] out_mesh Output triangle mesh
  /// @return true on success
  ///
  bool Tessellate(
      const TrimmedNurbsSurface &trimmed_surface,
      const TrimmedNurbsTessellationOptions &options,
      RenderMesh &out_mesh,
      std::string *err = nullptr);

private:
  ///
  /// Recursive adaptive subdivision for a rectangular parametric domain
  ///
  bool SubdivideDomain(
      const NurbsSurfaceData &surface,
      const std::vector<TrimLoop> &trim_loops,
      const TrimmedNurbsTessellationOptions &options,
      double u_min, double u_max, double v_min, double v_max,
      uint32_t depth,
      std::vector<value::point3f> &out_vertices,
      std::vector<value::normal3f> &out_normals,
      std::vector<value::texcoord2f> &out_uvs,
      std::vector<uint32_t> &out_indices);

  ///
  /// Check if a rectangular domain is "flat" enough for tessellation
  /// Uses screen-space error metric
  ///
  bool IsDomainFlat(
      const NurbsSurfaceData &surface,
      const TrimmedNurbsTessellationOptions &options,
      double u_min, double u_max, double v_min, double v_max);

  ///
  /// Generate mesh for a flat rectangular domain
  ///
  void GenerateQuadMesh(
      const NurbsSurfaceData &surface,
      const std::vector<TrimLoop> &trim_loops,
      const TrimmedNurbsTessellationOptions &options,
      double u_min, double u_max, double v_min, double v_max,
      uint32_t u_divs, uint32_t v_divs,
      std::vector<value::point3f> &out_vertices,
      std::vector<value::normal3f> &out_normals,
      std::vector<value::texcoord2f> &out_uvs,
      std::vector<uint32_t> &out_indices);
};

}  // namespace tydra
}  // namespace tinyusdz
