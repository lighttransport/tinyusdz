// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: closed-form limit positions and limit normals.
//
// Positions and tangents are evaluated with the OpenSubdiv Sdc limit masks
// at the final refinement level (where all faces are quads for Catmull-Clark
// or triangles for Loop):
//   - Catmull-Clark smooth: vertex n/(n+5), 4/(n(n+5)) per edge endpoint,
//     1/(n(n+5)) per face-diagonal vertex (Halstead/Kass/DeRose)
//   - Loop smooth: regular 1/2 + 1/12 ring; else edge weight
//     1/(n + 3/(8 gamma)) with gamma = (5/8 - beta^2)/n
//   - crease: 2/3 vertex + 1/6 per crease-end endpoint (both schemes)
//   - corner: the vertex itself
// Tangents use the Sdc limit tangent masks over a counter-clockwise ordered
// ring of incident edges/faces; normal = normalize(tan1 x tan2).
//
// Bilinear's limit surface is the refined polygon mesh itself: SnapToLimit
// is the identity and limit normals are not defined (rejected).

#include <cmath>
#include <cstring>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

namespace {

constexpr double kPi = 3.14159265358979323846;

enum class VRule : uint8_t { Smooth, Crease, Corner };

inline VRule DetermineVertexRule(float vert_sharpness,
                                 uint32_t sharp_edge_count) {
  if (IsSharp(vert_sharpness)) {
    return VRule::Corner;
  }
  if (sharp_edge_count > 2) {
    return VRule::Corner;
  }
  return (sharp_edge_count == 2) ? VRule::Crease : VRule::Smooth;
}

// Re-runs the topology/sharpness pipeline of Refine() (values are not
// computed) and returns the FINAL level's topology, face-vertex indices and
// sharpness arrays.
Result BuildFinalLevelState(const MeshView &mesh, const Options &options,
                            Topology *topo, std::vector<uint32_t> *out_fvi,
                            std::vector<float> *edge_sharp,
                            std::vector<float> *vert_sharp,
                            std::string *err) {
  const bool loop = (options.scheme == Scheme::Loop);

  // The limit entry points take raw MeshViews; validate exactly like
  // Refine() before touching any authored arrays.
  Result r = ValidateInput(mesh, nullptr, 0, nullptr, 0, options, err);
  if (r != Result::Success) {
    return r;
  }
  if (loop) {
    for (uint32_t f = 0; f < mesh.num_faces; f++) {
      if (mesh.face_vertex_counts[f] != 3) {
        return Fail(Result::InvalidTopology, err,
                    "Loop scheme requires an all-triangle mesh.");
      }
    }
  }

  CreaseEdges creases;
  r = CanonicalizeCreases(mesh, &creases, err);
  if (r != Result::Success) {
    return r;
  }

  uint32_t num_points = mesh.num_points;
  std::vector<uint32_t> fvc(mesh.face_vertex_counts,
                            mesh.face_vertex_counts + mesh.num_faces);
  std::vector<uint32_t> fvi(
      mesh.face_vertex_indices,
      mesh.face_vertex_indices + mesh.num_face_vertex_indices);

  Topology prev_topo;
  std::vector<float> prev_edge_sharp;
  std::vector<float> prev_vert_sharp;

  for (int32_t lvl = 0; lvl <= options.level; lvl++) {
    r = BuildTopology(fvc.data(), uint32_t(fvc.size()), fvi.data(),
                      uint32_t(fvi.size()), num_points, topo, err);
    if (r != Result::Success) {
      return r;
    }

    if (lvl == 0) {
      BakeLevel0Sharpness(mesh, options, *topo, creases, edge_sharp,
                          vert_sharp, nullptr, nullptr);
    } else {
      std::vector<float> es;
      std::vector<float> vs;
      DeriveChildSharpness(prev_topo, prev_edge_sharp, prev_vert_sharp, *topo,
                           options.creasing, &es, &vs);
      *edge_sharp = std::move(es);
      *vert_sharp = std::move(vs);
    }

    if (lvl == options.level) {
      break;
    }

    ChildTopo child;
    r = loop ? BuildChildTopologyTri(*topo, fvi.data(), &child, err)
             : BuildChildTopologyQuad(*topo, fvi.data(), &child, err);
    if (r != Result::Success) {
      return r;
    }
    num_points = child.num_points;
    fvc = std::move(child.fvc);
    fvi = std::move(child.fvi);
    prev_topo = std::move(*topo);
    prev_edge_sharp = std::move(*edge_sharp);
    prev_vert_sharp = std::move(*vert_sharp);
    *topo = Topology();
  }

  *out_fvi = std::move(fvi);
  return Result::Success;
}

// Local corner index of vertex v within face f.
inline uint32_t CornerOfVertex(const Topology &topo, const uint32_t *fvi,
                               uint32_t f, uint32_t v) {
  const uint32_t begin = topo.face_offsets[f];
  const uint32_t n = topo.face_offsets[f + 1] - begin;
  for (uint32_t k = 0; k < n; k++) {
    if (fvi[begin + k] == v) {
      return k;
    }
  }
  return kInvalidIndex;
}

// Counter-clockwise ordered ring of incident edges and faces around v
// (consistent with face winding): face i lies between edges[i] and
// edges[i+1]; the edge that face f traverses outward from v (v -> next)
// precedes f, the inward edge (prev -> v) follows it. For boundary vertices
// the ring starts at the leading boundary edge and edges.size() ==
// faces.size() + 1; for interior vertices edges.size() == faces.size()
// (ring is cyclic, start arbitrary). Returns false on inconsistency.
bool OrderRing(const Topology &topo, const uint32_t *fvi, uint32_t v,
               std::vector<uint32_t> *edges, std::vector<uint32_t> *faces) {
  edges->clear();
  faces->clear();
  const uint32_t ebegin = topo.vert_edge_offsets[v];
  const uint32_t eend = topo.vert_edge_offsets[v + 1];
  const uint32_t valence = eend - ebegin;
  if (valence == 0) {
    return false;
  }

  // Outgoing edge of face f at v (edge from v to the next corner).
  auto outgoing_edge = [&](uint32_t f) -> uint32_t {
    const uint32_t k = CornerOfVertex(topo, fvi, f, v);
    return topo.face_edges[topo.face_offsets[f] + k];
  };
  // Inward edge of face f at v (edge from the previous corner to v).
  auto inward_edge = [&](uint32_t f) -> uint32_t {
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;
    const uint32_t k = CornerOfVertex(topo, fvi, f, v);
    return topo.face_edges[begin + ((k == 0) ? (n - 1) : (k - 1))];
  };
  // The face for which edge e is the outgoing edge at v.
  auto face_with_outgoing = [&](uint32_t e) -> uint32_t {
    for (int i = 0; i < 2; i++) {
      const uint32_t f = topo.edge_faces[2 * e + i];
      if (f != kInvalidIndex && outgoing_edge(f) == e) {
        return f;
      }
    }
    return kInvalidIndex;
  };

  // Start edge: for boundary vertices, the boundary edge that leads a face
  // (is outgoing); for interior vertices, any incident edge.
  uint32_t start = topo.vert_edges[ebegin];
  if (topo.vert_is_boundary[v]) {
    start = kInvalidIndex;
    for (uint32_t i = ebegin; i < eend; i++) {
      const uint32_t e = topo.vert_edges[i];
      if (topo.IsBoundaryEdge(e) && face_with_outgoing(e) != kInvalidIndex) {
        start = e;
        break;
      }
    }
    if (start == kInvalidIndex) {
      return false;
    }
  }

  uint32_t e = start;
  for (uint32_t guard = 0; guard <= valence; guard++) {
    edges->push_back(e);
    const uint32_t f = face_with_outgoing(e);
    if (f == kInvalidIndex) {
      // Trailing boundary edge: ring complete.
      return topo.vert_is_boundary[v] != 0;
    }
    faces->push_back(f);
    e = inward_edge(f);
    if (e == start) {
      return !topo.vert_is_boundary[v];  // interior ring closed
    }
  }
  return false;  // inconsistent ring
}

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
};

