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
#include <thread>
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
// Refinement: analytic checks
// ---------------------------------------------------------------------------

void test_bilinear_quad_level1_exact() {
  corpus::Mesh quad = corpus::SingleQuad();
  Options opts;
  opts.scheme = Scheme::Bilinear;
  opts.level = 1;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(quad), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  // 4 verts + 4 edge midpoints + 1 centroid; 4 quads.
  CHECK(out.points.size() == 9 * 3);
  CHECK(out.face_vertex_counts.size() == 4);
  for (uint32_t c : out.face_vertex_counts) {
    CHECK(c == 4);
  }
  // Original vertices pass through exactly.
  for (size_t i = 0; i < 12; i++) {
    CHECK(out.points[i] == quad.points[i]);
  }
  // Centroid (last point).
  for (int c = 0; c < 3; c++) {
    const float expect = 0.25f * (quad.points[0 + c] + quad.points[3 + c] +
                                  quad.points[6 + c] + quad.points[9 + c]);
    CHECK(std::fabs(out.points[8 * 3 + size_t(c)] - expect) < 1e-6f);
  }
}

void test_catmark_cube_level1_smooth_vertex() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  opts.level = 1;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  // V + E + F = 8 + 12 + 6 = 26 points; 6*4 = 24 quads.
  CHECK(out.points.size() == 26 * 3);
  CHECK(out.face_vertex_counts.size() == 24);

  // Hand-derive the smooth vertex rule for vertex 0 (valence 3):
  // V' = (n-2)/n*V + sum(other endpoints)/n^2 + sum(child face points)/n^2.
  // Child face points (centroids) occupy out.points[(8+12+f)*3].
  tinyusdz::tsd::Topology topo;
  CHECK(BuildTopology(cube.face_vertex_counts.data(), 6,
                      cube.face_vertex_indices.data(), 24, 8, &topo,
                      nullptr) == Result::Success);
  const uint32_t v = 0;
  const float n = 3.0f;
  for (int c = 0; c < 3; c++) {
    float acc = (n - 2.0f) / n * cube.points[v * 3 + size_t(c)];
    for (uint32_t i = topo.vert_edge_offsets[v];
         i < topo.vert_edge_offsets[v + 1]; i++) {
      const uint32_t e = topo.vert_edges[i];
      const uint32_t other = (topo.edge_verts[2 * e] == v)
                                 ? topo.edge_verts[2 * e + 1]
                                 : topo.edge_verts[2 * e];
      acc += cube.points[other * 3 + size_t(c)] / (n * n);
    }
    for (uint32_t i = topo.vert_face_offsets[v];
         i < topo.vert_face_offsets[v + 1]; i++) {
      const uint32_t f = topo.vert_faces[i];
      acc += out.points[(8 + 12 + f) * 3 + size_t(c)] / (n * n);
    }
    CHECK(std::fabs(out.points[v * 3 + size_t(c)] - acc) < 1e-6f);
  }
}

void test_infinite_corner_pinned() {
  corpus::Mesh cube = corpus::Cube();
  cube.corner_indices = {3};
  cube.corner_sharpnesses = {10.0f};
  Options opts;
  opts.level = 3;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  for (int c = 0; c < 3; c++) {
    CHECK(out.points[3 * 3 + size_t(c)] == cube.points[3 * 3 + size_t(c)]);
  }
}

void test_infinite_crease_midpoints() {
  // Crease ring at sharpness 10: at level 1 the crease edge children are
  // exact midpoints and ring vertices follow (6V + ea + eb) / 8.
  corpus::Mesh cube = corpus::CreasedCube(10.0f, "creased_inf");
  Options opts;
  opts.level = 1;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);

  tinyusdz::tsd::Topology topo;
  CHECK(BuildTopology(cube.face_vertex_counts.data(), 6,
                      cube.face_vertex_indices.data(), 24, 8, &topo,
                      nullptr) == Result::Success);
  // Ring edges: (2,3), (3,5), (5,4), (4,2). Edge child ids are 8 + e.
  const uint32_t ring[4][2] = {{2, 3}, {3, 5}, {5, 4}, {4, 2}};
  for (const auto &rv : ring) {
    // Locate the edge id.
    uint32_t edge = 0xFFFFFFFFu;
    for (uint32_t e = 0; e < topo.num_edges; e++) {
      const uint32_t a = topo.edge_verts[2 * e];
      const uint32_t b = topo.edge_verts[2 * e + 1];
      if ((a == rv[0] && b == rv[1]) || (a == rv[1] && b == rv[0])) {
        edge = e;
        break;
      }
    }
    CHECK(edge != 0xFFFFFFFFu);
    for (int c = 0; c < 3; c++) {
      const float mid = 0.5f * (cube.points[rv[0] * 3 + size_t(c)] +
                                cube.points[rv[1] * 3 + size_t(c)]);
      CHECK(std::fabs(out.points[(8 + edge) * 3 + size_t(c)] - mid) < 1e-6f);
    }
  }
  // Ring vertex 3 has crease neighbors 2 and 5 on the ring.
  for (int c = 0; c < 3; c++) {
    const float expect =
        (6.0f * cube.points[3 * 3 + size_t(c)] +
         cube.points[2 * 3 + size_t(c)] + cube.points[5 * 3 + size_t(c)]) /
        8.0f;
    CHECK(std::fabs(out.points[3 * 3 + size_t(c)] - expect) < 1e-6f);
  }
}

