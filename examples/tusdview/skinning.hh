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
#include <vector>

#include "gpu_scene.hh"
#include "stage.hh"
#include "tydra/render-data.hh"
#include "value-types.hh"

namespace tusdview {

// True if any mesh in `render` carries skeletal skinning data or blendshape
// targets (i.e. would deform over time). Cheap topology check.
bool SceneHasDeformation(const tinyusdz::tydra::RenderScene& render);
bool SceneHasSkeletalSkinning(const tinyusdz::tydra::RenderScene& render);
bool SceneHasBlendShapes(const tinyusdz::tydra::RenderScene& render);
bool SceneHasNonSkeletalAnimation(const tinyusdz::tydra::RenderScene& render);

// Per-skeleton skinning matrices:
// skinMat[j] = inverse(bind[j]) * posedWorld[j] (USD row-vector convention).
bool BuildSkinningMatrices(const tinyusdz::tydra::RenderScene& render,
                           int skelId, double timecode,
                           std::vector<tinyusdz::value::matrix4d>* skinOut);

// Pack the per-frame GPU bone texture from `render` into `frame`, using the
// per-mesh skin layout stored in `draw`. Also updates skinned mesh and scene
// AABBs in `draw` so GUI/MCP bounds match the displayed GPU-skinned pose.
//
// Blendshapes: for each mesh carrying `morphs`, the rest vertices are morphed
// to their pose at `timecode` (weights read from the Stage's SkelAnimation
// prims) and returned in `morphedOut` as (meshIndex, morphedVertices) so the
// caller can re-upload them; the GPU vertex shader then skins the morphed
// input. Bounds account for the morph. Pass `morphedOut == nullptr` to skip
// blendshapes (skeletal only).
bool BuildGpuSkinningFrame(
    const tinyusdz::tydra::RenderScene& render, const tinyusdz::Stage& stage,
    DrawScene* draw, double timecode, SkinningFrameCPU* frame,
    std::vector<std::pair<int, std::vector<DrawVertex>>>* morphedOut);

// Update each draw mesh's world transform to its value at `timecode`, evaluated
// from the Stage's xform hierarchy. For scenes whose node transforms animate
// alongside GPU skeletal skinning (e.g. a moving SkelRoot), this poses node
// motion without a geometry re-pack. Returns true if any world changed; the
// renderer must be told separately (Renderer::updateMeshWorld).
bool UpdateAnimatedMeshWorlds(const tinyusdz::Stage& stage, DrawScene* draw,
                              double timecode);

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
