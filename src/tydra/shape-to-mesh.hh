// SPDX-License-Identifier: Apache 2.0
// Copyright 2025, Light Transport Entertainment Inc.
//
// Shape to Mesh conversion utilities for Tydra
// Converts parametric primitives (Cube, Sphere, etc.) to triangle meshes
//

#pragma once

#include <vector>
#include <cmath>
#include <array>

#include "../../src/math-util.inc"
#include "../../src/value-types.hh"

namespace tinyusdz {
namespace tydra {

namespace {
constexpr double kPI = 3.14159265358979323846;
constexpr double kPI_2 = 1.57079632679489661923;  // PI / 2
}

///
/// Sphere tessellation modes
///
enum class SphereTessellation {
  UV,         // UV sphere (latitude/longitude grid)
  Icosphere   // Icosphere (subdivided icosahedron) - default
};

///
/// Generate cube mesh geometry
///
/// @param[in] size Cube size (half-extent from center)
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts (all 4 for quads)
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output per-face-vertex normals
/// @param[out] uvs Output per-face-vertex texture coordinates
///
inline void GenerateCubeMesh(
    double size,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  float s = float(size) * 0.5f;

  // 8 cube vertices
  points = {
    {-s, -s, -s}, // 0
    { s, -s, -s}, // 1
    { s,  s, -s}, // 2
    {-s,  s, -s}, // 3
    {-s, -s,  s}, // 4
    { s, -s,  s}, // 5
    { s,  s,  s}, // 6
    {-s,  s,  s}  // 7
  };

  // 6 faces (quads), 24 vertex indices total
  // Front, Back, Left, Right, Top, Bottom
  faceVertexIndices = {
    // Front face (+Z)
    4, 5, 6, 7,
    // Back face (-Z)
    1, 0, 3, 2,
    // Left face (-X)
    0, 4, 7, 3,
    // Right face (+X)
    5, 1, 2, 6,
    // Top face (+Y)
    7, 6, 2, 3,
    // Bottom face (-Y)
    4, 0, 1, 5
  };

  // All faces are quads
  faceVertexCounts = {4, 4, 4, 4, 4, 4};

  // Face-varying normals (one per face vertex)
  normals = {
    // Front (+Z)
    {0, 0, 1}, {0, 0, 1}, {0, 0, 1}, {0, 0, 1},
    // Back (-Z)
    {0, 0, -1}, {0, 0, -1}, {0, 0, -1}, {0, 0, -1},
    // Left (-X)
    {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    // Right (+X)
    {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0},
    // Top (+Y)
    {0, 1, 0}, {0, 1, 0}, {0, 1, 0}, {0, 1, 0},
    // Bottom (-Y)
    {0, -1, 0}, {0, -1, 0}, {0, -1, 0}, {0, -1, 0}
  };

  // Face-varying UVs (standard cube mapping)
  uvs = {
    // Front
    {0, 0}, {1, 0}, {1, 1}, {0, 1},
    // Back
    {0, 0}, {1, 0}, {1, 1}, {0, 1},
    // Left
    {0, 0}, {1, 0}, {1, 1}, {0, 1},
    // Right
    {0, 0}, {1, 0}, {1, 1}, {0, 1},
    // Top
    {0, 0}, {1, 0}, {1, 1}, {0, 1},
    // Bottom
    {0, 0}, {1, 0}, {1, 1}, {0, 1}
  };
}

///
/// Generate UV sphere mesh geometry (latitude/longitude grid)
///
/// @param[in] radius Sphere radius
/// @param[in] divisions Number of subdivisions (rings and segments), default 16
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output per-face-vertex normals
/// @param[out] uvs Output per-face-vertex texture coordinates
///
inline void GenerateUVSphereMesh(
    double radius,
    int divisions,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  const float r = float(radius);
  const int rings = divisions;
  const int sectors = divisions * 2;

  const float R = 1.0f / static_cast<float>(rings - 1);
  const float S = 1.0f / static_cast<float>(sectors - 1);

  points.clear();
  points.reserve(static_cast<size_t>(rings) * static_cast<size_t>(sectors));

  // Generate vertices
  for (int ring = 0; ring < rings; ring++) {
    for (int sector = 0; sector < sectors; sector++) {
      float const ringF = static_cast<float>(ring);
      float const sectorF = static_cast<float>(sector);
      float const pi_f = static_cast<float>(kPI);
      float const pi_2_f = static_cast<float>(kPI_2);
      float const y = std::sin(-pi_2_f + pi_f * ringF * R);
      float const x = std::cos(2.0f * pi_f * sectorF * S) * std::sin(pi_f * ringF * R);
      float const z = std::sin(2.0f * pi_f * sectorF * S) * std::sin(pi_f * ringF * R);

      points.push_back({x * r, y * r, z * r});
    }
  }

  // Generate quad faces
  faceVertexIndices.clear();
  faceVertexCounts.clear();
  normals.clear();
  uvs.clear();

  for (int ring = 0; ring < rings - 1; ring++) {
    for (int sector = 0; sector < sectors - 1; sector++) {
      int current = ring * sectors + sector;
      int next = current + sectors;

      // Quad indices (counter-clockwise)
      faceVertexIndices.push_back(current);
      faceVertexIndices.push_back(next);
      faceVertexIndices.push_back(next + 1);
      faceVertexIndices.push_back(current + 1);

      faceVertexCounts.push_back(4);

      // Normals (pointing outward from sphere center)
      for (int i = 0; i < 4; i++) {
        int idx = faceVertexIndices[faceVertexIndices.size() - 4 + static_cast<size_t>(i)];
        value::float3 n = points[static_cast<size_t>(idx)];
        float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len > 0.0f) {
          n[0] /= len;
          n[1] /= len;
          n[2] /= len;
        }
        normals.push_back(n);
      }

      // UV coordinates
      float u0 = static_cast<float>(sector) * S;
      float u1 = static_cast<float>(sector + 1) * S;
      float v0 = static_cast<float>(ring) * R;
      float v1 = static_cast<float>(ring + 1) * R;

      uvs.push_back({u0, v0});
      uvs.push_back({u0, v1});
      uvs.push_back({u1, v1});
      uvs.push_back({u1, v0});
    }
  }
}

///
/// Generate icosphere mesh geometry (subdivided icosahedron)
///
/// @param[in] radius Sphere radius
/// @param[in] subdivisions Number of subdivision levels (0-5 recommended), default 2
/// @param[out] points Output vertex positions
/// @param[out] faceVertexCounts Output face vertex counts (all triangles)
/// @param[out] faceVertexIndices Output face vertex indices
/// @param[out] normals Output per-face-vertex normals
/// @param[out] uvs Output per-face-vertex texture coordinates
///
inline void GenerateIcosphereMesh(
    double radius,
    int subdivisions,
    std::vector<value::float3> &points,
    std::vector<int> &faceVertexCounts,
    std::vector<int> &faceVertexIndices,
    std::vector<value::float3> &normals,
    std::vector<value::float2> &uvs) {

  const float r = float(radius);

  // Golden ratio
  const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;

  // Initial 12 vertices of icosahedron
  std::vector<value::float3> vertices = {
    {-1,  t,  0}, { 1,  t,  0}, {-1, -t,  0}, { 1, -t,  0},
    { 0, -1,  t}, { 0,  1,  t}, { 0, -1, -t}, { 0,  1, -t},
    { t,  0, -1}, { t,  0,  1}, {-t,  0, -1}, {-t,  0,  1}
  };

  // Normalize initial vertices to unit sphere
  for (auto &v : vertices) {
    float len = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    v[0] /= len;
    v[1] /= len;
    v[2] /= len;
  }

  // Initial 20 triangular faces
  std::vector<std::array<int, 3>> faces = {
    {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
    {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
    {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
    {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
  };

  // Subdivide
  for (int subdiv = 0; subdiv < subdivisions; subdiv++) {
    std::vector<std::array<int, 3>> new_faces;
    std::map<std::pair<int, int>, int> edge_midpoints;

    auto get_midpoint = [&](int v1, int v2) -> int {
      auto key = std::make_pair(std::min(v1, v2), std::max(v1, v2));
      auto it = edge_midpoints.find(key);
      if (it != edge_midpoints.end()) {
        return it->second;
      }

      // Create new midpoint vertex
      value::float3 mid = {
        (vertices[static_cast<size_t>(v1)][0] + vertices[static_cast<size_t>(v2)][0]) * 0.5f,
        (vertices[static_cast<size_t>(v1)][1] + vertices[static_cast<size_t>(v2)][1]) * 0.5f,
        (vertices[static_cast<size_t>(v1)][2] + vertices[static_cast<size_t>(v2)][2]) * 0.5f
      };

      // Normalize to unit sphere
      float len = std::sqrt(mid[0] * mid[0] + mid[1] * mid[1] + mid[2] * mid[2]);
      mid[0] /= len;
      mid[1] /= len;
      mid[2] /= len;

      int idx = int(vertices.size());
      vertices.push_back(mid);
      edge_midpoints[key] = idx;
      return idx;
    };

    for (const auto &face : faces) {
      int v0 = face[0], v1 = face[1], v2 = face[2];
      int m01 = get_midpoint(v0, v1);
      int m12 = get_midpoint(v1, v2);
      int m20 = get_midpoint(v2, v0);

      // Create 4 new triangles
      new_faces.push_back({v0, m01, m20});
      new_faces.push_back({v1, m12, m01});
      new_faces.push_back({v2, m20, m12});
      new_faces.push_back({m01, m12, m20});
    }

    faces = std::move(new_faces);
  }

  // Scale to radius and build output
  points.clear();
  for (const auto &v : vertices) {
    points.push_back({v[0] * r, v[1] * r, v[2] * r});
  }

  // Build face indices and normals
  faceVertexIndices.clear();
  faceVertexCounts.clear();
  normals.clear();
  uvs.clear();

  for (const auto &face : faces) {
    faceVertexIndices.push_back(face[0]);
    faceVertexIndices.push_back(face[1]);
    faceVertexIndices.push_back(face[2]);
    faceVertexCounts.push_back(3);

    // Normals (unit vectors from center)
    for (int i = 0; i < 3; i++) {
      normals.push_back(vertices[static_cast<size_t>(face[static_cast<size_t>(i)])]);
    }

    // UV coordinates (spherical projection)
    for (int i = 0; i < 3; i++) {
      const value::float3 &v = vertices[static_cast<size_t>(face[static_cast<size_t>(i)])];
      float const pi_f = static_cast<float>(kPI);
      float u = 0.5f + std::atan2(v[2], v[0]) / (2.0f * pi_f);
      float v_coord = 0.5f - std::asin(v[1]) / pi_f;
      uvs.push_back({u, v_coord});
    }
  }
}

}  // namespace tydra
}  // namespace tinyusdz
