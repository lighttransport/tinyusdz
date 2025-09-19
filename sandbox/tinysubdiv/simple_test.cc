#include "tinysubdiv.hh"
#include <iostream>

using namespace tinysubdiv;

int main() {
  std::cout << "Creating mesh..." << std::endl;

  // Create a simple quad
  float vertices[] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f
  };

  uint32_t indices[] = {0, 1, 2, 3};
  uint32_t face_counts[] = {4};

  SubdivisionMesh mesh;

  std::cout << "Setting vertices..." << std::endl;
  mesh.set_vertices(vertices, 4);

  std::cout << "Setting faces..." << std::endl;
  mesh.set_faces(indices, face_counts, 1);

  std::cout << "Subdividing..." << std::endl;
  try {
    mesh.subdivide_catmull_clark(1);
  } catch (const std::exception& e) {
    std::cerr << "Exception: " << e.what() << std::endl;
    return 1;
  }

  std::cout << "Success!" << std::endl;
  return 0;
}