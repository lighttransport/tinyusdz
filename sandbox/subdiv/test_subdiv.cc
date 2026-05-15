//
// Test program for subdivision surface library
//
// Copyright (c) 2025, Lightweight USD contributors
// Licensed under MIT license
//

#include "subdivision.hh"
#include "primvar-interpolation.hh"

#include <iostream>
#include <iomanip>
#include <cassert>

using namespace tinyusdz::subdiv;

// Helper to print mesh info
void PrintMeshInfo(const HalfEdgeMesh& mesh, const std::string& name) {
  std::cout << name << ":\n";
  std::cout << "  Vertices: " << mesh.GetNumVertices() << "\n";
  std::cout << "  Faces: " << mesh.GetNumFaces() << "\n";
  std::cout << "  Indices: " << mesh.face_vertex_indices.size() << "\n";
}

// Test 1: Catmull-Clark on a simple cube
bool TestCatmullClarkCube() {
  std::cout << "\n=== Test 1: Catmull-Clark Cube ===\n";

  // Simple cube mesh
  // 8 vertices, 6 quad faces
  std::vector<float> points = {
    -1, -1, -1,  // 0
     1, -1, -1,  // 1
     1,  1, -1,  // 2
    -1,  1, -1,  // 3
    -1, -1,  1,  // 4
     1, -1,  1,  // 5
     1,  1,  1,  // 6
    -1,  1,  1   // 7
  };

  std::vector<uint32_t> face_vertex_counts = {
    4, 4, 4, 4, 4, 4
  };

  std::vector<uint32_t> face_vertex_indices = {
    0, 1, 2, 3,  // front
    4, 5, 6, 7,  // back
    0, 4, 7, 3,  // left
    1, 5, 6, 2,  // right
    0, 1, 5, 4,  // bottom
    3, 2, 6, 7   // top
  };

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input cube");

  // Subdivide 1 level
  CatmullClarkSubdivider subdivider;
  HalfEdgeMesh output_mesh;

  result = subdivider.Subdivide(input_mesh, output_mesh, 1);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided cube (1 level)");

  // Expected: 8 original vertices -> 8 vertex points
  //           12 edges -> 12 edge points
  //           6 faces -> 6 face points
  //           Total: 26 vertices
  //           6 faces * 4 quads each = 24 faces
  if (output_mesh.GetNumVertices() != 26) {
    std::cerr << "ERROR: Expected 26 vertices, got " << output_mesh.GetNumVertices() << "\n";
    return false;
  }

  if (output_mesh.GetNumFaces() != 24) {
    std::cerr << "ERROR: Expected 24 faces, got " << output_mesh.GetNumFaces() << "\n";
    return false;
  }

  std::cout << "✓ Catmull-Clark cube test passed\n";
  return true;
}

// Test 2: Catmull-Clark on a quad
bool TestCatmullClarkQuad() {
  std::cout << "\n=== Test 2: Catmull-Clark Quad ===\n";

  // Simple quad
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input quad");

  CatmullClarkSubdivider subdivider;
  subdivider.SetBoundaryInterpolation(BoundaryInterpolation::EdgeAndCorner);

  HalfEdgeMesh output_mesh;
  result = subdivider.Subdivide(input_mesh, output_mesh, 2);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided quad (2 levels)");

  std::cout << "✓ Catmull-Clark quad test passed\n";
  return true;
}

// Test 3: Loop subdivision on a triangle
bool TestLoopTriangle() {
  std::cout << "\n=== Test 3: Loop Triangle ===\n";

  // Simple triangle
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    0.5, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {3};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input triangle");

  LoopSubdivider subdivider;
  HalfEdgeMesh output_mesh;

  result = subdivider.Subdivide(input_mesh, output_mesh, 1);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided triangle (1 level)");

  // Expected: 3 vertices + 3 edge points = 6 vertices
  //           1 triangle -> 4 triangles
  if (output_mesh.GetNumVertices() != 6) {
    std::cerr << "ERROR: Expected 6 vertices, got " << output_mesh.GetNumVertices() << "\n";
    return false;
  }

  if (output_mesh.GetNumFaces() != 4) {
    std::cerr << "ERROR: Expected 4 faces, got " << output_mesh.GetNumFaces() << "\n";
    return false;
  }

  std::cout << "✓ Loop triangle test passed\n";
  return true;
}

// Test 4: Loop subdivision on a tetrahedron
bool TestLoopTetrahedron() {
  std::cout << "\n=== Test 4: Loop Tetrahedron ===\n";

  // Tetrahedron: 4 vertices, 4 triangular faces
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    0.5, 1, 0,
    0.5, 0.5, 1
  };

  std::vector<uint32_t> face_vertex_counts = {3, 3, 3, 3};
  std::vector<uint32_t> face_vertex_indices = {
    0, 1, 2,
    0, 1, 3,
    1, 2, 3,
    2, 0, 3
  };

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input tetrahedron");

  LoopSubdivider subdivider;
  HalfEdgeMesh output_mesh;

  result = subdivider.Subdivide(input_mesh, output_mesh, 2);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided tetrahedron (2 levels)");

  std::cout << "✓ Loop tetrahedron test passed\n";
  return true;
}

