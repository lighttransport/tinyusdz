// SPDX-License-Identifier: Apache-2.0
#include "skinning.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
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

bool MatrixNearlyIdentity(const matrix4d& m) {
  const matrix4d ident = matrix4d::identity();
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      if (std::abs(m.m[r][c] - ident.m[r][c]) > 1e-8) return false;
    }
  }
  return true;
}

bool AllIdentity(const std::vector<matrix4d>& m) {
  if (m.empty()) return false;
  for (const matrix4d& v : m) {
    if (!MatrixNearlyIdentity(v)) return false;
  }
  return true;
}

bool IsPointJointSkeleton(const tydra::SkelHierarchy& skel) {
  const size_t nj = skel.num_joints();
  if (nj < 512 || skel.parent_joint_indices.empty()) return false;
  // Some exporters encode dense vertex/point deformation as a huge star-shaped
  // UsdSkel rig. Its rotation/scale channels are not meaningful for LBS, and
  // applying them collapses the mesh; translations carry the deformation.
  if (nj >= 1024) return true;
  if (skel.root_node.children.size() + 1 == nj) return true;
  size_t roots = 0;
  size_t rootChildren = 0;
  for (int parent : skel.parent_joint_indices) {
    if (parent < 0) {
      ++roots;
    } else if (parent == 0) {
      ++rootChildren;
    }
  }
  return roots == 1 && rootChildren + 1 == nj;
}

const tydra::AnimationClip* FindSkeletonClip(const tydra::RenderScene& render,
                                             const tydra::SkelHierarchy& skel,
                                             int skelId) {
  auto hasChannels = [&](const tydra::AnimationClip& clip) {
    for (const tydra::AnimationChannel& ch : clip.channels) {
      if (ch.target_type == tydra::ChannelTargetType::SkeletonJoint &&
          ch.skeleton_id == skelId) {
        return true;
      }
    }
    return false;
  };
  if (skel.anim_id >= 0 && skel.anim_id < static_cast<int>(render.animations.size())) {
    const auto& clip = render.animations[static_cast<size_t>(skel.anim_id)];
    if (hasChannels(clip)) return &clip;
  }
  for (const tydra::AnimationClip& clip : render.animations) {
    if (hasChannels(clip)) return &clip;
  }
  return nullptr;
}

double FirstSkeletonSampleTime(const tydra::AnimationClip& clip, int skelId) {
  double first = std::numeric_limits<double>::infinity();
  for (const tydra::AnimationChannel& ch : clip.channels) {
    if (ch.target_type != tydra::ChannelTargetType::SkeletonJoint) continue;
    if (ch.skeleton_id != skelId) continue;
    if (ch.sampler < 0 || ch.sampler >= static_cast<int>(clip.samplers.size())) {
      continue;
    }
    const auto& times = clip.samplers[static_cast<size_t>(ch.sampler)].times;
    if (!times.empty()) first = std::min(first, static_cast<double>(times.front()));
  }
  return std::isfinite(first) ? first : 0.0;
}

