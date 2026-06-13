// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: streaming (memory-bounded) refinement driver.
//
// Runs levels 0..N-1 with the same machinery as the bulk Refine driver, then
// emits the final level per-parent-face in bounded batches to a sink. The
// (largest) level-N output is never materialized: peak working memory is the
// level-(N-1) state plus one batch. Final-level vertex values are recomputed
// on demand from the resident level-(N-1) topology via the shared per-element
// kernels (tsd-kernel.hh), so streamed output is bit-identical to Refine's at
// matching canonical child-vertex ids.
//
// v1 scope: geometry + "vertex"/"varying" primvars. faceVarying and limit
// normals are not yet streamed here.

#include <cstring>
#include <unordered_map>
#include <vector>

#include "tsd-internal.hh"
#include "tsd-kernel.hh"

namespace tinyusdz {
namespace tsd {

namespace {

// Computes one final-level child value (geometry or a vertex primvar) for the
// canonical child id `id`: vertex-child id in [0,V), edge-child V+e, face-child
// V+E+f. `vals`/`stride` select the channel (geometry uses stride 3).
inline void ComputeChildValue(const Options &opts, const Topology &topo,
                              const uint32_t *fvi, const float *vals,
                              uint32_t stride, const SharpnessCtx &sharp,
                              bool is_vertex_pv, uint32_t id, uint32_t V,
                              uint32_t E, float *out) {
  const bool catmark = (opts.scheme == Scheme::CatmullClark);
  const bool loop = (opts.scheme == Scheme::Loop);
  // "varying" primvars use linear (bilinear) masks regardless of scheme.
  const bool linear = !is_vertex_pv;
  float scratch[4];
  auto face_child = [&](uint32_t g) -> const float * {
    ComputeFaceChild(topo, fvi, vals, stride, g, scratch);
    return scratch;
  };

  if (id < V) {  // vertex child
    if (linear || (!catmark && !loop)) {
      ComputeBilinearVertexChild(vals, stride, id, out);
    } else if (catmark) {
      ComputeCatmarkVertexChild(topo, vals, stride, sharp, opts, id, face_child,
                                out);
    } else {
      ComputeLoopVertexChild(topo, vals, stride, sharp, opts, id, out);
    }
    return;
  }
  if (id < V + E) {  // edge child
    const uint32_t e = id - V;
    if (linear || (!catmark && !loop)) {
      ComputeBilinearEdgeChild(topo, vals, stride, e, out);
    } else if (catmark) {
      ComputeCatmarkEdgeChild(topo, vals, stride, sharp, opts, e, face_child,
                              out);
    } else {
      ComputeLoopEdgeChild(topo, fvi, vals, stride, sharp, e, out);
    }
    return;
  }
  // face child (quad split only; Loop has no face children)
  ComputeFaceChild(topo, fvi, vals, stride, id - V - E, out);
}

// Accumulates one batch of streamed output, deduplicating shared vertices by
// canonical child id, and flushes to the sink.
struct Emitter {
  // --- config / resident inputs ---
  const Options *opts = nullptr;
  const StreamOptions *sopts = nullptr;
  StreamSink sink = nullptr;
  void *user = nullptr;
  const Topology *topo = nullptr;
  const uint32_t *fvi = nullptr;
  const float *geom = nullptr;
  uint32_t num_pv = 0;
  const std::vector<std::vector<float>> *pvs = nullptr;  // resident primvars
  const uint32_t *pv_stride = nullptr;
  SharpnessCtx sharp;
  uint32_t V = 0, E = 0;
  bool want_normals = false;
  // Resident level-(N-1) vertex limit normals (catmark/loop), size V*3 or null.
  const float *parent_normals = nullptr;

  // --- batch buffers (reused) ---
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<std::vector<float>> pv_buf;
  std::vector<uint32_t> vsource;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> face_source;
  std::unordered_map<uint32_t, uint32_t> dedup;
  uint32_t faces_in_batch = 0;
  uint32_t batch_index = 0;
  bool aborted = false;
  bool passthrough = false;  // level 0: canonical id == resident vertex id