inline Vec3 Load(const float *p) { return Vec3{p[0], p[1], p[2]}; }

inline void AddScaled(Vec3 *a, const Vec3 &b, float w) {
  a->x += w * b.x;
  a->y += w * b.y;
  a->z += w * b.z;
}

inline Vec3 Cross(const Vec3 &a, const Vec3 &b) {
  return Vec3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
              a.x * b.y - a.y * b.x};
}

// Gathered, ring-ordered neighborhood of a vertex at the final level.
struct VertexRing {
  uint32_t valence = 0;       // incident edge count
  uint32_t face_count = 0;
  bool boundary = false;
  std::vector<uint32_t> edges;
  std::vector<uint32_t> faces;
  std::vector<float> edge_sharpness;     // per ring edge
  std::vector<uint32_t> edge_other;      // other endpoint per ring edge
  std::vector<uint32_t> face_opposite;   // diagonal vertex per ring face
  int crease_ends[2] = {-1, -1};         // first/last sharp ring edges
};

bool GatherRing(const Topology &topo, const uint32_t *fvi,
                const std::vector<float> &edge_sharp, uint32_t v,
                VertexRing *ring) {
  if (!OrderRing(topo, fvi, v, &ring->edges, &ring->faces)) {
    return false;
  }
  ring->valence = uint32_t(ring->edges.size());
  ring->face_count = uint32_t(ring->faces.size());
  ring->boundary = topo.vert_is_boundary[v] != 0;

  ring->edge_sharpness.resize(ring->valence);
  ring->edge_other.resize(ring->valence);
  ring->crease_ends[0] = -1;
  ring->crease_ends[1] = -1;
  for (uint32_t i = 0; i < ring->valence; i++) {
    const uint32_t e = ring->edges[i];
    ring->edge_sharpness[i] = edge_sharp[e];
    ring->edge_other[i] = (topo.edge_verts[2 * e] == v)
                              ? topo.edge_verts[2 * e + 1]
                              : topo.edge_verts[2 * e];
    if (IsSharp(ring->edge_sharpness[i])) {
      if (ring->crease_ends[0] < 0) {
        ring->crease_ends[0] = int(i);
      }
      ring->crease_ends[1] = int(i);
    }
  }

  ring->face_opposite.resize(ring->face_count);
  for (uint32_t i = 0; i < ring->face_count; i++) {
    const uint32_t f = ring->faces[i];
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;
    uint32_t k = CornerOfVertex(topo, fvi, f, v);
    k += 2;
    if (k >= n) {
      k -= n;
    }
    ring->face_opposite[i] = fvi[begin + k];
  }
  return true;
}

}  // namespace

