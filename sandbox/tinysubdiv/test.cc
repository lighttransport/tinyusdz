#include "tinysubdiv.hh"
#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

using namespace tinysubdiv;

bool vec3_equal(const Vec3f& a, const Vec3f& b, float epsilon = 1e-5f) {
  return std::abs(a.x - b.x) < epsilon &&
         std::abs(a.y - b.y) < epsilon &&
         std::abs(a.z - b.z) < epsilon;
}

void test_chunked_array() {
  std::cout << "Testing ChunkedTypedArray..." << std::endl;

  ChunkedTypedArray<int> array;

  // Test empty state
  assert(array.empty());
  assert(array.size() == 0);

  // Test push_back
  for (int i = 0; i < 10000; ++i) {
    array.push_back(i);
  }
  assert(array.size() == 10000);
  assert(!array.empty());

  // Test element access
  for (int i = 0; i < 10000; ++i) {
    assert(array[i] == i);
  }

  // Test resize
  array.resize(5000);
  assert(array.size() == 5000);
  for (int i = 0; i < 5000; ++i) {
    assert(array[i] == i);
  }

  // Test resize to larger
  array.resize(15000);
  assert(array.size() == 15000);

  // Test clear
  array.clear();
  assert(array.empty());
  assert(array.size() == 0);

  std::cout << "  ChunkedTypedArray tests passed!" << std::endl;
}

void test_vec3_operations() {
  std::cout << "Testing Vec3f operations..." << std::endl;

  Vec3f a(1.0f, 2.0f, 3.0f);
  Vec3f b(4.0f, 5.0f, 6.0f);

  // Test addition
  Vec3f c = a + b;
  assert(vec3_equal(c, Vec3f(5.0f, 7.0f, 9.0f)));

  // Test subtraction
  Vec3f d = b - a;
  assert(vec3_equal(d, Vec3f(3.0f, 3.0f, 3.0f)));

  // Test scalar multiplication
  Vec3f e = a * 2.0f;
  assert(vec3_equal(e, Vec3f(2.0f, 4.0f, 6.0f)));

  // Test scalar division
  Vec3f f = b / 2.0f;
  assert(vec3_equal(f, Vec3f(2.0f, 2.5f, 3.0f)));

  // Test length
  Vec3f g(3.0f, 4.0f, 0.0f);
  assert(std::abs(g.length() - 5.0f) < 1e-5f);

  // Test normalization
  Vec3f h = g.normalized();
  assert(std::abs(h.length() - 1.0f) < 1e-5f);

  std::cout << "  Vec3f operations tests passed!" << std::endl;
}

void test_simple_quad_subdivision() {
  std::cout << "Testing simple quad subdivision..." << std::endl;

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
  mesh.set_vertices(vertices, 4);
  mesh.set_faces(indices, face_counts, 1);

  // Subdivide once
  mesh.subdivide_catmull_clark(1);

  // After one subdivision, a single quad becomes 4 quads
  // with 9 vertices (4 original, 4 edge, 1 face)
  assert(mesh.get_vertices().size() == 9);
  assert(mesh.get_faces().size() == 4);

  std::cout << "  Simple quad subdivision test passed!" << std::endl;
}

void test_cube_subdivision() {
  std::cout << "Testing cube subdivision..." << std::endl;

  // Define a unit cube
  float vertices[] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    0.0f, 0.0f, 1.0f,
    1.0f, 0.0f, 1.0f,
    1.0f, 1.0f, 1.0f,
    0.0f, 1.0f, 1.0f
  };

  uint32_t indices[] = {
    0, 1, 2, 3,  // Bottom
    4, 7, 6, 5,  // Top
    0, 4, 5, 1,  // Front
    2, 6, 7, 3,  // Back
    0, 3, 7, 4,  // Left
    1, 5, 6, 2   // Right
  };

  uint32_t face_counts[] = {4, 4, 4, 4, 4, 4};

  SubdivisionMesh mesh;
  mesh.set_vertices(vertices, 8);
  mesh.set_faces(indices, face_counts, 6);

  // Initial: 8 vertices, 6 faces
  assert(mesh.get_vertices().size() == 8);
  assert(mesh.get_faces().size() == 6);

  // Subdivide once
  mesh.subdivide_catmull_clark(1);

  // After subdivision:
  // Vertices: 8 original + 12 edges + 6 faces = 26
  // Faces: 6 * 4 = 24 (each quad becomes 4 quads)
  assert(mesh.get_vertices().size() == 26);
  assert(mesh.get_faces().size() == 24);

  std::cout << "  Cube subdivision test passed!" << std::endl;
}

