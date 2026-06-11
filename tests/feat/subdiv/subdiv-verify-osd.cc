// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv vs OpenSubdiv verification.
//
// Refines every corpus mesh through both tsd (src/tsd) and OpenSubdiv
// (Far::TopologyRefiner / PrimvarRefiner, compiled from the source checkout
// given by OpenSubdiv_ROOT) across the USD subdiv feature matrix, and
// compares results with an ordering-independent comparator:
//   1. vertex/face counts must match
//   2. vertices are matched bijectively by position (eps; corpus meshes are
//      jittered so refined positions are pairwise distinct)
//   3. faces are remapped through the vertex bijection, canonicalized by
//      rotation and compared as sets
//   4. per-corner fvar values and per-vertex varying values compare within
//      eps at matched corners/vertices
//
// Holes: OpenSubdiv keeps hole faces in the refined topology and tags them;
// tsd filters them when remove_holes is set. We compare tsd's filtered
// output against OSD's non-hole faces, which verifies hole propagation.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/primvarRefiner.h>

#include "tsd/tinysubdiv.hh"

#include "subdiv-mesh-corpus.hh"

namespace {

namespace tsd = tinyusdz::tsd;
using namespace OpenSubdiv;

int g_failed_cases = 0;
int g_passed_cases = 0;
bool g_verbose = false;

constexpr float kPosEps = 2e-5f;
constexpr float kValEps = 2e-5f;

// --- OSD primvar carriers ----------------------------------------------------

template <int N>
struct VtxN {
  float v[N];
  void Clear(void * = nullptr) {
    for (int i = 0; i < N; i++) v[i] = 0.0f;
  }
  void AddWithWeight(VtxN const &src, float w) {
    for (int i = 0; i < N; i++) v[i] += w * src.v[i];
  }
};

// --- Case description ----------------------------------------------------------

struct Case {
  const corpus::Mesh *mesh;
  tsd::Options opts;
  bool with_fvar = false;
  tsd::FVarLinearInterpolation fvar_mode = tsd::FVarLinearInterpolation::All;
  std::string label;
};

Sdc::Options::FVarLinearInterpolation ToSdcFVar(
    tsd::FVarLinearInterpolation m) {
  switch (m) {
    case tsd::FVarLinearInterpolation::None:
      return Sdc::Options::FVAR_LINEAR_NONE;
    case tsd::FVarLinearInterpolation::CornersOnly:
      return Sdc::Options::FVAR_LINEAR_CORNERS_ONLY;
    case tsd::FVarLinearInterpolation::CornersPlus1:
      return Sdc::Options::FVAR_LINEAR_CORNERS_PLUS1;
    case tsd::FVarLinearInterpolation::CornersPlus2:
      return Sdc::Options::FVAR_LINEAR_CORNERS_PLUS2;
    case tsd::FVarLinearInterpolation::Boundaries:
      return Sdc::Options::FVAR_LINEAR_BOUNDARIES;
    case tsd::FVarLinearInterpolation::All:
      return Sdc::Options::FVAR_LINEAR_ALL;
  }
  return Sdc::Options::FVAR_LINEAR_ALL;
}

const char *FVarModeName(tsd::FVarLinearInterpolation m) {
  switch (m) {
    case tsd::FVarLinearInterpolation::None:
      return "fvNone";
    case tsd::FVarLinearInterpolation::CornersOnly:
      return "fvCornersOnly";
    case tsd::FVarLinearInterpolation::CornersPlus1:
      return "fvCornersPlus1";
    case tsd::FVarLinearInterpolation::CornersPlus2:
      return "fvCornersPlus2";
    case tsd::FVarLinearInterpolation::Boundaries:
      return "fvBoundaries";
    case tsd::FVarLinearInterpolation::All:
      return "fvAll";
  }
  return "?";
}

Sdc::Options ToSdcOptions(const tsd::Options &o,
                          tsd::FVarLinearInterpolation fvar_mode) {
  Sdc::Options so;
  switch (o.boundary) {
    case tsd::BoundaryInterpolation::EdgeAndCorner:
      so.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);
      break;
    case tsd::BoundaryInterpolation::EdgeOnly:
      so.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);
      break;
    case tsd::BoundaryInterpolation::None:
      so.SetVtxBoundaryInterpolation(Sdc::Options::VTX_BOUNDARY_NONE);
      break;
  }
  so.SetFVarLinearInterpolation(ToSdcFVar(fvar_mode));
  so.SetCreasingMethod(o.creasing == tsd::CreasingMethod::Chaikin
                           ? Sdc::Options::CREASE_CHAIKIN
                           : Sdc::Options::CREASE_UNIFORM);
  so.SetTriangleSubdivision(
      o.triangle_subdivision == tsd::TriangleSubdivision::Smooth
          ? Sdc::Options::TRI_SUB_SMOOTH
          : Sdc::Options::TRI_SUB_CATMARK);
  return so;
}