Result SnapToLimit(const MeshView &base_mesh, const Options &options,
                   RefinedMesh *inout, std::string *err) {
  if (!inout) {
    return Fail(Result::InvalidArgument, err, "inout RefinedMesh is null.");
  }
  if (options.scheme == Scheme::None) {
    return Fail(Result::UnsupportedScheme, err,
                "scheme 'none' has no limit surface.");
  }
  if (options.level < 1) {
    return Fail(Result::InvalidArgument, err,
                "SnapToLimit requires options.level >= 1.");
  }
  if (options.scheme == Scheme::Bilinear) {
    return Result::Success;  // bilinear limit == refined mesh
  }

  Topology topo;
  std::vector<uint32_t> fvi;
  std::vector<float> edge_sharp;
  std::vector<float> vert_sharp;
  Result r = BuildFinalLevelState(base_mesh, options, &topo, &fvi,
                                  &edge_sharp, &vert_sharp, err);
  if (r != Result::Success) {
    return r;
  }
  if (inout->points.size() != size_t(topo.num_points) * 3) {
    return Fail(Result::InvalidArgument, err,
                "RefinedMesh does not match base_mesh refined to "
                "options.level.");
  }

  const bool loop = (options.scheme == Scheme::Loop);
  const std::vector<float> src = inout->points;  // snapshot
  std::vector<float> &dst = inout->points;

  ParallelFor(options, topo.num_points, [&](uint32_t v) {
    const uint32_t ebegin = topo.vert_edge_offsets[v];
    const uint32_t eend = topo.vert_edge_offsets[v + 1];
    const uint32_t valence = eend - ebegin;
    if (valence == 0) {
      return;  // unreferenced point stays
    }

    uint32_t sharp_count = 0;
    uint32_t sharp_other[2] = {0, 0};
    for (uint32_t i = ebegin; i < eend; i++) {
      const uint32_t e = topo.vert_edges[i];
      if (IsSharp(edge_sharp[e])) {
        if (sharp_count < 2) {
          sharp_other[sharp_count] = (topo.edge_verts[2 * e] == v)
                                         ? topo.edge_verts[2 * e + 1]
                                         : topo.edge_verts[2 * e];
        }
        sharp_count++;
      }
    }
    const VRule rule = DetermineVertexRule(vert_sharp[v], sharp_count);

    Vec3 acc;
    if (rule == VRule::Corner) {
      return;  // limit == refined position
    }
    if (rule == VRule::Crease) {
      AddScaled(&acc, Load(&src[size_t(v) * 3]), 2.0f / 3.0f);
      AddScaled(&acc, Load(&src[size_t(sharp_other[0]) * 3]), 1.0f / 6.0f);
      AddScaled(&acc, Load(&src[size_t(sharp_other[1]) * 3]), 1.0f / 6.0f);
    } else if (!loop) {
      // Catmull-Clark smooth limit; valence 2 degenerates to corner.
      const uint32_t fbegin = topo.vert_face_offsets[v];
      const uint32_t fend = topo.vert_face_offsets[v + 1];
      if (valence == 2) {
        return;
      }
      const float n = float(valence);
      const float fw = 1.0f / (n * (n + 5.0f));
      const float ew = 4.0f * fw;
      const float vw = 1.0f - n * (ew + fw);
      AddScaled(&acc, Load(&src[size_t(v) * 3]), vw);
      for (uint32_t i = ebegin; i < eend; i++) {
        const uint32_t e = topo.vert_edges[i];
        const uint32_t other = (topo.edge_verts[2 * e] == v)
                                   ? topo.edge_verts[2 * e + 1]
                                   : topo.edge_verts[2 * e];
        AddScaled(&acc, Load(&src[size_t(other) * 3]), ew);
      }
      for (uint32_t i = fbegin; i < fend; i++) {
        const uint32_t f = topo.vert_faces[i];
        const uint32_t begin = topo.face_offsets[f];
        const uint32_t nf = topo.face_offsets[f + 1] - begin;
        uint32_t k = CornerOfVertex(topo, fvi.data(), f, v);
        k += 2;
        if (k >= nf) {
          k -= nf;
        }
        AddScaled(&acc, Load(&src[size_t(fvi[begin + k]) * 3]), fw);
      }
    } else {
      // Loop smooth limit.
      float ew = 1.0f / 12.0f;
      if (valence != 6) {
        const double n = double(valence);
        const double beta = 0.25 * std::cos(2.0 * kPi / n) + 0.375;
        const double gamma = (0.625 - beta * beta) / n;
        ew = float(1.0 / (n + 3.0 / (8.0 * gamma)));
      }
      const float vw = 1.0f - ew * float(valence);
      AddScaled(&acc, Load(&src[size_t(v) * 3]), vw);
      for (uint32_t i = ebegin; i < eend; i++) {
        const uint32_t e = topo.vert_edges[i];
        const uint32_t other = (topo.edge_verts[2 * e] == v)
                                   ? topo.edge_verts[2 * e + 1]
                                   : topo.edge_verts[2 * e];
        AddScaled(&acc, Load(&src[size_t(other) * 3]), ew);
      }
    }
    dst[size_t(v) * 3] = acc.x;
    dst[size_t(v) * 3 + 1] = acc.y;
    dst[size_t(v) * 3 + 2] = acc.z;
  });

  return Result::Success;
}

