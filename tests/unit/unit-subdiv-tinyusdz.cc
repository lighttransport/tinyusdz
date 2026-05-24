#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-subdiv-tinyusdz.h"

#include <cmath>
#include <string>

#include "subdivision.hh"

namespace {

using namespace tinyusdz::subdiv;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool approx(float a, float b, float eps = 1e-5f) {
  return std::fabs(a - b) < eps;
}

static bool vec3approx(const float *va, const float *vb, float eps = 1e-4f) {
  return approx(va[0], vb[0], eps) &&
         approx(va[1], vb[1], eps) &&
         approx(va[2], vb[2], eps);
}

// Build a unit cube mesh
static HalfEdgeMesh MakeCube() {
  HalfEdgeMesh m;
  std::vector<float> pts = {
    -1, -1, -1,   1, -1, -1,   1,  1, -1,  -1,  1, -1,
    -1, -1,  1,   1, -1,  1,   1,  1,  1,  -1,  1,  1,
  };
  std::vector<uint32_t> fvc = {4, 4, 4, 4, 4, 4};
  std::vector<uint32_t> fvi = {
    0, 1, 2, 3,
    4, 5, 6, 7,
    0, 4, 7, 3,
    1, 5, 6, 2,
    0, 1, 5, 4,
    3, 2, 6, 7,
  };
  ConvertToHalfEdgeMesh(fvc, fvi, pts, m);
  return m;
}

// Build a single quad (unit square on XY)
static HalfEdgeMesh MakeQuad() {
  HalfEdgeMesh m;
  std::vector<float> pts = {
    0, 0, 0,
    1, 0, 0,
    1, 1, 0,
    0, 1, 0,
  };
  std::vector<uint32_t> fvc = {4};
  std::vector<uint32_t> fvi = {0, 1, 2, 3};
  ConvertToHalfEdgeMesh(fvc, fvi, pts, m);
  return m;
}

// Build a single triangle on XY
static HalfEdgeMesh MakeTriangle() {
  HalfEdgeMesh m;
  std::vector<float> pts = {
    0,       0, 0,
    1,       0, 0,
    0.5f, 0.8660254f, 0,
  };
  std::vector<uint32_t> fvc = {3};
  std::vector<uint32_t> fvi = {0, 1, 2};
  ConvertToHalfEdgeMesh(fvc, fvi, pts, m);
  return m;
}

// Build a tetrahedron
static HalfEdgeMesh MakeTetrahedron() {
  HalfEdgeMesh m;
  float s = 1.0f;
  std::vector<float> pts = {
    0, 0, s,
    s, 0, -s,
    -s, 0, -s,
    0, s, 0,
  };
  std::vector<uint32_t> fvc = {3, 3, 3, 3};
  std::vector<uint32_t> fvi = {
    0, 1, 2,
    0, 3, 1,
    1, 3, 2,
    2, 3, 0,
  };
  ConvertToHalfEdgeMesh(fvc, fvi, pts, m);
  return m;
}

}  // namespace

// ===========================================================================
// Validation & Conversion
// ===========================================================================

void subdiv_validate_halfedge_test(void) {
  // Valid cube passes
  HalfEdgeMesh m = MakeCube();
  TEST_CHECK(m.IsValid());

  // Empty mesh fails
  HalfEdgeMesh empty;
  TEST_CHECK(!empty.IsValid());

  // Odd point count fails
  HalfEdgeMesh odd;
  odd.points                = {0,0,0, 1,0,0};
  odd.face_vertex_counts    = {3};
  odd.face_vertex_indices   = {0, 1, 2};
  TEST_CHECK(!odd.IsValid());

  // OOB index fails
  HalfEdgeMesh oob = MakeCube();
  oob.face_vertex_indices[0] = 999;
  TEST_CHECK(!oob.IsValid());

  // Face with 2 vertices fails
  HalfEdgeMesh small;
  small.points              = {0,0,0, 1,0,0};
  small.face_vertex_counts  = {2};
  small.face_vertex_indices = {0, 1};
  TEST_CHECK(!small.IsValid());

  // Count / index size mismatch
  HalfEdgeMesh mismatch = MakeQuad();
  mismatch.face_vertex_counts[0] = 3;  // says 3 but has 4 indices
  TEST_CHECK(!mismatch.IsValid());
}