  std::vector<uint8_t> pv_is_vertex;  // per primvar: smooth(true)/varying(false)

  void Init() {
    pv_buf.resize(num_pv);
  }

  // Limit normal for a final-level canonical id: exact at vertex-children
  // (the limit normal at the parent vertex is the same surface point), a
  // normalized blend of parent-vertex normals at edge/face-children. Keyed by
  // canonical id, so a batch-duplicated vertex always gets the same normal.
  void ChildNormal(uint32_t id, float *out) const {
    auto put = [&](float x, float y, float z) {
      const float len = std::sqrt(x * x + y * y + z * z);
      const float s = (len > 0.0f) ? (1.0f / len) : 0.0f;
      out[0] = x * s;
      out[1] = y * s;
      out[2] = z * s;
    };
    if (id < V) {
      out[0] = parent_normals[size_t(id) * 3];
      out[1] = parent_normals[size_t(id) * 3 + 1];
      out[2] = parent_normals[size_t(id) * 3 + 2];
      return;
    }
    if (id < V + E) {
      const uint32_t e = id - V;
      const uint32_t a = topo->edge_verts[2 * e], b = topo->edge_verts[2 * e + 1];
      put(parent_normals[size_t(a) * 3] + parent_normals[size_t(b) * 3],
          parent_normals[size_t(a) * 3 + 1] + parent_normals[size_t(b) * 3 + 1],
          parent_normals[size_t(a) * 3 + 2] + parent_normals[size_t(b) * 3 + 2]);
      return;
    }
    const uint32_t f = id - V - E;
    const uint32_t begin = topo->face_offsets[f];
    const uint32_t n = topo->face_offsets[f + 1] - begin;
    float sx = 0.0f, sy = 0.0f, sz = 0.0f;
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t vid = fvi[begin + k];
      sx += parent_normals[size_t(vid) * 3];
      sy += parent_normals[size_t(vid) * 3 + 1];
      sz += parent_normals[size_t(vid) * 3 + 2];
    }
    put(sx, sy, sz);
  }

  uint32_t Local(uint32_t canonical_id) {
    if (sopts->dedup_within_batch) {
      auto it = dedup.find(canonical_id);
      if (it != dedup.end()) {
        return it->second;
      }
    }
    const uint32_t local = uint32_t(positions.size() / 3);
    float p[4];
    if (passthrough) {
      memcpy(p, &geom[size_t(canonical_id) * 3], sizeof(float) * 3);
    } else {
      ComputeChildValue(*opts, *topo, fvi, geom, 3, sharp, /*is_vertex_pv=*/true,
                        canonical_id, V, E, p);
    }
    positions.push_back(p[0]);
    positions.push_back(p[1]);
    positions.push_back(p[2]);
    if (want_normals && parent_normals) {
      float nrm[3];
      ChildNormal(canonical_id, nrm);
      normals.push_back(nrm[0]);
      normals.push_back(nrm[1]);
      normals.push_back(nrm[2]);
    }
    for (uint32_t c = 0; c < num_pv; c++) {
      const uint32_t stride = pv_stride[c];
      float v[4];
      if (passthrough) {
        memcpy(v, &(*pvs)[c][size_t(canonical_id) * stride],
               sizeof(float) * stride);
      } else {
        ComputeChildValue(*opts, *topo, fvi, (*pvs)[c].data(), stride, sharp,
                          pv_is_vertex[c], canonical_id, V, E, v);
      }
      std::vector<float> &buf = pv_buf[c];
      for (uint32_t k = 0; k < stride; k++) {
        buf.push_back(v[k]);
      }
    }
    vsource.push_back(canonical_id);
    if (sopts->dedup_within_batch) {
      dedup.emplace(canonical_id, local);
    }
    return local;
  }

  void Tri(uint32_t a, uint32_t b, uint32_t c) {
    indices.push_back(a);
    indices.push_back(b);
    indices.push_back(c);
  }

