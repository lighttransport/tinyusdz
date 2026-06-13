// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// libFuzzer harness for tinysubdiv (src/tsd): decodes arbitrary bytes into
// a MeshView + Options and refines. Any crash/UB/sanitizer report is a bug;
// malformed input must be rejected through tsd::Result codes.

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

#include "tsd/tinysubdiv.hh"

namespace {

// Structural invariant: a violation here is a tsd bug (independent of the
// arbitrary, possibly non-finite, input values), so trap for the sanitizer.
inline void Require(bool cond) {
  if (!cond) {
    abort();
  }
}

// Sequential byte reader with rollover.
struct Reader {
  const uint8_t *data;
  size_t size;
  size_t pos = 0;

  uint8_t U8() {
    if (size == 0) {
      return 0;
    }
    return data[pos++ % size];
  }
  uint32_t U32() {
    return uint32_t(U8()) | (uint32_t(U8()) << 8) | (uint32_t(U8()) << 16) |
           (uint32_t(U8()) << 24);
  }
  float F32() {
    const uint32_t u = U32();
    float f;
    memcpy(&f, &u, sizeof(f));
    return f;
  }
};

// 3 float positions keyed by their exact bit patterns (NaN-safe identity).
inline std::array<uint32_t, 3> PosKey(const float *p) {
  std::array<uint32_t, 3> k;
  memcpy(k.data(), p, sizeof(float) * 3);
  return k;
}

// Streaming differential context: structural invariants on every batch, plus
// (when bulk succeeded on finite input) bit-exact subset agreement with bulk.
struct StreamCtx {
  uint32_t base_faces = 0;
  uint32_t num_fvar = 0;
  uint32_t fvar_stride = 0;
  uint32_t num_pv = 0;
  uint32_t pv_stride = 0;
  bool want_normals = false;
  // Non-null iff a differential is in effect (bulk succeeded + finite input).
  const std::set<std::array<uint32_t, 3>> *bulk_pos = nullptr;
  volatile double sink_acc = 0.0;  // forces ASAN to read advertised bytes
};

bool StreamFuzzSink(void *user, const tinyusdz::tsd::StreamBatch *b) {
  StreamCtx *c = static_cast<StreamCtx *>(user);
  Require(b->positions != nullptr);
  Require(b->vertex_source != nullptr);
  Require(b->face_source != nullptr);
  Require(b->indices != nullptr);

  // Per-corner walk: arities sum to num_indices; every index is in range; touch
  // vertex_source by index.
  uint32_t off = 0;
  for (uint32_t f = 0; f < b->num_faces; f++) {
    const uint32_t n = b->face_vertex_counts
                           ? b->face_vertex_counts[f]
                           : (b->num_faces ? b->num_indices / b->num_faces : 0);
    Require(off + n <= b->num_indices);
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t vi = b->indices[off + k];
      Require(vi < b->num_vertices);
      c->sink_acc += b->vertex_source[vi];
    }
    off += n;
    Require(b->face_source[f] < c->base_faces);
  }
  Require(off == b->num_indices);

  // Touch every advertised value byte so OOB views trip the sanitizer.
  double a = 0.0;
  for (uint32_t i = 0; i < b->num_vertices * 3u; i++) a += b->positions[i];
  if (c->want_normals && b->normals) {
    for (uint32_t i = 0; i < b->num_vertices * 3u; i++) a += b->normals[i];
  }
  Require(b->num_fvar == c->num_fvar);
  for (uint32_t ch = 0; ch < b->num_fvar; ch++) {
    Require(b->fvar[ch].stride == c->fvar_stride);
    Require(b->fvar[ch].values != nullptr);
    for (uint32_t i = 0; i < b->num_indices * b->fvar[ch].stride; i++) {
      a += b->fvar[ch].values[i];
    }
  }
  Require(b->num_vertex_primvars == c->num_pv);
  for (uint32_t ch = 0; ch < b->num_vertex_primvars; ch++) {
    Require(b->vertex_primvars[ch].stride == c->pv_stride);
    for (uint32_t i = 0; i < b->num_vertices * b->vertex_primvars[ch].stride;
         i++) {
      a += b->vertex_primvars[ch].values[i];
    }
  }
  c->sink_acc += a;

  // Differential: streamed geometry is bit-identical to a subset of bulk's
  // (RefineStream == Refine at matching canonical ids). Only meaningful with
  // finite input (NaN/Inf bits can diverge under block mode's reorder).
  if (c->bulk_pos) {
    for (uint32_t v = 0; v < b->num_vertices; v++) {
      Require(c->bulk_pos->count(PosKey(&b->positions[v * 3])) != 0);
    }
  }
  return true;
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (size < 8) {
    return 0;
  }
  Reader r{data, size};