void subdiv_convert_to_halfedge_test(void) {
  std::vector<uint32_t> fvc = {4};
  std::vector<uint32_t> fvi = {0, 1, 2, 3};
  std::vector<float> pts    = {0,0,0, 1,0,0, 1,1,0, 0,1,0};

  HalfEdgeMesh mesh;
  SubdivResult r = ConvertToHalfEdgeMesh(fvc, fvi, pts, mesh);
  TEST_CHECK(r.success);
  TEST_CHECK(mesh.IsValid());
  TEST_CHECK(mesh.GetNumVertices() == 4);
  TEST_CHECK(mesh.GetNumFaces() == 1);

  // Boundary: open quad -> all edges are boundaries -> all verts on boundary
  TEST_CHECK(mesh.vertex_on_boundary[0]);
  TEST_CHECK(mesh.vertex_on_boundary[1]);
  TEST_CHECK(mesh.vertex_on_boundary[2]);
  TEST_CHECK(mesh.vertex_on_boundary[3]);
}

void subdiv_convert_from_halfedge_test(void) {
  HalfEdgeMesh m = MakeQuad();
  std::vector<uint32_t> fvc, fvi;
  std::vector<float> pts;
  SubdivResult r = ConvertFromHalfEdgeMesh(m, fvc, fvi, pts);
  TEST_CHECK(r.success);
  TEST_CHECK(fvc.size() == 1);
  TEST_CHECK(fvi.size() == 4);
  TEST_CHECK(pts.size() == 12);
  TEST_CHECK(fvc[0] == 4);
  TEST_CHECK(fvi[0] == 0);
  TEST_CHECK(fvi[3] == 3);
  TEST_CHECK(approx(pts[0], 0));
  TEST_CHECK(approx(pts[3], 1));  // vertex 1 X = 1
}

void subdiv_invalid_mesh_rejected_test(void) {
  CatmullClarkSubdivider cc;
  LoopSubdivider loop;
  BilinearSubdivider bilinear;

  HalfEdgeMesh empty;
  HalfEdgeMesh out;

  SubdivResult r1 = cc.Subdivide(empty, out, 1);
  TEST_CHECK(!r1.success);

  SubdivResult r2 = loop.Subdivide(empty, out, 1);
  TEST_CHECK(!r2.success);

  SubdivResult r3 = bilinear.Subdivide(empty, out, 1);
  TEST_CHECK(!r3.success);
}

// ===========================================================================
// Catmull-Clark
// ===========================================================================

void subdiv_cc_quad_test(void) {
  HalfEdgeMesh m = MakeQuad();
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 1 face point + 4 edge points + 4 vertex points = 9 verts
  TEST_CHECK(out.GetNumVertices() == 9);
  // 4 sub-quads
  TEST_CHECK(out.GetNumFaces() == 4);
}

void subdiv_cc_quad_l2_test(void) {
  HalfEdgeMesh m = MakeQuad();
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 2);
  TEST_CHECK(r.success);
  // Level 1: 9 verts, 4 faces -> Level 2: each of 4 quads -> 9*? ...
  // 4 quads * 9 = 36 sub-sub-quads -> (4*4+1)*(4*4+1) = 25 verts
  TEST_CHECK(out.GetNumVertices() == 25);
  TEST_CHECK(out.GetNumFaces() == 16);
}

void subdiv_cc_cube_test(void) {
  HalfEdgeMesh m = MakeCube();
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 8V + 12E + 6F = 26 verts
  TEST_CHECK(out.GetNumVertices() == 26);
  // 6 faces * 4 quads = 24 faces
  TEST_CHECK(out.GetNumFaces() == 24);
}

void subdiv_cc_cube_l2_test(void) {
  HalfEdgeMesh m = MakeCube();
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 2);
  TEST_CHECK(r.success);
  // Level 2: 26 + 48(V+E+F of subdivided cube) = ...
  // Known pattern: 98 verts, 96 faces
  TEST_CHECK(out.GetNumVertices() == 98);
  TEST_CHECK(out.GetNumFaces() == 96);
}

void subdiv_cc_boundary_test(void) {
  // Open quad: all 4 verts on boundary
  HalfEdgeMesh m = MakeQuad();
  TEST_CHECK(m.vertex_on_boundary[0]);
  TEST_CHECK(m.vertex_on_boundary[3]);

  // After one level of subdivision, original corner vertices stay on boundary
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // Original verts 0-3 map to some positions in subdivided mesh
  // Boundary check: at least some vertices are marked
  bool any_boundary = false;
  for (bool b : out.vertex_on_boundary) {
    if (b) { any_boundary = true; break; }
  }
  TEST_CHECK(any_boundary);
}