  // Emit one quad-split child face [a,b,c,d] (canonical ids), base face src.
  void EmitQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t src) {
    const uint32_t la = Local(a), lb = Local(b), lc = Local(c), ld = Local(d);
    if (sopts->emit_triangles) {
      Tri(la, lb, lc);
      Tri(la, lc, ld);
      face_source.push_back(src);
      face_source.push_back(src);
    } else {
      indices.push_back(la);
      indices.push_back(lb);
      indices.push_back(lc);
      indices.push_back(ld);
      face_source.push_back(src);
    }
  }

  // Emit one tri-split child face [a,b,c].
  void EmitTri(uint32_t a, uint32_t b, uint32_t c, uint32_t src) {
    Tri(Local(a), Local(b), Local(c));
    face_source.push_back(src);
  }

  bool MaybeFlush() {
    faces_in_batch++;
    if (faces_in_batch >= sopts->batch_faces) {
      return Flush();
    }
    return true;
  }

  bool Flush() {
    if (face_source.empty()) {
      return true;
    }
    StreamBatch batch;
    batch.batch_index = batch_index;
    batch.num_vertices = uint32_t(positions.size() / 3);
    batch.positions = positions.data();
    batch.normals = (want_normals && parent_normals) ? normals.data() : nullptr;
    batch.vertex_source = vsource.data();
    std::vector<StreamPrimvar> pv_views(num_pv);
    for (uint32_t c = 0; c < num_pv; c++) {
      pv_views[c].stride = pv_stride[c];
      pv_views[c].values = pv_buf[c].data();
    }
    batch.num_vertex_primvars = num_pv;
    batch.vertex_primvars = num_pv ? pv_views.data() : nullptr;
    batch.num_faces = uint32_t(face_source.size());
    batch.num_indices = uint32_t(indices.size());
    batch.face_vertex_counts = nullptr;  // uniform arity
    batch.indices = indices.data();
    batch.face_source = face_source.data();

    const bool keep_going = sink(user, &batch);
    batch_index++;
    positions.clear();
    normals.clear();
    for (auto &b : pv_buf) {
      b.clear();
    }
    vsource.clear();
    indices.clear();
    face_source.clear();
    dedup.clear();
    faces_in_batch = 0;
    if (!keep_going) {
      aborted = true;
    }
    return keep_going;
  }
};

// Streams the final (child) level from the resident level-(N-1) state.
Result StreamFinalLevel(Emitter &em, const std::vector<uint8_t> &hole,
                        const std::vector<uint32_t> &face_source_in,
                        bool remove_holes) {
  const Topology &topo = *em.topo;
  const uint32_t V = em.V;
  const uint32_t E = em.E;
  const bool loop = (em.opts->scheme == Scheme::Loop);

  for (uint32_t f = 0; f < topo.num_faces; f++) {
    if (remove_holes && hole[f]) {
      continue;
    }
    const uint32_t src = face_source_in[f];
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;

    if (loop) {
      const uint32_t v0 = em.fvi[begin], v1 = em.fvi[begin + 1],
                     v2 = em.fvi[begin + 2];
      const uint32_t m01 = V + topo.face_edges[begin];
      const uint32_t m12 = V + topo.face_edges[begin + 1];
      const uint32_t m20 = V + topo.face_edges[begin + 2];
      em.EmitTri(v0, m01, m20, src);
      em.EmitTri(v1, m12, m01, src);
      em.EmitTri(v2, m20, m12, src);
      em.EmitTri(m01, m12, m20, src);
    } else {
      const uint32_t fchild = V + E + f;
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t prev = (k == 0) ? (n - 1) : (k - 1);
        em.EmitQuad(em.fvi[begin + k], V + topo.face_edges[begin + k], fchild,
                    V + topo.face_edges[begin + prev], src);
      }
    }
    if (!em.MaybeFlush()) {
      return Result::Success;  // sink aborted
    }
  }
  em.Flush();
  return Result::Success;
}

