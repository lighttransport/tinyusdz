#include "tinysubdiv.hh"
#include <iostream>

using namespace tinysubdiv;

int main() {
  std::cout << "Simple Loop subdivision test..." << std::endl;

  // Single triangle
  float vertices[] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    0.5f, 1.0f, 0.0f
  };

  uint32_t indices[] = {0, 1, 2};
  uint32_t face_counts[] = {3};

  SubdivisionMesh mesh;
  mesh.set_vertices(vertices, 3);
  mesh.set_faces(indices, face_counts, 1);

  std::cout << "Initial mesh: " << mesh.get_vertices().size() << " vertices, "
            << mesh.get_faces().size() << " faces" << std::endl;

  mesh.subdivide_loop(1);

  std::cout << "After subdivision: " << mesh.get_vertices().size() << " vertices, "
            << mesh.get_faces().size() << " faces" << std::endl;

  std::cout << "Test completed!" << std::endl;
  return 0;
}