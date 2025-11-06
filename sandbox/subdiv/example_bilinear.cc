//
// Bilinear subdivision example: Demonstrates difference from Catmull-Clark
//

#include "subdivision.hh"
#include <iostream>
#include <iomanip>

int main() {
  using namespace tinyusdz::subdiv;

  std::cout << "Bilinear vs Catmull-Clark Comparison\n";
  std::cout << "=====================================\n\n";

  // Create a simple quad
  std::vector<float> points = {
    0.0f, 0.0f, 0.0f,   // vertex 0
    1.0f, 0.0f, 0.0f,   // vertex 1
    1.0f, 1.0f, 0.0f,   // vertex 2
    0.0f, 1.0f, 0.0f    // vertex 3
  };

  std::vector<uint32_t> face_vertex_counts = {4};
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  // Convert to half-edge mesh
  HalfEdgeMesh input_mesh;
  ConvertToHalfEdgeMesh(face_vertex_counts, face_vertex_indices, points, input_mesh);

  std::cout << "Input mesh: " << input_mesh.GetNumVertices() << " vertices\n\n";

  // Bilinear subdivision (no smoothing)
  BilinearSubdivider bilinear;
  HalfEdgeMesh bilinear_output;
  bilinear.Subdivide(input_mesh, bilinear_output, 1);

  // Catmull-Clark subdivision (with smoothing)
  CatmullClarkSubdivider catmull_clark;
  HalfEdgeMesh catmull_output;
  catmull_clark.Subdivide(input_mesh, catmull_output, 1);

  std::cout << "After 1 level of subdivision:\n";
  std::cout << "  Bilinear:      " << bilinear_output.GetNumVertices() << " vertices\n";
  std::cout << "  Catmull-Clark: " << catmull_output.GetNumVertices() << " vertices\n\n";

  // Compare original vertices (index 0-3)
  std::cout << "Original vertices comparison:\n";
  std::cout << std::fixed << std::setprecision(4);

  for (uint32_t i = 0; i < 4; ++i) {
    float bx = bilinear_output.points[i * 3 + 0];
    float by = bilinear_output.points[i * 3 + 1];
    float cx = catmull_output.points[i * 3 + 0];
    float cy = catmull_output.points[i * 3 + 1];

    std::cout << "  v" << i << ":\n";
    std::cout << "    Bilinear:      (" << bx << ", " << by << ")\n";
    std::cout << "    Catmull-Clark: (" << cx << ", " << cy << ")\n";

    if (bx != points[i * 3 + 0] || by != points[i * 3 + 1]) {
      std::cout << "    ⚠ Bilinear changed original vertex!\n";
    } else {
      std::cout << "    ✓ Bilinear preserved original vertex\n";
    }

    if (cx == points[i * 3 + 0] && cy == points[i * 3 + 1]) {
      std::cout << "    ⚠ Catmull-Clark didn't smooth vertex!\n";
    } else {
      std::cout << "    ✓ Catmull-Clark smoothed vertex\n";
    }
  }

  std::cout << "\nKey differences:\n";
  std::cout << "  - Bilinear: Preserves original vertices (no smoothing)\n";
  std::cout << "  - Catmull-Clark: Smooths vertices for curved surfaces\n";
  std::cout << "  - Bilinear: Use for sharp features, tessellation\n";
  std::cout << "  - Catmull-Clark: Use for smooth, organic shapes\n";

  return 0;
}
