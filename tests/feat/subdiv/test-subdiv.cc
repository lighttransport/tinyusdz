// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv (src/tsd) feature test: analytic, golden and hardening tests.
// Runs in normal CI; no OpenSubdiv required.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "tsd/tinysubdiv.hh"
#include "tsd/tsd-internal.hh"

#include "subdiv-mesh-corpus.hh"

namespace {

int g_failures = 0;
int g_checks = 0;
const char *g_current_test = "";

#define CHECK(cond)                                                       \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      g_failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d [%s]: %s\n", __FILE__, __LINE__,        \
              g_current_test, #cond);                                     \
    }                                                                     \
  } while (0)

#define CHECK_MSG(cond, msg)                                              \
  do {                                                                    \
    g_checks++;                                                           \
    if (!(cond)) {                                                        \
      g_failures++;                                                       \
      fprintf(stderr, "FAIL %s:%d [%s]: %s (%s)\n", __FILE__, __LINE__,   \
              g_current_test, #cond, std::string(msg).c_str());           \
    }                                                                     \
  } while (0)

#define TEST(fn)                       \
  do {                                 \
    g_current_test = #fn;              \
    fn();                              \
  } while (0)

using tinyusdz::tsd::BoundaryInterpolation;
using tinyusdz::tsd::MeshView;
using tinyusdz::tsd::Options;
using tinyusdz::tsd::RefinedMesh;
using tinyusdz::tsd::Result;
using tinyusdz::tsd::Scheme;

MeshView ToView(const corpus::Mesh &m) {
  MeshView v;
  v.points = m.points.data();
  v.num_points = uint32_t(m.points.size() / 3);
  v.face_vertex_counts = m.face_vertex_counts.data();
  v.num_faces = uint32_t(m.face_vertex_counts.size());
  v.face_vertex_indices = m.face_vertex_indices.data();
  v.num_face_vertex_indices = uint32_t(m.face_vertex_indices.size());
  if (!m.corner_indices.empty()) {
    v.corner_indices = m.corner_indices.data();
    v.num_corners = uint32_t(m.corner_indices.size());
    v.corner_sharpnesses = m.corner_sharpnesses.data();
  }
  if (!m.crease_lengths.empty()) {
    v.crease_indices = m.crease_indices.data();
    v.num_crease_indices = uint32_t(m.crease_indices.size());
    v.crease_lengths = m.crease_lengths.data();
    v.num_crease_lengths = uint32_t(m.crease_lengths.size());
    v.crease_sharpnesses = m.crease_sharpnesses.data();
    v.num_crease_sharpnesses = uint32_t(m.crease_sharpnesses.size());
  }
  if (!m.hole_indices.empty()) {
    v.hole_indices = m.hole_indices.data();
    v.num_holes = uint32_t(m.hole_indices.size());
  }
  return v;
}

// ---------------------------------------------------------------------------
// Validation / hardening
// ---------------------------------------------------------------------------

void test_scheme_none_rejected() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  opts.scheme = Scheme::None;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK(r == Result::UnsupportedScheme);
  CHECK(!err.empty());
}

void test_null_output_rejected() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  std::string err;
  CHECK(Refine(ToView(cube), opts, nullptr, &err) ==
        Result::InvalidArgument);
}

void test_empty_mesh_rejected() {
  MeshView v;
  Options opts;
  RefinedMesh out;
  CHECK(Refine(v, opts, &out, nullptr) == Result::InvalidArgument);
}

void test_bad_level_rejected() {
  corpus::Mesh cube = corpus::Cube();
  RefinedMesh out;
  Options opts;
  opts.level = -1;
  CHECK(Refine(ToView(cube), opts, &out, nullptr) ==
        Result::InvalidArgument);
  opts.level = tinyusdz::tsd::kMaxLevel + 1;
  CHECK(Refine(ToView(cube), opts, &out, nullptr) ==
        Result::InvalidArgument);
}

void test_out_of_range_index_rejected() {
  corpus::Mesh cube = corpus::Cube();
  cube.face_vertex_indices[5] = 1000;  // > num_points
  Options opts;
  RefinedMesh out;
  std::string err;
  CHECK(Refine(ToView(cube), opts, &out, &err) == Result::InvalidTopology);
}

void test_count_mismatch_rejected() {
  corpus::Mesh cube = corpus::Cube();
  cube.face_vertex_counts[0] = 5;  // sum != num indices
  Options opts;
  RefinedMesh out;
  CHECK(Refine(ToView(cube), opts, &out, nullptr) ==
        Result::InvalidTopology);
}

void test_bad_face_degree_rejected() {
  corpus::Mesh m = corpus::SingleQuad();
  m.face_vertex_counts = {2, 2};  // degree < 3 (sum still 4)
  Options opts;
  RefinedMesh out;
  CHECK(Refine(ToView(m), opts, &out, nullptr) == Result::InvalidTopology);
}