void test_partition_of_unity() {
  // A constant "vertex" primvar must stay exactly constant through smooth
  // refinement (weights sum to 1), and a constant "varying" one likewise.
  corpus::Mesh cube = corpus::CreasedCube(1.5f, "creased");
  std::vector<float> ones(8, 1.0f);
  tinyusdz::tsd::VertexPrimvarView pv[2];
  pv[0].values = ones.data();
  pv[0].stride = 1;
  pv[0].varying = false;
  pv[1].values = ones.data();
  pv[1].stride = 1;
  pv[1].varying = true;

  Options opts;
  opts.level = 3;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), nullptr, 0, pv, 2, opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.vertex_primvars.size() == 2);
  for (const auto &channel : out.vertex_primvars) {
    CHECK(channel.size() == out.points.size() / 3);
    for (float v : channel) {
      CHECK(std::fabs(v - 1.0f) < 1e-6f);
    }
  }
}

void test_face_source_cube() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  opts.level = 2;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.face_source.size() == 96);  // 6 * 4 * 4
  uint32_t counts[6] = {0, 0, 0, 0, 0, 0};
  for (uint32_t s : out.face_source) {
    CHECK(s < 6);
    counts[s]++;
  }
  for (uint32_t c : counts) {
    CHECK(c == 16);
  }
}

void test_holes_filtered() {
  corpus::Mesh m = corpus::CubeWithHoles();  // holes: faces 1, 4
  Options opts;
  opts.level = 1;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(m), opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.face_vertex_counts.size() == 16);  // (6 - 2) * 4
  for (uint32_t s : out.face_source) {
    CHECK(s != 1 && s != 4);
  }

  // With remove_holes=false all 24 children remain.
  opts.remove_holes = false;
  RefinedMesh out2;
  r = Refine(ToView(m), opts, &out2, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out2.face_vertex_counts.size() == 24);
}

void test_caps_enforced() {
  corpus::Mesh cube = corpus::Cube();
  Options opts;
  opts.level = 4;
  opts.max_faces = 100;  // cube L4 would need 6*4^4 = 1536 faces
  RefinedMesh out;
  std::string err;
  CHECK(Refine(ToView(cube), opts, &out, &err) == Result::LimitExceeded);
}

// ---------------------------------------------------------------------------
// FaceVarying smooth modes
// ---------------------------------------------------------------------------

void test_fvar_continuous_equals_vertex_refinement() {
  // A fvar channel that is continuous everywhere (indexed by vertex) on a
  // closed mesh must refine identically to a "vertex" primvar under any
  // smooth fvar mode.
  corpus::Mesh cube = corpus::Cube();
  std::vector<float> vals(8 * 2);
  for (size_t v = 0; v < 8; v++) {
    vals[2 * v] = cube.points[3 * v];
    vals[2 * v + 1] = cube.points[3 * v + 1];
  }

  tinyusdz::tsd::FVarChannelView ch;
  ch.values = vals.data();
  ch.num_values = 8;
  ch.indices = cube.face_vertex_indices.data();  // continuous: vertex ids
  ch.stride = 2;
  ch.interpolation = tinyusdz::tsd::FVarLinearInterpolation::CornersPlus1;

  tinyusdz::tsd::VertexPrimvarView pv;
  pv.values = vals.data();
  pv.stride = 2;
  pv.varying = false;

  Options opts;
  opts.level = 2;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(cube), &ch, 1, &pv, 1, opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  CHECK(out.fvar.size() == 1);
  CHECK(out.vertex_primvars.size() == 1);

  // Expand the vertex primvar per corner and compare.
  const std::vector<float> &vpv = out.vertex_primvars[0];
  const std::vector<float> &fv = out.fvar[0];
  CHECK(fv.size() == out.face_vertex_indices.size() * 2);
  for (size_t i = 0; i < out.face_vertex_indices.size(); i++) {
    const uint32_t v = out.face_vertex_indices[i];
    for (size_t c = 0; c < 2; c++) {
      CHECK(std::fabs(fv[2 * i + c] - vpv[2 * v + c]) < 1e-6f);
    }
  }
}