// --- OSD reference refinement ----------------------------------------------------

struct OsdResult {
  std::vector<float> points;                  // last-level, xyz
  std::vector<uint32_t> face_vertex_counts;   // non-hole faces only
  std::vector<uint32_t> face_vertex_indices;
  std::vector<float> fvar_uv;                 // per corner (of kept faces), stride 2
  bool ok = false;
};

OsdResult RefineWithOsd(const corpus::Mesh &m, const tsd::Options &topts,
                        bool with_fvar,
                        tsd::FVarLinearInterpolation fvar_mode) {
  OsdResult out;

  Sdc::SchemeType type = (topts.scheme == tsd::Scheme::Bilinear)
                             ? Sdc::SCHEME_BILINEAR
                             : (topts.scheme == tsd::Scheme::Loop)
                                   ? Sdc::SCHEME_LOOP
                                   : Sdc::SCHEME_CATMARK;

  std::vector<int> fvc(m.face_vertex_counts.begin(), m.face_vertex_counts.end());
  std::vector<int> fvi(m.face_vertex_indices.begin(), m.face_vertex_indices.end());

  Far::TopologyDescriptor desc;
  memset(&desc, 0, sizeof(desc));
  desc.numVertices = int(m.points.size() / 3);
  desc.numFaces = int(fvc.size());
  desc.numVertsPerFace = fvc.data();
  desc.vertIndicesPerFace = fvi.data();

  // Expand crease chains to vertex pairs (per-crease or per-edge sharpness).
  std::vector<int> crease_pairs;
  std::vector<float> crease_weights;
  if (!m.crease_lengths.empty()) {
    const bool per_crease =
        (m.crease_sharpnesses.size() == m.crease_lengths.size());
    size_t index_off = 0;
    size_t edge_off = 0;
    for (size_t c = 0; c < m.crease_lengths.size(); c++) {
      const size_t len = size_t(m.crease_lengths[c]);
      for (size_t k = 0; k + 1 < len; k++) {
        crease_pairs.push_back(m.crease_indices[index_off + k]);
        crease_pairs.push_back(m.crease_indices[index_off + k + 1]);
        crease_weights.push_back(per_crease
                                     ? m.crease_sharpnesses[c]
                                     : m.crease_sharpnesses[edge_off + k]);
      }
      index_off += len;
      edge_off += len - 1;
    }
    desc.numCreases = int(crease_weights.size());
    desc.creaseVertexIndexPairs = crease_pairs.data();
    desc.creaseWeights = crease_weights.data();
  }

  if (!m.corner_indices.empty()) {
    desc.numCorners = int(m.corner_indices.size());
    desc.cornerVertexIndices = m.corner_indices.data();
    desc.cornerWeights = m.corner_sharpnesses.data();
  }
  if (!m.hole_indices.empty()) {
    desc.numHoles = int(m.hole_indices.size());
    desc.holeIndices = m.hole_indices.data();
  }

  std::vector<int> fvar_indices;
  Far::TopologyDescriptor::FVarChannel fvar_channel;
  if (with_fvar) {
    if (!m.fvar_indices.empty()) {
      fvar_indices.assign(m.fvar_indices.begin(), m.fvar_indices.end());
      fvar_channel.numValues = int(m.fvar_uv.size() / 2);
    } else {
      fvar_indices.resize(m.face_vertex_indices.size());
      for (size_t i = 0; i < fvar_indices.size(); i++) {
        fvar_indices[i] = int(i);  // identity: one value per corner
      }
      fvar_channel.numValues = int(fvar_indices.size());
    }
    fvar_channel.valueIndices = fvar_indices.data();
    desc.numFVarChannels = 1;
    desc.fvarChannels = &fvar_channel;
  }

  using Factory = Far::TopologyRefinerFactory<Far::TopologyDescriptor>;
  Far::TopologyRefiner *refiner =
      Factory::Create(desc,
                      Factory::Options(type, ToSdcOptions(topts, fvar_mode)));
  if (!refiner) {
    return out;
  }

  {
    Far::TopologyRefiner::UniformOptions uo(topts.level);
    uo.fullTopologyInLastLevel = true;
    refiner->RefineUniform(uo);
  }

  // Interpolate positions across levels.
  std::vector<VtxN<3>> verts(size_t(refiner->GetNumVerticesTotal()));
  for (size_t i = 0; i < m.points.size() / 3; i++) {
    verts[i].v[0] = m.points[3 * i];
    verts[i].v[1] = m.points[3 * i + 1];
    verts[i].v[2] = m.points[3 * i + 2];
  }
  {
    Far::PrimvarRefiner primvar(*refiner);
    VtxN<3> *src = verts.data();
    for (int lvl = 1; lvl <= topts.level; lvl++) {
      VtxN<3> *dst = src + refiner->GetLevel(lvl - 1).GetNumVertices();
      primvar.Interpolate(lvl, src, dst);
      src = dst;
    }
  }

  // FVar values across levels.
  std::vector<VtxN<2>> fvar_vals;
  if (with_fvar) {
    fvar_vals.resize(size_t(refiner->GetNumFVarValuesTotal(0)));
    for (size_t i = 0; i < m.fvar_uv.size() / 2; i++) {
      fvar_vals[i].v[0] = m.fvar_uv[2 * i];
      fvar_vals[i].v[1] = m.fvar_uv[2 * i + 1];
    }
    Far::PrimvarRefiner primvar(*refiner);
    VtxN<2> *src = fvar_vals.data();
    for (int lvl = 1; lvl <= topts.level; lvl++) {
      VtxN<2> *dst = src + refiner->GetLevel(lvl - 1).GetNumFVarValues(0);
      primvar.InterpolateFaceVarying(lvl, src, dst, 0);
      src = dst;
    }
  }

  Far::TopologyLevel const &last = refiner->GetLevel(topts.level);
  const int nverts = last.GetNumVertices();
  const int nfaces = last.GetNumFaces();

  size_t vert_offset = size_t(refiner->GetNumVerticesTotal()) - size_t(nverts);
  out.points.resize(size_t(nverts) * 3);
  for (int i = 0; i < nverts; i++) {
    out.points[3 * size_t(i)] = verts[vert_offset + size_t(i)].v[0];
    out.points[3 * size_t(i) + 1] = verts[vert_offset + size_t(i)].v[1];
    out.points[3 * size_t(i) + 2] = verts[vert_offset + size_t(i)].v[2];
  }

  size_t fvar_offset = 0;
  if (with_fvar) {
    fvar_offset = size_t(refiner->GetNumFVarValuesTotal(0)) -
                  size_t(last.GetNumFVarValues(0));
  }

  for (int f = 0; f < nfaces; f++) {
    if (last.IsFaceHole(f)) {
      continue;
    }
    Far::ConstIndexArray fverts = last.GetFaceVertices(f);
    out.face_vertex_counts.push_back(uint32_t(fverts.size()));
    for (int k = 0; k < fverts.size(); k++) {
      out.face_vertex_indices.push_back(uint32_t(fverts[k]));
    }
    if (with_fvar) {
      Far::ConstIndexArray fv = last.GetFaceFVarValues(f, 0);
      for (int k = 0; k < fv.size(); k++) {
        out.fvar_uv.push_back(fvar_vals[fvar_offset + size_t(fv[k])].v[0]);
        out.fvar_uv.push_back(fvar_vals[fvar_offset + size_t(fv[k])].v[1]);
      }
    }
  }

  delete refiner;
  out.ok = true;
  return out;
}

