// SPDX-License-Identifier: Apache-2.0
#include "skinning.hh"

#include <cmath>
#include <unordered_map>
#include <vector>

#include "tydra/scene-access.hh"  // SkinPointsLBS, ConcatJointTransforms, ListPrims
#include "usdSkel.hh"             // SkelAnimation
#include "xform.hh"               // inverse, to_matrix

namespace tusdview {

namespace {

namespace tydra = tinyusdz::tydra;
using tinyusdz::value::matrix4d;
using tinyusdz::value::point3f;
using tinyusdz::value::quatf;

// Evaluate a flat keyframe sampler (comps floats per key) at time code `t`.
// Held or linear interpolation; clamps outside the key range.
void EvalSampler(const tydra::KeyframeSampler& s, double t, int comps,
                 float* out) {
  const size_t n = s.times.size();
  if (n == 0 || s.values.size() < n * static_cast<size_t>(comps)) {
    for (int i = 0; i < comps; ++i) out[i] = 0.0f;
    return;
  }
  auto key = [&](size_t k, int i) { return s.values[k * comps + i]; };
  if (t <= s.times[0]) {
    for (int i = 0; i < comps; ++i) out[i] = key(0, i);
    return;
  }
  if (t >= s.times[n - 1]) {
    for (int i = 0; i < comps; ++i) out[i] = key(n - 1, i);
    return;
  }
  size_t k0 = 0;
  while (k0 + 1 < n && s.times[k0 + 1] <= t) ++k0;
  const size_t k1 = k0 + 1;
  float a = 0.0f;
  if (s.interpolation != tydra::AnimationInterpolation::Step) {
    const float span = s.times[k1] - s.times[k0];
    a = span > 0.0f ? static_cast<float>((t - s.times[k0]) / span) : 0.0f;
  }
  for (int i = 0; i < comps; ++i) {
    out[i] = key(k0, i) * (1.0f - a) + key(k1, i) * a;
  }
}

// Build a joint-local transform from animated TRS (row-vector convention,
// matching SkelMakeTransform: translation in row 3, scale applied per row).
matrix4d MakeLocal(const float t[3], const quatf& r, const float s[3]) {
  matrix4d m = tinyusdz::to_matrix(r);
  m.m[0][0] *= s[0]; m.m[0][1] *= s[0]; m.m[0][2] *= s[0];
  m.m[1][0] *= s[1]; m.m[1][1] *= s[1]; m.m[1][2] *= s[1];
  m.m[2][0] *= s[2]; m.m[2][1] *= s[2]; m.m[2][2] *= s[2];
  m.m[3][0] = t[0]; m.m[3][1] = t[1]; m.m[3][2] = t[2];
  return m;
}

// Per-skeleton skinning matrices skinMat[j] = inverse(bind[j]) * posedWorld[j]
// (row-vector: a point in bind space maps to posed world). Joints without an
// animation channel keep their rest-pose local transform.
bool BuildSkinningMatrices(const tydra::RenderScene& render, int skelId,
                           double t, std::vector<matrix4d>* skinOut) {
  if (skelId < 0 || skelId >= static_cast<int>(render.skeletons.size())) {
    return false;
  }
  const tydra::SkelHierarchy& skel = render.skeletons[static_cast<size_t>(skelId)];
  const size_t nj = skel.num_joints();
  if (nj == 0 || skel.bind_transforms.size() != nj ||
      skel.rest_transforms.size() != nj) {
    return false;
  }

  std::vector<matrix4d> local = skel.rest_transforms;  // default = rest pose
  std::vector<bool> animated(nj, false);
  std::vector<float> T(nj * 3, 0.0f);
  std::vector<quatf> R(nj);
  std::vector<float> S(nj * 3, 1.0f);
  // Default each joint's TRS from its rest-pose local transform, so a joint
  // whose animation authors only some components (e.g. rotation, the common
  // Blender export) keeps its rest bone offset / scale instead of collapsing.
  for (size_t j = 0; j < nj; ++j) {
    R[j].imag[0] = R[j].imag[1] = R[j].imag[2] = 0.0f;
    R[j].real = 1.0f;
    tinyusdz::value::double3 dt, ds;
    tinyusdz::value::quatd dr;
    if (tinyusdz::decompose(skel.rest_transforms[j], &dt, &dr, &ds)) {
      T[j * 3 + 0] = static_cast<float>(dt[0]);
      T[j * 3 + 1] = static_cast<float>(dt[1]);
      T[j * 3 + 2] = static_cast<float>(dt[2]);
      R[j].imag[0] = static_cast<float>(dr.imag[0]);
      R[j].imag[1] = static_cast<float>(dr.imag[1]);
      R[j].imag[2] = static_cast<float>(dr.imag[2]);
      R[j].real = static_cast<float>(dr.real);
      S[j * 3 + 0] = static_cast<float>(ds[0]);
      S[j * 3 + 1] = static_cast<float>(ds[1]);
      S[j * 3 + 2] = static_cast<float>(ds[2]);
    }
  }

  // Override with animated TRS from any SkeletonJoint channel targeting this
  // skeleton (a present channel replaces that component; absent ones keep rest).
  if (skel.anim_id >= 0 &&
      skel.anim_id < static_cast<int>(render.animations.size())) {
    const tydra::AnimationClip& clip =
        render.animations[static_cast<size_t>(skel.anim_id)];
    for (const tydra::AnimationChannel& ch : clip.channels) {
      if (ch.target_type != tydra::ChannelTargetType::SkeletonJoint) continue;
      if (ch.skeleton_id != skelId) continue;
      if (ch.joint_id < 0 || ch.joint_id >= static_cast<int>(nj)) continue;
      if (ch.sampler < 0 ||
          ch.sampler >= static_cast<int>(clip.samplers.size())) {
        continue;
      }
      const tydra::KeyframeSampler& smp =
          clip.samplers[static_cast<size_t>(ch.sampler)];
      const size_t j = static_cast<size_t>(ch.joint_id);
      animated[j] = true;
      if (ch.path == tydra::AnimationPath::Translation) {
        EvalSampler(smp, t, 3, &T[j * 3]);
      } else if (ch.path == tydra::AnimationPath::Scale) {
        EvalSampler(smp, t, 3, &S[j * 3]);
      } else if (ch.path == tydra::AnimationPath::Rotation) {
        float q[4];
        EvalSampler(smp, t, 4, q);  // x, y, z, w
        float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
        if (len < 1e-12f) { q[0] = q[1] = q[2] = 0.0f; q[3] = 1.0f; len = 1.0f; }
        R[j].imag[0] = q[0] / len;
        R[j].imag[1] = q[1] / len;
        R[j].imag[2] = q[2] / len;
        R[j].real = q[3] / len;
      }
    }
  }

  for (size_t j = 0; j < nj; ++j) {
    if (animated[j]) local[j] = MakeLocal(&T[j * 3], R[j], &S[j * 3]);
  }

  std::vector<matrix4d> world;
  if (!tydra::ConcatJointTransforms(skel.parent_joint_indices, local, &world)) {
    return false;
  }

  skinOut->resize(nj);
  for (size_t j = 0; j < nj; ++j) {
    (*skinOut)[j] = tinyusdz::inverse(skel.bind_transforms[j]) * world[j];
  }
  return true;
}

// Map of blendshape prim name -> animated weight at time `t`, gathered from all
// SkelAnimation prims in the stage (Tydra does not emit blendShapeWeights).
std::unordered_map<std::string, float> GatherBlendWeights(
    const tinyusdz::Stage& stage, double t) {
  std::unordered_map<std::string, float> out;
  tydra::PathPrimMap<tinyusdz::SkelAnimation> anims;
  if (!tydra::ListPrims(stage, anims)) return out;
  for (auto& kv : anims) {
    tinyusdz::SkelAnimation* sa = const_cast<tinyusdz::SkelAnimation*>(kv.second);
    if (!sa) continue;
    std::vector<tinyusdz::value::token> names;
    if (!sa->get_blendShapes(&names) || names.empty()) continue;
    std::vector<float> weights;
    if (!sa->get_blendShapeWeights(&weights, t)) continue;
    const size_t n = std::min(names.size(), weights.size());
    for (size_t i = 0; i < n; ++i) out[names[i].str()] = weights[i];
  }
  return out;
}

bool MeshIsSkinned(const tydra::RenderMesh& m) {
  return m.skel_id >= 0 && !m.joint_and_weights.jointIndices.empty();
}

}  // namespace

bool SceneHasDeformation(const tydra::RenderScene& render) {
  for (const tydra::RenderMesh& m : render.meshes) {
    if (MeshIsSkinned(m) || !m.targets.empty()) return true;
  }
  return false;
}

void DeformSkinnedMeshes(const tinyusdz::Stage& stage,
                         tydra::RenderScene& render, double timecode) {
  if (!SceneHasDeformation(render)) return;
  const double t = timecode;  // time codes (matches Tydra sampler times)

  std::unordered_map<std::string, float> blendWeights;
  bool gatheredBlend = false;

  // Cache skinning matrices per skeleton (shared across meshes).
  std::unordered_map<int, std::vector<matrix4d>> skinCache;

  for (tydra::RenderMesh& mesh : render.meshes) {
    const bool skinned = MeshIsSkinned(mesh);
    const bool morphed = !mesh.targets.empty();
    if (!skinned && !morphed) continue;

    const size_t np = mesh.points.size();
    if (np == 0) continue;

    // Working copy of points (rest pose) to deform.
    std::vector<point3f> pts(np);
    for (size_t i = 0; i < np; ++i) {
      pts[i].x = mesh.points[i][0];
      pts[i].y = mesh.points[i][1];
      pts[i].z = mesh.points[i][2];
    }

    // (1) Blendshapes: accumulate weighted offsets onto the rest points.
    if (morphed) {
      if (!gatheredBlend) {
        blendWeights = GatherBlendWeights(stage, t);
        gatheredBlend = true;
      }
      for (const auto& kv : mesh.targets) {
        auto wit = blendWeights.find(kv.first);
        if (wit == blendWeights.end()) continue;
        const float w = wit->second;
        if (w == 0.0f) continue;
        const tydra::ShapeTarget& tgt = kv.second;
        const size_t no = tgt.pointOffsets.size();
        for (size_t k = 0; k < tgt.pointIndices.size() && k < no; ++k) {
          const uint32_t vid = tgt.pointIndices[k];
          if (vid >= np) continue;
          pts[vid].x += w * tgt.pointOffsets[k][0];
          pts[vid].y += w * tgt.pointOffsets[k][1];
          pts[vid].z += w * tgt.pointOffsets[k][2];
        }
      }
    }

    // (2) Linear-blend skinning of the (blendshaped) points.
    if (skinned) {
      const auto& jw = mesh.joint_and_weights;
      const int infl = jw.elementSize < 1 ? 1 : jw.elementSize;
      const size_t expect = np * static_cast<size_t>(infl);
      std::vector<float> weights(jw.jointWeights.size());
      for (size_t i = 0; i < weights.size(); ++i) weights[i] = jw.jointWeights[i];
      if (jw.jointIndices.size() == expect && weights.size() == expect) {
        auto cit = skinCache.find(mesh.skel_id);
        if (cit == skinCache.end()) {
          std::vector<matrix4d> sm;
          if (BuildSkinningMatrices(render, mesh.skel_id, t, &sm)) {
            cit = skinCache.emplace(mesh.skel_id, std::move(sm)).first;
          }
        }
        if (cit != skinCache.end()) {
          std::vector<point3f> skinned_pts;
          if (tydra::SkinPointsLBS(pts, jw.geomBindTransform, cit->second,
                                   jw.jointIndices, weights, infl,
                                   &skinned_pts) &&
              skinned_pts.size() == np) {
            pts.swap(skinned_pts);
          }
        }
      }
    }

    // Write deformed points back; clear normals so the packer regenerates
    // smooth normals from the posed geometry.
    for (size_t i = 0; i < np; ++i) {
      mesh.points[i][0] = pts[i].x;
      mesh.points[i][1] = pts[i].y;
      mesh.points[i][2] = pts[i].z;
    }
    mesh.normals = tydra::VertexAttribute();
  }
}

}  // namespace tusdview