// Streams the base mesh directly (level 0 == passthrough).
Result StreamLevel0(Emitter &em, const MeshView &mesh, bool remove_holes) {
  std::vector<uint8_t> hole(mesh.num_faces, 0);
  for (uint32_t i = 0; i < mesh.num_holes; i++) {
    hole[uint32_t(mesh.hole_indices[i])] = 1;
  }
  uint32_t corner = 0;
  for (uint32_t f = 0; f < mesh.num_faces; f++) {
    const uint32_t n = mesh.face_vertex_counts[f];
    if (remove_holes && hole[f]) {
      corner += n;
      continue;
    }
    // Fan-triangulate (or native) using base vertex ids as canonical ids.
    if (em.sopts->emit_triangles) {
      const uint32_t a = mesh.face_vertex_indices[corner];
      for (uint32_t k = 1; k + 1 < n; k++) {
        em.EmitTri(a, mesh.face_vertex_indices[corner + k],
                   mesh.face_vertex_indices[corner + k + 1], f);
      }
    } else if (n == 4) {
      em.EmitQuad(mesh.face_vertex_indices[corner],
                  mesh.face_vertex_indices[corner + 1],
                  mesh.face_vertex_indices[corner + 2],
                  mesh.face_vertex_indices[corner + 3], f);
    } else {
      const uint32_t a = mesh.face_vertex_indices[corner];
      for (uint32_t k = 1; k + 1 < n; k++) {
        em.EmitTri(a, mesh.face_vertex_indices[corner + k],
                   mesh.face_vertex_indices[corner + k + 1], f);
      }
    }
    corner += n;
    if (!em.MaybeFlush()) {
      return Result::Success;
    }
  }
  em.Flush();
  return Result::Success;
}

}  // namespace