  namespace tsd = tinyusdz::tsd;

  tsd::Options opts;
  opts.scheme = tsd::Scheme(r.U8() % 4);
  opts.boundary = tsd::BoundaryInterpolation(r.U8() % 3);
  opts.creasing = tsd::CreasingMethod(r.U8() % 2);
  opts.triangle_subdivision = tsd::TriangleSubdivision(r.U8() % 2);
  opts.level = int32_t(r.U8() % 5);
  // Keep output small so the fuzzer spends time on parsing, not refining.
  opts.max_vertices = 1u << 16;
  opts.max_faces = 1u << 16;
  opts.max_face_vertex_indices = 1u << 18;
  opts.remove_holes = (r.U8() & 1) != 0;

  const uint32_t num_points = 1 + (r.U32() % 64);
  const uint32_t num_faces = 1 + (r.U32() % 32);

  std::vector<float> points(size_t(num_points) * 3);
  for (float &p : points) {
    p = r.F32();
  }

  std::vector<uint32_t> fvc(num_faces);
  uint64_t corner_count = 0;
  for (uint32_t &c : fvc) {
    c = r.U32() % 8;  // intentionally allows invalid degrees (< 3)
    corner_count += c;
  }
  if (corner_count == 0 || corner_count > 4096) {
    return 0;
  }
  std::vector<uint32_t> fvi(static_cast<size_t>(corner_count));
  for (uint32_t &i : fvi) {
    i = r.U32() % (num_points + 2);  // sometimes out of range
  }

  tsd::MeshView view;
  view.points = points.data();
  view.num_points = num_points;
  view.face_vertex_counts = fvc.data();
  view.num_faces = num_faces;
  view.face_vertex_indices = fvi.data();
  view.num_face_vertex_indices = uint32_t(corner_count);

  // Optional subdiv tags (often invalid by construction).
  std::vector<int32_t> corner_indices;
  std::vector<float> corner_sharp;
  if (r.U8() & 1) {
    const uint32_t n = r.U32() % 8;
    for (uint32_t i = 0; i < n; i++) {
      corner_indices.push_back(int32_t(r.U32() % (num_points + 2)) - 1);
      corner_sharp.push_back(r.F32());
    }
    view.corner_indices = corner_indices.data();
    view.num_corners = uint32_t(corner_indices.size());
    view.corner_sharpnesses = corner_sharp.data();
  }

  std::vector<int32_t> crease_indices;
  std::vector<int32_t> crease_lengths;
  std::vector<float> crease_sharp;
  if (r.U8() & 1) {
    const uint32_t chains = 1 + (r.U32() % 4);
    for (uint32_t c = 0; c < chains; c++) {
      const int32_t len = int32_t(r.U32() % 6);
      crease_lengths.push_back(len);
      for (int32_t k = 0; k < len; k++) {
        crease_indices.push_back(int32_t(r.U32() % (num_points + 2)) - 1);
      }
    }
    const uint32_t ns = r.U32() % 16;
    for (uint32_t i = 0; i < ns; i++) {
      crease_sharp.push_back(r.F32());
    }
    view.crease_indices = crease_indices.data();
    view.num_crease_indices = uint32_t(crease_indices.size());
    view.crease_lengths = crease_lengths.data();
    view.num_crease_lengths = uint32_t(crease_lengths.size());
    view.crease_sharpnesses = crease_sharp.data();
    view.num_crease_sharpnesses = uint32_t(crease_sharp.size());
  }

  std::vector<int32_t> holes;
  if (r.U8() & 1) {
    const uint32_t n = r.U32() % 8;
    for (uint32_t i = 0; i < n; i++) {
      holes.push_back(int32_t(r.U32() % (num_faces + 2)) - 1);
    }
    view.hole_indices = holes.data();
    view.num_holes = uint32_t(holes.size());
  }

  // Optional fvar channel.
  std::vector<float> fvar_values;
  std::vector<uint32_t> fvar_indices;
  tsd::FVarChannelView fvar;
  uint32_t num_fvar = 0;
  if (r.U8() & 1) {
    fvar.stride = 1 + (r.U8() % 4);
    const uint32_t nvals = 1 + (r.U32() % 32);
    fvar_values.resize(size_t(nvals) * fvar.stride);
    for (float &v : fvar_values) {
      v = r.F32();
    }
    fvar.values = fvar_values.data();
    fvar.num_values = nvals;
    if (r.U8() & 1) {
      fvar_indices.resize(corner_count);
      for (uint32_t &i : fvar_indices) {
        i = r.U32() % (nvals + 2);  // sometimes out of range
      }
      fvar.indices = fvar_indices.data();
    }
    fvar.interpolation = tsd::FVarLinearInterpolation(r.U8() % 6);
    num_fvar = 1;
  }

