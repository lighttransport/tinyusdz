// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: input validation and USD crease canonicalization.

#include <algorithm>
#include <cstring>
#include <limits>

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

const char *to_string(Result result) {
  switch (result) {
    case Result::Success:
      return "success";
    case Result::InvalidArgument:
      return "invalid argument";
    case Result::InvalidTopology:
      return "invalid topology";
    case Result::UnsupportedScheme:
      return "unsupported scheme";
    case Result::LimitExceeded:
      return "limit exceeded";
    case Result::OutOfMemory:
      return "out of memory";
  }
  return "unknown";
}

namespace {

inline float ClampSharpness(float s) {
  if (!(s > 0.0f)) {  // also catches NaN
    return 0.0f;
  }
  if (s > kInfiniteSharpness) {
    return kInfiniteSharpness;
  }
  return s;
}

}  // namespace

Result ValidateInput(const MeshView &mesh,
                     const FVarChannelView *fvar_channels,
                     uint32_t num_fvar_channels,
                     const VertexPrimvarView *vertex_primvars,
                     uint32_t num_vertex_primvars, const Options &options,
                     std::string *err) {
  if (options.level < 0 || options.level > kMaxLevel) {
    return Fail(Result::InvalidArgument, err,
                "options.level out of range [0, 10].");
  }
  if (options.max_vertices == 0 || options.max_faces == 0 ||
      options.max_face_vertex_indices == 0) {
    return Fail(Result::InvalidArgument, err,
                "options.max_vertices/max_faces/max_face_vertex_indices must "
                "be non-zero.");
  }

  if (!mesh.points || mesh.num_points == 0) {
    return Fail(Result::InvalidArgument, err, "mesh.points is empty.");
  }
  if (!mesh.face_vertex_counts || mesh.num_faces == 0) {
    return Fail(Result::InvalidArgument, err,
                "mesh.face_vertex_counts is empty.");
  }
  if (!mesh.face_vertex_indices || mesh.num_face_vertex_indices == 0) {
    return Fail(Result::InvalidArgument, err,
                "mesh.face_vertex_indices is empty.");
  }
  if (mesh.num_points > options.max_vertices) {
    return Fail(Result::LimitExceeded, err,
                "base mesh exceeds options.max_vertices.");
  }
  if (mesh.num_faces > options.max_faces) {
    return Fail(Result::LimitExceeded, err,
                "base mesh exceeds options.max_faces.");
  }

  // Face degrees and corner-count consistency (64-bit accumulation).
  uint64_t corner_sum = 0;
  for (uint32_t f = 0; f < mesh.num_faces; f++) {
    const uint32_t n = mesh.face_vertex_counts[f];
    if (n < 3 || n > kMaxFaceDegree) {
      return Fail(Result::InvalidTopology, err,
                  "face degree out of range [3, 256].");
    }
    corner_sum += n;
    if (corner_sum > (std::numeric_limits<uint32_t>::max)()) {
      return Fail(Result::LimitExceeded, err,
                  "base mesh corner count exceeds 32-bit index space.");
    }
    if (corner_sum > options.max_face_vertex_indices) {
      return Fail(Result::LimitExceeded, err,
                  "base mesh exceeds options.max_face_vertex_indices.");
    }
  }
  if (corner_sum != mesh.num_face_vertex_indices) {
    return Fail(Result::InvalidTopology, err,
                "sum(face_vertex_counts) != num_face_vertex_indices.");
  }

  for (uint32_t i = 0; i < mesh.num_face_vertex_indices; i++) {
    if (mesh.face_vertex_indices[i] >= mesh.num_points) {
      return Fail(Result::InvalidTopology, err,
                  "face_vertex_indices entry out of range.");
    }
  }

  // Corners.
  if (mesh.num_corners) {
    if (!mesh.corner_indices || !mesh.corner_sharpnesses) {
      return Fail(Result::InvalidArgument, err,
                  "corner_indices/corner_sharpnesses missing while "
                  "num_corners > 0.");
    }
    for (uint32_t i = 0; i < mesh.num_corners; i++) {
      const int32_t v = mesh.corner_indices[i];
      if (v < 0 || uint32_t(v) >= mesh.num_points) {
        return Fail(Result::InvalidTopology, err,
                    "corner_indices entry out of range.");
      }
    }
  }

  // Creases (chain structure; per-edge mapping happens in
  // CanonicalizeCreases).
  if (mesh.num_crease_lengths) {
    if (!mesh.crease_indices || !mesh.crease_lengths ||
        !mesh.crease_sharpnesses) {
      return Fail(Result::InvalidArgument, err,
                  "crease arrays missing while num_crease_lengths > 0.");
    }
    uint64_t index_sum = 0;
    uint64_t edge_sum = 0;
    for (uint32_t c = 0; c < mesh.num_crease_lengths; c++) {
      const int32_t len = mesh.crease_lengths[c];
      if (len < 2) {
        return Fail(Result::InvalidTopology, err,
                    "crease_lengths entry must be >= 2.");
      }
      index_sum += uint64_t(len);
      edge_sum += uint64_t(len) - 1;
    }
    if (index_sum != mesh.num_crease_indices) {
      return Fail(Result::InvalidTopology, err,
                  "sum(crease_lengths) != num_crease_indices.");
    }
    if (mesh.num_crease_sharpnesses != mesh.num_crease_lengths &&
        mesh.num_crease_sharpnesses != edge_sum) {
      return Fail(
          Result::InvalidTopology, err,
          "num_crease_sharpnesses must be per-crease (== num_crease_lengths) "
          "or per-edge (== sum(crease_lengths[i] - 1)).");
    }
    for (uint32_t i = 0; i < mesh.num_crease_indices; i++) {
      const int32_t v = mesh.crease_indices[i];
      if (v < 0 || uint32_t(v) >= mesh.num_points) {
        return Fail(Result::InvalidTopology, err,
                    "crease_indices entry out of range.");
      }
    }
  } else if (mesh.num_crease_indices || mesh.num_crease_sharpnesses) {
    return Fail(Result::InvalidTopology, err,
                "crease_indices/sharpnesses authored without crease_lengths.");
  }

  // Holes.
  if (mesh.num_holes) {
    if (!mesh.hole_indices) {
      return Fail(Result::InvalidArgument, err,
                  "hole_indices missing while num_holes > 0.");
    }
    for (uint32_t i = 0; i < mesh.num_holes; i++) {
      const int32_t f = mesh.hole_indices[i];
      if (f < 0 || uint32_t(f) >= mesh.num_faces) {
        return Fail(Result::InvalidTopology, err,
                    "hole_indices entry out of range.");
      }
    }
  }

  // FaceVarying channels.
  if (num_fvar_channels && !fvar_channels) {
    return Fail(Result::InvalidArgument, err,
                "fvar_channels is null while num_fvar_channels > 0.");
  }
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    const FVarChannelView &ch = fvar_channels[c];
    if (!ch.values || ch.num_values == 0) {
      return Fail(Result::InvalidArgument, err, "fvar channel has no values.");
    }
    if (ch.stride < 1 || ch.stride > 4) {
      return Fail(Result::InvalidArgument, err,
                  "fvar channel stride out of range [1, 4].");
    }
    if (ch.indices) {
      for (uint32_t i = 0; i < mesh.num_face_vertex_indices; i++) {
        if (ch.indices[i] >= ch.num_values) {
          return Fail(Result::InvalidTopology, err,
                      "fvar channel index out of range.");
        }
      }
    } else if (ch.num_values != mesh.num_face_vertex_indices) {
      return Fail(Result::InvalidTopology, err,
                  "fvar channel without indices must have one value per "
                  "face corner.");
    }
  }

  // Vertex/varying primvars.
  if (num_vertex_primvars && !vertex_primvars) {
    return Fail(Result::InvalidArgument, err,
                "vertex_primvars is null while num_vertex_primvars > 0.");
  }
  for (uint32_t p = 0; p < num_vertex_primvars; p++) {
    const VertexPrimvarView &pv = vertex_primvars[p];
    if (!pv.values) {
      return Fail(Result::InvalidArgument, err,
                  "vertex primvar has no values.");
    }
    if (pv.stride < 1 || pv.stride > 4) {
      return Fail(Result::InvalidArgument, err,
                  "vertex primvar stride out of range [1, 4].");
    }
  }

  return Result::Success;
}