Result RefineStream(const MeshView &mesh,
                    const VertexPrimvarView *vertex_primvars,
                    uint32_t num_vertex_primvars, const Options &options,
                    const StreamOptions &stream_options, StreamSink sink,
                    void *sink_user, std::string *err) {
  if (!sink) {
    return Fail(Result::InvalidArgument, err, "stream sink is null.");
  }
  if (stream_options.batch_faces == 0) {
    return Fail(Result::InvalidArgument, err,
                "stream_options.batch_faces must be non-zero.");
  }
  Result r = ValidateInput(mesh, nullptr, 0, vertex_primvars,
                           num_vertex_primvars, options, err);
  if (r != Result::Success) {
    return r;
  }
  if (options.scheme == Scheme::None) {
    return Fail(Result::UnsupportedScheme, err,
                "subdivisionScheme 'none' is not subdividable.");
  }
  const bool catmark = (options.scheme == Scheme::CatmullClark);
  const bool loop = (options.scheme == Scheme::Loop);
  const bool smooth_scheme = catmark || loop;
  if (loop) {
    for (uint32_t f = 0; f < mesh.num_faces; f++) {
      if (mesh.face_vertex_counts[f] != 3) {
        return Fail(Result::InvalidTopology, err,
                    "Loop scheme requires an all-triangle mesh.");
      }
    }
  }

  std::vector<uint32_t> pv_stride(num_vertex_primvars);
  std::vector<uint8_t> pv_is_vertex(num_vertex_primvars);
  for (uint32_t p = 0; p < num_vertex_primvars; p++) {
    pv_stride[p] = vertex_primvars[p].stride;
    pv_is_vertex[p] = vertex_primvars[p].varying ? 0 : 1;
  }

  Emitter em;
  em.opts = &options;
  em.sopts = &stream_options;
  em.sink = sink;
  em.user = sink_user;
  em.num_pv = num_vertex_primvars;
  em.pv_stride = pv_stride.data();
  em.pv_is_vertex = std::vector<uint8_t>(pv_is_vertex.begin(),
                                         pv_is_vertex.end());
  em.Init();

  // --- Level 0: passthrough emit ---------------------------------------------
  if (options.level == 0) {
    // Resident "values" are the base mesh; geometry + primvars copied as-is.
    std::vector<float> geom0(mesh.points,
                             mesh.points + size_t(mesh.num_points) * 3);
    std::vector<std::vector<float>> pvs0(num_vertex_primvars);
    for (uint32_t p = 0; p < num_vertex_primvars; p++) {
      pvs0[p].assign(
          vertex_primvars[p].values,
          vertex_primvars[p].values + size_t(mesh.num_points) * pv_stride[p]);
    }
    Topology topo0;  // unused for level 0 (identity values)
    em.passthrough = true;
    em.topo = &topo0;
    em.fvi = mesh.face_vertex_indices;
    em.geom = geom0.data();
    em.pvs = &pvs0;
    em.V = mesh.num_points;
    em.E = 0;
    return StreamLevel0(em, mesh, options.remove_holes);
  }

  // --- Resident per-level state (geometry + vertex primvars) -----------------
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
  std::vector<float> geom(mesh.points,
                          mesh.points + size_t(mesh.num_points) * 3);
  std::vector<std::vector<float>> pvs(num_vertex_primvars);
  for (uint32_t p = 0; p < num_vertex_primvars; p++) {
    pvs[p].assign(
        vertex_primvars[p].values,
        vertex_primvars[p].values + size_t(mesh.num_points) * pv_stride[p]);
  }

  std::vector<uint8_t> hole(mesh.num_faces, 0);
  for (uint32_t i = 0; i < mesh.num_holes; i++) {
    hole[uint32_t(mesh.hole_indices[i])] = 1;
  }
  std::vector<uint32_t> face_source(mesh.num_faces);
  for (uint32_t f = 0; f < mesh.num_faces; f++) {
    face_source[f] = f;
  }

  std::vector<float> edge_sharp, vert_sharp;
  Topology prev_topo;
  std::vector<float> prev_edge_sharp, prev_vert_sharp;
  Topology topo;

  for (int32_t lvl = 0; lvl < options.level; lvl++) {
    r = BuildTopology(fvc.data(), uint32_t(fvc.size()), fvi.data(),
                      uint32_t(fvi.size()), num_points, &topo, err);
    if (r != Result::Success) {
      return r;
    }

    if (lvl == 0) {
      BakeLevel0Sharpness(mesh, options, topo, creases, &edge_sharp,
                          &vert_sharp, &hole, nullptr);
    } else if (smooth_scheme) {
      std::vector<float> new_edge_sharp, new_vert_sharp;
      DeriveChildSharpness(prev_topo, prev_edge_sharp, prev_vert_sharp, topo,
                           options.creasing, &new_edge_sharp, &new_vert_sharp);
      edge_sharp = std::move(new_edge_sharp);
      vert_sharp = std::move(new_vert_sharp);
    }

    const uint64_t child_points64 =
        loop ? (uint64_t(topo.num_points) + topo.num_edges)
             : (uint64_t(topo.num_points) + topo.num_edges + topo.num_faces);
    const uint64_t child_faces64 =
        loop ? (uint64_t(topo.num_faces) * 4) : uint64_t(fvi.size());
    const uint64_t child_corners64 = child_faces64 * (loop ? 3 : 4);
    if (child_points64 > options.max_vertices ||
        child_faces64 > options.max_faces ||
        child_corners64 > options.max_face_vertex_indices ||
        child_points64 > 0xFFFFFFFFull || child_corners64 > 0xFFFFFFFFull) {
      return Fail(Result::LimitExceeded, err,
                  "refinement exceeds max_vertices/max_faces/"
                  "max_face_vertex_indices caps.");
    }

    SharpnessCtx sharp;
    sharp.edge_sharpness = edge_sharp.empty() ? nullptr : edge_sharp.data();
    sharp.vert_sharpness = vert_sharp.empty() ? nullptr : vert_sharp.data();

    // --- Final level: stream instead of materializing ------------------------
    if (lvl + 1 == options.level) {
      em.topo = &topo;
      em.fvi = fvi.data();
      em.geom = geom.data();
      em.pvs = &pvs;
      em.sharp = sharp;
      em.V = topo.num_points;
      em.E = topo.num_edges;
      // Resident-level vertex limit normals: exact at vertex-children, blended
      // at edge/face-children (smooth schemes only). Bounded by the working
      // set; not the full level-N output.
      std::vector<float> parent_normals;
      if (stream_options.want_normals && smooth_scheme) {
        ComputeVertexLimitNormals(topo, fvi.data(), edge_sharp, vert_sharp,
                                  geom.data(), options, &parent_normals);
        em.want_normals = true;
        em.parent_normals = parent_normals.data();
      }
      return StreamFinalLevel(em, hole, face_source, options.remove_holes);
    }

    // --- Otherwise advance to the next level (geometry + primvars) -----------
    ChildTopo child;
    r = loop ? BuildChildTopologyTri(topo, fvi.data(), &child, err)
             : BuildChildTopologyQuad(topo, fvi.data(), &child, err);
    if (r != Result::Success) {
      return r;
    }

    std::vector<float> child_geom(size_t(child.num_points) * 3);
    if (catmark) {
      CatmarkRefineValues(topo, fvi.data(), geom.data(), 3, sharp, options,
                          child_geom.data());
    } else if (loop) {
      LoopRefineValues(topo, fvi.data(), geom.data(), 3, sharp, options,
                       child_geom.data());
    } else {
      BilinearRefineValues(topo, fvi.data(), geom.data(), 3, options,
                           child_geom.data());
    }
    geom = std::move(child_geom);

    for (uint32_t p = 0; p < num_vertex_primvars; p++) {
      const uint32_t stride = pv_stride[p];
      std::vector<float> child_pv(size_t(child.num_points) * stride);
      if (catmark && pv_is_vertex[p]) {
        CatmarkRefineValues(topo, fvi.data(), pvs[p].data(), stride, sharp,
                            options, child_pv.data());
      } else if (loop && pv_is_vertex[p]) {
        LoopRefineValues(topo, fvi.data(), pvs[p].data(), stride, sharp,
                         options, child_pv.data());
      } else if (loop) {
        // Linear ("varying") under the tri split: copy + edge midpoints.
        for (uint32_t v = 0; v < topo.num_points; v++) {
          memcpy(&child_pv[size_t(v) * stride], &pvs[p][size_t(v) * stride],
                 sizeof(float) * stride);
        }
        for (uint32_t e = 0; e < topo.num_edges; e++) {
          const float *a = &pvs[p][size_t(topo.edge_verts[2 * e]) * stride];
          const float *b = &pvs[p][size_t(topo.edge_verts[2 * e + 1]) * stride];
          float *d = &child_pv[(size_t(topo.num_points) + e) * stride];
          for (uint32_t cc = 0; cc < stride; cc++) {
            d[cc] = 0.5f * (a[cc] + b[cc]);
          }
        }
      } else {
        BilinearRefineValues(topo, fvi.data(), pvs[p].data(), stride, options,
                             child_pv.data());
      }
      pvs[p] = std::move(child_pv);
    }

    // Hole + provenance propagation.
    {
      std::vector<uint8_t> child_hole(child.face_parent.size(), 0);
      std::vector<uint32_t> child_source(child.face_parent.size());
      for (size_t cf = 0; cf < child.face_parent.size(); cf++) {
        const uint32_t parent = child.face_parent[cf];
        child_hole[cf] = hole[parent];
        child_source[cf] = face_source[parent];
      }
      hole = std::move(child_hole);
      face_source = std::move(child_source);
    }

    num_points = child.num_points;
    fvc = std::move(child.fvc);
    fvi = std::move(child.fvi);
    // The advance only runs when a next iteration exists; that iteration (the
    // next level, possibly the final streamed one) derives its sharpness from
    // this level, so always retain prev_* for smooth schemes.
    if (smooth_scheme) {
      prev_topo = std::move(topo);
      prev_edge_sharp = std::move(edge_sharp);
      prev_vert_sharp = std::move(vert_sharp);
      topo = Topology();
      edge_sharp.clear();
      vert_sharp.clear();
    }
  }

  return Result::Success;  // unreachable (loop returns at final level)
}

}  // namespace tsd
}  // namespace tinyusdz