void test_crease_sharpness_count_rejected() {
  corpus::Mesh cube = corpus::Cube();
  cube.crease_indices = {0, 1, 3};  // one chain of 3 verts = 2 edges
  cube.crease_lengths = {3};
  cube.crease_sharpnesses = {1.0f, 2.0f, 3.0f};  // neither 1 nor 2 entries
  Options opts;
  RefinedMesh out;
  CHECK(Refine(ToView(cube), opts, &out, nullptr) ==
        Result::InvalidTopology);
}

void test_hole_index_out_of_range_rejected() {
  corpus::Mesh cube = corpus::Cube();
  cube.hole_indices = {6};  // cube has faces 0..5
  Options opts;
  RefinedMesh out;
  CHECK(Refine(ToView(cube), opts, &out, nullptr) ==
        Result::InvalidTopology);
}

// ---------------------------------------------------------------------------
// Level-0 passthrough
// ---------------------------------------------------------------------------

void test_level0_passthrough() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  opts.level = 0;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.points == cube.points);
  CHECK(out.face_vertex_counts == cube.face_vertex_counts);
  CHECK(out.face_vertex_indices == cube.face_vertex_indices);
  CHECK(out.face_source.size() == cube.face_vertex_counts.size());
  for (size_t f = 0; f < out.face_source.size(); f++) {
    CHECK(out.face_source[f] == f);
  }
}

void test_level0_fvar_expansion() {
  corpus::Mesh quad = corpus::SingleQuad();
  // 4 corners indexing 2 distinct UV values.
  const float uv_values[] = {0.0f, 0.0f, 1.0f, 1.0f};
  const uint32_t uv_indices[] = {0, 1, 1, 0};
  tinyusdz::tsd::FVarChannelView ch;
  ch.values = uv_values;
  ch.num_values = 2;
  ch.indices = uv_indices;
  ch.stride = 2;

  Options opts;
  opts.level = 0;
  RefinedMesh out;
  std::string err;
  Result r =
      Refine(ToView(quad), &ch, 1, nullptr, 0, opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.fvar.size() == 1);
  const std::vector<float> expect = {0.0f, 0.0f, 1.0f, 1.0f,
                                     1.0f, 1.0f, 0.0f, 0.0f};
  CHECK(out.fvar[0] == expect);
}

// ---------------------------------------------------------------------------
// Topology builder (internal)
// ---------------------------------------------------------------------------

