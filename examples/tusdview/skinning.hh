// SPDX-License-Identifier: Apache-2.0
// tusdview - CPU linear-blend skinning + blendshape deformation.
//
// Tydra bakes a rest-pose mesh plus separate skeleton/animation data; it does
// not deform geometry for skinning, and it does not emit animated blendshape
// weights. This module fills both gaps on the CPU: given a RenderScene already
// converted at time `timecode` (so node transforms/value-clips are resolved)
// and the source Stage, it deforms each skinned / blendshaped RenderMesh's
// points in place so the existing pack + upload path renders the posed mesh.
#pragma once

#include <string>

#include "stage.hh"
#include "tydra/render-data.hh"

namespace tusdview {

// True if any mesh in `render` carries skeletal skinning data or blendshape
// targets (i.e. would deform over time). Cheap topology check.
bool SceneHasDeformation(const tinyusdz::tydra::RenderScene& render);

// Deform every skinned / blendshaped mesh in `render` to its pose at
// `timecode`: applies animated blendshape offsets (weights read from the
// Stage's SkelAnimation prims) then linear-blend skinning (joint poses from
// `render.animations`). Mesh `points` are overwritten with the posed result and
// `normals` are cleared so the packer regenerates them from the posed geometry.
// No-op for meshes without skinning/blendshapes. `stage` must be the source of
// `render`.
void DeformSkinnedMeshes(const tinyusdz::Stage& stage,
                         tinyusdz::tydra::RenderScene& render, double timecode);

}  // namespace tusdview