// --- Comparator ---------------------------------------------------------------

// Bijective vertex match by position via spatial hashing. Returns map from
// tsd vertex id -> osd vertex id, or empty on failure.
bool MatchVertices(const std::vector<float> &a, const std::vector<float> &b,
                   std::vector<uint32_t> *a_to_b, std::string *why) {
  const size_t n = a.size() / 3;
  if (b.size() != a.size()) {
    *why = "vertex count mismatch: tsd " + std::to_string(n) + " vs osd " +
           std::to_string(b.size() / 3);
    return false;
  }

  const float cell = 4.0f * kPosEps;
  auto key = [cell](float x, float y, float z) -> uint64_t {
    const int64_t qx = int64_t(std::floor(x / cell));
    const int64_t qy = int64_t(std::floor(y / cell));
    const int64_t qz = int64_t(std::floor(z / cell));
    uint64_t h = uint64_t(qx) * 0x9E3779B185EBCA87ull;
    h ^= uint64_t(qy) * 0xC2B2AE3D27D4EB4Full;
    h ^= uint64_t(qz) * 0x165667B19E3779F9ull;
    return h;
  };

  std::multimap<uint64_t, uint32_t> grid;
  for (uint32_t i = 0; i < n; i++) {
    grid.insert({key(b[3 * i], b[3 * i + 1], b[3 * i + 2]), i});
  }

  a_to_b->assign(n, 0xFFFFFFFFu);
  std::vector<uint8_t> b_used(n, 0);
  for (uint32_t i = 0; i < n; i++) {
    const float x = a[3 * i];
    const float y = a[3 * i + 1];
    const float z = a[3 * i + 2];
    uint32_t match = 0xFFFFFFFFu;
    int match_count = 0;
    // Inspect the 27 neighboring cells.
    for (int dx = -1; dx <= 1; dx++) {
      for (int dy = -1; dy <= 1; dy++) {
        for (int dz = -1; dz <= 1; dz++) {
          auto range = grid.equal_range(
              key(x + float(dx) * cell, y + float(dy) * cell,
                  z + float(dz) * cell));
          for (auto it = range.first; it != range.second; ++it) {
            const uint32_t j = it->second;
            if (std::fabs(b[3 * j] - x) <= kPosEps &&
                std::fabs(b[3 * j + 1] - y) <= kPosEps &&
                std::fabs(b[3 * j + 2] - z) <= kPosEps) {
              if (match != j) {
                match = j;
                match_count++;
              }
            }
          }
        }
      }
    }
    if (match_count != 1 || b_used[match]) {
      *why = "ambiguous or missing position match for tsd vertex " +
             std::to_string(i);
      return false;
    }
    b_used[match] = 1;
    (*a_to_b)[i] = match;
  }
  return true;
}

