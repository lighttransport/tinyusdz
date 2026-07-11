// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// Procedural test meshes shared by the tsd feature test (test-subdiv.cc)
// and the OpenSubdiv verification program (subdiv-verify-osd.cc).
//
// Vertex positions are intentionally irregular (jittered) so that refined
// vertex positions are pairwise distinct: the OSD comparator matches
// vertices by position, and symmetric meshes would make that ambiguous.

#ifndef TINYUSDZ_TESTS_SUBDIV_MESH_CORPUS_HH_
#define TINYUSDZ_TESTS_SUBDIV_MESH_CORPUS_HH_

#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace corpus {

struct Mesh {
  std::string name;
  std::vector<float> points;  // xyz
  std::vector<uint32_t> face_vertex_counts;
  std::vector<uint32_t> face_vertex_indices;

  // Optional USD subdiv tags.
  std::vector<int32_t> corner_indices;
  std::vector<float> corner_sharpnesses;
  std::vector<int32_t> crease_indices;
  std::vector<int32_t> crease_lengths;
  std::vector<float> crease_sharpnesses;
  std::vector<int32_t> hole_indices;

  // Optional UV faceVarying channel (per-corner values, identity indices
  // unless fvar_indices is non-empty).
  std::vector<float> fvar_uv;  // stride 2
  std::vector<uint32_t> fvar_indices;

  bool is_all_tris() const {
    for (uint32_t c : face_vertex_counts) {
      if (c != 3) {
        return false;
      }
    }
    return true;
  }
  bool has_boundary_hint = false;  // set for open meshes (grids, strips)
};

// Deterministic position jitter (keeps topology, breaks symmetry).
inline void Jitter(Mesh *m, float amount = 0.013f) {
  uint32_t state = 0x243F6A88u;  // pi fractional bits; fixed seed
  for (float &v : m->points) {
    state = state * 1664525u + 1013904223u;
    const float r = (float(state >> 8) / float(1u << 24)) - 0.5f;
    v += amount * r;
  }
}

inline Mesh Cube() {
  Mesh m;
  m.name = "cube";
  m.points = {
      -0.5f, -0.5f, 0.5f,  0.5f,  -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,
      0.5f,  0.5f,  0.5f,  -0.5f, 0.5f,  -0.5f, 0.5f,  0.5f,  -0.5f,
      -0.5f, -0.5f, -0.5f, 0.5f,  -0.5f, -0.5f,
  };
  m.face_vertex_counts = {4, 4, 4, 4, 4, 4};
  m.face_vertex_indices = {
      0, 1, 3, 2,  // +z
      2, 3, 5, 4,  // +y
      4, 5, 7, 6,  // -z
      6, 7, 1, 0,  // -y
      1, 7, 5, 3,  // +x
      6, 0, 2, 4,  // -x
  };
  Jitter(&m);
  return m;
}

