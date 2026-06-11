// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: uniform refinement driver.
//
// Keeps geometry, vertex/varying primvars, faceVarying channels, sharpness
// and hole flags in lockstep through the per-level loop. Boundary modes are
// realized by baking infinite sharpness into the level-0 edge/vertex
// sharpness arrays (matching OpenSubdiv's TopologyRefinerFactory):
//   - boundary edges are always infinitely sharp
//   - "edgeAndCorner" additionally pins topological corner vertices
//     (1 incident face, 2 incident edges)
//   - "none" tags faces incident to boundary vertices as holes
// Subsequent levels inherit boundary sharpness automatically (infinite
// sharpness never decays, and the child edges of a boundary edge are
// exactly the boundary edges of the child level).

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

// Removes hole-flagged faces from the refined output (faces, face_source and
// all per-corner fvar arrays; points are left untouched).
void FilterHoles(const std::vector<uint8_t> &face_is_hole, RefinedMesh *out) {
  const size_t num_faces = out->face_vertex_counts.size();
  size_t kept_faces = 0;
  size_t kept_corners = 0;
  size_t src_corner = 0;

  const uint32_t num_fvar = uint32_t(out->fvar.size());
  std::vector<uint32_t> fvar_stride(num_fvar);
  const size_t total_corners = out->face_vertex_indices.size();
  for (uint32_t c = 0; c < num_fvar; c++) {
    fvar_stride[c] =
        total_corners ? uint32_t(out->fvar[c].size() / total_corners) : 0;
  }

  for (size_t f = 0; f < num_faces; f++) {
    const uint32_t n = out->face_vertex_counts[f];
    if (face_is_hole[f]) {
      src_corner += n;
      continue;
    }
    out->face_vertex_counts[kept_faces] = n;
    out->face_source[kept_faces] = out->face_source[f];
    for (uint32_t k = 0; k < n; k++) {
      out->face_vertex_indices[kept_corners + k] =
          out->face_vertex_indices[src_corner + k];
    }
    for (uint32_t c = 0; c < num_fvar; c++) {
      const uint32_t stride = fvar_stride[c];
      if (!stride) {
        continue;
      }
      memmove(&out->fvar[c][kept_corners * stride],
              &out->fvar[c][src_corner * stride],
              sizeof(float) * stride * n);
    }
    kept_corners += n;
    src_corner += n;
    kept_faces++;
  }

  out->face_vertex_counts.resize(kept_faces);
  out->face_source.resize(kept_faces);
  out->face_vertex_indices.resize(kept_corners);
  for (uint32_t c = 0; c < num_fvar; c++) {
    out->fvar[c].resize(kept_corners * size_t(fvar_stride[c]));
  }
}

}  // namespace

