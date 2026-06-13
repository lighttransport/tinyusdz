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
// Geometry, "vertex"/"varying" primvars and closed-form limit normals stream.
// faceVarying streams in the per-batch path (linear modes); the smooth
// seam-split modes fall back to bulk.
//
// Block mode (StreamOptions::block_faces > 0) additionally bounds the WORKING
// set, not just the output: base faces are partitioned into blocks, each block
// is refined together with a halo of neighbouring base faces (so owned faces
// have full stencil support), and only the block's owned faces are emitted.
// Peak is then one block-plus-halo refinement, independent of total mesh size.

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
// `centroids` (when non-null) holds this channel's precomputed per-face
// centroids (`stride` floats each); the Catmull-Clark vertex/edge masks consume
// each incident face's centroid, so they index it instead of recomputing.
inline void ComputeChildValue(const Options &opts, const Topology &topo,
                              const uint32_t *fvi, const float *vals,
                              uint32_t stride, const SharpnessCtx &sharp,
                              bool is_vertex_pv, uint32_t id, uint32_t V,
                              uint32_t E, const float *centroids, float *out) {
  const bool catmark = (opts.scheme == Scheme::CatmullClark);
  const bool loop = (opts.scheme == Scheme::Loop);
  // "varying" primvars use linear (bilinear) masks regardless of scheme.
  const bool linear = !is_vertex_pv;
  float scratch[4];
  auto face_child = [&](uint32_t g) -> const float * {
    if (centroids) {
      return &centroids[size_t(g) * stride];
    }
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
  if (centroids) {
    const float *src = &centroids[size_t(id - V - E) * stride];
    for (uint32_t c = 0; c < stride; c++) {
      out[c] = src[c];
    }
    return;
  }
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
  // Linear faceVarying: per channel stride; emitted per-corner (parallel to
  // indices), not deduplicated. `fvars` holds the resident level-(N-1) (or base,
  // for level 0) per-corner values, one tuple per face-corner.
  uint32_t num_fvar = 0;
  const uint32_t *fvar_stride = nullptr;
  const std::vector<std::vector<float>> *fvars = nullptr;

  // --- batch buffers (reused) ---
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<std::vector<float>> pv_buf;
  std::vector<std::vector<float>> fvar_buf;  // per channel, per emitted corner
  std::vector<uint32_t> vsource;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> face_source;
  std::vector<uint32_t> face_arity;  // per emitted face (3 or 4)
  std::unordered_map<uint32_t, uint32_t> dedup;
  uint32_t faces_in_batch = 0;
  uint32_t batch_index = 0;
  bool aborted = false;
  bool passthrough = false;  // level 0: canonical id == resident vertex id

  std::vector<uint8_t> pv_is_vertex;  // per primvar: smooth(true)/varying(false)

  // Per-face centroids for each channel (geometry + vertex primvars), precomputed
  // once for the resident final level. Empty unless use_centroids is set.
  bool use_centroids = false;
  std::vector<float> geom_centroids;            // num_faces * 3
  std::vector<std::vector<float>> pv_centroids;  // per channel: num_faces * stride

  void Init() {
    pv_buf.resize(num_pv);
    fvar_buf.resize(num_fvar);
  }

  // Precompute per-face centroids for every channel. The Catmull-Clark
  // vertex/edge masks reference each incident face's centroid, and centroids do
  // not change between batches, so this replaces O(valence) recomputes per
  // vertex-child with a single indexed load. Memory is num_faces * stride per
  // channel -- bounded by the resident working set (<< the unmaterialized
  // level-N output), so the streaming memory bound is preserved.
  void PrecomputeCentroids() {
    if (!use_centroids) {
      return;
    }
    const uint32_t nf = topo->num_faces;
    geom_centroids.resize(size_t(nf) * 3);
    for (uint32_t f = 0; f < nf; f++) {
      ComputeFaceChild(*topo, fvi, geom, 3, f, &geom_centroids[size_t(f) * 3]);
    }
    pv_centroids.resize(num_pv);
    for (uint32_t c = 0; c < num_pv; c++) {
      const uint32_t st = pv_stride[c];
      pv_centroids[c].resize(size_t(nf) * st);
      for (uint32_t f = 0; f < nf; f++) {
        ComputeFaceChild(*topo, fvi, (*pvs)[c].data(), st, f,
                         &pv_centroids[c][size_t(f) * st]);
      }
    }
  }

  // Push the fvar tuples of one child-face corner `k` (per channel). `fc[c]`
  // points at that child face's `corner_count * stride[c]` fvar values.
  void PushFvarCorner(uint32_t k, const std::vector<const float *> &fc) {
    for (uint32_t c = 0; c < num_fvar; c++) {
      const uint32_t st = fvar_stride[c];
      for (uint32_t s = 0; s < st; s++) {
        fvar_buf[c].push_back(fc[c][size_t(k) * st + s]);
      }
    }
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
                        canonical_id, V, E,
                        use_centroids ? geom_centroids.data() : nullptr, p);
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
                          pv_is_vertex[c], canonical_id, V, E,
                          use_centroids ? pv_centroids[c].data() : nullptr, v);
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

  // Records one emitted face: provenance + arity (so non-uniform batches can
  // report `face_vertex_counts`).
  void PushFace(uint32_t src, uint32_t arity) {
    face_source.push_back(src);
    face_arity.push_back(arity);
  }

  // Emit one quad-split child face [a,b,c,d] (canonical ids), base face src.
  void EmitQuad(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t src) {
    const uint32_t la = Local(a), lb = Local(b), lc = Local(c), ld = Local(d);
    if (sopts->emit_triangles) {
      Tri(la, lb, lc);
      Tri(la, lc, ld);
      PushFace(src, 3);
      PushFace(src, 3);
    } else {
      indices.push_back(la);
      indices.push_back(lb);
      indices.push_back(lc);
      indices.push_back(ld);
      PushFace(src, 4);
    }
  }

  // Emit one tri-split child face [a,b,c].
  void EmitTri(uint32_t a, uint32_t b, uint32_t c, uint32_t src) {
    Tri(Local(a), Local(b), Local(c));
    PushFace(src, 3);
  }

  // Emit one native n-gon (canonical ids); used for level-0 passthrough.
  void EmitPoly(const uint32_t *canon, uint32_t n, uint32_t src) {
    for (uint32_t k = 0; k < n; k++) {
      indices.push_back(Local(canon[k]));
    }
    PushFace(src, n);
  }

  // fvar-aware variants: `fc[c]` holds this child face's per-corner fvar
  // (corner order matches a/b/c/d), pushed parallel to the emitted indices.
  void EmitQuadF(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t src,
                 const std::vector<const float *> &fc) {
    const uint32_t la = Local(a), lb = Local(b), lc = Local(c), ld = Local(d);
    if (sopts->emit_triangles) {
      Tri(la, lb, lc);
      PushFace(src, 3);
      PushFvarCorner(0, fc);
      PushFvarCorner(1, fc);
      PushFvarCorner(2, fc);
      Tri(la, lc, ld);
      PushFace(src, 3);
      PushFvarCorner(0, fc);
      PushFvarCorner(2, fc);
      PushFvarCorner(3, fc);
    } else {
      indices.push_back(la);
      indices.push_back(lb);
      indices.push_back(lc);
      indices.push_back(ld);
      PushFace(src, 4);
      PushFvarCorner(0, fc);
      PushFvarCorner(1, fc);
      PushFvarCorner(2, fc);
      PushFvarCorner(3, fc);
    }
  }

  void EmitTriF(uint32_t a, uint32_t b, uint32_t c, uint32_t src,
                const std::vector<const float *> &fc) {
    Tri(Local(a), Local(b), Local(c));
    PushFace(src, 3);
    PushFvarCorner(0, fc);
    PushFvarCorner(1, fc);
    PushFvarCorner(2, fc);
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
    std::vector<StreamPrimvar> fvar_views(num_fvar);
    for (uint32_t c = 0; c < num_fvar; c++) {
      fvar_views[c].stride = fvar_stride[c];
      fvar_views[c].values = fvar_buf[c].data();
    }
    batch.num_fvar = num_fvar;
    batch.fvar = num_fvar ? fvar_views.data() : nullptr;
    batch.num_faces = uint32_t(face_source.size());
    batch.num_indices = uint32_t(indices.size());
    // Report per-face arity only when the batch is non-uniform (e.g. a level-0
    // mixed-degree passthrough); null => every face has num_indices/num_faces
    // corners (the common final-level case: all quads or all triangles).
    bool uniform = true;
    for (size_t i = 1; i < face_arity.size(); i++) {
      if (face_arity[i] != face_arity[0]) {
        uniform = false;
        break;
      }
    }
    batch.face_vertex_counts = uniform ? nullptr : face_arity.data();
    batch.indices = indices.data();
    batch.face_source = face_source.data();

    const bool keep_going = sink(user, &batch);
    batch_index++;
    positions.clear();
    normals.clear();
    for (auto &b : pv_buf) {
      b.clear();
    }
    for (auto &b : fvar_buf) {
      b.clear();
    }
    vsource.clear();
    indices.clear();
    face_source.clear();
    face_arity.clear();
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

  const uint32_t nf = em.num_fvar;
  // Per-channel scratch: this child face's per-corner fvar (max 4 corners).
  std::vector<std::vector<float>> fc_buf(nf);
  std::vector<const float *> fc(nf);
  // Per-channel face average of the parent face's corner fvar (quad split).
  std::vector<std::vector<float>> favg(nf);

  for (uint32_t f = 0; f < topo.num_faces; f++) {
    if (remove_holes && hole[f]) {
      continue;
    }
    const uint32_t src = face_source_in[f];
    const uint32_t begin = topo.face_offsets[f];
    const uint32_t n = topo.face_offsets[f + 1] - begin;

    // Parent-face corner fvar accessor: tuple of corner k, channel c.
    auto pcorner = [&](uint32_t c, uint32_t corner) -> const float * {
      return &(*em.fvars)[c][size_t(begin + corner) * em.fvar_stride[c]];
    };

    if (loop) {
      const uint32_t v0 = em.fvi[begin], v1 = em.fvi[begin + 1],
                     v2 = em.fvi[begin + 2];
      const uint32_t e01 = V + topo.face_edges[begin];
      const uint32_t e12 = V + topo.face_edges[begin + 1];
      const uint32_t e20 = V + topo.face_edges[begin + 2];
      if (nf == 0) {
        em.EmitTri(v0, e01, e20, src);
        em.EmitTri(v1, e12, e01, src);
        em.EmitTri(v2, e20, e12, src);
        em.EmitTri(e01, e12, e20, src);
      } else {
        // c0,c1,c2 and the 3 edge midpoints per channel.
        for (uint32_t c = 0; c < nf; c++) {
          const uint32_t st = em.fvar_stride[c];
          fc_buf[c].assign(size_t(3) * st, 0.0f);  // reused per child tri
        }
        // Helper to fill fc[c] for a tri with 3 source tuples (corner or mid).
        auto tri = [&](uint32_t ca, uint32_t cb, uint32_t cc, uint32_t ga,
                       uint32_t gb, uint32_t gc) {
          for (uint32_t c = 0; c < nf; c++) {
            const uint32_t st = em.fvar_stride[c];
            const float *p0 = pcorner(c, 0), *p1 = pcorner(c, 1),
                        *p2 = pcorner(c, 2);
            // mids: 3=m01,4=m12,5=m20 encoded; we just compute inline.
            auto val = [&](uint32_t which, uint32_t s) -> float {
              switch (which) {
                case 0: return p0[s];
                case 1: return p1[s];
                case 2: return p2[s];
                case 3: return 0.5f * (p0[s] + p1[s]);  // m01
                case 4: return 0.5f * (p1[s] + p2[s]);  // m12
                default: return 0.5f * (p2[s] + p0[s]); // m20
              }
            };
            for (uint32_t s = 0; s < st; s++) {
              fc_buf[c][0 * st + s] = val(ca, s);
              fc_buf[c][1 * st + s] = val(cb, s);
              fc_buf[c][2 * st + s] = val(cc, s);
            }
            fc[c] = fc_buf[c].data();
          }
          em.EmitTriF(ga, gb, gc, src, fc);
        };
        tri(0, 3, 5, v0, e01, e20);   // c0, m01, m20
        tri(1, 4, 3, v1, e12, e01);   // c1, m12, m01
        tri(2, 5, 4, v2, e20, e12);   // c2, m20, m12
        tri(3, 4, 5, e01, e12, e20);  // m01, m12, m20
      }
    } else {
      const uint32_t fchild = V + E + f;
      if (nf) {
        for (uint32_t c = 0; c < nf; c++) {
          const uint32_t st = em.fvar_stride[c];
          favg[c].assign(st, 0.0f);
          const float w = 1.0f / float(n);
          for (uint32_t k = 0; k < n; k++) {
            const float *p = pcorner(c, k);
            for (uint32_t s = 0; s < st; s++) {
              favg[c][s] += w * p[s];
            }
          }
          fc_buf[c].assign(size_t(4) * st, 0.0f);
        }
      }
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t prev = (k == 0) ? (n - 1) : (k - 1);
        const uint32_t next = (k + 1 == n) ? 0 : (k + 1);
        const uint32_t ga = em.fvi[begin + k];
        const uint32_t gb = V + topo.face_edges[begin + k];
        const uint32_t gd = V + topo.face_edges[begin + prev];
        if (nf == 0) {
          em.EmitQuad(ga, gb, fchild, gd, src);
        } else {
          for (uint32_t c = 0; c < nf; c++) {
            const uint32_t st = em.fvar_stride[c];
            const float *ck = pcorner(c, k), *cn = pcorner(c, next),
                        *cp = pcorner(c, prev);
            for (uint32_t s = 0; s < st; s++) {
              fc_buf[c][0 * st + s] = ck[s];
              fc_buf[c][1 * st + s] = 0.5f * (ck[s] + cn[s]);
              fc_buf[c][2 * st + s] = favg[c][s];
              fc_buf[c][3 * st + s] = 0.5f * (cp[s] + ck[s]);
            }
            fc[c] = fc_buf[c].data();
          }
          em.EmitQuadF(ga, gb, fchild, gd, src, fc);
        }
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
    // Base vertex ids are the canonical ids at level 0. emit_triangles fans
    // every face to triangles; otherwise faces pass through with native arity.
    if (em.sopts->emit_triangles) {
      const uint32_t a = mesh.face_vertex_indices[corner];
      for (uint32_t k = 1; k + 1 < n; k++) {
        em.EmitTri(a, mesh.face_vertex_indices[corner + k],
                   mesh.face_vertex_indices[corner + k + 1], f);
      }
    } else {
      em.EmitPoly(&mesh.face_vertex_indices[corner], n, f);
    }
    corner += n;
    if (!em.MaybeFlush()) {
      return Result::Success;
    }
  }
  em.Flush();
  return Result::Success;
}

// --- Block + halo (bounded working set) --------------------------------------

// Grows the owned face range [fstart, fend) by `rings` vertex-adjacency rings
// and returns the ordered set of base faces in the block + halo.
std::vector<uint32_t> GatherHalo(const Topology &topo, const uint32_t *fvi,
                                 uint32_t fstart, uint32_t fend,
                                 uint32_t rings) {
  std::vector<uint8_t> in(topo.num_faces, 0);
  std::vector<uint32_t> frontier;
  for (uint32_t f = fstart; f < fend; f++) {
    in[f] = 1;
    frontier.push_back(f);
  }
  for (uint32_t r = 0; r < rings && !frontier.empty(); r++) {
    std::vector<uint32_t> next;
    for (uint32_t f : frontier) {
      const uint32_t b = topo.face_offsets[f];
      const uint32_t n = topo.face_offsets[f + 1] - b;
      for (uint32_t k = 0; k < n; k++) {
        const uint32_t v = fvi[b + k];
        for (uint32_t i = topo.vert_face_offsets[v];
             i < topo.vert_face_offsets[v + 1]; i++) {
          const uint32_t g = topo.vert_faces[i];
          if (!in[g]) {
            in[g] = 1;
            next.push_back(g);
          }
        }
      }
    }
    frontier.swap(next);
  }
  std::vector<uint32_t> halo;
  for (uint32_t f = 0; f < topo.num_faces; f++) {
    if (in[f]) {
      halo.push_back(f);
    }
  }
  return halo;
}

// A self-contained submesh (the block + halo), with base tags sliced/remapped
// onto its local vertex/face ids, ready to feed to bulk Refine.
struct Submesh {
  std::vector<float> points;
  std::vector<uint32_t> fvc;
  std::vector<uint32_t> fvi;
  std::vector<uint32_t> sub_to_global;  // submesh face -> global base face
  std::vector<uint8_t> owned;           // submesh face is owned by the block
  std::vector<std::vector<float>> pvs;  // per vertex primvar channel
  std::vector<int32_t> crease_indices;
  std::vector<int32_t> crease_lengths;
  std::vector<float> crease_sharpnesses;
  std::vector<int32_t> corner_indices;
  std::vector<float> corner_sharpnesses;
  std::vector<int32_t> hole_indices;
};

void BuildSubmesh(const MeshView &mesh, const Topology &base_topo,
                  const CreaseEdges &creases,
                  const VertexPrimvarView *vertex_primvars, uint32_t num_pv,
                  const std::vector<uint32_t> &halo, uint32_t fstart,
                  uint32_t fend, Submesh *sub) {
  std::vector<uint32_t> vloc(mesh.num_points, kInvalidIndex);
  std::vector<uint32_t> floc(mesh.num_faces, kInvalidIndex);
  for (size_t i = 0; i < halo.size(); i++) {
    floc[halo[i]] = uint32_t(i);
  }
  sub->pvs.assign(num_pv, {});

  for (uint32_t g : halo) {
    const uint32_t b = base_topo.face_offsets[g];
    const uint32_t n = base_topo.face_offsets[g + 1] - b;
    sub->fvc.push_back(n);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t v = mesh.face_vertex_indices[b + k];
      if (vloc[v] == kInvalidIndex) {
        vloc[v] = uint32_t(sub->points.size() / 3);
        sub->points.push_back(mesh.points[size_t(v) * 3]);
        sub->points.push_back(mesh.points[size_t(v) * 3 + 1]);
        sub->points.push_back(mesh.points[size_t(v) * 3 + 2]);
        for (uint32_t c = 0; c < num_pv; c++) {
          const uint32_t st = vertex_primvars[c].stride;
          for (uint32_t s = 0; s < st; s++) {
            sub->pvs[c].push_back(vertex_primvars[c].values[size_t(v) * st + s]);
          }
        }
      }
      sub->fvi.push_back(vloc[v]);
    }
    sub->sub_to_global.push_back(g);
    sub->owned.push_back((g >= fstart && g < fend) ? 1 : 0);
  }

  // Creases: each canonical crease edge whose both endpoints are in the
  // submesh becomes a length-2 chain with its sharpness.
  for (size_t e = 0; e < creases.sharpnesses.size(); e++) {
    const uint32_t a = creases.edge_verts[2 * e];
    const uint32_t b = creases.edge_verts[2 * e + 1];
    if (vloc[a] != kInvalidIndex && vloc[b] != kInvalidIndex) {
      sub->crease_indices.push_back(int32_t(vloc[a]));
      sub->crease_indices.push_back(int32_t(vloc[b]));
      sub->crease_lengths.push_back(2);
      sub->crease_sharpnesses.push_back(creases.sharpnesses[e]);
    }
  }
  for (uint32_t i = 0; i < mesh.num_corners; i++) {
    const uint32_t v = uint32_t(mesh.corner_indices[i]);
    if (v < mesh.num_points && vloc[v] != kInvalidIndex) {
      sub->corner_indices.push_back(int32_t(vloc[v]));
      sub->corner_sharpnesses.push_back(mesh.corner_sharpnesses[i]);
    }
  }
  for (uint32_t i = 0; i < mesh.num_holes; i++) {
    const uint32_t h = uint32_t(mesh.hole_indices[i]);
    if (h < mesh.num_faces && floc[h] != kInvalidIndex) {
      sub->hole_indices.push_back(int32_t(floc[h]));
    }
  }
}

// Emits a block's owned refined faces (from the materialized submesh refine)
// to the sink: positions/primvars/normals are copied by refined-vertex id, and
// face_source is remapped to the global base face.
// Returns false if the sink aborted (the block loop then stops).
bool EmitBlockOwned(Emitter &em, const RefinedMesh &refined,
                    const std::vector<float> &normals,
                    const std::vector<uint32_t> &sub_to_global,
                    const std::vector<uint8_t> &owned, bool loop) {
  em.passthrough = true;
  em.geom = refined.points.data();
  em.pvs = &refined.vertex_primvars;
  em.V = uint32_t(refined.points.size() / 3);
  em.E = 0;
  em.want_normals = !normals.empty();
  em.parent_normals = normals.empty() ? nullptr : normals.data();

  const uint32_t nf = uint32_t(refined.face_vertex_counts.size());
  const uint32_t arity =
      nf ? uint32_t(refined.face_vertex_indices.size() / nf) : 0;
  for (uint32_t f = 0; f < nf; f++) {
    const uint32_t sub_base = refined.face_source[f];
    if (!owned[sub_base]) {
      continue;
    }
    const uint32_t gsrc = sub_to_global[sub_base];
    const uint32_t *c = &refined.face_vertex_indices[size_t(f) * arity];
    if (loop) {
      em.EmitTri(c[0], c[1], c[2], gsrc);
    } else {
      em.EmitQuad(c[0], c[1], c[2], c[3], gsrc);
    }
    if (!em.MaybeFlush()) {  // honor batch_faces + sink abort
      return false;
    }
  }
  // Flush per block: canonical ids are submesh-local, so they must not carry
  // across into the next block's batch.
  return em.Flush();
}

}  // namespace

