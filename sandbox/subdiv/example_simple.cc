//
// Simple example: Subdivide a quad using Catmull-Clark
//

#include "subdivision.hh"
#include <iostream>

int main() {
  using namespace tinyusdz::subdiv;

  std::cout << "Simple Catmull-Clark Subdivision Example\n";
  std::cout << "=========================================\n\n";

  // Create a simple quad
  std::vector<float> points = {
    0.0f, 0.0f, 0.0f,   // vertex 0
    1.0f, 0.0f, 0.0f,   // vertex 1
    1.0f, 1.0f, 0.0f,   // vertex 2
    0.0f, 1.0f, 0.0f    // vertex 3
  };

  std::vector<uint32_t> face_vertex_counts = {4};  // One quad face
  std::vector<uint32_t> face_vertex_indices = {0, 1, 2, 3};

  // Convert to half-edge mesh
  HalfEdgeMesh input_mesh;
  SubdivResult result = ConvertToHalfEdgeMesh(
      face_vertex_counts, face_vertex_indices, points, input_mesh);

  if (!result.success) {
    std::cerr << "Error: " << result.error << "\n";
    return 1;
  }

  std::cout << "Input mesh:\n";
  std::cout << "  Vertices: " << input_mesh.GetNumVertices() << "\n";
  std::cout << "  Faces: " << input_mesh.GetNumFaces() << "\n\n";

  // Create subdivider
  CatmullClarkSubdivider subdivider;
  subdivider.SetBoundaryInterpolation(BoundaryInterpolation::EdgeAndCorner);

  // Subdivide 2 levels
  HalfEdgeMesh output_mesh;
  result = subdivider.Subdivide(input_mesh, output_mesh, 2);

  if (!result.success) {
    std::cerr << "Subdivision error: " << result.error << "\n";
    return 1;
  }

  std::cout << "Output mesh (2 subdivision levels):\n";
  std::cout << "  Vertices: " << output_mesh.GetNumVertices() << "\n";
  std::cout << "  Faces: " << output_mesh.GetNumFaces() << "\n\n";

  // Print first few vertices
  std::cout << "First 5 output vertices:\n";
  for (uint32_t i = 0; i < 5 && i < output_mesh.GetNumVertices(); ++i) {
    std::cout << "  v" << i << ": ("
              << output_mesh.points[i * 3 + 0] << ", "
              << output_mesh.points[i * 3 + 1] << ", "
              << output_mesh.points[i * 3 + 2] << ")\n";
  }

  std::cout << "\nSuccess!\n";
  return 0;
}