void test_topology_cube() {
  corpus::Mesh cube = corpus::Cube();
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(cube.face_vertex_counts.data(),
                           uint32_t(cube.face_vertex_counts.size()),
                           cube.face_vertex_indices.data(),
                           uint32_t(cube.face_vertex_indices.size()),
                           uint32_t(cube.points.size() / 3), &topo, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(topo.num_edges == 12);
  for (uint32_t e = 0; e < topo.num_edges; e++) {
    CHECK(!topo.IsBoundaryEdge(e));
  }
  for (uint32_t v = 0; v < topo.num_points; v++) {
    CHECK(topo.vert_is_boundary[v] == 0);
    // Cube: every vertex has 3 incident faces and 3 incident edges.
    CHECK(topo.vert_face_offsets[v + 1] - topo.vert_face_offsets[v] == 3);
    CHECK(topo.vert_edge_offsets[v + 1] - topo.vert_edge_offsets[v] == 3);
  }
}

void test_topology_single_quad_boundary() {
  corpus::Mesh quad = corpus::SingleQuad();
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(quad.face_vertex_counts.data(), 1,
                           quad.face_vertex_indices.data(), 4, 4, &topo, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(topo.num_edges == 4);
  for (uint32_t e = 0; e < 4; e++) {
    CHECK(topo.IsBoundaryEdge(e));
  }
  for (uint32_t v = 0; v < 4; v++) {
    CHECK(topo.vert_is_boundary[v] == 1);
  }
}

void test_topology_mixed_degree() {
  corpus::Mesh m = corpus::MixedDegree();
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(m.face_vertex_counts.data(),
                           uint32_t(m.face_vertex_counts.size()),
                           m.face_vertex_indices.data(),
                           uint32_t(m.face_vertex_indices.size()),
                           uint32_t(m.points.size() / 3), &topo, &err);
  CHECK_MSG(r == Result::Success, err);
  // V - E + F = 2 - boundary_loops for a disk: 8 - E + 4 = 1 => E = 11.
  CHECK(topo.num_edges == 11);
}

void test_topology_non_manifold_rejected() {
  corpus::Mesh m = corpus::NonManifoldFan();
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(m.face_vertex_counts.data(),
                           uint32_t(m.face_vertex_counts.size()),
                           m.face_vertex_indices.data(),
                           uint32_t(m.face_vertex_indices.size()),
                           uint32_t(m.points.size() / 3), &topo, &err);
  CHECK(r == Result::InvalidTopology);
}

void test_topology_inconsistent_winding_rejected() {
  // Two quads sharing edge (1,2) traversed in the SAME direction.
  const uint32_t fvc[] = {4, 4};
  const uint32_t fvi[] = {0, 1, 2, 3, 1, 2, 4, 5};
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(fvc, 2, fvi, 8, 6, &topo, &err);
  CHECK(r == Result::InvalidTopology);
}

void test_topology_degenerate_edge_rejected() {
  const uint32_t fvc[] = {3};
  const uint32_t fvi[] = {0, 0, 1};
  tinyusdz::tsd::Topology topo;
  std::string err;
  Result r = BuildTopology(fvc, 1, fvi, 3, 2, &topo, &err);
  CHECK(r == Result::InvalidTopology);
}

// ---------------------------------------------------------------------------
// Crease canonicalization (internal)
// ---------------------------------------------------------------------------

void test_crease_canonicalize_per_crease() {
  corpus::Mesh cube = corpus::Cube();
  cube.crease_indices = {0, 1, 3};  // chain: edges (0,1), (1,3)
  cube.crease_lengths = {3};
  cube.crease_sharpnesses = {2.5f};  // per-crease

  tinyusdz::tsd::CreaseEdges ce;
  std::string err;
  Result r = CanonicalizeCreases(ToView(cube), &ce, &err);
  CHECK(r == Result::Success);
  CHECK(ce.sharpnesses.size() == 2);
  for (float s : ce.sharpnesses) {
    CHECK(std::fabs(s - 2.5f) < 1e-6f);
  }
  // Sorted vertex pairs.
  CHECK(ce.edge_verts[0] < ce.edge_verts[1]);
  CHECK(ce.edge_verts[2] < ce.edge_verts[3]);
}

void test_crease_canonicalize_per_edge_and_dedup() {
  corpus::Mesh cube = corpus::Cube();
  // Two chains; edge (0,1) appears in both with different sharpness.
  cube.crease_indices = {0, 1, 3, 1, 0};
  cube.crease_lengths = {3, 2};
  cube.crease_sharpnesses = {1.0f, 2.0f, 4.0f};  // per-edge (2 + 1)

  tinyusdz::tsd::CreaseEdges ce;
  std::string err;
  Result r = CanonicalizeCreases(ToView(cube), &ce, &err);
  CHECK(r == Result::Success);
  CHECK(ce.sharpnesses.size() == 2);  // (0,1) deduped, (1,3) kept
  bool found01 = false;
  bool found13 = false;
  for (size_t i = 0; i < ce.sharpnesses.size(); i++) {
    const uint32_t a = ce.edge_verts[2 * i];
    const uint32_t b = ce.edge_verts[2 * i + 1];
    if (a == 0 && b == 1) {
      found01 = true;
      CHECK(std::fabs(ce.sharpnesses[i] - 4.0f) < 1e-6f);  // max(1, 4)
    }
    if (a == 1 && b == 3) {
      found13 = true;
      CHECK(std::fabs(ce.sharpnesses[i] - 2.0f) < 1e-6f);
    }
  }
  CHECK(found01);
  CHECK(found13);
}

void test_crease_sharpness_clamped() {
  corpus::Mesh cube = corpus::Cube();
  cube.crease_indices = {0, 1};
  cube.crease_lengths = {2};
  cube.crease_sharpnesses = {1.0e10f};  // way past infinite

  tinyusdz::tsd::CreaseEdges ce;
  std::string err;
  CHECK(CanonicalizeCreases(ToView(cube), &ce, &err) == Result::Success);
  CHECK(ce.sharpnesses.size() == 1);
  CHECK(std::fabs(ce.sharpnesses[0] - tinyusdz::tsd::kInfiniteSharpness) <
        1e-6f);
}

}  // namespace

int main() {
  // Validation / hardening
  TEST(test_scheme_none_rejected);
  TEST(test_null_output_rejected);
  TEST(test_empty_mesh_rejected);
  TEST(test_bad_level_rejected);
  TEST(test_out_of_range_index_rejected);
  TEST(test_count_mismatch_rejected);
  TEST(test_bad_face_degree_rejected);
  TEST(test_crease_sharpness_count_rejected);
  TEST(test_hole_index_out_of_range_rejected);

  // Passthrough
  TEST(test_level0_passthrough);
  TEST(test_level0_fvar_expansion);

  // Topology
  TEST(test_topology_cube);
  TEST(test_topology_single_quad_boundary);
  TEST(test_topology_mixed_degree);
  TEST(test_topology_non_manifold_rejected);
  TEST(test_topology_inconsistent_winding_rejected);
  TEST(test_topology_degenerate_edge_rejected);

  // Crease canonicalization
  TEST(test_crease_canonicalize_per_crease);
  TEST(test_crease_canonicalize_per_edge_and_dedup);
  TEST(test_crease_sharpness_clamped);

  printf("feat-subdiv: %d checks, %d failures\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
