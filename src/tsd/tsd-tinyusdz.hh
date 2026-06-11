// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv <-> tinyusdz adapter: refine a GeomMesh's evaluated geometry
// using its authored USD subdivision attributes.

#ifndef TINYUSDZ_TSD_TINYUSDZ_HH_
#define TINYUSDZ_TSD_TINYUSDZ_HH_

#include <cstdint>
#include <string>
#include <vector>

#include "tinysubdiv.hh"

namespace tinyusdz {

struct GeomMesh;

namespace tsd {

// Uniformly refines pre-evaluated mesh geometry `level` times using the
// GeomMesh's authored subdivision attributes:
//   - subdivisionScheme (catmullClark/loop/bilinear; "none" is rejected --
//     skip subdivision on the caller side)
//   - interpolateBoundary
//   - creaseIndices/creaseLengths/creaseSharpnesses, cornerIndices/
//     cornerSharpnesses, holeIndices (evaluated at default time; subdiv
//     tags are not animatable in practice)
// `points_xyz` is interleaved xyz (3 floats per point), `faceVertexCounts`/
// `faceVertexIndices` the evaluated topology at the caller's timecode.
// Output faces are quads (catmullClark/bilinear) or triangles (loop);
// `out->face_source` maps each refined face to its base-face id (use it to
// remap per-face data such as GeomSubset indices and uniform primvars).
bool RefineGeomMesh(const GeomMesh &mesh, int32_t level,
                    const std::vector<float> &points_xyz,
                    const std::vector<uint32_t> &faceVertexCounts,
                    const std::vector<uint32_t> &faceVertexIndices,
                    RefinedMesh *out, std::string *err);

}  // namespace tsd
}  // namespace tinyusdz

#endif  // TINYUSDZ_TSD_TINYUSDZ_HH_