Result ComputeLimitNormals(const MeshView &base_mesh, const Options &options,
                           const RefinedMesh &refined,
                           std::vector<float> *out_normals, std::string *err) {
  if (!out_normals) {
    return Fail(Result::InvalidArgument, err, "out_normals is null.");
  }
  if (options.scheme != Scheme::CatmullClark &&
      options.scheme != Scheme::Loop) {
    return Fail(Result::UnsupportedScheme, err,
                "limit normals are only defined for catmullClark and loop.");
  }
  if (options.level < 1) {
    return Fail(Result::InvalidArgument, err,
                "ComputeLimitNormals requires options.level >= 1.");
  }

  Topology topo;
  std::vector<uint32_t> fvi;
  std::vector<float> edge_sharp;
  std::vector<float> vert_sharp;
  Result r = BuildFinalLevelState(base_mesh, options, &topo, &fvi,
                                  &edge_sharp, &vert_sharp, err);
  if (r != Result::Success) {
    return r;
  }
  if (refined.points.size() != size_t(topo.num_points) * 3) {
    return Fail(Result::InvalidArgument, err,
                "RefinedMesh does not match base_mesh refined to "
                "options.level.");
  }

  const bool loop = (options.scheme == Scheme::Loop);
  const std::vector<float> &src = refined.points;
  out_normals->assign(size_t(topo.num_points) * 3, 0.0f);

  // Tangent masks need ring-ordered neighborhoods; rings allocate, so this
  // pass stays serial unless a parallel_for is provided (each iteration is
  // still independent).
  ParallelFor(options, topo.num_points, [&](uint32_t v) {
    VertexRing ring;
    if (!GatherRing(topo, fvi.data(), edge_sharp, v, &ring)) {
      return;  // leave zero normal (unreferenced/degenerate)
    }
    const uint32_t n = ring.valence;

    uint32_t sharp_count = 0;
    for (uint32_t i = 0; i < n; i++) {
      sharp_count += IsSharp(ring.edge_sharpness[i]) ? 1u : 0u;
    }
    const VRule rule = DetermineVertexRule(vert_sharp[v], sharp_count);

    const Vec3 pv = Load(&src[size_t(v) * 3]);
    auto edge_pt = [&](uint32_t i) {
      return Load(&src[size_t(ring.edge_other[i]) * 3]);
    };
    auto face_pt = [&](uint32_t i) {
      return Load(&src[size_t(ring.face_opposite[i]) * 3]);
    };

    Vec3 t1;
    Vec3 t2;

    if (rule == VRule::Corner || (rule == VRule::Smooth && n == 2)) {
      // Corner tangents: along the first and last ring edges.
      AddScaled(&t1, pv, -1.0f);
      AddScaled(&t1, edge_pt(0), 1.0f);
      AddScaled(&t2, pv, -1.0f);
      AddScaled(&t2, edge_pt(n - 1), 1.0f);
      // (Loop scales these by 3; irrelevant for the normal direction.)
    } else if (rule == VRule::Crease) {
      const uint32_t c0 = uint32_t(ring.crease_ends[0]);
      const uint32_t c1 = uint32_t(ring.crease_ends[1]);
      // Tangent along the crease.
      AddScaled(&t1, edge_pt(c0), 0.5f);
      AddScaled(&t1, edge_pt(c1), -0.5f);
      // Tangent across the surface sector between the crease ends.
      const uint32_t interior = c1 - c0 - 1;
      if (!loop) {
        if (interior == 1) {
          AddScaled(&t2, pv, -4.0f / 6.0f);
          AddScaled(&t2, edge_pt(c0), -1.0f / 6.0f);
          AddScaled(&t2, edge_pt(c0 + 1), 4.0f / 6.0f);
          AddScaled(&t2, edge_pt(c1), -1.0f / 6.0f);
          AddScaled(&t2, face_pt(c0), 1.0f / 6.0f);
          AddScaled(&t2, face_pt(c0 + 1), 1.0f / 6.0f);
        } else if (interior > 1) {
          // Biermann et al. (matching Sdc's catmark crease tangent).
          const double k = double(interior + 1);
          const double theta = kPi / k;
          const double cos_t = std::cos(theta);
          const double sin_t = std::sin(theta);
          const double denom = 1.0 / (k * (3.0 + cos_t));
          const double R = (cos_t + 1.0) / sin_t;
          AddScaled(&t2, pv, float(4.0 * R * (cos_t - 1.0) * denom));
          const float cw = float(-R * (1.0 + 2.0 * cos_t) * denom);
          AddScaled(&t2, edge_pt(c0), cw);
          AddScaled(&t2, edge_pt(c1), cw);
          AddScaled(&t2, face_pt(c0), float(sin_t * denom));
          double sin_i = 0.0;
          double sin_i1 = sin_t;
          for (uint32_t i = 1; i < uint32_t(k); i++) {
            sin_i = sin_i1;
            sin_i1 = std::sin(double(i + 1) * theta);
            AddScaled(&t2, edge_pt(c0 + i), float(4.0 * sin_i * denom));
            AddScaled(&t2, face_pt(c0 + i),
                      float((sin_i + sin_i1) * denom));
          }
        } else {
          // Single face between the crease edges.
          AddScaled(&t2, pv, -6.0f);
          AddScaled(&t2, edge_pt(c0), 3.0f);
          AddScaled(&t2, edge_pt(c1), 3.0f);
        }
      } else {
        if (interior == 2) {
          const float root3 = 1.73205080756887729352f;
          AddScaled(&t2, pv, -root3);
          AddScaled(&t2, edge_pt(c0), -0.5f * root3);
          AddScaled(&t2, edge_pt(c1), -0.5f * root3);
          AddScaled(&t2, edge_pt(c0 + 1), root3);
          AddScaled(&t2, edge_pt(c0 + 2), root3);
        } else if (interior > 2) {
          const double theta = kPi / double(interior + 1);
          const float cw = float(-3.0 * std::sin(theta));
          AddScaled(&t2, edge_pt(c0), cw);
          AddScaled(&t2, edge_pt(c1), cw);
          const double ec = -3.0 * 2.0 * (std::cos(theta) - 1.0);
          for (uint32_t i = 1; i <= interior; i++) {
            AddScaled(&t2, edge_pt(c0 + i),
                      float(ec * std::sin(double(i) * theta)));
          }
        } else if (interior == 1) {
          AddScaled(&t2, pv, -3.0f);
          AddScaled(&t2, edge_pt(c0 + 1), 3.0f);
        } else {
          AddScaled(&t2, pv, -6.0f);
          AddScaled(&t2, edge_pt(c0), 3.0f);
          AddScaled(&t2, edge_pt(c1), 3.0f);
        }
      }
    } else if (!loop) {
      // Catmark smooth tangents.
      if (n == 4) {
        AddScaled(&t1, edge_pt(0), 4.0f);
        AddScaled(&t1, edge_pt(2), -4.0f);
        AddScaled(&t1, face_pt(0), 1.0f);
        AddScaled(&t1, face_pt(1), -1.0f);
        AddScaled(&t1, face_pt(2), -1.0f);
        AddScaled(&t1, face_pt(3), 1.0f);
        AddScaled(&t2, edge_pt(1), 4.0f);
        AddScaled(&t2, edge_pt(3), -4.0f);
        AddScaled(&t2, face_pt(0), 1.0f);
        AddScaled(&t2, face_pt(1), 1.0f);
        AddScaled(&t2, face_pt(2), -1.0f);
        AddScaled(&t2, face_pt(3), -1.0f);
      } else {
        const double theta = 2.0 * kPi / double(n);
        const double cos_t = std::cos(theta);
        const double cos_ht = std::cos(0.5 * theta);
        const double lambda =
            (5.0 / 16.0) +
            (1.0 / 16.0) * (cos_t + cos_ht * std::sqrt(2.0 * (9.0 + cos_t)));
        const double fscale = 1.0 / (4.0 * lambda - 1.0);
        for (uint32_t i = 0; i < n; i++) {
          const double ci = std::cos(double(i) * theta);
          const double ci1 = std::cos(double(i + 1) * theta);
          const float e1 = float(4.0 * ci);
          const float f1 = float(fscale * (ci + ci1));
          AddScaled(&t1, edge_pt(i), e1);
          AddScaled(&t1, face_pt(i), f1);
          // tan2 = tan1 rotated one ring position.
          const uint32_t j = (i + 1) % n;
          AddScaled(&t2, edge_pt(j), e1);
          AddScaled(&t2, face_pt(j), f1);
        }
      }
    } else {
      // Loop smooth tangents: cos/sin ring weights.
      const double alpha = 2.0 * kPi / double(n);
      for (uint32_t i = 0; i < n; i++) {
        AddScaled(&t1, edge_pt(i), float(std::cos(alpha * double(i))));
        AddScaled(&t2, edge_pt(i), float(std::sin(alpha * double(i))));
      }
    }

    Vec3 nrm = Cross(t1, t2);
    const float len =
        std::sqrt(nrm.x * nrm.x + nrm.y * nrm.y + nrm.z * nrm.z);
    if (len > 0.0f) {
      nrm.x /= len;
      nrm.y /= len;
      nrm.z /= len;
    }
    (*out_normals)[size_t(v) * 3] = nrm.x;
    (*out_normals)[size_t(v) * 3 + 1] = nrm.y;
    (*out_normals)[size_t(v) * 3 + 2] = nrm.z;
  });

  return Result::Success;
}

}  // namespace tsd
}  // namespace tinyusdz