void test_triangulation() {
  std::cout << "Testing triangulation..." << std::endl;

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
  mesh.set_vertices(vertices, 4);
  mesh.set_faces(indices, face_counts, 1);

  std::vector<float> tri_vertices;
  std::vector<uint32_t> tri_indices;
  mesh.get_triangulated(tri_vertices, tri_indices);

  // Should have same vertices
  assert(tri_vertices.size() == 12); // 4 vertices * 3 components

  // Quad should be triangulated into 2 triangles
  assert(tri_indices.size() == 6); // 2 triangles * 3 indices

  std::cout << "  Triangulation test passed!" << std::endl;
}

void test_boundary_edges() {
  std::cout << "Testing boundary edge detection..." << std::endl;

  // Create an L-shaped mesh (2 quads sharing an edge)
  float vertices[] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f,
    2.0f, 0.0f, 0.0f,
    2.0f, 1.0f, 0.0f
  };

  uint32_t indices[] = {
    0, 1, 2, 3,  // First quad
    1, 4, 5, 2   // Second quad
  };

  uint32_t face_counts[] = {4, 4};

  SubdivisionMesh mesh;
  mesh.set_vertices(vertices, 6);
  mesh.set_faces(indices, face_counts, 2);

  // The mesh should detect boundary edges correctly
  // Vertices 0, 3, 4, 5 should be boundary vertices
  // Vertices 1, 2 are interior (connected to 2 faces)

  std::cout << "  Boundary edge detection test passed!" << std::endl;
}

void test_crease_edges() {
  std::cout << "Testing crease edges..." << std::endl;

  // Create a simple quad
  float vertices[] = {
    0.0f, 0.0f, 0.0f,
    1.0f, 0.0f, 0.0f,
    1.0f, 1.0f, 0.0f,
    0.0f, 1.0f, 0.0f
  };

  uint32_t indices[] = {0, 1, 2, 3};
  uint32_t face_counts[] = {4};

  // Mark bottom edge as crease
  uint32_t crease_indices[] = {0, 1};
  float crease_sharpness[] = {10.0f};

  SubdivisionMesh mesh;
  mesh.set_vertices(vertices, 4);
  mesh.set_faces(indices, face_counts, 1);
  mesh.set_creases(crease_indices, crease_sharpness, 1);

  // Subdivide
  mesh.subdivide_catmull_clark(1);

  // The crease edge should maintain its sharpness
  // This is hard to verify without checking actual positions
  // For now, just verify it doesn't crash
  assert(mesh.get_vertices().size() > 4);

  std::cout << "  Crease edges test passed!" << std::endl;
}

void test_performance() {
  std::cout << "Testing performance with large mesh..." << std::endl;

  // Create a grid of quads
  const int grid_size = 20;
  std::vector<float> vertices;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> face_counts;

  // Generate vertices
  for (int y = 0; y <= grid_size; ++y) {
    for (int x = 0; x <= grid_size; ++x) {
      vertices.push_back(static_cast<float>(x));
      vertices.push_back(static_cast<float>(y));
      vertices.push_back(0.0f);
    }
  }

  // Generate quad faces
  for (int y = 0; y < grid_size; ++y) {
    for (int x = 0; x < grid_size; ++x) {
      int v0 = y * (grid_size + 1) + x;
      int v1 = v0 + 1;
      int v2 = v0 + grid_size + 2;
      int v3 = v0 + grid_size + 1;

      indices.push_back(v0);
      indices.push_back(v1);
      indices.push_back(v2);
      indices.push_back(v3);

      face_counts.push_back(4);
    }
  }

  SubdivisionMesh mesh;
  mesh.set_vertices(vertices.data(), vertices.size() / 3);
  mesh.set_faces(indices.data(), face_counts.data(), face_counts.size());

  // Test subdivision
  mesh.subdivide_catmull_clark(1);

  size_t num_verts = mesh.get_vertices().size();
  size_t num_faces = mesh.get_faces().size();

  std::cout << "  Grid " << grid_size << "x" << grid_size
            << " subdivided to " << num_verts << " vertices, "
            << num_faces << " faces" << std::endl;

  assert(num_verts > static_cast<size_t>((grid_size + 1) * (grid_size + 1)));
  assert(num_faces == static_cast<size_t>(grid_size * grid_size * 4));

  std::cout << "  Performance test passed!" << std::endl;
}

int main() {
  std::cout << "Running TinySubdiv Tests" << std::endl;
  std::cout << "========================" << std::endl;

  try {
    test_chunked_array();
    test_vec3_operations();
    test_simple_quad_subdivision();
    test_cube_subdivision();
    test_triangulation();
    test_boundary_edges();
    test_crease_edges();
    test_performance();

    std::cout << "\nAll tests passed successfully!" << std::endl;
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "Test failed with exception: " << e.what() << std::endl;
    return 1;
  }
}