struct CanonicalFace {
  std::vector<uint32_t> verts;  // rotated so smallest vertex id first
  uint32_t source_face;         // index into the original face order
  uint32_t rotation;            // how many steps the face was rotated

  bool operator<(const CanonicalFace &o) const { return verts < o.verts; }
};

std::vector<CanonicalFace> CanonicalizeFaces(
    const std::vector<uint32_t> &fvc, const std::vector<uint32_t> &fvi,
    const std::vector<uint32_t> *vert_remap) {
  std::vector<CanonicalFace> out(fvc.size());
  size_t off = 0;
  for (size_t f = 0; f < fvc.size(); f++) {
    const uint32_t n = fvc[f];
    CanonicalFace &cf = out[f];
    cf.verts.resize(n);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t v = fvi[off + k];
      cf.verts[k] = vert_remap ? (*vert_remap)[v] : v;
    }
    uint32_t best = 0;
    for (uint32_t k = 1; k < n; k++) {
      if (cf.verts[k] < cf.verts[best]) {
        best = k;
      }
    }
    std::rotate(cf.verts.begin(), cf.verts.begin() + best, cf.verts.end());
    cf.source_face = uint32_t(f);
    cf.rotation = best;
    off += n;
  }
  std::sort(out.begin(), out.end());
  return out;
}