void test_fvar_island_constants_preserved() {
  // Constant values within each UV island must stay exactly constant
  // through smooth refinement (partition of unity on the split mesh).
  corpus::Mesh m = corpus::Cube();
  // Two islands of 3 faces; one constant value id per island.
  const uint32_t face_island[6] = {0, 0, 0, 1, 1, 1};
  std::vector<float> vals = {5.0f, -1.0f, 9.0f, 4.0f};  // 2 values, stride 2
  std::vector<uint32_t> indices(24);
  for (size_t f = 0; f < 6; f++) {
    for (size_t k = 0; k < 4; k++) {
      indices[f * 4 + k] = face_island[f];
    }
  }

  tinyusdz::tsd::FVarChannelView ch;
  ch.values = vals.data();
  ch.num_values = 2;
  ch.indices = indices.data();
  ch.stride = 2;
  ch.interpolation = tinyusdz::tsd::FVarLinearInterpolation::None;

  Options opts;
  opts.level = 2;
  RefinedMesh out;
  std::string err;
  Result r = Refine(ToView(m), &ch, 1, nullptr, 0, opts, &out, &err);
  CHECK_MSG(r == Result::Success, err);
  size_t corner = 0;
  bool all_match = true;
  for (size_t f = 0; f < out.face_vertex_counts.size(); f++) {
    const uint32_t island = face_island[out.face_source[f]];
    for (uint32_t k = 0; k < out.face_vertex_counts[f]; k++, corner++) {
      all_match &=
          std::fabs(out.fvar[0][2 * corner] - vals[2 * island]) < 1e-6f;
      all_match &=
          std::fabs(out.fvar[0][2 * corner + 1] - vals[2 * island + 1]) <
          1e-6f;
    }
  }
  CHECK(all_match);
}

void test_fvar_modes_differ_at_seams() {
  // Sanity: "all" (linear) and "none" (smooth) must produce different
  // refined values for a channel with seams.
  corpus::Mesh m = corpus::UVSeamGrid();
  tinyusdz::tsd::FVarChannelView ch;
  ch.values = m.fvar_uv.data();
  ch.num_values = uint32_t(m.fvar_uv.size() / 2);
  ch.indices = m.fvar_indices.data();
  ch.stride = 2;

  Options opts;
  opts.level = 2;
  RefinedMesh out_all;
  RefinedMesh out_none;
  std::string err;
  ch.interpolation = tinyusdz::tsd::FVarLinearInterpolation::All;
  CHECK(Refine(ToView(m), &ch, 1, nullptr, 0, opts, &out_all, &err) ==
        Result::Success);
  ch.interpolation = tinyusdz::tsd::FVarLinearInterpolation::None;
  CHECK(Refine(ToView(m), &ch, 1, nullptr, 0, opts, &out_none, &err) ==
        Result::Success);
  CHECK(out_all.fvar[0].size() == out_none.fvar[0].size());
  CHECK(out_all.fvar[0] != out_none.fvar[0]);
}

// ---------------------------------------------------------------------------
// Determinism (serial vs parallel)
// ---------------------------------------------------------------------------

void parallel_for_4threads(void *user, uint32_t count,
                           void (*body)(void *, uint32_t), void *body_user) {
  (void)user;
  std::vector<std::thread> threads;
  const uint32_t num_threads = 4;
  for (uint32_t t = 0; t < num_threads; t++) {
    threads.emplace_back([=]() {
      for (uint32_t i = t; i < count; i += num_threads) {
        body(body_user, i);
      }
    });
  }
  for (std::thread &th : threads) {
    th.join();
  }
}

void test_parallel_determinism() {
  corpus::Mesh m = corpus::CreasedCube(2.7f, "creased");
  Options serial;
  serial.level = 3;
  RefinedMesh a;
  std::string err;
  CHECK(Refine(ToView(m), serial, &a, &err) == Result::Success);

  Options parallel = serial;
  parallel.parallel_for = parallel_for_4threads;
  RefinedMesh b;
  CHECK(Refine(ToView(m), parallel, &b, &err) == Result::Success);

  CHECK(a.points.size() == b.points.size());
  CHECK(memcmp(a.points.data(), b.points.data(),
               a.points.size() * sizeof(float)) == 0);
  CHECK(a.face_vertex_indices == b.face_vertex_indices);

  // Serial runs are reproducible.
  RefinedMesh c;
  CHECK(Refine(ToView(m), serial, &c, &err) == Result::Success);
  CHECK(memcmp(a.points.data(), c.points.data(),
               a.points.size() * sizeof(float)) == 0);
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

  // Refinement: analytic
  TEST(test_bilinear_quad_level1_exact);
  TEST(test_catmark_cube_level1_smooth_vertex);
  TEST(test_infinite_corner_pinned);
  TEST(test_infinite_crease_midpoints);
  TEST(test_partition_of_unity);
  TEST(test_face_source_cube);
  TEST(test_holes_filtered);
  TEST(test_caps_enforced);
  TEST(test_parallel_determinism);

  // FaceVarying smooth modes
  TEST(test_fvar_continuous_equals_vertex_refinement);
  TEST(test_fvar_island_constants_preserved);
  TEST(test_fvar_modes_differ_at_seams);

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
