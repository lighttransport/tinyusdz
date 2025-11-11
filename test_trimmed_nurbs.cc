// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Test program for Trimmed NURBS Surface tessellation
//
// This example demonstrates:
// 1. Creating a NURBS surface from scratch
// 2. Adding trim curves
// 3. Tessellating to a triangle mesh
// 4. Outputting to file format
//

#include <iostream>
#include <vector>
#include <cmath>
#include <iomanip>

#include "src/tydra/trimmed-nurbs.hh"
#include "src/tydra/trimmed-nurbs-integration.hh"
#include "src/tydra/render-data.hh"

using namespace tinyusdz;
using namespace tinyusdz::tydra;

///
/// Create a simple 4x4 bicubic NURBS sphere-like surface
///
NurbsSurfaceData CreateTestNurbsSurface() {
  NurbsSurfaceData surface;

  surface.degree_u = 3;
  surface.degree_v = 3;
  surface.num_ctrl_u = 4;
  surface.num_ctrl_v = 4;

  // Control points for a bicubic surface (4x4 grid)
  // Simplified sphere-like shape
  float scale = 1.0f;
  surface.control_points = {
    // Row 0 (v=0)
    value::point3f{-1.0f * scale, -1.0f * scale, -1.0f * scale},
    value::point3f{-0.33f * scale, -1.0f * scale, -1.5f * scale},
    value::point3f{0.33f * scale, -1.0f * scale, -1.5f * scale},
    value::point3f{1.0f * scale, -1.0f * scale, -1.0f * scale},
    // Row 1
    value::point3f{-1.0f * scale, -0.33f * scale, -1.5f * scale},
    value::point3f{-0.5f * scale, -0.5f * scale, -2.0f * scale},
    value::point3f{0.5f * scale, -0.5f * scale, -2.0f * scale},
    value::point3f{1.0f * scale, -0.33f * scale, -1.5f * scale},
    // Row 2
    value::point3f{-1.0f * scale, 0.33f * scale, -1.5f * scale},
    value::point3f{-0.5f * scale, 0.5f * scale, -2.0f * scale},
    value::point3f{0.5f * scale, 0.5f * scale, -2.0f * scale},
    value::point3f{1.0f * scale, 0.33f * scale, -1.5f * scale},
    // Row 3 (v=1)
    value::point3f{-1.0f * scale, 1.0f * scale, -1.0f * scale},
    value::point3f{-0.33f * scale, 1.0f * scale, -1.5f * scale},
    value::point3f{0.33f * scale, 1.0f * scale, -1.5f * scale},
    value::point3f{1.0f * scale, 1.0f * scale, -1.0f * scale},
  };

  // Knot vectors (clamped cubic)
  surface.knots_u = {0, 0, 0, 0, 1, 1, 1, 1};
  surface.knots_v = {0, 0, 0, 0, 1, 1, 1, 1};

  // All weights = 1.0 (non-rational B-spline)
  // surface.weights left empty

  surface.param_u_start = 0.0;
  surface.param_u_end = 1.0;
  surface.param_v_start = 0.0;
  surface.param_v_end = 1.0;

  return surface;
}

///
/// Create a simple circular trim curve in the parametric domain
///
TrimCurve2D CreateCircularTrimCurve(
    double center_u = 0.5, double center_v = 0.5, double radius = 0.3) {
  TrimCurve2D curve;
  curve.type = TrimCurve2D::CurveType::CircleArc;
  curve.circle_center = ParamPoint{center_u, center_v};
  curve.circle_radius = radius;
  curve.degree = 1;

  // Start and end points on the circle
  double angle_start = 0.0;
  double angle_end = 2.0 * 3.14159265359;

  curve.control_points.push_back(ParamPoint{
      center_u + radius * std::cos(angle_start),
      center_v + radius * std::sin(angle_start)});
  curve.control_points.push_back(ParamPoint{
      center_u + radius * std::cos(angle_end),
      center_v + radius * std::sin(angle_end)});

  return curve;
}

///
/// Create a trim loop containing one circular trim curve
///
TrimLoop CreateTestTrimLoop() {
  TrimLoop loop;
  loop.outer_boundary = true;
  loop.curves.push_back(CreateCircularTrimCurve(0.5, 0.5, 0.3));
  return loop;
}

///
/// Test: Evaluate surface at various points
///
void TestSurfaceEvaluation() {
  std::cout << "\n=== Surface Evaluation Test ===\n";

  auto surface = CreateTestNurbsSurface();

  if (!surface.Validate()) {
    std::cerr << "Surface validation failed\n";
    return;
  }

  std::cout << "Control points: " << surface.control_points.size() << "\n";
  std::cout << "Degree: U=" << surface.degree_u << " V=" << surface.degree_v << "\n";
  std::cout << "Knots U: " << surface.knots_u.size()
            << " Knots V: " << surface.knots_v.size() << "\n\n";

  // Evaluate at several points
  const int samples = 5;
  std::cout << "Surface Evaluation (u, v) -> (x, y, z):\n";
  std::cout << std::fixed << std::setprecision(3);

  for (int i = 0; i <= samples; ++i) {
    for (int j = 0; j <= samples; ++j) {
      double u = static_cast<double>(i) / samples;
      double v = static_cast<double>(j) / samples;

      auto pt = EvaluateNurbsSurface(surface, u, v);

      std::cout << "(" << u << ", " << v << ") -> "
                << "(" << pt.x << ", " << pt.y << ", " << pt.z << ")\n";
    }
  }
}