bool CompareCase(const Case &cs) {
  const corpus::Mesh &m = *cs.mesh;

  // --- tsd ------------------------------------------------------------------
  tsd::MeshView view;
  view.points = m.points.data();
  view.num_points = uint32_t(m.points.size() / 3);
  view.face_vertex_counts = m.face_vertex_counts.data();
  view.num_faces = uint32_t(m.face_vertex_counts.size());
  view.face_vertex_indices = m.face_vertex_indices.data();
  view.num_face_vertex_indices = uint32_t(m.face_vertex_indices.size());
  if (!m.corner_indices.empty()) {
    view.corner_indices = m.corner_indices.data();
    view.num_corners = uint32_t(m.corner_indices.size());
    view.corner_sharpnesses = m.corner_sharpnesses.data();
  }
  if (!m.crease_lengths.empty()) {
    view.crease_indices = m.crease_indices.data();
    view.num_crease_indices = uint32_t(m.crease_indices.size());
    view.crease_lengths = m.crease_lengths.data();
    view.num_crease_lengths = uint32_t(m.crease_lengths.size());
    view.crease_sharpnesses = m.crease_sharpnesses.data();
    view.num_crease_sharpnesses = uint32_t(m.crease_sharpnesses.size());
  }
  if (!m.hole_indices.empty()) {
    view.hole_indices = m.hole_indices.data();
    view.num_holes = uint32_t(m.hole_indices.size());
  }

  tsd::FVarChannelView fvar;
  fvar.values = m.fvar_uv.data();
  fvar.num_values = uint32_t(m.fvar_uv.size() / 2);
  fvar.indices = m.fvar_indices.empty() ? nullptr : m.fvar_indices.data();
  fvar.stride = 2;
  fvar.interpolation = cs.fvar_mode;

  tsd::RefinedMesh refined;
  std::string err;
  tsd::Result r =
      Refine(view, cs.with_fvar ? &fvar : nullptr, cs.with_fvar ? 1u : 0u,
             nullptr, 0, cs.opts, &refined, &err);
  if (r != tsd::Result::Success) {
    fprintf(stderr, "[FAIL] %s: tsd Refine failed: %s (%s)\n",
            cs.label.c_str(), tsd::to_string(r), err.c_str());
    return false;
  }

  // --- OSD --------------------------------------------------------------------
  OsdResult osd = RefineWithOsd(m, cs.opts, cs.with_fvar, cs.fvar_mode);
  if (!osd.ok) {
    fprintf(stderr, "[FAIL] %s: OSD refinement failed\n", cs.label.c_str());
    return false;
  }

  // --- Compare ------------------------------------------------------------------
  std::string why;
  std::vector<uint32_t> vmap;
  if (!MatchVertices(refined.points, osd.points, &vmap, &why)) {
    fprintf(stderr, "[FAIL] %s: %s\n", cs.label.c_str(), why.c_str());
    return false;
  }

  if (refined.face_vertex_counts.size() != osd.face_vertex_counts.size()) {
    fprintf(stderr, "[FAIL] %s: face count mismatch: tsd %zu vs osd %zu\n",
            cs.label.c_str(), refined.face_vertex_counts.size(),
            osd.face_vertex_counts.size());
    return false;
  }

  std::vector<CanonicalFace> tf = CanonicalizeFaces(
      refined.face_vertex_counts, refined.face_vertex_indices, &vmap);
  std::vector<CanonicalFace> of =
      CanonicalizeFaces(osd.face_vertex_counts, osd.face_vertex_indices,
                        nullptr);

  for (size_t i = 0; i < tf.size(); i++) {
    if (tf[i].verts != of[i].verts) {
      fprintf(stderr, "[FAIL] %s: face set mismatch at sorted index %zu\n",
              cs.label.c_str(), i);
      return false;
    }
  }

  // FVar comparison at matched corners (corner correspondence through the
  // canonical rotations of the matched face pair).
  if (cs.with_fvar) {
    std::vector<uint32_t> t_face_off(refined.face_vertex_counts.size() + 1, 0);
    for (size_t f = 0; f < refined.face_vertex_counts.size(); f++) {
      t_face_off[f + 1] = t_face_off[f] + refined.face_vertex_counts[f];
    }
    std::vector<uint32_t> o_face_off(osd.face_vertex_counts.size() + 1, 0);
    for (size_t f = 0; f < osd.face_vertex_counts.size(); f++) {
      o_face_off[f + 1] = o_face_off[f] + osd.face_vertex_counts[f];
    }
    const std::vector<float> &tuv = refined.fvar[0];
    for (size_t i = 0; i < tf.size(); i++) {
      const uint32_t n = uint32_t(tf[i].verts.size());
      for (uint32_t k = 0; k < n; k++) {
        // Corner k of the canonical face = corner (rotation + k) mod n of
        // the source face on each side.
        const uint32_t tk = (tf[i].rotation + k) % n;
        const uint32_t ok = (of[i].rotation + k) % n;
        const uint32_t tcorner = t_face_off[tf[i].source_face] + tk;
        const uint32_t ocorner = o_face_off[of[i].source_face] + ok;
        for (uint32_t c = 0; c < 2; c++) {
          const float tv = tuv[size_t(tcorner) * 2 + c];
          const float ov = osd.fvar_uv[size_t(ocorner) * 2 + c];
          if (std::fabs(tv - ov) > kValEps) {
            fprintf(stderr,
                    "[FAIL] %s: fvar mismatch face %zu corner %u: tsd %f vs "
                    "osd %f\n",
                    cs.label.c_str(), i, k, double(tv), double(ov));
            return false;
          }
        }
      }
    }
  }

  if (g_verbose) {
    printf("[ok] %s (%zu verts, %zu faces)\n", cs.label.c_str(),
           refined.points.size() / 3, refined.face_vertex_counts.size());
  }
  return true;
}