void BakeLevel0Sharpness(const MeshView &mesh, const Options &options,
                         const Topology &topo, const CreaseEdges &creases,
                         std::vector<float> *edge_sharp,
                         std::vector<float> *vert_sharp,
                         std::vector<uint8_t> *hole, bool *any_hole) {
  const bool smooth_scheme = (options.scheme == Scheme::CatmullClark) ||
                             (options.scheme == Scheme::Loop);
  const uint32_t num_points = topo.num_points;

  edge_sharp->assign(topo.num_edges, 0.0f);
  MapCreasesToEdges(topo, creases, edge_sharp);

  vert_sharp->assign(num_points, 0.0f);
  for (uint32_t i = 0; i < mesh.num_corners; i++) {
    const uint32_t v = uint32_t(mesh.corner_indices[i]);
    float s = mesh.corner_sharpnesses[i];
    if (!(s > 0.0f)) {
      s = 0.0f;
    } else if (s > kInfiniteSharpness) {
      s = kInfiniteSharpness;
    }
    if ((*vert_sharp)[v] < s) {
      (*vert_sharp)[v] = s;
    }
  }

  // interpolateBoundary "none": faces incident to a boundary vertex become
  // holes -- unless every incident boundary edge was already authored
  // infinitely sharp (OpenSubdiv's escape hatch). Bilinear is exempt
  // (OpenSubdiv only applies this to schemes with a local neighborhood,
  // i.e. catmark/loop).
  if (options.boundary == BoundaryInterpolation::None && smooth_scheme &&
      hole) {
    for (uint32_t v = 0; v < num_points; v++) {
      if (!topo.vert_is_boundary[v]) {
        continue;
      }
      bool exclude = false;
      const uint32_t ebegin = topo.vert_edge_offsets[v];
      const uint32_t eend = topo.vert_edge_offsets[v + 1];
      for (uint32_t i = ebegin; i < eend && !exclude; i++) {
        const uint32_t e = topo.vert_edges[i];
        exclude =
            topo.IsBoundaryEdge(e) && !IsInfinitelySharp((*edge_sharp)[e]);
      }
      if (exclude) {
        const uint32_t fbegin = topo.vert_face_offsets[v];
        const uint32_t fend = topo.vert_face_offsets[v + 1];
        for (uint32_t i = fbegin; i < fend; i++) {
          (*hole)[topo.vert_faces[i]] = 1;
          if (any_hole) {
            *any_hole = true;
          }
        }
      }
    }
  }

  // Boundary edges are always infinitely sharp (all modes).
  for (uint32_t e = 0; e < topo.num_edges; e++) {
    if (topo.IsBoundaryEdge(e)) {
      (*edge_sharp)[e] = kInfiniteSharpness;
    }
  }

  // "edgeAndCorner": pin topological corners (1 face, 2 edges).
  if (options.boundary == BoundaryInterpolation::EdgeAndCorner) {
    for (uint32_t v = 0; v < num_points; v++) {
      const uint32_t nf =
          topo.vert_face_offsets[v + 1] - topo.vert_face_offsets[v];
      const uint32_t ne =
          topo.vert_edge_offsets[v + 1] - topo.vert_edge_offsets[v];
      if (nf == 1 && ne == 2) {
        (*vert_sharp)[v] = kInfiniteSharpness;
      }
    }
  }
}

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
  if (options.scheme == Scheme::Loop) {
    for (uint32_t f = 0; f < mesh.num_faces; f++) {
      if (mesh.face_vertex_counts[f] != 3) {
        return Fail(Result::InvalidTopology, err,
                    "Loop scheme requires an all-triangle mesh.");
      }
    }
  }

  if (options.level == 0) {
    r = Passthrough(mesh, fvar_channels, num_fvar_channels, vertex_primvars,
                    num_vertex_primvars, out);
    if (r == Result::Success && options.remove_holes && mesh.num_holes) {
      std::vector<uint8_t> hole(mesh.num_faces, 0);
      for (uint32_t i = 0; i < mesh.num_holes; i++) {
        hole[uint32_t(mesh.hole_indices[i])] = 1;
      }
      FilterHoles(hole, out);
    }
    return r;
  }

  const bool catmark = (options.scheme == Scheme::CatmullClark);
  const bool loop = (options.scheme == Scheme::Loop);
  const bool smooth_scheme = catmark || loop;

  // FVar channels split into the per-corner linear path ("all", or any mode
  // under bilinear whose masks are all linear) and the seam-split smooth
  // path refined with the scheme kernels.
  std::vector<uint8_t> fvar_is_linear(num_fvar_channels, 1);
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    fvar_is_linear[c] =
        (!smooth_scheme ||
         fvar_channels[c].interpolation == FVarLinearInterpolation::All)
            ? 1
            : 0;
  }

  CreaseEdges creases;
  r = CanonicalizeCreases(mesh, &creases, err);
  if (r != Result::Success) {
    return r;
  }

  // --- Mutable per-level state ----------------------------------------------
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
    pvs[p].assign(vertex_primvars[p].values,
                  vertex_primvars[p].values +
                      size_t(mesh.num_points) * vertex_primvars[p].stride);
  }

  // Linear fvar channels: expand to per-corner tuples once, then refine
  // linearly. Smooth channels get their split state built at level 0 below.
  std::vector<std::vector<float>> fvars(num_fvar_channels);
  std::vector<FVarSplitState> fvar_split(num_fvar_channels);
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    if (!fvar_is_linear[c]) {
      continue;
    }
    const FVarChannelView &ch = fvar_channels[c];
    fvars[c].resize(size_t(mesh.num_face_vertex_indices) * ch.stride);
    for (uint32_t i = 0; i < mesh.num_face_vertex_indices; i++) {
      const uint32_t src = ch.indices ? ch.indices[i] : i;
      memcpy(&fvars[c][size_t(i) * ch.stride],
             &ch.values[size_t(src) * ch.stride], sizeof(float) * ch.stride);
    }
  }

  std::vector<uint8_t> hole(mesh.num_faces, 0);
  bool any_hole = false;
  for (uint32_t i = 0; i < mesh.num_holes; i++) {
    hole[uint32_t(mesh.hole_indices[i])] = 1;
    any_hole = true;
  }

  std::vector<uint32_t> face_source(mesh.num_faces);
  for (uint32_t f = 0; f < mesh.num_faces; f++) {
    face_source[f] = f;
  }

  std::vector<float> edge_sharp;
  std::vector<float> vert_sharp;
  // Previous-level data for deriving child sharpness (chaikin needs the
  // parent vertex's incident-edge sharpness).
  Topology prev_topo;
  std::vector<float> prev_edge_sharp;
  std::vector<float> prev_vert_sharp;

  Topology topo;
  for (int32_t lvl = 0; lvl < options.level; lvl++) {
    r = BuildTopology(fvc.data(), uint32_t(fvc.size()), fvi.data(),
                      uint32_t(fvi.size()), num_points, &topo, err);
    if (r != Result::Success) {
      return r;
    }

    // --- Sharpness state for this level -------------------------------------
    if (lvl == 0) {
      BakeLevel0Sharpness(mesh, options, topo, creases, &edge_sharp,
                          &vert_sharp, &hole, &any_hole);

      // Seam-split state for smooth fvar channels.
      for (uint32_t c = 0; c < num_fvar_channels; c++) {
        if (fvar_is_linear[c]) {
          continue;
        }
        r = BuildFVarSplitLevel0(topo, fvc.data(), fvi.data(),
                                 fvar_channels[c], edge_sharp, vert_sharp,
                                 &fvar_split[c], err);
        if (r != Result::Success) {
          return r;
        }
      }
    } else if (smooth_scheme) {
      std::vector<float> new_edge_sharp;
      std::vector<float> new_vert_sharp;
      DeriveChildSharpness(prev_topo, prev_edge_sharp, prev_vert_sharp, topo,
                           options.creasing, &new_edge_sharp,
                           &new_vert_sharp);
      edge_sharp = std::move(new_edge_sharp);
      vert_sharp = std::move(new_vert_sharp);
    }

    // --- Capacity check for the child level ----------------------------------
    const uint64_t child_points64 =
        loop ? (uint64_t(topo.num_points) + topo.num_edges)
             : (uint64_t(topo.num_points) + topo.num_edges + topo.num_faces);
    const uint64_t child_faces64 =
        loop ? (uint64_t(topo.num_faces) * 4) : uint64_t(fvi.size());
    const uint64_t child_corners64 = child_faces64 * (loop ? 3 : 4);
    if (child_points64 > options.max_vertices ||
        child_faces64 > options.max_faces || child_points64 > 0xFFFFFFFFull ||
        child_corners64 > 0xFFFFFFFFull) {
      return Fail(Result::LimitExceeded, err,
                  "refinement exceeds max_vertices/max_faces caps.");
    }

    // --- Child topology -------------------------------------------------------
    ChildTopo child;
    r = loop ? BuildChildTopologyTri(topo, fvi.data(), &child, err)
             : BuildChildTopologyQuad(topo, fvi.data(), &child, err);
    if (r != Result::Success) {
      return r;
    }

    // --- Values ----------------------------------------------------------------
    SharpnessCtx sharp;
    sharp.edge_sharpness = edge_sharp.empty() ? nullptr : edge_sharp.data();
    sharp.vert_sharpness = vert_sharp.empty() ? nullptr : vert_sharp.data();

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
      const uint32_t stride = vertex_primvars[p].stride;
      std::vector<float> child_pv(size_t(child.num_points) * stride);
      if (catmark && !vertex_primvars[p].varying) {
        CatmarkRefineValues(topo, fvi.data(), pvs[p].data(), stride, sharp,
                            options, child_pv.data());
      } else if (loop && !vertex_primvars[p].varying) {
        LoopRefineValues(topo, fvi.data(), pvs[p].data(), stride, sharp,
                         options, child_pv.data());
      } else if (loop) {
        // Linear ("varying") under the tri split: vertex children copy,
        // edge children midpoint; reuse the Loop kernel with a sharpness
        // context that forces midpoints? No -- linear weights are simply
        // copy + midpoint, which LinearTriVarying below provides inline.
        for (uint32_t v = 0; v < topo.num_points; v++) {
          memcpy(&child_pv[size_t(v) * stride],
                 &pvs[p][size_t(v) * stride], sizeof(float) * stride);
        }
        for (uint32_t e = 0; e < topo.num_edges; e++) {
          const float *a =
              &pvs[p][size_t(topo.edge_verts[2 * e]) * stride];
          const float *b =
              &pvs[p][size_t(topo.edge_verts[2 * e + 1]) * stride];
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

    for (uint32_t c = 0; c < num_fvar_channels; c++) {
      if (fvar_is_linear[c]) {
        const uint32_t stride = fvar_channels[c].stride;
        std::vector<float> child_fv(size_t(child_corners64) * stride);
        if (loop) {
          LinearFVarRefineTri(topo, fvars[c].data(), stride, options,
                              child_fv.data());
        } else {
          LinearFVarRefineQuad(topo, fvars[c].data(), stride, options,
                               child_fv.data());
        }
        fvars[c] = std::move(child_fv);
      } else {
        r = RefineFVarSplitOnce(&fvar_split[c], options,
                                lvl + 1 < options.level, err);
        if (r != Result::Success) {
          return r;
        }
      }
    }

    // --- Hole flags + provenance ------------------------------------------------
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

    // --- Advance ------------------------------------------------------------------
    num_points = child.num_points;
    fvc = std::move(child.fvc);
    fvi = std::move(child.fvi);
    if (smooth_scheme && (lvl + 1 < options.level)) {
      prev_topo = std::move(topo);
      prev_edge_sharp = std::move(edge_sharp);
      prev_vert_sharp = std::move(vert_sharp);
      topo = Topology();
      edge_sharp.clear();
      vert_sharp.clear();
    }
  }

  // Expand smooth fvar channels to per-corner tuples.
  for (uint32_t c = 0; c < num_fvar_channels; c++) {
    if (fvar_is_linear[c]) {
      continue;
    }
    const FVarSplitState &st = fvar_split[c];
    fvars[c].resize(st.fvi.size() * st.stride);
    for (size_t i = 0; i < st.fvi.size(); i++) {
      memcpy(&fvars[c][i * st.stride],
             &st.values[size_t(st.fvi[i]) * st.stride],
             sizeof(float) * st.stride);
    }
  }

  // --- Output ------------------------------------------------------------------
  out->points = std::move(geom);
  out->face_vertex_counts = std::move(fvc);
  out->face_vertex_indices = std::move(fvi);
  out->face_source = std::move(face_source);
  out->fvar = std::move(fvars);
  out->vertex_primvars = std::move(pvs);

  if (options.remove_holes && any_hole) {
    FilterHoles(hole, out);
  }

  return Result::Success;
}

}  // namespace tsd
}  // namespace tinyusdz