Result RefineStream(const MeshView &mesh,
                    const FVarChannelView *fvar_channels,
                    uint32_t num_fvar_channels,
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
  Result r = ValidateInput(mesh, fvar_channels, num_fvar_channels,
                           vertex_primvars, num_vertex_primvars, options, err);
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

  // faceVarying: only the linear path (the "all" mode, or any mode under
  // bilinear) streams here; smooth seam-split channels fall back to bulk.
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    const bool linear =
        (!smooth_scheme ||
         fvar_channels[c].interpolation == FVarLinearInterpolation::All);
    if (!linear) {
      return Fail(Result::InvalidArgument, err,
                  "smooth faceVarying is not streamable; use Refine.");
    }
  }
  if (num_fvar_channels && stream_options.block_faces > 0) {
    return Fail(Result::InvalidArgument, err,
                "faceVarying streaming is not supported in block mode.");
  }
  if (num_fvar_channels && options.level == 0) {
    return Fail(Result::InvalidArgument, err,
                "faceVarying streaming requires level >= 1; use Refine.");
  }

  std::vector<uint32_t> pv_stride(num_vertex_primvars);
  std::vector<uint8_t> pv_is_vertex(num_vertex_primvars);
  for (uint32_t p = 0; p < num_vertex_primvars; p++) {
    pv_stride[p] = vertex_primvars[p].stride;
    pv_is_vertex[p] = vertex_primvars[p].varying ? 0 : 1;
  }
  std::vector<uint32_t> fvar_stride(num_fvar_channels);
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    fvar_stride[c] = fvar_channels[c].stride;
  }

  Emitter em;
  em.opts = &options;
  em.sopts = &stream_options;
  em.sink = sink;
  em.user = sink_user;
  em.num_pv = num_vertex_primvars;
  em.pv_stride = pv_stride.data();
  em.num_fvar = num_fvar_channels;
  em.fvar_stride = fvar_stride.data();
  em.pv_is_vertex = std::vector<uint8_t>(pv_is_vertex.begin(),
                                         pv_is_vertex.end());
  em.Init();

  // --- Block + halo mode: bound the working set, not just the output ---------
  if (stream_options.block_faces > 0 &&
      stream_options.block_faces < mesh.num_faces && options.level >= 1) {
    Topology base_topo;
    r = BuildTopology(mesh.face_vertex_counts, mesh.num_faces,
                      mesh.face_vertex_indices, mesh.num_face_vertex_indices,
                      mesh.num_points, &base_topo, err);
    if (r != Result::Success) {
      return r;
    }
    CreaseEdges creases;
    r = CanonicalizeCreases(mesh, &creases, err);
    if (r != Result::Success) {
      return r;
    }
    // Stencil support is LOCAL and level-independent: an owned face's refined
    // vertices -- and, for limit normals, their final-level 1-ring -- depend
    // only on the 1-ring of base faces around that face. Child vertices cluster
    // toward their parent as the level rises, so the base-face footprint never
    // grows past one ring. One vertex-adjacency ring (GatherHalo rings == 1)
    // already covers that 1-ring, so it is the proven-sufficient floor; we
    // default to one extra ring of margin. Verified bit-identical to whole-mesh
    // Refine (positions + limit normals) across tori / open / holed / semi-sharp
    // creased meshes at levels 1..4 with a single ring.
    //
    // (The previous `level + 1` policy was correct but inflated the per-block
    // working set super-linearly with level -- e.g. a 7-ring halo at level 6 --
    // defeating block mode's "bound the working set" purpose at high levels.)
    constexpr uint32_t kHaloFloor = 1;    // proven-sufficient minimum
    constexpr uint32_t kHaloDefault = 2;  // floor + one ring of margin
    uint32_t halo_rings =
        stream_options.halo_rings ? stream_options.halo_rings : kHaloDefault;
    if (halo_rings < kHaloFloor) {
      halo_rings = kHaloFloor;
    }
    for (uint32_t fstart = 0; fstart < mesh.num_faces;
         fstart += stream_options.block_faces) {
      const uint32_t fend =
          (fstart + stream_options.block_faces < mesh.num_faces)
              ? (fstart + stream_options.block_faces)
              : mesh.num_faces;
      const std::vector<uint32_t> halo =
          GatherHalo(base_topo, mesh.face_vertex_indices, fstart, fend,
                     halo_rings);
      Submesh sub;
      BuildSubmesh(mesh, base_topo, creases, vertex_primvars,
                   num_vertex_primvars, halo, fstart, fend, &sub);

      MeshView sv;
      sv.points = sub.points.data();
      sv.num_points = uint32_t(sub.points.size() / 3);
      sv.face_vertex_counts = sub.fvc.data();
      sv.num_faces = uint32_t(sub.fvc.size());
      sv.face_vertex_indices = sub.fvi.data();
      sv.num_face_vertex_indices = uint32_t(sub.fvi.size());
      if (!sub.corner_indices.empty()) {
        sv.corner_indices = sub.corner_indices.data();
        sv.num_corners = uint32_t(sub.corner_indices.size());
        sv.corner_sharpnesses = sub.corner_sharpnesses.data();
      }
      if (!sub.crease_lengths.empty()) {
        sv.crease_indices = sub.crease_indices.data();
        sv.num_crease_indices = uint32_t(sub.crease_indices.size());
        sv.crease_lengths = sub.crease_lengths.data();
        sv.num_crease_lengths = uint32_t(sub.crease_lengths.size());
        sv.crease_sharpnesses = sub.crease_sharpnesses.data();
        sv.num_crease_sharpnesses = uint32_t(sub.crease_sharpnesses.size());
      }
      if (!sub.hole_indices.empty()) {
        sv.hole_indices = sub.hole_indices.data();
        sv.num_holes = uint32_t(sub.hole_indices.size());
      }

      std::vector<VertexPrimvarView> spv(num_vertex_primvars);
      for (uint32_t c = 0; c < num_vertex_primvars; c++) {
        spv[c].values = sub.pvs[c].data();
        spv[c].stride = vertex_primvars[c].stride;
        spv[c].varying = vertex_primvars[c].varying;
      }

      RefinedMesh refined;
      r = Refine(sv, nullptr, 0,
                 num_vertex_primvars ? spv.data() : nullptr,
                 num_vertex_primvars, options, &refined, err);
      if (r != Result::Success) {
        return r;
      }
      std::vector<float> normals;
      if (stream_options.want_normals && smooth_scheme) {
        r = ComputeLimitNormals(sv, options, refined, &normals, err);
        if (r != Result::Success) {
          return r;
        }
      }
      if (!EmitBlockOwned(em, refined, normals, sub.sub_to_global, sub.owned,
                          loop)) {
        return Result::Success;  // sink aborted; partial output already emitted
      }
    }
    return Result::Success;
  }

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

  // Linear faceVarying expanded to per-corner tuples (refined linearly each
  // level alongside geometry).
  std::vector<std::vector<float>> fvars(num_fvar_channels);
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    const FVarChannelView &ch = fvar_channels[c];
    fvars[c].resize(size_t(mesh.num_face_vertex_indices) * ch.stride);
    for (uint32_t i = 0; i < mesh.num_face_vertex_indices; i++) {
      const uint32_t src = ch.indices ? ch.indices[i] : i;
      memcpy(&fvars[c][size_t(i) * ch.stride],
             &ch.values[size_t(src) * ch.stride], sizeof(float) * ch.stride);
    }
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
      em.fvars = &fvars;
      em.sharp = sharp;
      em.V = topo.num_points;
      em.E = topo.num_edges;
      // Catmull-Clark vertex/edge masks consume incident face centroids; these
      // are invariant across batches, so precompute them once per channel
      // (bounded by the resident working set) instead of O(valence) recomputes
      // per vertex-child. Bilinear/Loop don't re-consume centroids.
      em.use_centroids = catmark;
      em.PrecomputeCentroids();
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

    // Linear faceVarying: per-corner refinement (local per face).
    for (uint32_t c = 0; c < num_fvar_channels; c++) {
      const uint32_t stride = fvar_stride[c];
      std::vector<float> child_fv(size_t(child_corners64) * stride);
      if (loop) {
        LinearFVarRefineTri(topo, fvars[c].data(), stride, options,
                            child_fv.data());
      } else {
        LinearFVarRefineQuad(topo, fvars[c].data(), stride, options,
                             child_fv.data());
      }
      fvars[c] = std::move(child_fv);
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