Result CanonicalizeCreases(const MeshView &mesh, CreaseEdges *out,
                           std::string *err) {
  out->edge_verts.clear();
  out->sharpnesses.clear();
  if (mesh.num_crease_lengths == 0) {
    return Result::Success;
  }

  const bool per_crease =
      (mesh.num_crease_sharpnesses == mesh.num_crease_lengths);

  // Expand chains into (v0 < v1, sharpness) entries.
  uint32_t index_offset = 0;
  uint32_t edge_offset = 0;
  for (uint32_t c = 0; c < mesh.num_crease_lengths; c++) {
    const uint32_t len = uint32_t(mesh.crease_lengths[c]);
    for (uint32_t k = 0; k + 1 < len; k++) {
      uint32_t a = uint32_t(mesh.crease_indices[index_offset + k]);
      uint32_t b = uint32_t(mesh.crease_indices[index_offset + k + 1]);
      if (a == b) {
        // Degenerate segment; skip (cannot be a mesh edge).
        continue;
      }
      if (a > b) {
        std::swap(a, b);
      }
      const float s = ClampSharpness(
          per_crease ? mesh.crease_sharpnesses[c]
                     : mesh.crease_sharpnesses[edge_offset + k]);
      if (s <= 0.0f) {
        continue;
      }
      out->edge_verts.push_back(a);
      out->edge_verts.push_back(b);
      out->sharpnesses.push_back(s);
    }
    index_offset += len;
    edge_offset += len - 1;
  }

  // Deduplicate, keeping max sharpness. Counts are small (authored data);
  // sort an index permutation by edge key.
  const size_t n = out->sharpnesses.size();
  if (n > 1) {
    std::vector<uint32_t> order(n);
    for (size_t i = 0; i < n; i++) {
      order[i] = uint32_t(i);
    }
    const std::vector<uint32_t> &ev = out->edge_verts;
    std::sort(order.begin(), order.end(), [&ev](uint32_t x, uint32_t y) {
      const uint64_t kx = (uint64_t(ev[2 * x]) << 32) | ev[2 * x + 1];
      const uint64_t ky = (uint64_t(ev[2 * y]) << 32) | ev[2 * y + 1];
      return kx < ky;
    });
    std::vector<uint32_t> dedup_verts;
    std::vector<float> dedup_sharp;
    dedup_verts.reserve(2 * n);
    dedup_sharp.reserve(n);
    for (size_t i = 0; i < n; i++) {
      const uint32_t idx = order[i];
      const uint32_t a = ev[2 * idx];
      const uint32_t b = ev[2 * idx + 1];
      if (!dedup_sharp.empty() && dedup_verts[dedup_verts.size() - 2] == a &&
          dedup_verts[dedup_verts.size() - 1] == b) {
        dedup_sharp.back() =
            std::max(dedup_sharp.back(), out->sharpnesses[idx]);
      } else {
        dedup_verts.push_back(a);
        dedup_verts.push_back(b);
        dedup_sharp.push_back(out->sharpnesses[idx]);
      }
    }
    out->edge_verts = std::move(dedup_verts);
    out->sharpnesses = std::move(dedup_sharp);
  }

  (void)err;
  return Result::Success;
}

}  // namespace tsd
}  // namespace tinyusdz