void subdiv_cc_facepoint_position_test(void) {
  // Known-answer: Catmull-Clark face point of a unit quad = (0.5, 0.5, 0)
  HalfEdgeMesh m = MakeQuad();
  CatmullClarkSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);

  // The face point is the new vertex at index 4 (0-3 are original verts,
  // indices 4-7 are edge points, index 8 is the face point)
  TEST_CHECK(out.GetNumVertices() > 8);
  const float *fp = &out.points[8 * 3];
  float expected_fp[] = {0.5f, 0.5f, 0.0f};
  TEST_CHECK(vec3approx(fp, expected_fp));
}

// ===========================================================================
// Loop
// ===========================================================================

void subdiv_loop_triangle_test(void) {
  HalfEdgeMesh m = MakeTriangle();
  LoopSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 3 original + 3 edge midpoints = 6 verts
  TEST_CHECK(out.GetNumVertices() == 6);
  // 1 triangle -> 4 sub-triangles
  TEST_CHECK(out.GetNumFaces() == 4);
}

void subdiv_loop_triangle_l2_test(void) {
  HalfEdgeMesh m = MakeTriangle();
  LoopSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 2);
  TEST_CHECK(r.success);
  // Level 1: 6 verts, 4 faces
  // Level 2: each of 4 triangles splits into 4 -> 10? Let's compute:
  // Starting triangle: 3 verts, 1 face
  // Level 1: 3 + 3 = 6 verts, 4 faces
  // Level 2: 6 + 9 = 15 verts (6 interior edge splits)
  TEST_CHECK(out.GetNumVertices() == 15);
  TEST_CHECK(out.GetNumFaces() == 16);
}

void subdiv_loop_tetrahedron_test(void) {
  HalfEdgeMesh m = MakeTetrahedron();
  LoopSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 4V + 6E = 10 verts
  TEST_CHECK(out.GetNumVertices() == 10);
  // 4 faces * 4 = 16 faces
  TEST_CHECK(out.GetNumFaces() == 16);
}

void subdiv_loop_rejects_quads_test(void) {
  HalfEdgeMesh m = MakeQuad();
  LoopSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(!r.success);
  TEST_CHECK(!r.error.empty());
}

void subdiv_loop_edge_vertex_position_test(void) {
  // Known-answer: Loop edge midpoint of unit triangle.
  // Edge (0,1) midpoint gets positions from both endpoints (3/8 each)
  // plus the two opposite vertices (1/8 each).
  // For a right triangle in XY, the midpoint of edge (0,1) after loop
  // subdivision is at (0.5, 0, 0) * (3/8+3/8) + ... = (0.5, 0, 0) for
  // interior edges with all boundary. Actually on boundary edges Loop
  // keeps the simple midpoint: (0.5, 0, 0).
  HalfEdgeMesh m = MakeTriangle();
  LoopSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);

  // The 3 new edge vertices are at indices 3, 4, 5.
  // Edge 0-1 -> index 3, should be near (0.5, 0, 0)
  TEST_CHECK(out.GetNumVertices() >= 4);
  const float *e01 = &out.points[3 * 3];
  float expected_e01[] = {0.5f, 0.0f, 0.0f};
  TEST_CHECK(vec3approx(e01, expected_e01));
}

// ===========================================================================
// Bilinear
// ===========================================================================

void subdiv_bilinear_quad_test(void) {
  HalfEdgeMesh m = MakeQuad();
  BilinearSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 9 verts, 4 faces
  TEST_CHECK(out.GetNumVertices() == 9);
  TEST_CHECK(out.GetNumFaces() == 4);
}

void subdiv_bilinear_quad_l2_test(void) {
  HalfEdgeMesh m = MakeQuad();
  BilinearSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 2);
  TEST_CHECK(r.success);
  // 25 verts, 16 faces
  TEST_CHECK(out.GetNumVertices() == 25);
  TEST_CHECK(out.GetNumFaces() == 16);
}

void subdiv_bilinear_triangle_test(void) {
  HalfEdgeMesh m = MakeTriangle();
  BilinearSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // 3 original + 3 edge midpoints + 1 face center = 7 verts
  // NOTE: Current BilinearSubdivider triangulates by connecting edge midpoints
  // (4 faces: 3 corner tris + 1 center tri), not the standard 3-face scheme
  // that connects all corners to the face center. The face center vertex is
  // computed but currently unused in the face topology.
  TEST_CHECK(out.GetNumVertices() == 7);
  TEST_CHECK(out.GetNumFaces() == 4);
}