// Single quad (boundary everywhere).
inline Mesh SingleQuad() {
  Mesh m;
  m.name = "single_quad";
  m.points = {
      0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.1f, 1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -0.1f,
  };
  m.face_vertex_counts = {4};
  m.face_vertex_indices = {0, 1, 2, 3};
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// Single triangle.
inline Mesh SingleTri() {
  Mesh m;
  m.name = "single_tri";
  m.points = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.2f, 0.4f, 1.0f, -0.1f};
  m.face_vertex_counts = {3};
  m.face_vertex_indices = {0, 1, 2};
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// nx x ny quad grid in the xy plane (open boundary).
inline Mesh QuadGrid(uint32_t nx, uint32_t ny, const char *name) {
  Mesh m;
  m.name = name;
  for (uint32_t y = 0; y <= ny; y++) {
    for (uint32_t x = 0; x <= nx; x++) {
      m.points.push_back(float(x));
      m.points.push_back(float(y));
      m.points.push_back(0.1f * float((x + y) % 3));
    }
  }
  const uint32_t w = nx + 1;
  for (uint32_t y = 0; y < ny; y++) {
    for (uint32_t x = 0; x < nx; x++) {
      m.face_vertex_counts.push_back(4);
      m.face_vertex_indices.push_back(y * w + x);
      m.face_vertex_indices.push_back(y * w + x + 1);
      m.face_vertex_indices.push_back((y + 1) * w + x + 1);
      m.face_vertex_indices.push_back((y + 1) * w + x);
    }
  }
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// Closed quad torus: nu x nv quads.
inline Mesh QuadTorus(uint32_t nu, uint32_t nv) {
  Mesh m;
  m.name = "quad_torus";
  const float kPi = 3.14159265358979323846f;
  const float R = 1.0f;
  const float r = 0.35f;
  for (uint32_t v = 0; v < nv; v++) {
    const float pv = 2.0f * kPi * float(v) / float(nv);
    for (uint32_t u = 0; u < nu; u++) {
      const float pu = 2.0f * kPi * float(u) / float(nu);
      const float cx = (R + r * cosf(pv)) * cosf(pu);
      const float cy = (R + r * cosf(pv)) * sinf(pu);
      const float cz = r * sinf(pv);
      m.points.push_back(cx);
      m.points.push_back(cy);
      m.points.push_back(cz);
    }
  }
  for (uint32_t v = 0; v < nv; v++) {
    for (uint32_t u = 0; u < nu; u++) {
      const uint32_t u1 = (u + 1) % nu;
      const uint32_t v1 = (v + 1) % nv;
      m.face_vertex_counts.push_back(4);
      m.face_vertex_indices.push_back(v * nu + u);
      m.face_vertex_indices.push_back(v * nu + u1);
      m.face_vertex_indices.push_back(v1 * nu + u1);
      m.face_vertex_indices.push_back(v1 * nu + u);
    }
  }
  Jitter(&m);
  return m;
}

// Triangle fan around a central vertex (valence n interior vertex).
inline Mesh TriFan(uint32_t n) {
  Mesh m;
  m.name = "tri_fan_" + std::to_string(n);
  const float kPi = 3.14159265358979323846f;
  m.points = {0.0f, 0.0f, 0.15f};
  for (uint32_t i = 0; i < n; i++) {
    const float a = 2.0f * kPi * float(i) / float(n);
    m.points.push_back(cosf(a));
    m.points.push_back(sinf(a));
    m.points.push_back(0.05f * float(i % 2));
  }
  for (uint32_t i = 0; i < n; i++) {
    m.face_vertex_counts.push_back(3);
    m.face_vertex_indices.push_back(0);
    m.face_vertex_indices.push_back(1 + i);
    m.face_vertex_indices.push_back(1 + ((i + 1) % n));
  }
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// Pentagon + hexagon sharing an edge (n-gon refinement).
inline Mesh NGons() {
  Mesh m;
  m.name = "ngons";
  // Pentagon 0-4; hexagon shares edge (0,1) reversed and adds 5-8.
  m.points = {
      0.0f, 0.0f,  0.0f, 1.0f,  0.0f, 0.1f,  1.4f, 1.0f,  0.0f,
      0.5f, 1.7f,  0.1f, -0.4f, 1.0f, 0.0f,  0.2f, -1.0f, 0.0f,
      0.9f, -1.6f, 0.1f, 1.7f,  -1.4f, 0.0f, 1.9f, -0.4f, 0.1f,
  };
  m.face_vertex_counts = {5, 6};
  m.face_vertex_indices = {
      0, 1, 2, 3, 4,        // pentagon (ccw)
      1, 0, 5, 6, 7, 8,     // hexagon (shares edge 0-1, opposite direction)
  };
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// Mixed-degree mesh: quad + 2 tris + pentagon, consistently wound.
//
//   6.......7
//   : pent  :
//   3---4---5      quad: (0,1,4,3)   tris: (4,1,2), (2,5,4)
//   |   |t /|      pentagon: (3,4,5,7,6)
//   | q | / |
//   |   |/ t|
//   0---1---2
inline Mesh MixedDegree() {
  Mesh m;
  m.name = "mixed_degree";
  m.points = {
      0.0f, 0.0f, 0.0f,  1.0f, 0.0f, 0.1f,  2.0f, 0.0f, 0.0f,
      0.0f, 1.0f, 0.1f,  1.0f, 1.0f, 0.0f,  2.0f, 1.0f, 0.1f,
      0.5f, 2.0f, 0.0f,  1.6f, 1.9f, 0.15f,
  };
  m.face_vertex_counts = {4, 3, 3, 5};
  m.face_vertex_indices = {
      0, 1, 4, 3,     // quad: edges 0-1, 1-4, 4-3, 3-0
      4, 1, 2,        // tri: 4-1 opposes the quad's 1-4
      2, 5, 4,        // tri: 4-2 opposes the first tri's 2-4
      3, 4, 5, 7, 6,  // pentagon: 3-4 and 4-5 oppose neighbors
  };
  m.has_boundary_hint = true;
  Jitter(&m);
  return m;
}

// Cube with a crease chain around the +y face ring (per-crease sharpness).
inline Mesh CreasedCube(float sharpness, const char *name) {
  Mesh m = Cube();
  m.name = name;
  // +y face is (2,3,5,4); close the ring back to 2.
  m.crease_indices = {2, 3, 5, 4, 2};
  m.crease_lengths = {5};
  m.crease_sharpnesses = {sharpness};
  return m;
}

// Cube with per-edge crease sharpness along the same ring.
inline Mesh PerEdgeCreasedCube() {
  Mesh m = Cube();
  m.name = "per_edge_creased_cube";
  m.crease_indices = {2, 3, 5, 4, 2};
  m.crease_lengths = {5};
  m.crease_sharpnesses = {0.4f, 1.5f, 2.7f, 10.0f};  // one per edge
  return m;
}

// 3x3 quad grid with sharpened vertices: one boundary corner and interior
// vertices with semi-sharp / infinite corner sharpness.
inline Mesh CorneredGrid() {
  Mesh m = QuadGrid(3, 3, "cornered_grid");
  m.corner_indices = {0, 5, 6, 10};
  m.corner_sharpnesses = {2.0f, 0.8f, 2.0f, 10.0f};
  return m;
}

// Cube with two faces tagged as holes.
inline Mesh CubeWithHoles() {
  Mesh m = Cube();
  m.name = "cube_with_holes";
  m.hole_indices = {1, 4};
  return m;
}

// Shared helper: assign indexed UVs where each face belongs to an island
// and values are shared per (island, vertex). Adjacent faces in the same
// island are continuous; island borders become seams.
inline void AssignIslandUVs(Mesh *m, const std::vector<uint32_t> &face_island,
                            float island_u_offset) {
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> value_ids;
  m->fvar_uv.clear();
  m->fvar_indices.clear();
  size_t corner = 0;
  for (size_t f = 0; f < m->face_vertex_counts.size(); f++) {
    const uint32_t island = face_island[f];
    for (uint32_t k = 0; k < m->face_vertex_counts[f]; k++, corner++) {
      const uint32_t v = m->face_vertex_indices[corner];
      const auto key = std::make_pair(island, v);
      auto it = value_ids.find(key);
      if (it == value_ids.end()) {
        const uint32_t id = uint32_t(m->fvar_uv.size() / 2);
        value_ids.emplace(key, id);
        // Distinct, irregular values per (island, vertex).
        m->fvar_uv.push_back(island_u_offset * float(island) +
                             0.13f * float(v) + 0.01f * float(v % 3));
        m->fvar_uv.push_back(0.21f * float(v) + 0.07f * float(island));
        m->fvar_indices.push_back(id);
      } else {
        m->fvar_indices.push_back(it->second);
      }
    }
  }
}

// 3x3 quad grid with two UV islands split down the middle column of edges
// (seam between face columns 0..1).
inline Mesh UVSeamGrid() {
  Mesh m = QuadGrid(3, 3, "uv_seam_grid");
  std::vector<uint32_t> island(m.face_vertex_counts.size());
  for (size_t f = 0; f < island.size(); f++) {
    island[f] = (f % 3 <= 1) ? 0 : 1;  // columns 0,1 vs column 2
  }
  AssignIslandUVs(&m, island, 2.0f);
  return m;
}

// Cube unwrapped into 2 UV islands of 3 faces each (seams on a closed mesh).
inline Mesh UVCube() {
  Mesh m = Cube();
  m.name = "uv_cube";
  AssignIslandUVs(&m, {0, 0, 0, 1, 1, 1}, 3.0f);
  return m;
}

// 2x2 quad grid with a seam on the lower half of the middle column only:
// the seam terminates at the interior center vertex (a "dart").
inline Mesh UVDartGrid() {
  Mesh m = QuadGrid(2, 2, "uv_dart_grid");
  // Faces: 0 = bottom-left, 1 = bottom-right, 2 = top-left, 3 = top-right.
  // Bottom faces get separate islands; top faces share island 0 so the
  // seam between faces 0|1 ends at the center vertex (id 4).
  std::map<std::pair<uint32_t, uint32_t>, uint32_t> value_ids;
  const uint32_t face_island[4] = {0, 1, 0, 0};
  m.fvar_uv.clear();
  m.fvar_indices.clear();
  size_t corner = 0;
  for (size_t f = 0; f < 4; f++) {
    for (uint32_t k = 0; k < 4; k++, corner++) {
      const uint32_t v = m.face_vertex_indices[corner];
      // Vertices on the seam (1 only -- the bottom middle) differ per
      // island; the center vertex 4 shares one value (dart point).
      const uint32_t island = (v == 1) ? face_island[f] : 0;
      const auto key = std::make_pair(island, v);
      auto it = value_ids.find(key);
      if (it == value_ids.end()) {
        const uint32_t id = uint32_t(m.fvar_uv.size() / 2);
        value_ids.emplace(key, id);
        m.fvar_uv.push_back(2.0f * float(island) + 0.17f * float(v));
        m.fvar_uv.push_back(0.23f * float(v) + 0.05f * float(island));
        m.fvar_indices.push_back(id);
      } else {
        m.fvar_indices.push_back(it->second);
      }
    }
  }
  return m;
}

// 2x1 quad grid whose fvar seam runs along the single shared edge (1,4) and
// terminates at the boundary vertices: the two faces use the SAME fvar value at
// v1 but DIFFERENT values at v4. v1's only interior edge is the seam edge, so
// merging across it (one-endpoint test) vs treating the edge as discontinuous
// (both-endpoint test, OpenSubdiv) is observable here. Regression for the fvar
// span continuity rule (corners merge only when value ids agree at BOTH ends).
inline Mesh PartialSeamQuads() {
  Mesh m = QuadGrid(2, 1, "partial_seam_quads");
  // Faces: f0 = (0,1,4,3), f1 = (1,2,5,4). Shared edge (1,4).
  // Per-corner fvar value ids: match at v1 (id 1), differ at v4 (2 vs 6).
  m.fvar_indices = {0, 1, 2, 3, 1, 4, 5, 6};
  m.fvar_uv.clear();
  for (uint32_t id = 0; id < 7; id++) {
    m.fvar_uv.push_back(0.13f * float(id) + 0.01f * float(id % 3));
    m.fvar_uv.push_back(0.21f * float(id) + 0.07f);
  }
  return m;
}

// Creased cube with UV islands: exercises crease/fvar interaction.
inline Mesh UVCreasedCube() {
  Mesh m = UVCube();
  m.name = "uv_creased_cube";
  m.crease_indices = {2, 3, 5, 4, 2};
  m.crease_lengths = {5};
  m.crease_sharpnesses = {1.8f};
  return m;
}

// Icosahedron (closed all-triangle mesh; all vertices valence 5).
inline Mesh Icosahedron() {
  Mesh m;
  m.name = "icosahedron";
  const float p = 1.6180339887f;  // golden ratio
  m.points = {
      -1, p,  0,  1,  p,  0,  -1, -p, 0,  1,  -p, 0,  0,  -1, p,  0,  1, p,
      0,  -1, -p, 0,  1,  -p, p,  0,  -1, p,  0,  1,  -p, 0,  -1, -p, 0, 1,
  };
  m.face_vertex_counts.assign(20, 3);
  m.face_vertex_indices = {
      0, 11, 5,  0, 5,  1,  0, 1,  7,  0, 7,  10, 0, 10, 11,
      1, 5,  9,  5, 11, 4,  11, 10, 2,  10, 7,  6,  7, 1,  8,
      3, 9,  4,  3, 4,  2,  3, 2,  6,  3, 6,  8,  3, 8,  9,
      4, 9,  5,  2, 4,  11, 6, 2,  10, 8, 6,  7,  9, 8,  1,
  };
  Jitter(&m);
  return m;
}

// nx x ny grid of quads each split into 2 triangles (open boundary,
// interior vertices valence 6).
inline Mesh TriGrid(uint32_t nx, uint32_t ny, const char *name) {
  Mesh m = QuadGrid(nx, ny, name);
  std::vector<uint32_t> fvi;
  fvi.reserve(m.face_vertex_indices.size() / 4 * 6);
  for (size_t f = 0; f < m.face_vertex_counts.size(); f++) {
    const uint32_t *q = &m.face_vertex_indices[f * 4];
    fvi.insert(fvi.end(), {q[0], q[1], q[2]});
    fvi.insert(fvi.end(), {q[0], q[2], q[3]});
  }
  m.face_vertex_counts.assign(m.face_vertex_counts.size() * 2, 3);
  m.face_vertex_indices = std::move(fvi);
  return m;
}

// Tri grid with a semi-sharp crease chain along an interior row.
inline Mesh CreasedTriGrid() {
  Mesh m = TriGrid(3, 3, "creased_tri_grid");
  // Interior row of the 3x3 grid: vertices 4,5,6 (row y=1, width 4).
  m.crease_indices = {4, 5, 6, 7};
  m.crease_lengths = {4};
  m.crease_sharpnesses = {1.7f};
  return m;
}

// Tri grid with UV islands split between face columns (seams for Loop fvar).
inline Mesh UVTriGrid() {
  Mesh m = TriGrid(2, 2, "uv_tri_grid");
  std::vector<uint32_t> island(m.face_vertex_counts.size());
  for (size_t f = 0; f < island.size(); f++) {
    // Two tris per original quad; quads alternate columns 0,1 per row.
    island[f] = ((f / 2) % 2 == 0) ? 0 : 1;
  }
  AssignIslandUVs(&m, island, 2.5f);
  return m;
}

// Non-manifold "bowtie sheets": three quads sharing one edge.
// tsd must reject this (OpenSubdiv tolerates it).
inline Mesh NonManifoldFan() {
  Mesh m;
  m.name = "non_manifold_fan";
  m.points = {
      0.0f, 0.0f, 0.0f,  0.0f, 1.0f, 0.0f,   // shared edge 0-1
      1.0f, 0.0f, 0.0f,  1.0f, 1.0f, 0.0f,
      -1.0f, 0.0f, 0.3f, -1.0f, 1.0f, 0.3f,
      0.3f, 0.0f, 1.0f,  0.3f, 1.0f, 1.0f,
  };
  m.face_vertex_counts = {4, 4, 4};
  m.face_vertex_indices = {
      0, 2, 3, 1,  // sheet 1 (traverses 1->0 via wrap: edges 0-2,2-3,3-1,1-0)
      0, 1, 5, 4,  // sheet 2 (edge 0-1)
      0, 1, 7, 6,  // sheet 3 (edge 0-1 again -> non-manifold)
  };
  return m;
}

}  // namespace corpus

#endif  // TINYUSDZ_TESTS_SUBDIV_MESH_CORPUS_HH_