  // Optional vertex primvar.
  std::vector<float> pv_values;
  tsd::VertexPrimvarView pv;
  uint32_t num_pv = 0;
  if (r.U8() & 1) {
    pv.stride = 1 + (r.U8() % 4);
    pv_values.resize(size_t(num_points) * pv.stride);
    for (float &v : pv_values) {
      v = r.F32();
    }
    pv.values = pv_values.data();
    pv.varying = (r.U8() & 1) != 0;
    num_pv = 1;
  }

  tsd::RefinedMesh out;
  std::string err;
  const tsd::Result rr = tsd::Refine(view, num_fvar ? &fvar : nullptr, num_fvar,
                                     num_pv ? &pv : nullptr, num_pv, opts, &out,
                                     &err);

  if (rr == tsd::Result::Success) {
    // Structural invariants (cannot check finiteness/AABB: inputs may be
    // NaN/Inf by construction).
    Require(out.points.size() % 3 == 0);
    const size_t npts = out.points.size() / 3;
    Require(out.face_source.size() == out.face_vertex_counts.size());
    size_t corner_sum = 0;
    for (uint32_t c : out.face_vertex_counts) {
      corner_sum += c;
    }
    Require(corner_sum == out.face_vertex_indices.size());
    for (uint32_t i : out.face_vertex_indices) {
      Require(i < npts);
    }
    for (uint32_t s : out.face_source) {
      Require(s < num_faces);
    }
    if (opts.level >= 1) {
      // Refinement makes every face the scheme's arity (level 0 passes the
      // base topology through unchanged).
      const uint32_t arity = (opts.scheme == tsd::Scheme::Loop) ? 3u : 4u;
      for (uint32_t c : out.face_vertex_counts) {
        Require(c == arity);
      }
    }
    if (num_fvar) {
      Require(out.fvar.size() == 1);
      Require(out.fvar[0].size() ==
              out.face_vertex_indices.size() * size_t(fvar.stride));
    }
    if (num_pv) {
      Require(out.vertex_primvars.size() == 1);
      Require(out.vertex_primvars[0].size() == npts * size_t(pv.stride));
    }

    // Exercise the limit path on the (valid) refined mesh.
    if (opts.level >= 1 && opts.scheme != tsd::Scheme::None) {
      tsd::RefinedMesh limit = out;
      if (tsd::SnapToLimit(view, opts, &limit, &err) == tsd::Result::Success) {
        Require(limit.points.size() == out.points.size());
      }
      std::vector<float> normals;
      if (tsd::ComputeLimitNormals(view, opts, out, &normals, &err) ==
          tsd::Result::Success) {
        Require(normals.size() == out.points.size());
      }
    }
  }

  // --- Streaming: RefineStream must survive the same (often invalid) input, and
  //     when both it and bulk succeed on FINITE input its geometry is a bit-exact
  //     subset of bulk's. Exercises block mode, recursive blocks, smooth/linear
  //     fvar streaming, level 0, and bounded batching under the sanitizer. ---
  {
    tsd::StreamOptions so;
    so.batch_faces = 1 + (r.U32() % 64);
    so.emit_triangles = (r.U8() & 1) != 0;
    so.want_normals = (r.U8() & 1) != 0;
    so.dedup_within_batch = (r.U8() & 1) != 0;
    if (r.U8() & 1) {
      so.block_faces = 1 + (r.U32() % 8);
      so.halo_rings = r.U32() % 4;
    }
    so.recursive_blocks = (r.U8() & 1) != 0;

    bool finite = true;
    for (float v : points) finite = finite && std::isfinite(v);
    if (num_fvar) {
      for (float v : fvar_values) finite = finite && std::isfinite(v);
    }
    if (num_pv) {
      for (float v : pv_values) finite = finite && std::isfinite(v);
    }

    std::set<std::array<uint32_t, 3>> bulk_pos;
    StreamCtx sctx;
    sctx.base_faces = num_faces;
    sctx.num_fvar = num_fvar;
    sctx.fvar_stride = num_fvar ? fvar.stride : 0;
    sctx.num_pv = num_pv;
    sctx.pv_stride = num_pv ? pv.stride : 0;
    sctx.want_normals = so.want_normals;
    if (rr == tsd::Result::Success && finite) {
      for (size_t v = 0; v < out.points.size() / 3; v++) {
        bulk_pos.insert(PosKey(&out.points[v * 3]));
      }
      sctx.bulk_pos = &bulk_pos;
    }

    std::string serr;
    tsd::RefineStream(view, num_fvar ? &fvar : nullptr, num_fvar,
                      num_pv ? &pv : nullptr, num_pv, opts, so, StreamFuzzSink,
                      &sctx, &serr);
  }

  return 0;
}