void RunCase(const Case &cs) {
  if (CompareCase(cs)) {
    g_passed_cases++;
  } else {
    g_failed_cases++;
  }
}

const char *SchemeName(tsd::Scheme s) {
  switch (s) {
    case tsd::Scheme::CatmullClark:
      return "catmark";
    case tsd::Scheme::Loop:
      return "loop";
    case tsd::Scheme::Bilinear:
      return "bilinear";
    case tsd::Scheme::None:
      return "none";
  }
  return "?";
}

const char *BoundaryName(tsd::BoundaryInterpolation b) {
  switch (b) {
    case tsd::BoundaryInterpolation::EdgeAndCorner:
      return "edgeAndCorner";
    case tsd::BoundaryInterpolation::EdgeOnly:
      return "edgeOnly";
    case tsd::BoundaryInterpolation::None:
      return "none";
  }
  return "?";
}

}  // namespace

int main(int argc, char **argv) {
  for (int i = 1; i < argc; i++) {
    if (std::string(argv[i]) == "--verbose") {
      g_verbose = true;
    }
  }

  std::vector<corpus::Mesh> meshes;
  meshes.push_back(corpus::Cube());
  meshes.push_back(corpus::SingleQuad());
  meshes.push_back(corpus::SingleTri());
  meshes.push_back(corpus::QuadGrid(4, 4, "quad_grid_4x4"));
  meshes.push_back(corpus::QuadGrid(8, 1, "quad_strip_8x1"));
  meshes.push_back(corpus::QuadTorus(8, 6));
  meshes.push_back(corpus::TriFan(3));
  meshes.push_back(corpus::TriFan(5));
  meshes.push_back(corpus::TriFan(8));
  meshes.push_back(corpus::NGons());
  meshes.push_back(corpus::MixedDegree());
  meshes.push_back(corpus::CreasedCube(0.4f, "creased_cube_0_4"));
  meshes.push_back(corpus::CreasedCube(1.0f, "creased_cube_1_0"));
  meshes.push_back(corpus::CreasedCube(1.5f, "creased_cube_1_5"));
  meshes.push_back(corpus::CreasedCube(2.7f, "creased_cube_2_7"));
  meshes.push_back(corpus::CreasedCube(10.0f, "creased_cube_inf"));
  meshes.push_back(corpus::PerEdgeCreasedCube());
  meshes.push_back(corpus::CorneredGrid());
  meshes.push_back(corpus::CubeWithHoles());
  meshes.push_back(corpus::UVSeamGrid());
  meshes.push_back(corpus::UVCube());
  meshes.push_back(corpus::UVDartGrid());
  meshes.push_back(corpus::UVCreasedCube());

  const tsd::Scheme schemes[] = {tsd::Scheme::CatmullClark,
                                 tsd::Scheme::Bilinear};
  const tsd::BoundaryInterpolation boundaries[] = {
      tsd::BoundaryInterpolation::EdgeAndCorner,
      tsd::BoundaryInterpolation::EdgeOnly,
      tsd::BoundaryInterpolation::None,
  };

  for (const corpus::Mesh &m : meshes) {
    for (tsd::Scheme scheme : schemes) {
      for (tsd::BoundaryInterpolation boundary : boundaries) {
        const bool has_creases = !m.crease_lengths.empty();
        const bool has_fvar = !m.fvar_uv.empty();
        const int num_creasing = has_creases ? 2 : 1;
        const tsd::FVarLinearInterpolation fvar_modes[6] = {
            tsd::FVarLinearInterpolation::All,
            tsd::FVarLinearInterpolation::None,
            tsd::FVarLinearInterpolation::CornersOnly,
            tsd::FVarLinearInterpolation::CornersPlus1,
            tsd::FVarLinearInterpolation::CornersPlus2,
            tsd::FVarLinearInterpolation::Boundaries,
        };
        const int num_fvar_modes = has_fvar ? 6 : 1;
        for (int creasing = 0; creasing < num_creasing; creasing++) {
          for (int fm = 0; fm < num_fvar_modes; fm++) {
            for (int level = 1; level <= 3; level++) {
              Case cs;
              cs.mesh = &m;
              cs.opts.scheme = scheme;
              cs.opts.boundary = boundary;
              cs.opts.creasing = creasing ? tsd::CreasingMethod::Chaikin
                                          : tsd::CreasingMethod::Uniform;
              cs.opts.level = level;
              cs.with_fvar = has_fvar;
              cs.fvar_mode = fvar_modes[fm];
              cs.label = m.name + "/" + SchemeName(scheme) + "/" +
                         BoundaryName(boundary) +
                         (creasing ? "/chaikin" : "/uniform") +
                         (has_fvar ? (std::string("/") +
                                      FVarModeName(cs.fvar_mode))
                                   : std::string()) +
                         "/L" + std::to_string(level);
              RunCase(cs);
            }
          }
        }
      }
    }
  }

  printf("feat-subdiv-verify: %d passed, %d failed\n", g_passed_cases,
         g_failed_cases);
  return g_failed_cases ? 1 : 0;
}