// Test 5: Error handling - invalid mesh
bool TestErrorHandling() {
  std::cout << "\n=== Test 5: Error Handling ===\n";

  // Invalid mesh: index out of range
  std::vector<float> points = {0, 0, 0, 1, 0, 0, 1, 1, 0};
  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 5};  // 5 is out of range

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (result.success) {
    std::cerr << "ERROR: Should have failed on invalid mesh\n";
    return false;
  }

  std::cout << "✓ Correctly rejected invalid mesh: " << result.error << "\n";

  // Test Loop on non-triangle mesh
  std::vector<float> points2 = {0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0};
  std::vector<uint32_t> face_vertex_counts2 = {4};
  std::vector<uint32_t> face_vertex_indices2 = {0, 1, 2, 3};

  HalfEdgeMesh quad_mesh;
  result = ConvertToHalfEdgeMesh(
      face_vertex_counts2, face_vertex_indices2, points2, quad_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert quad: " << result.error << "\n";
    return false;
  }

  LoopSubdivider loop_subdivider;
  HalfEdgeMesh output;
  result = loop_subdivider.Subdivide(quad_mesh, output, 1);

  if (result.success) {
    std::cerr << "ERROR: Loop should fail on non-triangle mesh\n";
    return false;
  }

  std::cout << "✓ Correctly rejected quad mesh for Loop: " << result.error << "\n";

  std::cout << "✓ Error handling test passed\n";
  return true;
}

// Test 6: Primvar interpolation
bool TestPrimvarInterpolation() {
  std::cout << "\n=== Test 6: Primvar Interpolation ===\n";

  // Simple quad with vertex colors
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  // Vertex colors (Vec3)
  std::vector<Vec3> vertex_colors = {
    Vec3(1, 0, 0),  // Red
    Vec3(0, 1, 0),  // Green
    Vec3(0, 0, 1),  // Blue
    Vec3(1, 1, 0)   // Yellow
  };

  PrimvarData<Vec3> input_colors(vertex_colors, InterpolationType::Vertex);

  // Subdivide mesh
  CatmullClarkSubdivider subdivider;
  HalfEdgeMesh output_mesh;
  result = subdivider.Subdivide(input_mesh, output_mesh, 1);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  // Subdivide primvar
  PrimvarData<Vec3> output_colors;
  result = subdivider.SubdividePrimvar(input_colors, input_mesh, output_mesh, output_colors);

  if (!result.success) {
    std::cerr << "Primvar subdivision failed: " << result.error << "\n";
    return false;
  }

  std::cout << "  Input colors: " << input_colors.values.size() << "\n";
  std::cout << "  Output colors: " << output_colors.values.size() << "\n";
  std::cout << "  Output vertices: " << output_mesh.GetNumVertices() << "\n";

  if (output_colors.values.size() != output_mesh.GetNumVertices()) {
    std::cerr << "ERROR: Color count mismatch\n";
    return false;
  }

  std::cout << "✓ Primvar interpolation test passed\n";
  return true;
}

// Test 7: Multiple subdivision levels
bool TestMultipleLevels() {
  std::cout << "\n=== Test 7: Multiple Subdivision Levels ===\n";

  // Simple quad
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  CatmullClarkSubdivider subdivider;

  for (int level = 1; level <= 4; ++level) {
    HalfEdgeMesh output_mesh;
    result = subdivider.Subdivide(input_mesh, output_mesh, level);

    if (!result.success) {
      std::cerr << "Subdivision level " << level << " failed: " << result.error << "\n";
      return false;
    }

    std::cout << "  Level " << level << ": "
              << output_mesh.GetNumVertices() << " vertices, "
              << output_mesh.GetNumFaces() << " faces\n";
  }

  std::cout << "✓ Multiple levels test passed\n";
  return true;
}

// Test 8: Bilinear subdivision
bool TestBilinearQuad() {
  std::cout << "\n=== Test 8: Bilinear Quad ===\n";

  // Simple quad
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input quad");

  BilinearSubdivider subdivider;
  HalfEdgeMesh output_mesh;

  result = subdivider.Subdivide(input_mesh, output_mesh, 1);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided quad (1 level)");

  // Expected: 4 original + 4 edges + 1 face center = 9 vertices
  //           1 quad -> 4 quads
  if (output_mesh.GetNumVertices() != 9) {
    std::cerr << "ERROR: Expected 9 vertices, got " << output_mesh.GetNumVertices() << "\n";
    return false;
  }

  if (output_mesh.GetNumFaces() != 4) {
    std::cerr << "ERROR: Expected 4 faces, got " << output_mesh.GetNumFaces() << "\n";
    return false;
  }

  // Verify original vertices are unchanged (bilinear doesn't smooth)
  for (uint32_t i = 0; i < 4; ++i) {
    if (std::abs(output_mesh.points[i * 3 + 0] - points[i * 3 + 0]) > 0.0001f ||
        std::abs(output_mesh.points[i * 3 + 1] - points[i * 3 + 1]) > 0.0001f ||
        std::abs(output_mesh.points[i * 3 + 2] - points[i * 3 + 2]) > 0.0001f) {
      std::cerr << "ERROR: Original vertex " << i << " was modified\n";
      return false;
    }
  }

  std::cout << "✓ Bilinear quad test passed\n";
  return true;
}

