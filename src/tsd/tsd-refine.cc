// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: uniform refinement driver.

#include <cstring>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

namespace {

// Level-0 passthrough: deep copy of the input (with fvar expanded
// per-face-corner), no refinement.
Result Passthrough(const MeshView &mesh, const FVarChannelView *fvar_channels,
                   uint32_t num_fvar_channels,
                   const VertexPrimvarView *vertex_primvars,
                   uint32_t num_vertex_primvars, RefinedMesh *out) {
  out->points.assign(mesh.points, mesh.points + size_t(mesh.num_points) * 3);
  out->face_vertex_counts.assign(mesh.face_vertex_counts,
                                 mesh.face_vertex_counts + mesh.num_faces);
  out->face_vertex_indices.assign(
      mesh.face_vertex_indices,
      mesh.face_vertex_indices + mesh.num_face_vertex_indices);
  out->face_source.resize(mesh.num_faces);
  for (uint32_t f = 0; f < mesh.num_faces; f++) {
    out->face_source[f] = f;
  }

  out->fvar.resize(num_fvar_channels);
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    const FVarChannelView &ch = fvar_channels[c];
    std::vector<float> &dst = out->fvar[c];
    dst.resize(size_t(mesh.num_face_vertex_indices) * ch.stride);
    for (uint32_t i = 0; i < mesh.num_face_vertex_indices; i++) {
      const uint32_t src = ch.indices ? ch.indices[i] : i;
      memcpy(&dst[size_t(i) * ch.stride], &ch.values[size_t(src) * ch.stride],
             sizeof(float) * ch.stride);
    }
  }

  out->vertex_primvars.resize(num_vertex_primvars);
  for (uint32_t p = 0; p < num_vertex_primvars; p++) {
    const VertexPrimvarView &pv = vertex_primvars[p];
    out->vertex_primvars[p].assign(
        pv.values, pv.values + size_t(mesh.num_points) * pv.stride);
  }

  return Result::Success;
}

}  // namespace

Result Refine(const MeshView &mesh, const FVarChannelView *fvar_channels,
              uint32_t num_fvar_channels,
              const VertexPrimvarView *vertex_primvars,
              uint32_t num_vertex_primvars, const Options &options,
              RefinedMesh *out, std::string *err) {
  if (!out) {
    return Fail(Result::InvalidArgument, err, "output RefinedMesh is null.");
  }

  Result r = ValidateInput(mesh, fvar_channels, num_fvar_channels,
                           vertex_primvars, num_vertex_primvars, options, err);
  if (r != Result::Success) {
    return r;
  }

  if (options.scheme == Scheme::None) {
    return Fail(Result::UnsupportedScheme, err,
                "subdivisionScheme 'none' is not subdividable; skip "
                "subdivision on the caller side.");
  }

  if (options.level == 0) {
    return Passthrough(mesh, fvar_channels, num_fvar_channels, vertex_primvars,
                       num_vertex_primvars, out);
  }

  // TODO(tsd, M1): refinement kernels.
  return Fail(Result::UnsupportedScheme, err,
              "tsd refinement not implemented yet (scaffold).");
}

// TODO(tsd, M5): closed-form limit stencils (moves to tsd-limit.cc).
Result SnapToLimit(const MeshView &base_mesh, const Options &options,
                   RefinedMesh *inout, std::string *err) {
  (void)base_mesh;
  (void)options;
  (void)inout;
  return Fail(Result::InvalidArgument, err,
              "SnapToLimit not implemented yet.");
}

Result ComputeLimitNormals(const MeshView &base_mesh, const Options &options,
                           const RefinedMesh &refined,
                           std::vector<float> *out_normals, std::string *err) {
  (void)base_mesh;
  (void)options;
  (void)refined;
  (void)out_normals;
  return Fail(Result::InvalidArgument, err,
              "ComputeLimitNormals not implemented yet.");
}

}  // namespace tsd
}  // namespace tinyusdz
