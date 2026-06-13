// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// libFuzzer harness for tinysubdiv (src/tsd): decodes arbitrary bytes into
// a MeshView + Options and refines. Any crash/UB/sanitizer report is a bug;
// malformed input must be rejected through tsd::Result codes.

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
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

  return 0;
}