void BuildJointLocals(const tydra::AnimationClip* clip, int skelId, double t,
                      const std::vector<matrix4d>& baseLocal,
                      bool translationOnly,
                      std::vector<matrix4d>* localOut) {
  const size_t nj = baseLocal.size();
  std::vector<matrix4d> local = baseLocal;
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
    if (tinyusdz::decompose(baseLocal[j], &dt, &dr, &ds)) {
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
  if (clip) {
    for (const tydra::AnimationChannel& ch : clip->channels) {
      if (ch.target_type != tydra::ChannelTargetType::SkeletonJoint) continue;
      if (ch.skeleton_id != skelId) continue;
      if (ch.joint_id < 0 || ch.joint_id >= static_cast<int>(nj)) continue;
      if (ch.sampler < 0 ||
          ch.sampler >= static_cast<int>(clip->samplers.size())) {
        continue;
      }
      const tydra::KeyframeSampler& smp =
          clip->samplers[static_cast<size_t>(ch.sampler)];
      const size_t j = static_cast<size_t>(ch.joint_id);
      animated[j] = true;
      if (ch.path == tydra::AnimationPath::Translation) {
        EvalSampler(smp, t, 3, &T[j * 3]);
      } else if (!translationOnly && ch.path == tydra::AnimationPath::Scale) {
        EvalSampler(smp, t, 3, &S[j * 3]);
      } else if (!translationOnly && ch.path == tydra::AnimationPath::Rotation) {
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
  *localOut = std::move(local);
}

bool BuildSkeletonPose(const tydra::RenderScene& render, int skelId, double t,
                       std::vector<matrix4d>* worldOut,
                       std::vector<matrix4d>* bindWorldOut) {
  if (!worldOut) return false;
  if (skelId < 0 || skelId >= static_cast<int>(render.skeletons.size())) {
    return false;
  }
  const tydra::SkelHierarchy& skel = render.skeletons[static_cast<size_t>(skelId)];
  const size_t nj = skel.num_joints();
  if (nj == 0 || skel.bind_transforms.size() != nj ||
      skel.rest_transforms.size() != nj) {
    return false;
  }

  const tydra::AnimationClip* clip = FindSkeletonClip(render, skel, skelId);
  const bool synthesizeBindFromAnimation =
      clip && nj > 1 && AllIdentity(skel.bind_transforms) &&
      AllIdentity(skel.rest_transforms);

  std::vector<matrix4d> bindWorld = skel.bind_transforms;
  std::vector<matrix4d> baseLocal = skel.rest_transforms;
  const bool pointJointSkeleton = IsPointJointSkeleton(skel);
  bool translationOnly = pointJointSkeleton;
  if (synthesizeBindFromAnimation) {
    const double bindTime = FirstSkeletonSampleTime(*clip, skelId);
    BuildJointLocals(clip, skelId, bindTime, skel.rest_transforms,
                     /*translationOnly=*/false, &baseLocal);
    if (!tydra::ConcatJointTransforms(skel.parent_joint_indices, baseLocal, &bindWorld)) {
      return false;
    }
  } else {
    std::vector<matrix4d> restWorld;
    if (tydra::ConcatJointTransforms(skel.parent_joint_indices,
                                     skel.rest_transforms, &restWorld)) {
      if (pointJointSkeleton) {
        bindWorld = std::move(restWorld);
      }
    }
  }

  std::vector<matrix4d> local;
  BuildJointLocals(clip, skelId, t, baseLocal, translationOnly, &local);

  if (!tydra::ConcatJointTransforms(skel.parent_joint_indices, local, worldOut)) {
    return false;
  }
  if (bindWorldOut) *bindWorldOut = std::move(bindWorld);
  return worldOut->size() == nj;
}

}  // namespace

bool BuildSkeletonJointWorlds(const tydra::RenderScene& render, int skelId,
                              double timecode,
                              std::vector<matrix4d>* worldOut) {
  return BuildSkeletonPose(render, skelId, timecode, worldOut, nullptr);
}

// Per-skeleton skinning matrices skinMat[j] = inverse(bind[j]) * posedWorld[j]
// (row-vector: a point in bind space maps to posed world). Joints without an
// animation channel keep their rest-pose local transform.
bool BuildSkinningMatrices(const tydra::RenderScene& render, int skelId,
                           double t, std::vector<matrix4d>* skinOut) {
  if (!skinOut) return false;
  if (skelId < 0 || skelId >= static_cast<int>(render.skeletons.size())) {
    return false;
  }
  const tydra::SkelHierarchy& skel = render.skeletons[static_cast<size_t>(skelId)];
  const size_t nj = skel.num_joints();
  if (nj == 0 || skel.bind_transforms.size() != nj) {
    return false;
  }

  std::vector<matrix4d> world;
  std::vector<matrix4d> bindWorld;
  if (!BuildSkeletonPose(render, skelId, t, &world, &bindWorld)) return false;

  skinOut->resize(nj);
  for (size_t j = 0; j < nj; ++j) {
    (*skinOut)[j] = tinyusdz::inverse(bindWorld[j]) * world[j];
  }
  return true;
}

namespace {

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

void NormalizeSkinWeights(std::vector<float>* weights, size_t pointCount,
                          int influencesPerPoint) {
  if (!weights || influencesPerPoint < 1) return;
  const size_t infl = static_cast<size_t>(influencesPerPoint);
  if (weights->size() != pointCount * infl) return;
  for (size_t pi = 0; pi < pointCount; ++pi) {
    const size_t base = pi * infl;
    double sum = 0.0;
    for (size_t k = 0; k < infl; ++k) {
      float& w = (*weights)[base + k];
      if (!(w > 0.0f) || !std::isfinite(w)) {
        w = 0.0f;
        continue;
      }
      sum += static_cast<double>(w);
    }
    if (sum <= 0.0) continue;
    const float inv = static_cast<float>(1.0 / sum);
    for (size_t k = 0; k < infl; ++k) (*weights)[base + k] *= inv;
  }
}

matrix4d MatrixFromDraw(const float m[16]) {
  matrix4d out = matrix4d::identity();
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      out.m[r][c] = static_cast<double>(m[r * 4 + c]);
    }
  }
  return out;
}

void PackMatrix(const matrix4d& m, int matrixIndex, SkinningFrameCPU* frame) {
  if (!frame || matrixIndex < 0) return;
  const size_t base = static_cast<size_t>(matrixIndex) * 16;
  if (base + 16 > frame->rgba32f.size()) return;
  for (int row = 0; row < 4; ++row) {
    for (int c = 0; c < 4; ++c) {
      frame->rgba32f[base + static_cast<size_t>(row) * 4 + static_cast<size_t>(c)] =
          static_cast<float>(m.m[row][c]);
    }
  }
}

point3f TransformPointRow(const point3f& p, const matrix4d& m) {
  const double x = p.x, y = p.y, z = p.z;
  double ox = x * m.m[0][0] + y * m.m[1][0] + z * m.m[2][0] + m.m[3][0];
  double oy = x * m.m[0][1] + y * m.m[1][1] + z * m.m[2][1] + m.m[3][1];
  double oz = x * m.m[0][2] + y * m.m[1][2] + z * m.m[2][2] + m.m[3][2];
  double ow = x * m.m[0][3] + y * m.m[1][3] + z * m.m[2][3] + m.m[3][3];
  if (std::abs(ow) > 1e-10) {
    ox /= ow;
    oy /= ow;
    oz /= ow;
  }
  return point3f{static_cast<float>(ox), static_cast<float>(oy),
                 static_cast<float>(oz)};
}

// Recompute smooth normals from `mv`'s (morphed) positions over `dm.indices`,
// then orient each to the rest normal (in dm.vertices) so winding is preserved.
void RegenNormalsOriented(const DrawMeshCPU& dm, std::vector<DrawVertex>* mv) {
  const size_t n = mv->size();
  std::vector<float> nrm(n * 3, 0.0f);
  for (size_t t = 0; t + 2 < dm.indices.size(); t += 3) {
    const uint32_t i0 = dm.indices[t], i1 = dm.indices[t + 1],
                   i2 = dm.indices[t + 2];
    if (i0 >= n || i1 >= n || i2 >= n) continue;
    const DrawVertex& a = (*mv)[i0];
    const DrawVertex& b = (*mv)[i1];
    const DrawVertex& c = (*mv)[i2];
    const float e1[3] = {b.px - a.px, b.py - a.py, b.pz - a.pz};
    const float e2[3] = {c.px - a.px, c.py - a.py, c.pz - a.pz};
    const float fn[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                         e1[2] * e2[0] - e1[0] * e2[2],
                         e1[0] * e2[1] - e1[1] * e2[0]};
    for (uint32_t idx : {i0, i1, i2}) {
      nrm[idx * 3 + 0] += fn[0];
      nrm[idx * 3 + 1] += fn[1];
      nrm[idx * 3 + 2] += fn[2];
    }
  }
  for (size_t i = 0; i < n; ++i) {
    float x = nrm[i * 3 + 0], y = nrm[i * 3 + 1], z = nrm[i * 3 + 2];
    const float len = std::sqrt(x * x + y * y + z * z);
    if (len <= 1e-8f) continue;  // degenerate: keep rest normal
    x /= len; y /= len; z /= len;
    const DrawVertex& rest = dm.vertices[i];
    if (x * rest.nx + y * rest.ny + z * rest.nz < 0.0f) { x = -x; y = -y; z = -z; }
    (*mv)[i].nx = x; (*mv)[i].ny = y; (*mv)[i].nz = z;
  }
}

void TransformPointWorld(const float m[16], const point3f& p, float out[3]) {
  out[0] = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
  out[1] = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
  out[2] = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
}

}  // namespace

bool SceneHasDeformation(const tydra::RenderScene& render) {
  for (const tydra::RenderMesh& m : render.meshes) {
    if (MeshIsSkinned(m) || !m.targets.empty()) return true;
  }
  return false;
}

bool SceneHasSkeletalSkinning(const tydra::RenderScene& render) {
  for (const tydra::RenderMesh& m : render.meshes) {
    if (MeshIsSkinned(m)) return true;
  }
  return false;
}

bool SceneHasBlendShapes(const tydra::RenderScene& render) {
  for (const tydra::RenderMesh& m : render.meshes) {
    if (!m.targets.empty()) return true;
  }
  return false;
}

bool SceneHasNonSkeletalAnimation(const tydra::RenderScene& render) {
  for (const tydra::AnimationClip& clip : render.animations) {
    for (const tydra::AnimationChannel& ch : clip.channels) {
      if (ch.target_type == tydra::ChannelTargetType::SkeletonJoint) continue;
      if (ch.sampler < 0 ||
          ch.sampler >= static_cast<int>(clip.samplers.size())) {
        continue;
      }
      if (clip.samplers[static_cast<size_t>(ch.sampler)].times.size() > 1) {
        return true;
      }
    }
  }
  return false;
}

int MaxSkinInfluenceCount(const tydra::RenderScene& render) {
  int maxInfluences = 0;
  for (const tydra::RenderMesh& m : render.meshes) {
    if (MeshIsSkinned(m)) maxInfluences = std::max(maxInfluences, m.joint_and_weights.elementSize);
  }
  return maxInfluences;
}

bool BuildGpuSkinningFrame(
    const tydra::RenderScene& render, const tinyusdz::Stage& stage,
    DrawScene* draw, double timecode, SkinningFrameCPU* frame,
    bool updateSkinnedHelpers,
    std::vector<std::pair<int, std::vector<DrawVertex>>>* morphedOut) {
  if (!draw || !frame) return false;
  const int matrices = draw->boneMatrixCount;
  if (matrices > 0) {
    frame->matrixCount = matrices;
    const size_t floats = static_cast<size_t>(matrices) * 16;
    if (frame->rgba32f.size() != floats) {
      frame->rgba32f.assign(floats, 0.0f);
    } else {
      std::fill(frame->rgba32f.begin(), frame->rgba32f.end(), 0.0f);
    }
    frame->enabled = true;
  } else {
    *frame = SkinningFrameCPU{};  // morph-only scene: no bone texture
  }

  // Blendshape weights at this time (gathered once; empty if no morphs wanted).
  std::unordered_map<std::string, float> blendWeights;
  bool gatheredBlend = false;
  // Morphed rest vertices per mesh (DrawVertex order), keyed by mesh index.
  // Used for both bounds and GPU re-upload; absent meshes use rest vertices.
  std::unordered_map<int, std::vector<DrawVertex>> morphed;
  if (morphedOut) {
    for (size_t mi = 0; mi < draw->meshes.size(); ++mi) {
      DrawMeshCPU& dm = draw->meshes[mi];
      if (dm.morphs.empty()) continue;
      if (!gatheredBlend) {
        blendWeights = GatherBlendWeights(stage, timecode);
        gatheredBlend = true;
      }
      std::vector<DrawVertex> mv = dm.vertices;  // start from rest
      bool anyMorph = false;
      for (const MorphTargetCPU& mt : dm.morphs) {
        auto wit = blendWeights.find(mt.name);
        if (wit == blendWeights.end() || wit->second == 0.0f) continue;
        const float w = wit->second;
        anyMorph = true;
        for (size_t e = 0; e < mt.vtx.size(); ++e) {
          const uint32_t v = mt.vtx[e];
          if (v >= mv.size()) continue;
          mv[v].px += w * mt.dpos[e * 3 + 0];
          mv[v].py += w * mt.dpos[e * 3 + 1];
          mv[v].pz += w * mt.dpos[e * 3 + 2];
        }
      }
      // Only meshes with an active (non-zero) weight need a posed buffer; an
      // unweighted mesh stays at its rest vertices (the caller reverts any mesh
      // that was morphed last frame). Regenerate smooth normals from the morphed
      // positions (matching the CPU path), oriented to the rest normal.
      if (anyMorph) {
        RegenNormalsOriented(dm, &mv);
        morphed.emplace(static_cast<int>(mi), std::move(mv));
      }
    }
  }
  // Mesh `mi`'s base (morphed-or-rest) vertices for bounds/skin reads.
  auto baseVerts = [&](int mi, const DrawMeshCPU& dm) -> const std::vector<DrawVertex>& {
    auto it = morphed.find(mi);
    return it != morphed.end() ? it->second : dm.vertices;
  };

  std::unordered_map<int, std::vector<matrix4d>> skinCache;
  std::unordered_map<int, std::vector<matrix4d>> composedByBase;

  for (const DrawMeshCPU& dm : draw->meshes) {
    if (dm.skelId < 0 || dm.skinMatrixBase < 0 || dm.jointIdx.empty()) continue;
    if (dm.skelId >= static_cast<int>(render.skeletons.size())) continue;
    auto cit = skinCache.find(dm.skelId);
    if (cit == skinCache.end()) {
      std::vector<matrix4d> sm;
      if (!BuildSkinningMatrices(render, dm.skelId, timecode, &sm)) continue;
      cit = skinCache.emplace(dm.skelId, std::move(sm)).first;
    }
    const matrix4d geomBind = MatrixFromDraw(dm.skinGeomBind);
    const matrix4d invGeomBind = tinyusdz::inverse(geomBind);
    std::vector<matrix4d> composed;
    composed.reserve(cit->second.size());
    for (size_t j = 0; j < cit->second.size(); ++j) {
      matrix4d m = geomBind * cit->second[j] * invGeomBind;
      composed.push_back(m);
      const int row = dm.skinMatrixBase + static_cast<int>(j);
      if (row < matrices) PackMatrix(m, row, frame);
    }
    composedByBase[dm.skinMatrixBase] = std::move(composed);
  }

  constexpr size_t kMaxSkinnedHelperSamplesPerMesh = 8192;
  bool sceneFirst = true;
  float sceneMn[3] = {std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max(),
                      std::numeric_limits<float>::max()};
  float sceneMx[3] = {-std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max(),
                      -std::numeric_limits<float>::max()};
  auto updateScene = [&](const float mn[3], const float mx[3]) {
    for (int c = 0; c < 3; ++c) {
      sceneMn[c] = std::min(sceneMn[c], mn[c]);
      sceneMx[c] = std::max(sceneMx[c], mx[c]);
    }
    sceneFirst = false;
  };
  for (size_t mi = 0; mi < draw->meshes.size(); ++mi) {
    DrawMeshCPU& dm = draw->meshes[mi];
    const std::vector<DrawVertex>& verts = baseVerts(static_cast<int>(mi), dm);
    bool meshFirst = true;
    float mn[3] = {std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max(),
                   std::numeric_limits<float>::max()};
    float mx[3] = {-std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max(),
                   -std::numeric_limits<float>::max()};

    auto update = [&](const point3f& local) {
      float w[3];
      TransformPointWorld(dm.world, local, w);
      for (int c = 0; c < 3; ++c) {
        mn[c] = std::min(mn[c], w[c]);
        mx[c] = std::max(mx[c], w[c]);
      }
      meshFirst = false;
    };

    const bool skinned = dm.skelId >= 0 && dm.skinMatrixBase >= 0 &&
                         dm.jointIdx.size() == verts.size() * 4 &&
                         dm.jointWt.size() == verts.size() * 4;
    const auto bit = skinned ? composedByBase.find(dm.skinMatrixBase)
                             : composedByBase.end();
    const bool extendedSkinned =
        skinned && dm.influenceOffsetCount.size() == verts.size() * 2 &&
        !dm.influenceTexels.empty() && dm.influenceTexels.size() % 4 == 0;
    dm.skinnedHelperPoints.clear();
    if (!skinned && !extendedSkinned &&
        morphed.find(static_cast<int>(mi)) == morphed.end()) {
      if (dm.aabbMin[0] <= dm.aabbMax[0] && dm.aabbMin[1] <= dm.aabbMax[1] &&
          dm.aabbMin[2] <= dm.aabbMax[2]) {
        updateScene(dm.aabbMin, dm.aabbMax);
      }
      continue;
    } else if ((!skinned && !extendedSkinned) || bit == composedByBase.end()) {
      for (const DrawVertex& v : verts) {
        update(point3f{v.px, v.py, v.pz});
      }
    } else {
      const size_t sampleStep =
          updateSkinnedHelpers && verts.size() > kMaxSkinnedHelperSamplesPerMesh
              ? (verts.size() + kMaxSkinnedHelperSamplesPerMesh - 1) /
                    kMaxSkinnedHelperSamplesPerMesh
              : 1;
      if (updateSkinnedHelpers) {
        dm.skinnedHelperPoints.reserve(((verts.size() + sampleStep - 1) / sampleStep) * 3);
      }
      const std::vector<matrix4d>& mats = bit->second;
      for (size_t vi = 0; vi < verts.size(); ++vi) {
        const DrawVertex& v = verts[vi];
        const point3f p{v.px, v.py, v.pz};
        point3f acc{0.0f, 0.0f, 0.0f};
        float sum = 0.0f;
        if (extendedSkinned) {
          const uint32_t offset = dm.influenceOffsetCount[vi * 2 + 0];
          const uint32_t count = dm.influenceOffsetCount[vi * 2 + 1];
          const size_t texelCount = dm.influenceTexels.size() / 4;
          for (uint32_t k = 0; k < count; ++k) {
            const size_t texel = static_cast<size_t>(offset) + k;
            if (texel >= texelCount) break;
            const size_t base = texel * 4;
            const float w = dm.influenceTexels[base + 1];
            if (w <= 0.0f) continue;
            const uint32_t absIdx =
                static_cast<uint32_t>(std::max(0.0f, dm.influenceTexels[base] + 0.5f));
            if (absIdx < static_cast<uint32_t>(dm.skinMatrixBase)) continue;
            const size_t localIdx = static_cast<size_t>(absIdx - dm.skinMatrixBase);
            if (localIdx >= mats.size()) continue;
            const point3f q = TransformPointRow(p, mats[localIdx]);
            acc.x += q.x * w;
            acc.y += q.y * w;
            acc.z += q.z * w;
            sum += w;
          }
        } else {
          for (size_t k = 0; k < 4; ++k) {
            const float w = dm.jointWt[vi * 4 + k];
            if (w <= 0.0f) continue;
            const uint32_t absIdx = dm.jointIdx[vi * 4 + k];
            if (absIdx < static_cast<uint32_t>(dm.skinMatrixBase)) continue;
            const size_t localIdx = static_cast<size_t>(absIdx - dm.skinMatrixBase);
            if (localIdx >= mats.size()) continue;
            const point3f q = TransformPointRow(p, mats[localIdx]);
            acc.x += q.x * w;
            acc.y += q.y * w;
            acc.z += q.z * w;
            sum += w;
          }
        }
        const point3f skinnedPoint = sum > 0.0f ? acc : p;
        if (updateSkinnedHelpers && (vi % sampleStep) == 0) {
          dm.skinnedHelperPoints.push_back(skinnedPoint.x);
          dm.skinnedHelperPoints.push_back(skinnedPoint.y);
          dm.skinnedHelperPoints.push_back(skinnedPoint.z);
        }
        update(skinnedPoint);
      }
    }
    if (!meshFirst) {
      for (int c = 0; c < 3; ++c) {
        dm.aabbMin[c] = mn[c];
        dm.aabbMax[c] = mx[c];
      }
      updateScene(dm.aabbMin, dm.aabbMax);
    }
  }
  draw->hasBounds = !sceneFirst;
  if (draw->hasBounds) {
    for (int c = 0; c < 3; ++c) {
      draw->aabbMin[c] = sceneMn[c];
      draw->aabbMax[c] = sceneMx[c];
    }
  }

  if (morphedOut) {
    morphedOut->clear();
    for (auto& kv : morphed) {
      morphedOut->emplace_back(kv.first, std::move(kv.second));
    }
  }
  return true;
}

namespace {
void FlattenXformWorlds(const tydra::XformNode& n,
                        std::unordered_map<std::string, matrix4d>& out) {
  out[n.absolute_path.full_path_name()] = n.get_world_matrix();
  for (const tydra::XformNode& c : n.children) FlattenXformWorlds(c, out);
}
}  // namespace

bool UpdateAnimatedMeshWorlds(const tinyusdz::Stage& stage, DrawScene* draw,
                              double timecode) {
  if (!draw) return false;
  tydra::XformNode root;
  if (!tydra::BuildXformNodeFromStage(stage, &root, timecode)) return false;
  std::unordered_map<std::string, matrix4d> worlds;
  FlattenXformWorlds(root, worlds);
  bool changed = false;
  for (DrawMeshCPU& dm : draw->meshes) {
    auto it = worlds.find(dm.absPath);
    if (it == worlds.end()) continue;
    float w[16];
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        w[i * 4 + j] = static_cast<float>(it->second.m[i][j]);
      }
    }
    if (std::memcmp(dm.world, w, sizeof(w)) != 0) {
      std::memcpy(dm.world, w, sizeof(w));
      changed = true;
    }
  }
  return changed;
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
      if (jw.jointIndices.size() == expect && jw.jointWeights.size() == expect) {
        std::vector<float> weights(jw.jointWeights.size());
        for (size_t i = 0; i < weights.size(); ++i) weights[i] = jw.jointWeights[i];
        NormalizeSkinWeights(&weights, np, infl);
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