///
/// Test: Trim curve evaluation
///
void TestTrimCurveEvaluation() {
  std::cout << "\n=== Trim Curve Evaluation Test ===\n";

  auto curve = CreateCircularTrimCurve(0.5, 0.5, 0.3);

  std::cout << "Circular trim curve centered at (0.5, 0.5) with radius 0.3\n";
  std::cout << "Evaluation:\n";
  std::cout << std::fixed << std::setprecision(3);

  for (int i = 0; i <= 8; ++i) {
    double t = static_cast<double>(i) / 8.0;
    auto pt = curve.Evaluate(t);
    std::cout << "t=" << t << " -> (u=" << pt.u << ", v=" << pt.v << ")\n";
  }
}

///
/// Test: Point-in-trim-region
///
void TestPointInTrimRegion() {
  std::cout << "\n=== Point-in-Trim-Region Test ===\n";

  auto loop = CreateTestTrimLoop();

  std::cout << "Testing points inside/outside circular trim region\n\n";

  std::vector<std::pair<double, double>> test_points = {
      {0.5, 0.5},    // Center (inside)
      {0.5, 0.65},   // Just inside edge
      {0.5, 0.85},   // Outside
      {0.2, 0.5},    // Far outside
      {0.35, 0.5},   // Inside edge
  };

  for (const auto &[u, v] : test_points) {
    ParamPoint pt{u, v};
    bool inside = IsPointInTrimRegion(pt, loop);

    std::cout << "Point (" << u << ", " << v << "): "
              << (inside ? "INSIDE" : "OUTSIDE") << "\n";
  }
}

///
/// Test: Full tessellation
///
void TestTessellation() {
  std::cout << "\n=== Tessellation Test ===\n";

  auto surface = CreateTestNurbsSurface();
  TrimmedNurbsSurface trimmed_surface;
  trimmed_surface.surface = surface;
  trimmed_surface.trim_loops.push_back(CreateTestTrimLoop());

  TrimmedNurbsTessellationOptions options;
  options.adaptive = true;
  options.screen_space_error = 1.0f;
  options.max_edge_length = 0.1f;
  options.min_u_divisions = 4;
  options.max_u_divisions = 32;
  options.min_v_divisions = 4;
  options.max_v_divisions = 32;
  options.generate_normals = true;
  options.generate_uvs = true;

  RenderMesh mesh;
  TrimmedNurbsTessellator tessellator;

  std::cout << "Tessellating NURBS surface...\n";
  if (!tessellator.Tessellate(trimmed_surface, options, mesh)) {
    std::cerr << "Tessellation failed\n";
    return;
  }

  std::cout << "Tessellation complete!\n";
  std::cout << "  Vertices: " << mesh.points.size() << "\n";
  std::cout << "  Normals: " << mesh.normals.size() << "\n";
  std::cout << "  UVs: " << mesh.uvs.size() << "\n";
  std::cout << "  Faces: " << mesh.faceVertexCounts.size() << "\n";
  std::cout << "  Indices: " << mesh.faceVertexIndices.size() << "\n";

  // Print first few vertices
  std::cout << "\nFirst 5 vertices:\n";
  std::cout << std::fixed << std::setprecision(4);
  for (size_t i = 0; i < std::min(size_t(5), mesh.points.size()); ++i) {
    const auto &p = mesh.points[i];
    std::cout << "  [" << i << "] (" << p.x << ", " << p.y << ", " << p.z << ")\n";
  }
}

///
/// Test: Untrimmed surface tessellation
///
void TestUntrimmedTessellation() {
  std::cout << "\n=== Untrimmed Surface Tessellation Test ===\n";

  auto surface = CreateTestNurbsSurface();
  TrimmedNurbsSurface trimmed_surface;
  trimmed_surface.surface = surface;
  // No trim loops

  TrimmedNurbsTessellationOptions options;
  options.adaptive = false;  // Use uniform tessellation
  options.min_u_divisions = 8;
  options.max_u_divisions = 32;
  options.min_v_divisions = 8;
  options.max_v_divisions = 32;
  options.generate_normals = true;
  options.generate_uvs = true;

  RenderMesh mesh;
  TrimmedNurbsTessellator tessellator;

  std::cout << "Tessellating untrimmed NURBS surface (uniform tessellation)...\n";
  if (!tessellator.Tessellate(trimmed_surface, options, mesh)) {
    std::cerr << "Tessellation failed\n";
    return;
  }

  std::cout << "Tessellation complete!\n";
  std::cout << "  Vertices: " << mesh.points.size() << "\n";
  std::cout << "  Faces: " << mesh.faceVertexCounts.size() << "\n";
  std::cout << "  Triangles: " << mesh.faceVertexIndices.size() / 3 << "\n";
}

///
/// Main test runner
///
int main(int argc, char **argv) {
  std::cout << "===================================================\n";
  std::cout << "Trimmed NURBS Surface Tessellation Test Suite\n";
  std::cout << "===================================================\n";

  try {
    TestSurfaceEvaluation();
    TestTrimCurveEvaluation();
    TestPointInTrimRegion();
    TestUntrimmedTessellation();
    TestTessellation();

    std::cout << "\n===================================================\n";
    std::cout << "All tests completed successfully!\n";
    std::cout << "===================================================\n";

    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Test failed with exception: " << e.what() << "\n";
    return 1;
  }
}