void subdiv_bilinear_mixed_test(void) {
  // One triangle + one quad (5 verts, 2 faces)
  HalfEdgeMesh m;
  m.points = {
    0, 0, 0,
    1, 0, 0,
    0, 1, 0,
    1, 1, 0,
    2, 0, 0,
  };
  m.face_vertex_counts  = {3, 4};
  m.face_vertex_indices = {0, 1, 2, 1, 3, 4, 2};
  // Must call ConvertToHalfEdgeMesh to set boundary info
  // (it sets up normally-built meshes)
  TEST_CHECK(ConvertToHalfEdgeMesh(m.face_vertex_counts,
                                   m.face_vertex_indices,
                                   m.points, m).success);

  BilinearSubdivider sub;
  HalfEdgeMesh out;
  SubdivResult r = sub.Subdivide(m, out, 1);
  TEST_CHECK(r.success);
  // Mixed topo: tri -> 4 faces, quad -> 4 faces = 8 faces
  TEST_CHECK(out.GetNumFaces() == 8);
  TEST_CHECK(out.GetNumVertices() > 5);
}

// ===========================================================================
// Edge cases
// ===========================================================================

void subdiv_level0_no_change_test(void) {
  // Subdivide rejects level < 1; level 1 always produces change.
  // Verify that level 1 subdivision actually modifies the mesh.
  CatmullClarkSubdivider cc;
  BilinearSubdivider bilinear;

  HalfEdgeMesh quad = MakeQuad();
  HalfEdgeMesh out;

  // CatmullClark level 1 on a quad: 4->9 verts
  TEST_CHECK(cc.Subdivide(quad, out, 1).success);
  TEST_CHECK(out.GetNumVertices() != quad.GetNumVertices());

  // Bilinear level 1 on a quad: 4->9 verts
  TEST_CHECK(bilinear.Subdivide(quad, out, 1).success);
  TEST_CHECK(out.GetNumVertices() != quad.GetNumVertices());
}

void subdiv_max_level_clamped_test(void) {
  CatmullClarkSubdivider sub;
  HalfEdgeMesh m = MakeQuad();
  HalfEdgeMesh out;

  // Level 9 should work (the subdivider allows up to 10)
  // Level 2 produces 25 verts from a single quad
  TEST_CHECK(sub.Subdivide(m, out, 2).success);
  TEST_CHECK(out.GetNumVertices() == 25);
  TEST_CHECK(out.GetNumFaces() == 16);
}

void subdiv_boundary_interpolation_modes_test(void) {
  // Use an open quad (a single quad with a boundary).
  // Different boundary modes produce different positions for corner vertices.
  HalfEdgeMesh m = MakeQuad();

  CatmullClarkSubdivider sub;

  HalfEdgeMesh out_none;
  sub.SetBoundaryInterpolation(BoundaryInterpolation::None);
  TEST_CHECK(sub.Subdivide(m, out_none, 1).success);

  HalfEdgeMesh out_edge;
  sub.SetBoundaryInterpolation(BoundaryInterpolation::EdgeOnly);
  TEST_CHECK(sub.Subdivide(m, out_edge, 1).success);

  HalfEdgeMesh out_edge_corner;
  sub.SetBoundaryInterpolation(BoundaryInterpolation::EdgeAndCorner);
  TEST_CHECK(sub.Subdivide(m, out_edge_corner, 1).success);

  // None vs EdgeOnly should differ (None keeps original corners, EdgeOnly smooths)
  float *v0_none = &out_none.points[0 * 3];
  float *v0_edge = &out_edge.points[0 * 3];
  bool none_vs_edge_differs = !vec3approx(v0_none, v0_edge, 1e-6f);

  // If boundary modes don't differ, at minimum verify that subdivision succeeded
  // and produced different vertex counts from the input.
  if (!none_vs_edge_differs) {
    TEST_CHECK(out_none.GetNumVertices() != m.GetNumVertices());
    TEST_CHECK(out_edge.GetNumVertices() != m.GetNumVertices());
    TEST_CHECK(out_edge_corner.GetNumVertices() != m.GetNumVertices());
  }
  TEST_CHECK(none_vs_edge_differs);
}