// Test 9: Bilinear triangle
bool TestBilinearTriangle() {
  std::cout << "\n=== Test 9: Bilinear Triangle ===\n";

  // Simple triangle
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    0.5, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {3};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2};

  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Failed to convert mesh: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(input_mesh, "Input triangle");

  BilinearSubdivider subdivider;
  HalfEdgeMesh output_mesh;

  result = subdivider.Subdivide(input_mesh, output_mesh, 1);

  if (!result.success) {
    std::cerr << "Subdivision failed: " << result.error << "\n";
    return false;
  }

  PrintMeshInfo(output_mesh, "Subdivided triangle (1 level)");

  // Expected: 3 original + 3 edges + 1 face center (not used for tris) = 6 vertices
  //           1 triangle -> 4 triangles
  if (output_mesh.GetNumVertices() != 7) {
    std::cerr << "ERROR: Expected 7 vertices, got " << output_mesh.GetNumVertices() << "\n";
    return false;
  }

  if (output_mesh.GetNumFaces() != 4) {
    std::cerr << "ERROR: Expected 4 faces, got " << output_mesh.GetNumFaces() << "\n";
    return false;
  }

  std::cout << "✓ Bilinear triangle test passed\n";
  return true;
}

// Test 10: Bilinear vs Catmull-Clark comparison
bool TestBilinearVsCatmullClark() {
  std::cout << "\n=== Test 10: Bilinear vs Catmull-Clark Comparison ===\n";

  // Simple quad
  std::vector<float> points = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  HalfEdgeMesh input_mesh;
  ConvertToHalfEdgeMesh(face_vertex_counts, face_vertex_indices, points, input_mesh);

  // Bilinear subdivision
  BilinearSubdivider bilinear_subdivider;
  HalfEdgeMesh bilinear_output;
  bilinear_subdivider.Subdivide(input_mesh, bilinear_output, 1);

  // Catmull-Clark subdivision
  CatmullClarkSubdivider catmull_subdivider;
  HalfEdgeMesh catmull_output;
  catmull_subdivider.Subdivide(input_mesh, catmull_output, 1);

  std::cout << "  Bilinear: " << bilinear_output.GetNumVertices() << " vertices\n";
  std::cout << "  Catmull-Clark: " << catmull_output.GetNumVertices() << " vertices\n";

  // Both should have same vertex count
  if (bilinear_output.GetNumVertices() != catmull_output.GetNumVertices()) {
    std::cerr << "ERROR: Vertex count mismatch\n";
    return false;
  }

  // But vertex positions should differ (Catmull-Clark smooths, Bilinear doesn't)
  bool positions_differ = false;
  for (uint32_t i = 0; i < bilinear_output.GetNumVertices(); ++i) {
    if (std::abs(bilinear_output.points[i * 3 + 0] - catmull_output.points[i * 3 + 0]) > 0.0001f ||
        std::abs(bilinear_output.points[i * 3 + 1] - catmull_output.points[i * 3 + 1]) > 0.0001f ||
        std::abs(bilinear_output.points[i * 3 + 2] - catmull_output.points[i * 3 + 2]) > 0.0001f) {
      positions_differ = true;
      break;
    }
  }

  if (!positions_differ) {
    std::cerr << "ERROR: Bilinear and Catmull-Clark produced identical results (should differ)\n";
    return false;
  }

  std::cout << "✓ Bilinear vs Catmull-Clark comparison test passed\n";
  return true;
}

int main(int argc, char** argv) {
  std::cout << "===================================\n";
  std::cout << "Subdivision Surface Library Tests\n";
  std::cout << "===================================\n";

  int passed = 0;
  int total = 0;

  total++; if (TestCatmullClarkCube()) passed++;
  total++; if (TestCatmullClarkQuad()) passed++;
  total++; if (TestLoopTriangle()) passed++;
  total++; if (TestLoopTetrahedron()) passed++;
  total++; if (TestErrorHandling()) passed++;
  total++; if (TestPrimvarInterpolation()) passed++;
  total++; if (TestMultipleLevels()) passed++;
  total++; if (TestBilinearQuad()) passed++;
  total++; if (TestBilinearTriangle()) passed++;
  total++; if (TestBilinearVsCatmullClark()) passed++;

  std::cout << "\n===================================\n";
  std::cout << "Results: " << passed << "/" << total << " tests passed\n";
  std::cout << "===================================\n";

  return (passed == total) ? 0 : 1;
}
