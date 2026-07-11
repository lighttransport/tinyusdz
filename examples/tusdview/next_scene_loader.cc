// SPDX-License-Identifier: Apache-2.0
// tusdview - `next` loader -> tydra-next RenderScene -> DrawScene adapter, plus
// PointInstancer extraction into GPU-instanced draws.

#include "next_scene_loader.hh"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.hh"

// `next` + tydra-next (built on demand; see CMakeLists.txt).
#include "next/tinyusdz-next.hh"
#include "next/reader/usdz-reader.hh"  // USDZReader (embedded --next textures)
#include "next/schema/usd-shade.hh"    // GetInheritedBoundMaterialPath
#include "next/schema/usd-skel.hh"     // GetSkeletonData / GetSkelAnimationData
#include "tydra/scene-access.hh"       // SkinPointsLBS / ConcatJointTransforms
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-extract.hh"
#include "tydra/next/texture-cache.hh"  // shared decode + size cap + byte budget
#include "tydra/next/scene-access.hh"  // ComputeWorldTransform
#include "value-types.hh"              // value::matrix4d / quatf / double3
#include "xform.hh"                    // to_matrix3x3 / to_matrix / inverse
#include "io-util.hh"                  // io::GetBaseDir / SplitUDIMPath
#include "tydra/texture-util.hh"       // tydra::ResizeImage (UDIM tile normalize)
#include "image-loader.hh"             // DomeLight envmap decode (IBL bake)
#include "mesh_build.hh"               // UpdatePreviewLight
#include "lightrt_mtlx_bridge.hh"      // BakeLightRtOpenPBR (next material bake)
#include "texture_tools.hh"            // TexToolsBuildDomeIbl / ProbeToEquirect
#include "usdVol.hh"                   // OpenVDB (.vdb) loader

#include <chrono>

namespace tusdview {

namespace tydn = ::tinyusdz::tydra::next;
namespace tnext = ::tinyusdz::next;
using matrix4d = ::tinyusdz::value::matrix4d;

namespace {

// Pack a tinyusdz row-major matrix4d (m[row][col], row-vector p*M) into a 3x4
// object-to-world (12 floats): row k holds the coefficients of output component
// k, i.e. worldP.k = dot(vec4(p,1), o2w_row_k). Matches tusdrender Mat4ToObj2World
// and the instanced vertex shader's aRow0/1/2.
inline void Mat4dToO2W(const matrix4d& m, float out[12]) {
  for (int k = 0; k < 3; ++k) {
    out[k * 4 + 0] = static_cast<float>(m.m[0][k]);
    out[k * 4 + 1] = static_cast<float>(m.m[1][k]);
    out[k * 4 + 2] = static_cast<float>(m.m[2][k]);
    out[k * 4 + 3] = static_cast<float>(m.m[3][k]);
  }
}

inline matrix4d Mat4dFromArray(const double d[16]) {
  matrix4d m;
  for (int i = 0; i < 4; ++i)
    for (int j = 0; j < 4; ++j) m.m[i][j] = d[i * 4 + j];
  return m;
}

// Row-major matrix multiply matching tinyusdz value::Mult: (a*b).m[j][i] =
// sum_k a.m[j][k]*b.m[k][i]. Row-vector convention: p*(a*b) applies `a` first.
inline matrix4d Mul4(const matrix4d& a, const matrix4d& b) {
  matrix4d r;
  for (int j = 0; j < 4; ++j)
    for (int i = 0; i < 4; ++i) {
      double v = 0.0;
      for (int k = 0; k < 4; ++k) v += a.m[j][k] * b.m[k][i];
      r.m[j][i] = v;
    }
  return r;
}

// Per-instance local transform from position + orientation quaternion (xyzw) +
// scale, matching tusdrender's InstanceTRS (p * S * R, translation in row 3).
inline matrix4d InstanceTRS(const float* pos, const float* q_xyzw,
                            const float* s3) {
  ::tinyusdz::value::quatf q;
  q.imag[0] = q_xyzw[0];
  q.imag[1] = q_xyzw[1];
  q.imag[2] = q_xyzw[2];
  q.real = q_xyzw[3];
  ::tinyusdz::value::matrix3d rot = ::tinyusdz::to_matrix3x3(q);
  for (int i = 0; i < 3; ++i)
    for (int j = 0; j < 3; ++j) rot.m[i][j] *= static_cast<double>(s3[i]);
  ::tinyusdz::value::double3 t{static_cast<double>(pos[0]),
                               static_cast<double>(pos[1]),
                               static_cast<double>(pos[2])};
  return ::tinyusdz::to_matrix(rot, t);
}

// Lazy array readers: try the time sample then the default opinion; materialize a
// lazy (mmap-backed) value. Mirror tusdrender's ReadFloatArrayLazy.
std::vector<float> ReadFloats(const tnext::UsdPrim& p, const char* name, double t) {
  return tydn::ReadFloatArrayCopy(p, name, t);
}
std::vector<int32_t> ReadInts(const tnext::UsdPrim& p, const char* name, double t) {
  return tydn::ReadIntArrayCopy(p, name, t);
}
std::vector<int64_t> ReadInt64s(const tnext::UsdPrim& p, const char* name, double t) {
  return tydn::ReadInt64ArrayCopy(p, name, t);
}

bool PointInstanceHidden(size_t index, size_t instance_count,
                         const tydn::ValueArrayRead<int64_t>& ids,
                         const std::unordered_set<int64_t>& hidden) {
  if (hidden.empty()) return false;
  if (ids.size() == instance_count) return hidden.count(ids[index]) != 0;
  if (index > static_cast<size_t>(std::numeric_limits<int64_t>::max())) {
    return false;
  }
  return hidden.count(static_cast<int64_t>(index)) != 0;
}

// Linearly-interpolated float-array read across time samples (the next stage's
// GetValueAtTime/GetInterpolatedValue snap to the nearest sample for arrays).
// Brackets `t` between the two surrounding samples and lerps element-wise; falls
// back to the plain read (default opinion / single sample / no samples).
std::vector<float> ReadFloatsLerp(const tnext::UsdPrim& p, const char* name,
                                  double t) {
  const std::vector<double> times = p.GetTimeSampleTimes(name);
  if (times.size() < 2) return ReadFloats(p, name, t);
  if (t <= times.front()) return ReadFloats(p, name, times.front());
  if (t >= times.back()) return ReadFloats(p, name, times.back());
  size_t hi = 0;
  while (hi < times.size() && times[hi] < t) ++hi;
  const double t0 = times[hi - 1], t1 = times[hi];
  const std::vector<float> a = ReadFloats(p, name, t0);
  const std::vector<float> b = ReadFloats(p, name, t1);
  if (a.size() != b.size() || t1 <= t0) return a;
  const float f = static_cast<float>((t - t0) / (t1 - t0));
  std::vector<float> out(a.size());
  for (size_t i = 0; i < a.size(); ++i) out[i] = a[i] + f * (b[i] - a[i]);
  return out;
}
std::vector<std::string> ReadTokens(const tnext::UsdPrim& p, const char* name,
                                    double t) {
  auto pull = [](const tnext::Value* v) -> std::vector<std::string> {
    if (!v) return {};
    if (v->is_lazy()) {
      tnext::Value tmp = v->materialized_copy();
      if (const auto* a = tmp.as_token_array()) return *a;
      return {};
    }
    if (const auto* a = v->as_token_array()) return *a;
    return {};
  };
  std::vector<std::string> r = pull(p.GetValueAtTime(name, t));
  if (r.empty()) r = pull(p.GetPropertyValue(name));
  return r;
}

// One tydra-next float vertex attribute, sampled per triangulated corner in
// whatever interpolation it was authored with.
struct NextAttr {
  const tydn::FloatChunked* data{nullptr};
  const tydn::UInt32Chunked* indices{nullptr};  // indexed primvars; may be null
  tydn::Interpolation interp{tydn::Interpolation::Vertex};
  uint32_t comps{0};

  explicit operator bool() const { return data && comps > 0 && !data->empty(); }

  // Element index for a corner, given its point id, its authored face-vertex
  // (corner) index, and its face id. SIZE_MAX when out of range.
  size_t element(uint32_t pointId, uint32_t cornerId, uint32_t faceId) const {
    size_t e = 0;
    switch (interp) {
      case tydn::Interpolation::Constant: e = 0; break;
      case tydn::Interpolation::Uniform: e = faceId; break;
      case tydn::Interpolation::FaceVarying: e = cornerId; break;
      case tydn::Interpolation::Vertex:
      case tydn::Interpolation::Varying:
      default: e = pointId; break;
    }
    if (indices && !indices->empty()) {
      if (e >= indices->size()) return SIZE_MAX;
      e = (*indices)[e];
    }
    if ((e + 1) * comps > data->size()) return SIZE_MAX;
    return e;
  }

  // Read up to `n` components into `out` (zero-filled on a miss).
  void read(uint32_t pointId, uint32_t cornerId, uint32_t faceId, uint32_t n,
            float* out) const {
    for (uint32_t c = 0; c < n; ++c) out[c] = 0.0f;
    if (!*this) return;
    const size_t e = element(pointId, cornerId, faceId);
    if (e == SIZE_MAX) return;
    for (uint32_t c = 0; c < std::min(n, comps); ++c) {
      out[c] = (*data)[e * comps + c];
    }
  }
};

NextAttr MakeNextAttr(const tydn::FloatChunked& data, tydn::Interpolation interp,
                      uint32_t comps) {
  NextAttr a;
  if (!data.empty() && comps > 0) {
    a.data = &data;
    a.interp = interp;
    a.comps = comps;
  }
  return a;
}

// Find a generic primvar by name and expose it as a NextAttr.
NextAttr FindNextPrimvar(const tydn::RenderMesh& m, const char* name,
                         uint32_t comps) {
  for (const tydn::VertexAttribute& pv : m.primvars) {
    if (pv.name != name || pv.float_data.empty()) continue;
    NextAttr a;
    a.data = &pv.float_data;
    a.indices = pv.has_indices() ? &pv.indices : nullptr;
    a.interp = pv.interpolation;
    a.comps = comps;
    return a;
  }
  return NextAttr{};
}

// Build interleaved DrawVertex geometry (mesh-LOCAL space) + indices from a
// tydra-next RenderMesh. Returns false if there is no renderable geometry.
//
// tydra-next keeps every primvar in its AUTHORED interpolation (constant /
// uniform / vertex / varying / faceVarying) and hands us
// `triangulated_face_vertex_indices` to index the faceVarying ones against the
// triangulated topology. Production USD overwhelmingly authors faceVarying `st`
// and `normals`, so we resolve all five interpolations here and WELD the
// corners: a point is split into multiple DrawVertex entries only where its
// attributes actually differ (a UV seam or a hard edge). Naive per-corner
// expansion would multiply a quad mesh's vertex count ~4x, which is exactly the
// VRAM we are trying not to spend.
//
// `vertexToPoint` receives the source point id per emitted vertex, since the
// weld breaks the old vertex-i == point-i invariant that the skinning,
// blendshape, and wireframe passes relied on.
bool FillFlatGeometry(const tydn::RenderMesh& m, DrawMeshCPU* dm,
                      std::vector<uint32_t>* vertexToPoint) {
  const size_t np = m.point_count();
  const size_t ncorners = m.triangulated_indices.size();
  if (np == 0 || ncorners < 3) return false;

  // faceVarying lookups need the corner remap; without it, treat faceVarying
  // attributes as absent rather than reading garbage.
  const bool haveCornerMap =
      m.triangulated_face_vertex_indices.size() == ncorners;

  const NextAttr nrm = MakeNextAttr(m.normals, m.normals_interp, 3);
  const NextAttr uv0 = MakeNextAttr(m.texcoords_0, m.texcoords_0_interp, 2);
  const NextAttr uv1 = MakeNextAttr(m.texcoords_1, m.texcoords_1_interp, 2);
  // displayColor is color3f, but tydra-next also accepts a 4-component authoring
  // (rgba); the 4th component is folded into the alpha channel below. Component
  // count follows from the element count its interpolation implies.
  auto expectedElems = [&](tydn::Interpolation interp) -> size_t {
    switch (interp) {
      case tydn::Interpolation::Constant: return 1;
      case tydn::Interpolation::Uniform: return m.face_count();
      case tydn::Interpolation::FaceVarying: return m.face_vertex_indices.size();
      default: return np;
    }
  };
  uint32_t colorComps = 3;
  if (!m.colors.empty()) {
    const size_t elems = expectedElems(m.colors_interp);
    if (elems > 0 && m.colors.size() == elems * 4) colorComps = 4;
  }
  const NextAttr col = MakeNextAttr(m.colors, m.colors_interp, colorComps);
  // displayOpacity is not a tydra-next builtin: it lands in the generic primvar
  // bag as a float attribute.
  const NextAttr opacity = FindNextPrimvar(m, "displayOpacity", 1);
  // Tangents are computed only for normal-mapped meshes (see the tangent-aware
  // converter in LoadUSDViaNext); xyzw with w = handedness.
  const NextAttr tan = MakeNextAttr(m.tangents, m.tangents_interp, 4);

  auto usesFaceVarying = [&](const NextAttr& a) {
    return a && a.interp == tydn::Interpolation::FaceVarying;
  };
  if (!haveCornerMap &&
      (usesFaceVarying(nrm) || usesFaceVarying(uv0) || usesFaceVarying(uv1) ||
       usesFaceVarying(col) || usesFaceVarying(opacity) ||
       usesFaceVarying(tan))) {
    return false;  // triangulation did not produce a usable corner remap
  }

  // Authored normals (in any interpolation) -> smooth shading; otherwise shade
  // geometrically in the shader (screen-derivative normal), which reads
  // correctly on hard surfaces instead of being smeared by averaged normals.
  dm->geometricNormal = !static_cast<bool>(nrm);

  // Uniform (per-face) attributes need a corner -> face lookup. Only pay for it
  // when something is actually authored that way.
  auto usesUniform = [&](const NextAttr& a) {
    return a && a.interp == tydn::Interpolation::Uniform;
  };
  std::vector<uint32_t> cornerToFace;
  if (usesUniform(nrm) || usesUniform(uv0) || usesUniform(uv1) ||
      usesUniform(col) || usesUniform(opacity) || usesUniform(tan)) {
    const size_t nfaces = m.face_vertex_counts.size();
    size_t authoredCorners = 0;
    for (size_t f = 0; f < nfaces; ++f) authoredCorners += m.face_vertex_counts[f];
    cornerToFace.resize(authoredCorners);
    size_t off = 0;
    for (size_t f = 0; f < nfaces; ++f) {
      const uint32_t c = m.face_vertex_counts[f];
      for (uint32_t k = 0; k < c && off < authoredCorners; ++k, ++off) {
        cornerToFace[off] = static_cast<uint32_t>(f);
      }
    }
  }

  const bool wantColors = static_cast<bool>(col);
  const bool wantAlpha =
      static_cast<bool>(opacity) || (wantColors && colorComps == 4);
  const bool wantUv1 = static_cast<bool>(uv1);
  const bool wantTangents = static_cast<bool>(tan);

  dm->name = m.name;
  dm->absPath = m.prim_path;

  // Weld: per point, a short chain of already-emitted variants. Almost every
  // point has one; seams add a second. This is much cheaper in both time and
  // peak memory than hashing a full attribute tuple per corner.
  std::vector<int32_t> firstVariant(np, -1);
  std::vector<int32_t> nextVariant;
  std::vector<uint32_t>& v2p = *vertexToPoint;
  v2p.clear();

  nextVariant.reserve(np);
  v2p.reserve(np);
  dm->vertices.reserve(np);
  dm->indices.resize(ncorners);

  auto sameFloats = [](const float* a, const float* b, uint32_t n) {
    for (uint32_t i = 0; i < n; ++i) {
      if (a[i] != b[i]) return false;
    }
    return true;
  };

  for (size_t c = 0; c < ncorners; ++c) {
    const uint32_t pid = m.triangulated_indices[c];
    if (pid >= np) {  // sanitized upstream, but never index out of bounds
      dm->indices[c] = 0;
      continue;
    }
    const uint32_t cornerId =
        haveCornerMap ? m.triangulated_face_vertex_indices[c] : pid;
    const uint32_t faceId = (cornerToFace.empty() || cornerId >= cornerToFace.size())
                                ? 0u
                                : cornerToFace[cornerId];

    float n[3], t0[2], t1[2], rgb[4], a = 1.0f, tg[4];
    nrm.read(pid, cornerId, faceId, 3, n);
    uv0.read(pid, cornerId, faceId, 2, t0);
    uv1.read(pid, cornerId, faceId, 2, t1);
    if (wantColors) {
      col.read(pid, cornerId, faceId, colorComps, rgb);
    } else {
      rgb[0] = rgb[1] = rgb[2] = 1.0f;
      rgb[3] = 1.0f;
    }
    if (colorComps == 4 && wantColors) a = rgb[3];
    if (opacity) {
      float o[1];
      opacity.read(pid, cornerId, faceId, 1, o);
      a = o[0];
    }
    if (wantTangents) tan.read(pid, cornerId, faceId, 4, tg);

    // Flip V: USD `st` has v=0 at the image bottom, but decoded images are
    // top-row-first and uploaded so v=0 samples the top, so invert here (same as
    // the legacy path in mesh_build.cc).
    const float u0 = t0[0], v0 = 1.0f - t0[1];
    const float u1 = t1[0], v1 = 1.0f - t1[1];

    int32_t found = -1;
    for (int32_t vi = firstVariant[pid]; vi >= 0; vi = nextVariant[size_t(vi)]) {
      const DrawVertex& cand = dm->vertices[size_t(vi)];
      if (cand.nx != n[0] || cand.ny != n[1] || cand.nz != n[2]) continue;
      if (cand.u != u0 || cand.v != v0) continue;
      if (wantUv1 && (dm->uv1[size_t(vi) * 2 + 0] != u1 ||
                      dm->uv1[size_t(vi) * 2 + 1] != v1)) {
        continue;
      }
      if (wantColors &&
          !sameFloats(&dm->vertexColors[size_t(vi) * 3], rgb, 3)) {
        continue;
      }
      if (wantAlpha && dm->vertexAlpha[size_t(vi)] != a) continue;
      if (wantTangents &&
          !sameFloats(&dm->tangents[size_t(vi) * 3], tg, 3)) {
        continue;
      }
      found = vi;
      break;
    }

    if (found < 0) {
      found = static_cast<int32_t>(dm->vertices.size());
      DrawVertex v;
      v.px = m.points[3 * pid + 0];
      v.py = m.points[3 * pid + 1];
      v.pz = m.points[3 * pid + 2];
      v.nx = n[0]; v.ny = n[1]; v.nz = n[2];
      v.u = u0; v.v = v0;
      dm->vertices.push_back(v);
      if (wantUv1) { dm->uv1.push_back(u1); dm->uv1.push_back(v1); }
      if (wantColors) {
        dm->vertexColors.push_back(rgb[0]);
        dm->vertexColors.push_back(rgb[1]);
        dm->vertexColors.push_back(rgb[2]);
      }
      if (wantAlpha) dm->vertexAlpha.push_back(a);
      if (wantTangents) {
        dm->tangents.push_back(tg[0]);
        dm->tangents.push_back(tg[1]);
        dm->tangents.push_back(tg[2]);
        // Binormal from the handedness sign, so the shader gets a full basis.
        const float w = tg[3] < 0.0f ? -1.0f : 1.0f;
        dm->binormals.push_back((n[1] * tg[2] - n[2] * tg[1]) * w);
        dm->binormals.push_back((n[2] * tg[0] - n[0] * tg[2]) * w);
        dm->binormals.push_back((n[0] * tg[1] - n[1] * tg[0]) * w);
      }
      nextVariant.push_back(firstVariant[pid]);
      v2p.push_back(pid);
      firstVariant[pid] = found;
    }
    dm->indices[c] = static_cast<uint32_t>(found);
  }

  if (dm->vertices.empty()) return false;
  dm->submeshes.push_back(
      DrawSubmesh{0, static_cast<uint32_t>(dm->indices.size()), 0});

  // Original-polygon wireframe edges: the perimeter of each USD face, from the
  // pre-triangulation topology. This shows quads/ngons (not triangulation
  // diagonals) and is correct even for double-sided meshes (whose triangulation
  // doubles the tri count, defeating any per-triangle scheme). Point ids map to
  // their first emitted variant -- every variant of a point shares its position,
  // so any of them draws the same edge.
  {
    const std::vector<uint32_t> fvc = m.face_vertex_counts.flatten();
    const std::vector<uint32_t> fvi = m.face_vertex_indices.flatten();
    if (!fvc.empty() && !fvi.empty()) {
      std::unordered_set<uint64_t> seen;
      seen.reserve(fvi.size());
      std::vector<uint32_t>& wire = dm->wireframeIndices;
      wire.reserve(fvi.size() * 2);
      size_t off = 0;
      bool ok = true;
      for (uint32_t c : fvc) {
        if (off + c > fvi.size()) { ok = false; break; }
        for (uint32_t k = 0; k < c; ++k) {
          const uint32_t pa = fvi[off + k];
          const uint32_t pb = fvi[off + (k + 1u) % c];
          if (pa == pb || pa >= np || pb >= np) continue;
          const int32_t va = firstVariant[pa], vb = firstVariant[pb];
          if (va < 0 || vb < 0) continue;
          const uint32_t a = static_cast<uint32_t>(va);
          const uint32_t b = static_cast<uint32_t>(vb);
          const uint64_t key =
              a < b ? (uint64_t(a) << 32 | b) : (uint64_t(b) << 32 | a);
          if (seen.insert(key).second) { wire.push_back(a); wire.push_back(b); }
        }
        off += c;
      }
      if (!ok) wire.clear();
    }
  }
  return true;
}

struct Bounds {
  bool has = false;
  float mn[3]{0, 0, 0}, mx[3]{0, 0, 0};
  void add(const float p[3]) {
    if (!has) { for (int k = 0; k < 3; ++k) mn[k] = mx[k] = p[k]; has = true; }
    else {
      for (int k = 0; k < 3; ++k) {
        mn[k] = std::min(mn[k], p[k]); mx[k] = std::max(mx[k], p[k]);
      }
    }
  }
};

// Resolve a prim's inherited USD `purpose` (default/render/proxy/guide): the
// nearest authored, non-"default" purpose walking self->ancestors, else
// "default". Matches mesh_build.cc ResolveInheritedPurpose for the next stage.
std::string ResolveNextPurpose(const tnext::Stage& stage, const std::string& abs) {
  std::string p = abs;
  while (!p.empty()) {
    tnext::UsdPrim prim = stage.GetPrimAtPath(p);
    if (prim.IsValid()) {
      if (const tnext::Value* v = prim.GetPropertyValue("purpose")) {
        if (const std::string* t = v->as_token()) {
          if (!t->empty() && *t != "default" && *t != "inherited") return *t;
        }
      }
    }
    if (p == "/") break;
    const size_t slash = p.find_last_of('/');
    p = (slash == std::string::npos || slash == 0) ? "/" : p.substr(0, slash);
  }
  return "default";
}

// Resolve the SkelAnimation that drives a mesh's blendshapes, returning a
// blendShape-name -> weight map. The next converter emits no skel/morph data, so
// we read straight from the stage: prefer a `skel:animationSource` relationship
// (walking the mesh's ancestors, which is where SkelRoot/Skeleton authors it),
// else fall back to scanning the enclosing SkelRoot subtree for a SkelAnimation
// prim. Empty map => everything stays at rest. `time` picks the time sample.
std::unordered_map<std::string, float> ResolveBlendWeights(
    const tnext::Stage& stage, const tnext::UsdPrim& meshPrim, double time) {
  std::unordered_map<std::string, float> out;

  // Find the SkelAnimation prim.
  tnext::UsdPrim anim;
  tnext::UsdPrim skelRoot;
  for (tnext::UsdPrim a = meshPrim; a.IsValid(); a = a.GetParent()) {
    if (const std::vector<tnext::Path>* src =
            a.GetRelationship("skel:animationSource")) {
      if (!src->empty()) {
        tnext::UsdPrim cand = stage.GetPrimAtPath((*src)[0]);
        if (cand.IsValid() && cand.GetTypeName() == "SkelAnimation") {
          anim = cand;
          break;
        }
      }
    }
    if (a.GetTypeName() == "SkelRoot") skelRoot = a;
    if (a.GetPath().str() == "/") break;
  }
  // Fallback: first SkelAnimation under the enclosing SkelRoot.
  if (!anim.IsValid() && skelRoot.IsValid()) {
    std::function<tnext::UsdPrim(const tnext::UsdPrim&)> find =
        [&](const tnext::UsdPrim& p) -> tnext::UsdPrim {
      if (p.GetTypeName() == "SkelAnimation") return p;
      for (const tnext::UsdPrim& c : p.GetChildren()) {
        tnext::UsdPrim r = find(c);
        if (r.IsValid()) return r;
      }
      return tnext::UsdPrim();
    };
    anim = find(skelRoot);
  }
  if (!anim.IsValid()) return out;

  const std::vector<std::string> names = ReadTokens(anim, "blendShapes", time);
  // Linearly-interpolated weights so morph animates smoothly between time
  // samples (static scenes fall back to the default opinion).
  const std::vector<float> weights =
      ReadFloatsLerp(anim, "blendShapeWeights", time);
  for (size_t i = 0; i < names.size() && i < weights.size(); ++i)
    out[names[i]] = weights[i];
  return out;
}

// In-between samples of a `--next` BlendShape prim, read from its `inbetweens:*`
// attributes (vector3f[] offsets parallel to the prim's pointIndices, plus a
// `weight` attr-meta). Returned sorted ascending by weight. Mirrors
// ReadInbetweensFromPrim in skinning.cc for the next stage.
std::vector<std::pair<float, std::vector<float>>> ReadInbetweens(
    const tnext::UsdPrim& bs, double time) {
  std::vector<std::pair<float, std::vector<float>>> out;
  const tnext::PrimSpec* spec = bs.GetPrimSpec();
  if (!spec) return out;
  for (const std::string& name : bs.GetPropertyNames()) {
    if (name.rfind("inbetweens:", 0) != 0) continue;  // namespace prefix
    const tnext::PropMeta* pm = spec->property_meta(name);
    if (!pm || !(pm->authored & tnext::PropMeta::kWeight)) continue;
    std::vector<float> offs = ReadFloats(bs, name.c_str(), time);
    if (offs.empty()) continue;
    out.emplace_back(static_cast<float>(pm->weight), std::move(offs));
  }
  std::sort(out.begin(), out.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
  return out;
}

// Bracket a target weight `w` within the implied sample table {0, ibWeights...,
// 1} (ibWeights ascending). Returns table indices [lo, hi] (0 == implicit rest,
// last == primary) + lerp parameter t (extrapolates outside [0,1]). Identical to
// FindMorphBracket in skinning.cc -- keeps the static bake bit-for-bit with the
// GPU-morph coeff eval. With no in-betweens it degrades to {lo:0, hi:1, t:w},
// i.e. a plain linear primary scale.
struct MorphBracket { int lo; int hi; float t; };
MorphBracket FindMorphBracket(const std::vector<float>& ibWeights, float w) {
  const int N = static_cast<int>(ibWeights.size());
  auto wAt = [&](int i) -> float {
    return i == 0 ? 0.0f : (i == N + 1 ? 1.0f : ibWeights[i - 1]);
  };
  int hi = 1;
  while (hi < N + 1 && w > wAt(hi)) ++hi;
  const int lo = hi - 1;
  const float denom = wAt(hi) - wAt(lo);
  const float t = denom > 1e-12f ? (w - wAt(lo)) / denom : 0.0f;
  return {lo, hi, t};
}

// Bake blendshape (morph) targets into the prototype's local vertex positions
// once at load. The `--next` instanced path has no GPU morph, so resolving the
// morph into geometry here makes the existing flat instanced GL/VK path render N
// instances of the morphed prototype with no shader/attribute changes. The morph
// is per-prototype (shared by all instances), and `--next` is a static preview,
// so load-time weights suffice. `dm->vertices` is point-indexed (vertex i == point
// i), matching the BlendShape `pointIndices` which index authored points.
//
// USD in-between samples are interpolated (piecewise-lerp via FindMorphBracket,
// matching the GPU-morph coeff eval); without in-betweens this is a plain linear
// primary scale. Limitation (acceptable for a static preview): load-time weights
// only -- no animated morph. Authored smooth normals are recomputed below.
void RecomputeSmoothNormalsNext(DrawMeshCPU* dm);

void BakeBlendShapes(const tnext::Stage& stage, const tnext::UsdPrim& meshPrim,
                     double time, DrawMeshCPU* dm,
                     const std::vector<uint32_t>& vertexToPoint,
                     size_t numPoints) {
  const std::vector<std::string> shapeNames =
      ReadTokens(meshPrim, "skel:blendShapes", time);
  const std::vector<tnext::Path>* targets =
      meshPrim.GetRelationship("skel:blendShapeTargets");
  if (shapeNames.empty() || !targets || targets->empty()) return;

  const std::unordered_map<std::string, float> weights =
      ResolveBlendWeights(stage, meshPrim, time);
  if (weights.empty()) return;

  const size_t nv = dm->vertices.size();
  if (nv == 0 || numPoints == 0) return;
  if (!vertexToPoint.empty() && vertexToPoint.size() != nv) return;
  // Offsets are accumulated per authored POINT (that is what BlendShape
  // `pointIndices` index), then scattered onto the welded vertices.
  const size_t np = numPoints;
  std::vector<float> delta(3 * np, 0.0f);
  bool any = false;

  const size_t n = std::min(shapeNames.size(), targets->size());
  for (size_t i = 0; i < n; ++i) {
    auto it = weights.find(shapeNames[i]);
    if (it == weights.end() || std::fabs(it->second) < 1e-8f) continue;
    const float w = it->second;
    tnext::UsdPrim bs = stage.GetPrimAtPath((*targets)[i]);
    if (!bs.IsValid()) continue;
    const std::vector<float> primary = ReadFloats(bs, "offsets", time);
    const std::vector<int32_t> pointIndices = ReadInts(bs, "pointIndices", time);
    const size_t m = primary.size() / 3;
    if (m == 0) continue;

    // Sample table = [in-betweens (ascending)..., primary]; FindMorphBracket
    // gives the two table entries (rest index 0 contributes nothing) + lerp t.
    const std::vector<std::pair<float, std::vector<float>>> ib =
        ReadInbetweens(bs, time);
    std::vector<float> ibW;
    ibW.reserve(ib.size());
    for (const auto& s : ib) ibW.push_back(s.first);
    const MorphBracket br = FindMorphBracket(ibW, w);
    // Table index k in [1..N+1] -> array entry k-1 (in-between k-1, or primary).
    auto sampleAt = [&](int tableIdx) -> const std::vector<float>* {
      const int a = tableIdx - 1;
      if (a < 0) return nullptr;
      return (a < int(ib.size())) ? &ib[size_t(a)].second : &primary;
    };
    const std::vector<float>* sLo = br.lo >= 1 ? sampleAt(br.lo) : nullptr;
    const std::vector<float>* sHi = br.hi >= 1 ? sampleAt(br.hi) : nullptr;
    const float wLo = 1.0f - br.t, wHi = br.t;

    for (size_t k = 0; k < m; ++k) {
      // Absent pointIndices => offsets are per-point for all points (USD rule).
      const int64_t pidx =
          pointIndices.empty()
              ? int64_t(k)
              : (k < pointIndices.size() ? int64_t(pointIndices[k]) : -1);
      if (pidx < 0 || size_t(pidx) >= np) continue;
      float d[3] = {0, 0, 0};
      if (sLo && 3 * k + 2 < sLo->size())
        for (int c = 0; c < 3; ++c) d[c] += wLo * (*sLo)[3 * k + c];
      if (sHi && 3 * k + 2 < sHi->size())
        for (int c = 0; c < 3; ++c) d[c] += wHi * (*sHi)[3 * k + c];
      delta[3 * pidx + 0] += d[0];
      delta[3 * pidx + 1] += d[1];
      delta[3 * pidx + 2] += d[2];
      any = true;
    }
  }
  if (!any) return;
  for (size_t i = 0; i < nv; ++i) {
    const size_t p = vertexToPoint.empty() ? i : size_t(vertexToPoint[i]);
    if (p >= np) continue;
    dm->vertices[i].px += delta[3 * p + 0];
    dm->vertices[i].py += delta[3 * p + 1];
    dm->vertices[i].pz += delta[3 * p + 2];
  }

  // Recompute smooth vertex normals from the baked positions so authored-normal
  // (smooth-shaded) meshes don't keep stale rest normals. Geometric-shaded meshes
  // re-derive normals in the shader and ignore the attribute, so skip them.
  RecomputeSmoothNormalsNext(dm);
}

// Recompute area-weighted smooth vertex normals from positions (mirrors the
// BakeBlendShapes tail). Geometric-shaded meshes re-derive normals in the
// shader, so skip them.
void RecomputeSmoothNormalsNext(DrawMeshCPU* dm) {
  if (dm->geometricNormal) return;
  const size_t np = dm->vertices.size();
  for (size_t i = 0; i < np; ++i)
    dm->vertices[i].nx = dm->vertices[i].ny = dm->vertices[i].nz = 0.0f;
  for (size_t t = 0; t + 2 < dm->indices.size(); t += 3) {
    const uint32_t a = dm->indices[t], b = dm->indices[t + 1], c = dm->indices[t + 2];
    if (a >= np || b >= np || c >= np) continue;
    const DrawVertex& va = dm->vertices[a];
    const DrawVertex& vb = dm->vertices[b];
    const DrawVertex& vc = dm->vertices[c];
    const float e1x = vb.px - va.px, e1y = vb.py - va.py, e1z = vb.pz - va.pz;
    const float e2x = vc.px - va.px, e2y = vc.py - va.py, e2z = vc.pz - va.pz;
    const float fnx = e1y * e2z - e1z * e2y, fny = e1z * e2x - e1x * e2z,
                fnz = e1x * e2y - e1y * e2x;
    for (uint32_t v : {a, b, c}) {
      dm->vertices[v].nx += fnx; dm->vertices[v].ny += fny; dm->vertices[v].nz += fnz;
    }
  }
  for (size_t i = 0; i < np; ++i) {
    DrawVertex& v = dm->vertices[i];
    const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
    if (len > 1e-12f) { const float inv = 1.0f / len; v.nx *= inv; v.ny *= inv; v.nz *= inv; }
  }
}

tnext::UsdPrim FindSkeletonInSubtree(const tnext::UsdPrim& root) {
  if (tinyusdz::next::IsSkeleton(root)) return root;
  for (const tnext::UsdPrim& c : root.GetChildren()) {
    tnext::UsdPrim r = FindSkeletonInSubtree(c);
    if (r.IsValid()) return r;
  }
  return tnext::UsdPrim();
}

// Find the Skeleton bound to a skinned mesh: explicit skel:skeleton rel, else
// the Skeleton under the enclosing SkelRoot ancestor.
tnext::UsdPrim FindBoundSkeletonNext(const tnext::Stage& stage,
                                     const tnext::UsdPrim& meshPrim) {
  if (const std::vector<tnext::Path>* rel = meshPrim.GetRelationship("skel:skeleton")) {
    if (!rel->empty()) {
      tnext::UsdPrim s = stage.GetPrimAtPath((*rel)[0]);
      if (s.IsValid() && tinyusdz::next::IsSkeleton(s)) return s;
    }
  }
  tnext::UsdPrim p = meshPrim.GetParent();
  while (p.IsValid()) {
    if (p.GetTypeName() == "SkelRoot") {
      if (const std::vector<tnext::Path>* rel = p.GetRelationship("skel:skeleton")) {
        if (!rel->empty()) {
          tnext::UsdPrim s = stage.GetPrimAtPath((*rel)[0]);
          if (s.IsValid() && tinyusdz::next::IsSkeleton(s)) return s;
        }
      }
      tnext::UsdPrim found = FindSkeletonInSubtree(p);
      if (found.IsValid()) return found;
    }
    p = p.GetParent();
  }
  return tnext::UsdPrim();
}

// Find the SkelAnimation driving a skeleton: its animationSource, else a
// skel:animationSource rel on the mesh's ancestors.
tnext::UsdPrim FindSkelAnimationNext(const tnext::Stage& stage,
                                     const tnext::UsdPrim& meshPrim,
                                     const tinyusdz::next::SkeletonData& skel) {
  if (skel.hasAnimationSource && !skel.animationSource.empty()) {
    tnext::UsdPrim a = stage.GetPrimAtPath(skel.animationSource);
    if (a.IsValid() && tinyusdz::next::IsSkelAnimation(a)) return a;
  }
  tnext::UsdPrim p = meshPrim;
  while (p.IsValid()) {
    if (const std::vector<tnext::Path>* rel = p.GetRelationship("skel:animationSource")) {
      if (!rel->empty()) {
        tnext::UsdPrim a = stage.GetPrimAtPath((*rel)[0]);
        if (a.IsValid() && tinyusdz::next::IsSkelAnimation(a)) return a;
      }
    }
    p = p.GetParent();
  }
  return tnext::UsdPrim();
}

// Joint-local transform from TRS (row-vector; matches skinning.cc MakeLocal).
matrix4d SkinMakeLocal(const float t[3], const ::tinyusdz::value::quatf& r,
                       const float s[3]) {
  matrix4d m = ::tinyusdz::to_matrix(r);
  m.m[0][0] *= s[0]; m.m[0][1] *= s[0]; m.m[0][2] *= s[0];
  m.m[1][0] *= s[1]; m.m[1][1] *= s[1]; m.m[1][2] *= s[1];
  m.m[2][0] *= s[2]; m.m[2][1] *= s[2]; m.m[2][2] *= s[2];
  m.m[3][0] = t[0]; m.m[3][1] = t[1]; m.m[3][2] = t[2];
  return m;
}

// A mesh's POSE-INDEPENDENT skin binding: the bound skeleton/animation, the
// geomBindTransform, and the per-VERTEX influences. Resolved once at load and
// consumed either by the CPU bake (BakeSkinning) or by the GPU path, which keeps
// the influences as vertex attributes and re-poses the skeleton every frame
// (SetupGpuSkinNext / BuildNextSkinningFrame).
struct NextSkinBinding {
  std::string skelPath;
  std::string animPath;  // "" = no animation (rest pose)
  size_t numJoints = 0;
  int numInfl = 0;
  matrix4d geomBind = matrix4d::identity();
  std::vector<int> vidx;    // nv * numInfl, in SKELETON joint order
  std::vector<float> vwgt;  // nv * numInfl
};

// false = not skinned, or the skin data is missing/inconsistent (callers then
// leave the mesh in its rest pose).
bool ResolveNextSkinBinding(const tnext::Stage& stage,
                            const tnext::UsdPrim& meshPrim, double time,
                            size_t nv,
                            const std::vector<uint32_t>& vertexToPoint,
                            size_t numPoints, NextSkinBinding* out) {
  if (!out || nv == 0) return false;
  std::vector<int32_t> ji = ReadInts(meshPrim, "primvars:skel:jointIndices", time);
  std::vector<float> jw = ReadFloats(meshPrim, "primvars:skel:jointWeights", time);
  if (ji.empty() || ji.size() != jw.size()) return false;
  // skel:jointIndices/Weights are authored per POINT; the weld may have split
  // points into several vertices, so influences are gathered through
  // `vertexToPoint` below rather than read at the vertex index.
  if (numPoints == 0 || ji.size() % numPoints != 0) return false;
  const int numInfl = static_cast<int>(ji.size() / numPoints);
  if (numInfl <= 0) return false;
  if (!vertexToPoint.empty() && vertexToPoint.size() != nv) return false;

  tnext::UsdPrim skelPrim = FindBoundSkeletonNext(stage, meshPrim);
  if (!skelPrim.IsValid()) return false;
  tinyusdz::next::SkeletonData skel;
  if (!tinyusdz::next::GetSkeletonData(stage, skelPrim, &skel)) return false;
  const size_t nj = skel.joints.size();
  if (nj == 0) return false;

  // Remap mesh-authored joint order into skeleton order (when authored).
  std::vector<int> idx(ji.begin(), ji.end());
  std::vector<std::string> meshJoints = ReadTokens(meshPrim, "primvars:skel:joints", time);
  if (!meshJoints.empty()) {
    std::unordered_map<std::string, int> skelIdx;
    for (size_t j = 0; j < nj; ++j) skelIdx[skel.joints[j]] = static_cast<int>(j);
    std::vector<int> remap(meshJoints.size(), -1);
    for (size_t i = 0; i < meshJoints.size(); ++i) {
      auto it = skelIdx.find(meshJoints[i]);
      if (it != skelIdx.end()) remap[i] = it->second;
    }
    for (int& v : idx) {
      v = (v >= 0 && v < static_cast<int>(remap.size())) ? remap[v] : -1;
      if (v < 0) return false;  // unresolved joint -> leave rest pose (safe)
    }
  }
  for (int& v : idx)
    if (v < 0 || v >= static_cast<int>(nj)) v = 0;  // clamp stray indices

  // geomBindTransform (single matrix4d; identity when absent).
  matrix4d geomBind = matrix4d::identity();
  if (const tnext::Value* gv =
          meshPrim.GetPropertyValue("primvars:skel:geomBindTransform")) {
    tnext::Value tmp;
    const tnext::Value* v = gv;
    if (gv->is_lazy()) { tmp = gv->materialized_copy(); v = &tmp; }
    if (const double* d = v->as_matrix4d()) geomBind = Mat4dFromArray(d);
  }

  // Gather the per-point influences onto the (possibly welded) vertex array, so
  // every variant of a split point is skinned by that point's weights.
  std::vector<int> vidx(nv * size_t(numInfl));
  std::vector<float> vwgt(nv * size_t(numInfl));
  for (size_t i = 0; i < nv; ++i) {
    const size_t p = vertexToPoint.empty() ? i : size_t(vertexToPoint[i]);
    if (p >= numPoints) return false;
    for (int k = 0; k < numInfl; ++k) {
      vidx[i * size_t(numInfl) + size_t(k)] = idx[p * size_t(numInfl) + size_t(k)];
      vwgt[i * size_t(numInfl) + size_t(k)] = jw[p * size_t(numInfl) + size_t(k)];
    }
  }

  tnext::UsdPrim animPrim = FindSkelAnimationNext(stage, meshPrim, skel);
  out->skelPath = skelPrim.GetPath().str();
  out->animPath = animPrim.IsValid() ? animPrim.GetPath().str() : std::string();
  out->numJoints = nj;
  out->numInfl = numInfl;
  out->geomBind = geomBind;
  out->vidx = std::move(vidx);
  out->vwgt = std::move(vwgt);
  return true;
}

// Pose a skeleton at `time`: skinMat[j] carries a bind-space point (i.e. one the
// geomBindTransform has already been applied to) into the posed skeleton space.
// Row-vector convention, matching tydra::SkinPointsLBS.
bool PoseNextSkeleton(const tnext::Stage& stage, const std::string& skelPath,
                      const std::string& animPath, double time,
                      std::vector<matrix4d>* skinMat) {
  if (!skinMat) return false;
  tnext::UsdPrim skelPrim = stage.GetPrimAtPath(skelPath);
  if (!skelPrim.IsValid()) return false;
  tinyusdz::next::SkeletonData skel;
  if (!tinyusdz::next::GetSkeletonData(stage, skelPrim, &skel)) return false;
  const size_t nj = skel.joints.size();
  if (nj == 0) return false;

  std::vector<int> topo;
  std::string terr;
  if (!tinyusdz::next::BuildSkelTopology(skel.joints, topo, &terr) ||
      topo.size() != nj) {
    return false;
  }

  const bool haveRest = skel.restTransforms.size() == nj * 16;
  const bool haveBind = skel.bindTransforms.size() == nj * 16;
  std::vector<matrix4d> restLocal(nj), bindWorld(nj);
  for (size_t j = 0; j < nj; ++j) {
    restLocal[j] = haveRest ? Mat4dFromArray(&skel.restTransforms[j * 16])
                            : matrix4d::identity();
    bindWorld[j] = haveBind ? Mat4dFromArray(&skel.bindTransforms[j * 16])
                            : matrix4d::identity();
  }

  // Animated local transforms: default each joint's TRS from its rest local (so
  // a partial animation keeps rest offsets), override with the SkelAnimation.
  std::vector<matrix4d> local = restLocal;
  tnext::UsdPrim animPrim =
      animPath.empty() ? tnext::UsdPrim() : stage.GetPrimAtPath(animPath);
  if (animPrim.IsValid()) {
    tinyusdz::next::SkelAnimationData anim;
    if (tinyusdz::next::GetSkelAnimationData(stage, animPrim, &anim, time) &&
        !anim.joints.empty()) {

      std::unordered_map<std::string, int> skelIdx;
      for (size_t j = 0; j < nj; ++j) skelIdx[skel.joints[j]] = static_cast<int>(j);
      for (size_t a = 0; a < anim.joints.size(); ++a) {
        auto it = skelIdx.find(anim.joints[a]);
        if (it == skelIdx.end()) continue;
        const int j = it->second;
        float t3[3] = {0, 0, 0}, s3[3] = {1, 1, 1};
        ::tinyusdz::value::quatf q;
        q.imag[0] = q.imag[1] = q.imag[2] = 0.0f; q.real = 1.0f;
        ::tinyusdz::value::double3 dt, ds;
        ::tinyusdz::value::quatd dq;
        if (::tinyusdz::decompose(restLocal[j], &dt, &dq, &ds)) {
          t3[0] = float(dt[0]); t3[1] = float(dt[1]); t3[2] = float(dt[2]);
          s3[0] = float(ds[0]); s3[1] = float(ds[1]); s3[2] = float(ds[2]);
          q.imag[0] = float(dq.imag[0]); q.imag[1] = float(dq.imag[1]);
          q.imag[2] = float(dq.imag[2]); q.real = float(dq.real);
        }
        if (anim.hasTranslations && (a + 1) * 3 <= anim.translations.size()) {
          t3[0] = anim.translations[a * 3 + 0];
          t3[1] = anim.translations[a * 3 + 1];
          t3[2] = anim.translations[a * 3 + 2];
        }
        if (anim.hasRotations && (a + 1) * 4 <= anim.rotations.size()) {
          // next's canonical quat layout is REAL-FIRST (w, x, y, z) -- the crate
          // reader swizzles disk's imaginary-first order into it (see
          // CrateReader::Impl::UnpackQuatf), and ASCII parses in authored order.
          q.real = anim.rotations[a * 4 + 0];
          q.imag[0] = anim.rotations[a * 4 + 1];
          q.imag[1] = anim.rotations[a * 4 + 2];
          q.imag[2] = anim.rotations[a * 4 + 3];
        }
        if (anim.hasScales && (a + 1) * 3 <= anim.scales.size()) {
          s3[0] = anim.scales[a * 3 + 0];
          s3[1] = anim.scales[a * 3 + 1];
          s3[2] = anim.scales[a * 3 + 2];
        }
        local[j] = SkinMakeLocal(t3, q, s3);
      }
    }
  }

  std::vector<matrix4d> world;
  if (!tinyusdz::tydra::ConcatJointTransforms(topo, local, &world) ||
      world.size() != nj) {
    return false;
  }
  // Synthesize the bind pose from the rest world transform when bind is absent.
  if (!haveBind) {
    std::vector<matrix4d> restWorld;
    if (tinyusdz::tydra::ConcatJointTransforms(topo, restLocal, &restWorld) &&
        restWorld.size() == nj) {
      bindWorld = std::move(restWorld);
    }
  }
  skinMat->assign(nj, matrix4d::identity());
  for (size_t j = 0; j < nj; ++j)
    (*skinMat)[j] = ::tinyusdz::inverse(bindWorld[j]) * world[j];
  return true;
}

// Load-time skeletal skinning bake: pose the bound skeleton at `time` and LBS-
// deform dm->vertices (rest, point-indexed) in place, then recompute normals.
// The CPU-skinning path (and the CPU ray tracers, which read this geometry).
// No-op -- leaves the rest pose -- on any missing/mismatched skin/skeleton data.
// Returns true when the mesh was actually skinned (so the caller knows its
// vertices are ONE pose of an animated rig, not static geometry).
bool BakeSkinning(const tnext::Stage& stage, const tnext::UsdPrim& meshPrim,
                  double time, DrawMeshCPU* dm,
                  const std::vector<uint32_t>& vertexToPoint,
                  size_t numPoints) {
  if (!dm || dm->vertices.empty()) return false;
  const size_t nv = dm->vertices.size();
  NextSkinBinding bind;
  if (!ResolveNextSkinBinding(stage, meshPrim, time, nv, vertexToPoint,
                              numPoints, &bind)) {
    return false;
  }
  std::vector<matrix4d> skinMat;
  if (!PoseNextSkeleton(stage, bind.skelPath, bind.animPath, time, &skinMat)) {
    return false;
  }

  std::vector<::tinyusdz::value::point3f> rest(nv), skinned;
  for (size_t i = 0; i < nv; ++i) {
    rest[i].x = dm->vertices[i].px;
    rest[i].y = dm->vertices[i].py;
    rest[i].z = dm->vertices[i].pz;
  }
  std::string lerr;
  if (!tinyusdz::tydra::SkinPointsLBS(rest, bind.geomBind, skinMat, bind.vidx,
                                      bind.vwgt, bind.numInfl, &skinned, &lerr) ||
      skinned.size() != nv) {
    return false;
  }
  // Skin the NORMALS with the same blended matrix the GPU vertex shader uses,
  // rather than regenerating a smooth normal field from the posed positions:
  // the two disagree wherever the pose bends the surface, and the CPU and GPU
  // skinning paths must render the same image (tusdview-skinning-screenshot-diff).
  const matrix4d invGeomBind = ::tinyusdz::inverse(bind.geomBind);
  std::vector<matrix4d> composed(skinMat.size());
  for (size_t j = 0; j < skinMat.size(); ++j)
    composed[j] = bind.geomBind * skinMat[j] * invGeomBind;

  for (size_t i = 0; i < nv; ++i) {
    dm->vertices[i].px = skinned[i].x;
    dm->vertices[i].py = skinned[i].y;
    dm->vertices[i].pz = skinned[i].z;

    const float n[3] = {dm->vertices[i].nx, dm->vertices[i].ny,
                        dm->vertices[i].nz};
    double acc[3] = {0.0, 0.0, 0.0};
    double wsum = 0.0;
    for (int k = 0; k < bind.numInfl; ++k) {
      const float w = bind.vwgt[i * size_t(bind.numInfl) + size_t(k)];
      if (!(w > 0.0f)) continue;
      const int j = bind.vidx[i * size_t(bind.numInfl) + size_t(k)];
      if (j < 0 || j >= static_cast<int>(composed.size())) continue;
      const matrix4d& m = composed[size_t(j)];
      for (int c = 0; c < 3; ++c) {  // row-vector, rotation part only
        acc[c] += double(w) * (double(n[0]) * m.m[0][c] +
                               double(n[1]) * m.m[1][c] +
                               double(n[2]) * m.m[2][c]);
      }
      wsum += double(w);
    }
    if (wsum <= 0.0) continue;
    const double len =
        std::sqrt(acc[0] * acc[0] + acc[1] * acc[1] + acc[2] * acc[2]);
    if (len <= 1e-12) continue;
    dm->vertices[i].nx = static_cast<float>(acc[0] / len);
    dm->vertices[i].ny = static_cast<float>(acc[1] / len);
    dm->vertices[i].nz = static_cast<float>(acc[2] / len);
  }
  return true;
}

// GPU skinning alternative to BakeSkinning: keep the mesh in its REST pose and
// emit per-vertex joint attributes + a bone-matrix block, so the vertex shader
// poses it every frame. `worldM` is the mesh world transform the caller is about
// to bake into the vertices (row-vector, row-major); it is folded into the bone
// matrices instead of the attributes -- see BuildNextSkinningFrame.
//
// The GPU attribute path carries the 4 strongest influences per vertex (the
// shader's fixed 4-wide skin); meshes authored with more are renormalized onto
// those 4, matching mesh_build.cc's Tydra-path behavior.
// Returns false when the mesh is not skinned (caller leaves it alone).
bool SetupGpuSkinNext(const tnext::Stage& stage, const tnext::UsdPrim& meshPrim,
                      double time, DrawMeshCPU* dm,
                      const std::vector<uint32_t>& vertexToPoint,
                      size_t numPoints, const double worldM[16],
                      DrawScene* draw) {
  if (!dm || dm->vertices.empty() || !draw) return false;
  const size_t nv = dm->vertices.size();
  NextSkinBinding bind;
  if (!ResolveNextSkinBinding(stage, meshPrim, time, nv, vertexToPoint,
                              numPoints, &bind)) {
    return false;
  }
  const int nj = static_cast<int>(bind.numJoints);
  if (nj <= 0) return false;
  // Guard the bone-texture row space (int rows, absolute indices).
  if (draw->boneMatrixCount > std::numeric_limits<int>::max() - nj) return false;

  const int base = draw->boneMatrixCount;
  const int ni = bind.numInfl;
  dm->jointIdx.assign(nv * 4, 0u);
  dm->jointWt.assign(nv * 4, 0.0f);
  for (size_t v = 0; v < nv; ++v) {
    // Top-4 influences by weight.
    std::array<std::pair<float, int>, 4> top{};  // (weight, joint)
    for (auto& t : top) t = {0.0f, 0};
    for (int k = 0; k < ni; ++k) {
      const float w = bind.vwgt[v * size_t(ni) + size_t(k)];
      if (!(w > 0.0f)) continue;
      const int j = bind.vidx[v * size_t(ni) + size_t(k)];
      // Insertion sort into the 4-slot top list.
      for (int s = 0; s < 4; ++s) {
        if (w > top[size_t(s)].first) {
          for (int t = 3; t > s; --t) top[size_t(t)] = top[size_t(t - 1)];
          top[size_t(s)] = {w, j};
          break;
        }
      }
    }
    float sum = 0.0f;
    for (const auto& t : top) sum += t.first;
    for (int s = 0; s < 4; ++s) {
      dm->jointIdx[v * 4 + size_t(s)] =
          static_cast<uint32_t>(base + top[size_t(s)].second);
      dm->jointWt[v * 4 + size_t(s)] =
          sum > 0.0f ? top[size_t(s)].first / sum : 0.0f;
    }
  }

  DrawScene::NextSkelBinding nb;
  nb.skelPath = bind.skelPath;
  nb.animPath = bind.animPath;
  nb.meshPath = meshPrim.GetPath().str();
  nb.numJoints = nj;
  nb.matrixBase = base;
  for (int r = 0; r < 4; ++r)
    for (int c = 0; c < 4; ++c) nb.geomBind[r * 4 + c] = bind.geomBind.m[r][c];
  for (int k = 0; k < 16; ++k) nb.world[k] = worldM[k];
  draw->nextSkels.push_back(std::move(nb));
  draw->boneMatrixCount = base + nj;
  return true;
}

// Build GPU-morph CSR channels for a prototype mesh, so the instanced raster
// shader morphs per-frame from a tiny per-channel coefficient buffer instead of
// the morph being baked into geometry. Mirrors BuildMorphChannels in
// mesh_build.cc, reading directly from the next stage. A channel = one delta
// stream (an in-between sample or the primary); per target the channels are
// [in-betweens ascending..., primary] with usdWeights [ibWeights..., 1.0],
// matching EvalMorphChannelCoeffs' bracket eval. No-op (no channels) when the
// mesh has no resolvable blendshape targets.
//
// BlendShape `pointIndices` index authored POINTS, and FillFlatGeometry's weld
// can back one point with several vertices (UV seams / hard edges), so each
// delta entry fans out to every vertex of its point.
void BuildMorphChannelsNext(const tnext::Stage& stage,
                            const tnext::UsdPrim& meshPrim, double time,
                            DrawMeshCPU* dm,
                            const std::vector<uint32_t>& vertexToPoint,
                            size_t numPoints) {
  const std::vector<std::string> shapeNames =
      ReadTokens(meshPrim, "skel:blendShapes", time);
  const std::vector<tnext::Path>* targets =
      meshPrim.GetRelationship("skel:blendShapeTargets");
  if (shapeNames.empty() || !targets || targets->empty()) return;

  const size_t nv = dm->vertices.size();
  const size_t np = numPoints;
  if (nv == 0 || np == 0) return;
  if (!vertexToPoint.empty() && vertexToPoint.size() != nv) return;

  // point -> its welded vertices, as a CSR (counting sort over vertexToPoint).
  std::vector<uint32_t> pvOffset(np + 1, 0u);
  std::vector<uint32_t> pvVerts(nv, 0u);
  {
    for (size_t i = 0; i < nv; ++i) {
      const size_t p = vertexToPoint.empty() ? i : size_t(vertexToPoint[i]);
      if (p < np) pvOffset[p + 1]++;
    }
    for (size_t p = 0; p < np; ++p) pvOffset[p + 1] += pvOffset[p];
    std::vector<uint32_t> cur(pvOffset.begin(), pvOffset.end() - 1);
    for (size_t i = 0; i < nv; ++i) {
      const size_t p = vertexToPoint.empty() ? i : size_t(vertexToPoint[i]);
      if (p < np) pvVerts[cur[p]++] = static_cast<uint32_t>(i);
    }
  }

  // One delta stream per channel: its offsets (3/entry) + the point indices the
  // entries map to (empty => identity 0..M-1). Streams own their data so the
  // sparse target reads can be freed before the CSR scatter.
  struct Chan {
    int id;
    std::vector<float> offsets;       // 3 * M
    const std::vector<int32_t>* pidx; // M (points into `pidxStore`)
  };
  std::vector<Chan> chans;
  std::vector<std::vector<int32_t>> pidxStore;  // stable addresses for Chan::pidx
  pidxStore.reserve(targets->size());
  int nextChannel = 0;
  dm->morphTargetChannels.clear();

  const size_t n = std::min(shapeNames.size(), targets->size());
  for (size_t i = 0; i < n; ++i) {
    tnext::UsdPrim bs = stage.GetPrimAtPath((*targets)[i]);
    if (!bs.IsValid()) continue;
    std::vector<float> primary = ReadFloats(bs, "offsets", time);
    if (primary.size() < 3) continue;
    pidxStore.push_back(ReadInts(bs, "pointIndices", time));
    const std::vector<int32_t>* pidx = &pidxStore.back();
    std::vector<std::pair<float, std::vector<float>>> ib =
        ReadInbetweens(bs, time);

    MorphTargetChannelsCPU tc;
    tc.name = shapeNames[i];
    for (auto& s : ib) {  // in-betweens ascending
      const int ch = nextChannel++;
      tc.usdWeights.push_back(s.first);
      tc.channelIds.push_back(ch);
      chans.push_back({ch, std::move(s.second), pidx});
    }
    const int chPrimary = nextChannel++;  // primary == weight 1.0
    tc.usdWeights.push_back(1.0f);
    tc.channelIds.push_back(chPrimary);
    chans.push_back({chPrimary, std::move(primary), pidx});
    dm->morphTargetChannels.push_back(std::move(tc));
  }
  if (chans.empty()) return;
  dm->morphChannelCount = nextChannel;

  // The entry's target POINT (or -1 to skip); `fanout` visits every welded
  // vertex of that point.
  auto ptOf = [np](const Chan& c, size_t e) -> int64_t {
    const int64_t p = c.pidx->empty() ? int64_t(e) : int64_t((*c.pidx)[e]);
    return (p >= 0 && size_t(p) < np && e * 3 + 2 < c.offsets.size()) ? p : -1;
  };
  auto fanout = [&](int64_t p, const std::function<void(uint32_t)>& fn) {
    for (uint32_t k = pvOffset[size_t(p)]; k < pvOffset[size_t(p) + 1]; ++k) {
      fn(pvVerts[k]);
    }
  };

  // Pass 1: count entries per vertex. M = offsets/3 (== pidx size when present).
  std::vector<uint32_t> count(nv, 0u);
  for (const Chan& c : chans) {
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e) {
      const int64_t p = ptOf(c, e);
      if (p < 0) continue;
      fanout(p, [&](uint32_t v) { count[v]++; });
    }
  }
  // Prefix-sum into morphOffsetCount (offset,count per vertex).
  dm->morphOffsetCount.assign(nv * 2, 0u);
  uint64_t total = 0;
  for (size_t v = 0; v < nv; ++v) {
    dm->morphOffsetCount[v * 2 + 0] = static_cast<uint32_t>(total);
    dm->morphOffsetCount[v * 2 + 1] = count[v];
    total += count[v];
  }
  // Pass 2: scatter [channelId, dx, dy, dz] halfs + the uint16 channelId side
  // buffer (the shader's active-channel skip pre-check).
  auto h = [](float f) { return tinyusdz::value::float_to_half_full(f).value; };
  dm->morphDeltaHalf.assign(total * 4, 0);
  dm->morphChannelId.assign(total, 0);
  std::vector<uint32_t> cursor(nv, 0u);
  for (const Chan& c : chans) {
    const uint16_t chHalf = h(static_cast<float>(c.id));
    const uint16_t chId = static_cast<uint16_t>(c.id);
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e) {
      const int64_t p = ptOf(c, e);
      if (p < 0) continue;
      fanout(p, [&](uint32_t v) {
        const uint64_t slot = dm->morphOffsetCount[size_t(v) * 2 + 0] + cursor[v]++;
        uint16_t* o = &dm->morphDeltaHalf[slot * 4];
        o[0] = chHalf;
        o[1] = h(c.offsets[e * 3 + 0]);
        o[2] = h(c.offsets[e * 3 + 1]);
        o[3] = h(c.offsets[e * 3 + 2]);
        dm->morphChannelId[slot] = chId;
      });
    }
  }

  // Max per-axis morph displacement, to pad protoAabb for per-instance culling.
  // Conservative: per point, sum each axis's positive and negative deltas across
  // ALL channels (worst case = every channel at full coefficient), then take the
  // largest absolute swing. Safe superset (over-pads, never culls a visible
  // morphed instance); small overdrive (weight > 1) is not bounded.
  std::vector<float> sumPos(np * 3, 0.0f), sumNeg(np * 3, 0.0f);
  for (const Chan& c : chans) {
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e) {
      const int64_t p = ptOf(c, e);
      if (p < 0) continue;
      for (int a = 0; a < 3; ++a) {
        const float d = c.offsets[e * 3 + a];
        (d >= 0.0f ? sumPos : sumNeg)[size_t(p) * 3 + a] += d;
      }
    }
  }
  for (size_t p = 0; p < np; ++p)
    for (int a = 0; a < 3; ++a)
      dm->morphExtent[a] = std::max(
          dm->morphExtent[a],
          std::max(sumPos[p * 3 + a], -sumNeg[p * 3 + a]));
}

// Build a prototype mesh's local geometry (+ flat displayColor) from the
// converter, and its mesh-local -> proto-root-local transform `mesh_rel`. Shared
// by the PointInstancer and native-instance passes. Returns false if the mesh has
// no converter geometry.
bool BuildProtoMesh(const tnext::Stage& stage, tydn::RenderSceneConverter& conv,
                    const tnext::UsdPrim& mp, const matrix4d& inv_protoroot,
                    double time, DrawMeshCPU* dm, matrix4d* mesh_rel,
                    std::vector<uint32_t>* out_vertexToPoint,
                    size_t* out_numPoints) {
  // Convert just this mesh on demand (streaming) -- avoids holding the whole
  // RenderScene in RAM.
  tydn::RenderMesh rm;
  if (!conv.ConvertMesh(stage, mp, &rm)) return false;
  std::vector<uint32_t> vertexToPoint;
  if (!FillFlatGeometry(rm, dm, &vertexToPoint)) return false;
  const size_t numPoints = rm.point_count();
  // Skinning is resolved by the CALLER (it alone knows the instance count, which
  // decides GPU-skin vs static bake), so hand the weld map back out.
  if (out_numPoints) *out_numPoints = numPoints;
  // Blendshapes on the prototype. Default: build GPU-morph channels so the
  // instanced raster shader morphs per-frame (animated weights). Opt-out
  // (TUSDVIEW_NEXT_MORPH_BAKE=1): bake the morph into geometry at load -- a
  // static, lower-overhead path (no per-frame GPU morph, no morph buffers) for
  // huge static scenes. Mutually exclusive so morph is never applied twice. Both
  // no-op for non-blendshaped meshes.
  static const bool kBakeMorph = [] {
    const char* e = std::getenv("TUSDVIEW_NEXT_MORPH_BAKE");
    return e && e[0] == '1';
  }();
  if (kBakeMorph)
    BakeBlendShapes(stage, mp, time, dm, vertexToPoint, numPoints);
  else
    BuildMorphChannelsNext(stage, mp, time, dm, vertexToPoint, numPoints);
  dm->purpose = ResolveNextPurpose(stage, mp.GetPath().str());
  // Prototype displayColor is carried PER-VERTEX (FillFlatGeometry filled
  // dm->vertexColors -- uploaded to GL attrib 10 for instanced draws, shared by all
  // instances). Keep the per-instance constant neutral (white) so it doesn't tint
  // the per-vertex color; the instanced shader multiplies the two.
  if (!dm->vertexColors.empty()) {
    dm->flatColor[0] = dm->flatColor[1] = dm->flatColor[2] = 1.0f;
  }
  double mw16[16];
  tydn::ComputeWorldTransform(stage, mp, mw16, time);
  *mesh_rel = Mul4(Mat4dFromArray(mw16), inv_protoroot);
  if (out_vertexToPoint) *out_vertexToPoint = std::move(vertexToPoint);
  return true;
}

// Split a prototype subtree into its DIRECT mesh prims and its NESTED instancers
// (PointInstancer / scenegraph instanceable), WITHOUT descending into the latter.
// The prototype root itself is collected only if it is a Mesh. Mirrors the
// instancer skips in the static-batching gather + tusdrender CollectProtoMeshNesting.
void SplitProtoSubtree(const tnext::UsdPrim& root,
                       std::vector<tnext::UsdPrim>* meshes,
                       std::vector<tnext::UsdPrim>* instancers) {
  std::function<void(const tnext::UsdPrim&, bool)> rec =
      [&](const tnext::UsdPrim& p, bool isRoot) {
        if (!isRoot) {
          if (p.GetTypeName() == "PointInstancer") {
            instancers->push_back(p);
            return;
          }
          const auto* s = p.GetPrimSpec();
          if (s && !s->meta().instance_prototype().empty()) {
            instancers->push_back(p);
            return;
          }
        }
        if (p.GetTypeName() == "Mesh") meshes->push_back(p);
        for (const tnext::UsdPrim& c : p.GetChildren()) rec(c, false);
      };
  rec(root, true);
}

// Emit GPU-instanced DrawMeshCPU for a prototype subtree placed at the given world
// transforms `placements`. Direct (non-instancer) meshes become one DrawMeshCPU
// each (instanceXforms = mesh_rel * each placement). NESTED instancers are
// flattened: their per-instance transforms (relative to this prototype root) are
// composed with each outer placement and the inner prototype is emitted
// recursively, so a TLAS-less GL preview still shows nested instancing -- geometry
// stays deduped (shared VBO), only the per-instance matrix list grows. Routing the
// top-level PointInstancer/native passes through this is byte-identical when nothing
// nests (same mesh order, same per-placement loop). `placementColors`, when set, is
// 3 floats/placement applied as per-instance color to this level's direct meshes.
//
// SKINNED prototypes (`gpuSkinning`) are DE-INSTANCED: each placement becomes its
// own non-instanced DrawMeshCPU carrying the prototype's skin attributes, with the
// placement in `world`. The raster backends only skin the non-instanced program
// (the flat instanced shader has no bone path, on GL and Vulkan alike), and the
// tracers read the same DrawScene, so this is the one representation every path
// already poses. It costs a vertex-buffer copy per instance, so it is capped at
// kMaxSkinnedProtoInstances; past that the prototype keeps its instancing and
// falls back to a static baked pose. All instances of a prototype share ONE bone
// block: USD instancing requires identical composed contents, so they necessarily
// share a skeleton and animation -- which is also why the bone rows here fold in
// geomBind only, never a world transform (each copy's `world` supplies that).
void EmitInstancedProto(const tnext::Stage& stage,
                        tydn::RenderSceneConverter& conv,
                        const tnext::UsdPrim& protoRoot,
                        const std::vector<matrix4d>& placements,
                        const std::vector<float>* placementColors, double time,
                        bool gpuSkinning,
                        DrawScene* draw, Bounds* bounds, long long* instTotal,
                        long long* effectiveTris, size_t instBudget,
                        std::unordered_set<std::string>* consumed) {
  if (placements.empty()) return;
  double pr16[16];
  tydn::ComputeWorldTransform(stage, protoRoot, pr16, time);
  const matrix4d inv_proto = ::tinyusdz::inverse(Mat4dFromArray(pr16));

  std::vector<tnext::UsdPrim> directMeshes, nestedInstancers;
  SplitProtoSubtree(protoRoot, &directMeshes, &nestedInstancers);

  const bool haveColors =
      placementColors && placementColors->size() == placements.size() * 3;

  for (const tnext::UsdPrim& mp : directMeshes) {
    if (consumed) consumed->insert(mp.GetPath().str());
    DrawMeshCPU dm;
    matrix4d mesh_rel;
    std::vector<uint32_t> vertexToPoint;
    size_t numPoints = 0;
    if (!BuildProtoMesh(stage, conv, mp, inv_proto, time, &dm, &mesh_rel,
                        &vertexToPoint, &numPoints)) {
      continue;
    }

    // Skeletal skinning on the prototype, which stays INSTANCED either way. GPU:
    // emit skin attributes + a bone block with an IDENTITY world -- the bones are
    // prototype-local and the instanced vertex shader applies each instance's o2w
    // AFTER skinning, so all instances share the one block. (Sound because USD
    // instancing requires identical composed contents: one skeleton, one pose.)
    // CPU: bake the static pose at `time` into the prototype's geometry. Both
    // no-op for unskinned prototypes.
    bool gpuSkinned = false;
    if (gpuSkinning) {
      double identW[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
      gpuSkinned = SetupGpuSkinNext(stage, mp, time, &dm, vertexToPoint,
                                    numPoints, identW, draw);
    }
    if (!gpuSkinned) BakeSkinning(stage, mp, time, &dm, vertexToPoint, numPoints);

    // Prototype-LOCAL bbox over the (untransformed) vertices, for per-instance
    // frustum culling + CUDA instance world-AABBs (each instance transforms it).
    if (!dm.vertices.empty()) {
      float lo[3] = {dm.vertices[0].px, dm.vertices[0].py, dm.vertices[0].pz};
      float hi[3] = {lo[0], lo[1], lo[2]};
      for (const DrawVertex& v : dm.vertices) {
        lo[0] = std::min(lo[0], v.px); hi[0] = std::max(hi[0], v.px);
        lo[1] = std::min(lo[1], v.py); hi[1] = std::max(hi[1], v.py);
        lo[2] = std::min(lo[2], v.pz); hi[2] = std::max(hi[2], v.pz);
      }
      // Pad by the GPU morph's max displacement so a morphed instance is not
      // wrongly frustum-culled (the rest box would miss the displaced geometry).
      for (int k = 0; k < 3; ++k) {
        dm.protoAabbMin[k] = lo[k] - dm.morphExtent[k];
        dm.protoAabbMax[k] = hi[k] + dm.morphExtent[k];
      }
    }
    dm.instanceXforms.reserve(placements.size() * 12);
    if (haveColors) dm.instanceColors.reserve(placements.size() * 3);
    for (size_t k = 0; k < placements.size(); ++k) {
      if (static_cast<size_t>(*instTotal) + dm.instanceXforms.size() / 12 >=
          instBudget)
        break;
      const matrix4d fin = Mul4(mesh_rel, placements[k]);
      float o2w[12];
      Mat4dToO2W(fin, o2w);
      dm.instanceXforms.insert(dm.instanceXforms.end(), o2w, o2w + 12);
      if (haveColors) {
        dm.instanceColors.push_back((*placementColors)[k * 3 + 0]);
        dm.instanceColors.push_back((*placementColors)[k * 3 + 1]);
        dm.instanceColors.push_back((*placementColors)[k * 3 + 2]);
      }
      // Scene bounds from this placement's transformed prototype BOX, not just its
      // origin: a prototype's geometry extends around its instance translation, and
      // bounding only the translations yields a degenerate box (two points for a
      // 2-instance scene) that auto-framing then aims the camera at, pushing the
      // geometry out of frame.
      for (int c = 0; c < 8; ++c) {
        const float lp[3] = {(c & 1) ? dm.protoAabbMax[0] : dm.protoAabbMin[0],
                             (c & 2) ? dm.protoAabbMax[1] : dm.protoAabbMin[1],
                             (c & 4) ? dm.protoAabbMax[2] : dm.protoAabbMin[2]};
        float wp[3];
        for (int a = 0; a < 3; ++a) {
          wp[a] = static_cast<float>(lp[0] * fin.m[0][a] + lp[1] * fin.m[1][a] +
                                     lp[2] * fin.m[2][a] + fin.m[3][a]);
        }
        bounds->add(wp);
      }
    }
    if (dm.instanceXforms.empty()) continue;
    const size_t ninst = dm.instanceXforms.size() / 12;
    *instTotal += static_cast<long long>(ninst);
    *effectiveTris += (dm.indices.size() / 3) * ninst;
    for (int k = 0; k < 3; ++k) {
      dm.aabbMin[k] = bounds->mn[k];
      dm.aabbMax[k] = bounds->mx[k];
    }
    std::memset(dm.world, 0, sizeof(dm.world));
    dm.world[0] = dm.world[5] = dm.world[10] = dm.world[15] = 1.0f;
    draw->triangleCount += dm.indices.size() / 3;
    draw->meshes.push_back(std::move(dm));
  }

  // Nested instancers: compose each per-instance transform (relative to protoRoot)
  // with every outer placement, then recurse on the inner prototype.
  static const float kIdentQuat[4] = {0, 0, 0, 1};
  static const float kUnitScale[3] = {1, 1, 1};
  for (const tnext::UsdPrim& ni : nestedInstancers) {
    if (static_cast<size_t>(*instTotal) >= instBudget) break;
    if (ni.GetTypeName() == "PointInstancer") {
      double iw16[16];
      tydn::ComputeWorldTransform(stage, ni, iw16, time);
      const matrix4d ni_rel = Mul4(Mat4dFromArray(iw16), inv_proto);
      tydn::ValueArrayRead<float> positions;
      tydn::ReadFloatArray(ni, "positions", time, &positions);
      const size_t n = positions.size() / 3;
      tydn::ValueArrayRead<int32_t> protoIdx;
      tydn::ReadIntArray(ni, "protoIndices", time, &protoIdx);
      tydn::ValueArrayRead<float> orients;
      tydn::ReadFloatArray(ni, "orientations", time, &orients);
      tydn::ValueArrayRead<float> scales;
      tydn::ReadFloatArray(ni, "scales", time, &scales);
      tydn::ValueArrayRead<int64_t> invis;
      tydn::ReadInt64Array(ni, "invisibleIds", time, &invis);
      tydn::ValueArrayRead<int64_t> inactive;
      tydn::ReadInt64Array(ni, "inactiveIds", time, &inactive);
      tydn::ValueArrayRead<int64_t> ids;
      tydn::ReadInt64Array(ni, "ids", time, &ids);
      std::unordered_set<int64_t> hiddenSet(invis.begin(), invis.end());
      hiddenSet.insert(inactive.begin(), inactive.end());
      const std::vector<tnext::Path>* iprotos = ni.GetRelationship("prototypes");
      if (!iprotos) continue;
      std::vector<std::vector<uint32_t>> byProto(iprotos->size());
      for (size_t i = 0; i < n; ++i) {
        if (PointInstanceHidden(i, n, ids, hiddenSet)) continue;
        const int pix = (i < protoIdx.size()) ? protoIdx[i] : 0;
        if (pix >= 0 && pix < int(iprotos->size())) byProto[pix].push_back(uint32_t(i));
      }
      for (size_t pix = 0; pix < iprotos->size(); ++pix) {
        if (byProto[pix].empty()) continue;
        tnext::UsdPrim innerRoot = stage.GetPrimAtPath((*iprotos)[pix]);
        if (!innerRoot.IsValid()) continue;
        std::vector<matrix4d> innerPl;
        innerPl.reserve(byProto[pix].size() * placements.size());
        bool capped = false;
        for (const matrix4d& P : placements) {
          const matrix4d eff = Mul4(ni_rel, P);  // instancer effective world
          for (uint32_t j : byProto[pix]) {
            if (static_cast<size_t>(*instTotal) + innerPl.size() >= instBudget) {
              capped = true;
              break;
            }
            const float* q =
                (orients.size() >= (j + 1) * 4) ? &orients[j * 4] : kIdentQuat;
            const float* s =
                (scales.size() >= (j + 1) * 3) ? &scales[j * 3] : kUnitScale;
            innerPl.push_back(Mul4(InstanceTRS(&positions[j * 3], q, s), eff));
          }
          if (capped) break;
        }
        EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time,
                           gpuSkinning, draw, bounds, instTotal, effectiveTris,
                           instBudget, consumed);
      }
    } else {
      const auto* s = ni.GetPrimSpec();
      if (!s) continue;
      const std::string ipath = s->meta().instance_prototype();
      if (ipath.empty()) continue;
      double w16[16];
      tydn::ComputeWorldTransform(stage, ni, w16, time);
      const matrix4d m_rel = Mul4(Mat4dFromArray(w16), inv_proto);
      // The native instance's children are proxies of its prototype; consume them.
      std::vector<tnext::UsdPrim> proxies;
      tydn::GatherMeshPrims(ni, &proxies);
      if (consumed)
        for (const tnext::UsdPrim& m : proxies) consumed->insert(m.GetPath().str());
      tnext::UsdPrim innerRoot = stage.GetPrimAtPath(ipath);
      if (!innerRoot.IsValid()) continue;
      std::vector<matrix4d> innerPl;
      innerPl.reserve(placements.size());
      for (const matrix4d& P : placements) innerPl.push_back(Mul4(m_rel, P));
      EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time,
                         gpuSkinning, draw, bounds, instTotal, effectiveTris,
                         instBudget, consumed);
    }
  }
}

// Read a scalar float camera attribute, or `fallback` when absent/non-float.
float ReadCamFloatN(const tnext::UsdPrim& prim, const char* name, float fallback) {
  if (const tnext::Value* v = prim.GetPropertyValue(name)) {
    if (const float* f = v->as_float()) return *f;
  }
  return fallback;
}

bool FindNextCameraRec(const tnext::Stage& stage, const tnext::UsdPrim& prim,
                       const std::string& name, double time,
                       NextCameraPose* out) {
  if (prim.GetTypeName() == "Camera") {
    const std::string path = prim.GetPath().str();
    const std::string pname = prim.GetName();
    // Match by exact name, exact path, or a "/<name>" path suffix.
    const bool match =
        name.empty() || pname == name || path == name ||
        (path.size() > name.size() &&
         path.compare(path.size() - name.size(), name.size(), name) == 0 &&
         path[path.size() - name.size() - 1] == '/');
    if (match) {
      double mw[16];
      if (tydn::ComputeWorldTransform(stage, prim, mw, time)) {
        const matrix4d m = Mat4dFromArray(mw);
        // Row-major (p*M): translation in row 3, local axes in rows 0..2. USD
        // cameras look down local -Z with local +Y up (see Mat4dToO2W above).
        float up[3] = {float(m.m[1][0]), float(m.m[1][1]), float(m.m[1][2])};
        float fwd[3] = {-float(m.m[2][0]), -float(m.m[2][1]), -float(m.m[2][2])};
        auto norm3 = [](float v[3]) {
          float l = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
          if (l > 1e-12f) { v[0] /= l; v[1] /= l; v[2] /= l; }
        };
        norm3(up);
        norm3(fwd);
        out->eye[0] = float(m.m[3][0]);
        out->eye[1] = float(m.m[3][1]);
        out->eye[2] = float(m.m[3][2]);
        for (int k = 0; k < 3; ++k) {
          out->up[k] = up[k];
          out->forward[k] = fwd[k];
        }
        const float focal = ReadCamFloatN(prim, "focalLength", 50.0f);
        const float vap = ReadCamFloatN(prim, "verticalAperture", 15.2908f);
        out->fovYDeg = 2.0f *
                       std::atan(0.5f * vap / std::max(1.0e-6f, focal)) *
                       (180.0f / 3.14159265358979323846f);
        if (const tnext::Value* v = prim.GetPropertyValue("clippingRange")) {
          if (const float* f = v->as_float2()) {
            out->zNear = std::max(1.0e-4f, f[0]);
            out->zFar = std::max(out->zNear + 1.0e-3f, f[1]);
          }
        }
      }
      return true;
    }
  }
  for (const tnext::UsdPrim& child : prim.GetChildren()) {
    if (FindNextCameraRec(stage, child, name, time, out)) return true;
  }
  return false;
}

// --- Phase 2 --next texture loading -----------------------------------------
// The tydra-next converter records texture *metadata* (RenderTexture: asset
// path, wrap, value scale/bias, channel) into a scratch RenderScene even with
// load_textures=false, but never decodes pixels. Decoding is ours, and it runs
// through tydra::next::TextureDecoder -- the same decoder tusdrender uses, so
// the size cap and byte budget are applied identically and AT DECODE TIME (a
// large scene never has to hold every texture at full resolution first).

struct NextTexCache {
  std::unordered_map<std::string, int> byKey;  // key -> draw->textures index (-1 miss)
  std::unique_ptr<tydn::TextureDecoder> decoder;
};

// Decode an asset into an RGBA8 light3d::Image through the shared decoder.
bool DecodeNextImage(NextTexCache& tc, const std::string& asset,
                     bool srgb, light3d::Image* out) {
  if (!tc.decoder || asset.empty()) return false;
  tydn::DecodedImage img;
  if (!tc.decoder->Decode(asset, srgb, &img)) return false;
  out->width = static_cast<int>(img.width);
  out->height = static_cast<int>(img.height);
  out->channels = 4;
  out->data = std::move(img.pixels);
  return true;
}

int NextWrapToDraw(tydn::WrapMode w) {
  switch (w) {
    case tydn::WrapMode::Clamp: return static_cast<int>(WrapMode::ClampToEdge);
    case tydn::WrapMode::Mirror: return static_cast<int>(WrapMode::Mirror);
    case tydn::WrapMode::Black: return static_cast<int>(WrapMode::ClampToBorder);
    case tydn::WrapMode::Repeat:
    default: return static_cast<int>(WrapMode::Repeat);
  }
}

int NextScalarChannel(tydn::RenderTexture::Channel c) {
  switch (c) {
    case tydn::RenderTexture::Channel::G: return 1;
    case tydn::RenderTexture::Channel::B: return 2;
    case tydn::RenderTexture::Channel::A: return 3;
    default: return 0;  // R / RGB / RGBA
  }
}

// Resize an RGBA8 light3d::Image to (w,h) via tydra::ResizeImage. Mirrors
// mesh_build's ResizeDrawImage (minus the vendored textools fast path, which is
// file-local there) so --next UDIM tiles can be normalized to a common size.
bool NextResizeImage(light3d::Image* img, int w, int h, bool srgb) {
  if (!img || w <= 0 || h <= 0 || img->width <= 0 || img->height <= 0) return false;
  if (img->width == w && img->height == h) return true;
  tinyusdz::Image src;
  src.width = img->width;
  src.height = img->height;
  src.channels = img->channels;
  src.bpp = 8;
  src.format = tinyusdz::Image::PixelFormat::UInt;
  src.data = img->data;
  tinyusdz::Image dst;
  const auto filter = srgb ? tinyusdz::tydra::ResizeFilter::SRGB
                           : tinyusdz::tydra::ResizeFilter::Linear;
  std::string err;
  if (!tinyusdz::tydra::ResizeImage(src, w, h, &dst, filter, &err)) return false;
  img->width = dst.width;
  img->height = dst.height;
  img->channels = dst.channels;
  img->data = std::move(dst.data);
  return true;
}

// Enumerate + decode UDIM tiles for a `<UDIM>`-tagged asset path into a UDIM
// DrawTextureCPU. tydra-next carries the literal `<UDIM>` token through verbatim
// (no udim handling in the converter), so we expand it ourselves: probe ids
// 1001..1100, decode each existing tile (base_dir or .usdz), normalize to a
// common size, and build the udimLayer[100] LUT the sampler2DArray path reads.
// Returns the DrawScene texture index or -1 if no tile decoded. Mirrors
// mesh_build's BuildDrawTextures UDIM branch / NormalizeUdimTiles / InitUdimLookup.
int LoadNextUdimTexture(NextTexCache& tc, DrawScene* draw,
                        const tydn::RenderTexture& rt, const std::string& asset,
                        bool srgb) {
  std::string pre, post;
  if (!tinyusdz::io::SplitUDIMPath(asset, &pre, &post)) return -1;

  DrawTextureCPU dt;
  for (uint32_t id = 1001; id <= 1100; ++id) {
    const std::string tilePath = pre + std::to_string(id) + post;
    DrawUdimTileCPU tile;
    if (!DecodeNextImage(tc, tilePath, srgb, &tile.image)) continue;  // absent tile
    tile.udim = id;
    tile.u = (id - 1001u) % 10u;
    tile.v = (id - 1001u) / 10u;
    tile.assetIdentifier = tilePath;
    dt.udimTiles.push_back(std::move(tile));
  }
  if (dt.udimTiles.empty()) return -1;

  dt.isUdim = true;
  dt.assetIdentifier = asset;
  dt.srgb = srgb;
  dt.wrapS = NextWrapToDraw(rt.wrap_s);
  dt.wrapT = NextWrapToDraw(rt.wrap_t);

  // Normalize all tiles to the max width/height, then LUT: udim-1001 -> layer.
  int w = 0, h = 0;
  for (const DrawUdimTileCPU& t : dt.udimTiles) {
    w = std::max(w, t.image.width);
    h = std::max(h, t.image.height);
  }
  if (w <= 0 || h <= 0) return -1;
  // Resize every tile to the common (w,h); DROP any that fail — the renderer
  // uploads udimTiles as a sampler2DArray requiring all layers to be exactly
  // udimTileWidth/Height, so a leftover wrong-sized tile would render as a
  // white/garbage layer instead of the intended "missing" (magenta) sentinel.
  {
    std::vector<DrawUdimTileCPU> sized;
    sized.reserve(dt.udimTiles.size());
    for (DrawUdimTileCPU& t : dt.udimTiles) {
      if ((t.image.width != w || t.image.height != h) &&
          !NextResizeImage(&t.image, w, h, srgb)) {
        continue;  // drop; its UDIM id stays unmapped (-1) in the LUT
      }
      sized.push_back(std::move(t));
    }
    dt.udimTiles = std::move(sized);
  }
  if (dt.udimTiles.empty()) return -1;
  dt.udimTileWidth = w;
  dt.udimTileHeight = h;
  dt.image = dt.udimTiles.front().image;  // representative fallback
  dt.udimLayer.fill(-1);
  for (size_t i = 0; i < dt.udimTiles.size(); ++i) {
    const uint32_t u = dt.udimTiles[i].udim;
    if (u >= 1001 && u <= 1100) dt.udimLayer[u - 1001] = static_cast<int>(i);
  }
  const int idx = static_cast<int>(draw->textures.size());
  draw->textures.push_back(std::move(dt));
  return idx;
}

// Decode + register the texture referenced by scratch.textures[texId]. Deduped
// by (asset, srgb, wrap). Returns the DrawScene texture index or -1.
int LoadNextTexture(NextTexCache& tc, DrawScene* draw,
                    const tydn::RenderScene& scratch, int32_t texId, bool srgb) {
  if (texId < 0 || static_cast<size_t>(texId) >= scratch.textures.size()) return -1;
  const tydn::RenderTexture& rt = scratch.textures[static_cast<size_t>(texId)];
  // Prefer the image's RESOLVED path. `RenderTexture::asset_path` is the raw
  // authored string, and for a look layer nested below the root that is relative
  // to THAT layer (`../../texture/foo.png`) -- it does not resolve against the
  // scene file. `resolved_path` has been anchored to the authoring layer by the
  // converter (see next/layer/asset-anchor.hh). For root-layer and USDZ-internal
  // assets the two are identical, so this only ever adds the anchor.
  std::string asset;
  if (rt.image_id >= 0 &&
      static_cast<size_t>(rt.image_id) < scratch.images.size()) {
    asset = scratch.images[static_cast<size_t>(rt.image_id)].resolved_path;
  }
  if (asset.empty()) asset = rt.asset_path;
  if (asset.empty()) return -1;

  const std::string key = asset + (srgb ? "|s" : "|l") + "|" +
      std::to_string(static_cast<int>(rt.wrap_s)) + "," +
      std::to_string(static_cast<int>(rt.wrap_t));
  auto it = tc.byKey.find(key);
  if (it != tc.byKey.end()) return it->second;

  // UDIM: tydra-next carries the literal `<UDIM>` token through, so expand +
  // decode tiles ourselves into a sampler2DArray-backed UDIM texture.
  if (tinyusdz::io::IsUDIMPath(asset)) {
    const int uidx = LoadNextUdimTexture(tc, draw, rt, asset, srgb);
    tc.byKey[key] = uidx;
    return uidx;
  }

  DrawTextureCPU dt;
  if (!DecodeNextImage(tc, asset, srgb, &dt.image)) {
    tc.byKey[key] = -1;  // negative-cache the miss
    return -1;
  }
  dt.assetIdentifier = asset;
  dt.srgb = srgb;
  dt.wrapS = NextWrapToDraw(rt.wrap_s);
  dt.wrapT = NextWrapToDraw(rt.wrap_t);
  const int idx = static_cast<int>(draw->textures.size());
  draw->textures.push_back(std::move(dt));
  tc.byKey[key] = idx;
  return idx;
}

// Which UV set a texture samples. RenderTexture::uv_primvar carries the name the
// texture's UsdPrimvarReader asked for; the mesh reports the names it actually
// extracted into slots 0 and 1. Anything that is not the secondary set -- the
// usual case, and any unresolvable name -- falls back to slot 0, which is what
// the renderer did unconditionally before.
int ResolveUvSet(const tydn::RenderTexture& rt, const std::string& uv0Name,
                 const std::string& uv1Name) {
  if (uv1Name.empty() || rt.uv_primvar.empty()) return 0;
  if (rt.uv_primvar == uv1Name && uv1Name != uv0Name) return 1;
  return 0;
}

// Fill a DrawTexSampleCPU's UV affine + value scale/bias from a RenderTexture.
void FillNextSample(const tydn::RenderTexture& rt, DrawTexSampleCPU* smp,
                    const std::string& uv0Name = std::string(),
                    const std::string& uv1Name = std::string()) {
  smp->uvSet = ResolveUvSet(rt, uv0Name, uv1Name);
  const float c = std::cos(rt.rotation), s = std::sin(rt.rotation);
  smp->uv.m00 = c * rt.scale.x; smp->uv.m01 = -s * rt.scale.y;
  smp->uv.m10 = s * rt.scale.x; smp->uv.m11 =  c * rt.scale.y;
  smp->uv.tx = rt.offset.x; smp->uv.ty = rt.offset.y;
  smp->scale[0] = rt.scale_value.x; smp->scale[1] = rt.scale_value.y;
  smp->scale[2] = rt.scale_value.z; smp->scale[3] = rt.scale_value.w;
  smp->bias[0] = rt.bias.x; smp->bias[1] = rt.bias.y;
  smp->bias[2] = rt.bias.z; smp->bias[3] = rt.bias.w;
}

// Convert a bound material prim into a DrawMaterialCPU appended to `draw`, and
// return its index (>=1). Phase 1 baked PBR constants (base color, metallic,
// roughness, emissive, alpha) + Phase 2 textures (base color, emissive, normal,
// metal/rough). GeomSubset per-face materials and skinning remain follow-ups.
// Reuses tusdview's own BakeLightRtOpenPBR so the --next path shades materials
// through the same path the legacy loader uses. Returns -1 if the prim has no
// usable surface shader (caller then keeps the default gray material, index 0).
int BuildNextMaterial(const tnext::Stage& stage, tydn::RenderSceneConverter& conv,
                      const tnext::UsdPrim& matPrim, DrawScene* draw,
                      NextTexCache& texCache, const std::string& uv0Name,
                      const std::string& uv1Name) {
  tydn::RenderScene scratch;  // texture/image metadata (pixels decoded by us)
  tydn::RenderMaterial rm;
  if (!conv.ConvertMaterial(stage, matPrim, &rm, &scratch)) return -1;

  auto setRGB = [](float* dst, const tydn::Float4& v, float w) {
    dst[0] = v.x * w; dst[1] = v.y * w; dst[2] = v.z * w;
  };
  // Load a color texture into a slot; on success neutralise the baked constant
  // (so the texture isn't double-tinted) and fill the UV/scale sample.
  auto colorSlot = [&](const tydn::ShaderParam& sp, bool srgb, int* texField,
                       DrawTexSampleCPU* smp, float* neutralize3) {
    if (sp.texture_id < 0) return;
    int t = LoadNextTexture(texCache, draw, scratch, sp.texture_id, srgb);
    if (t < 0) return;
    *texField = t;
    FillNextSample(scratch.textures[static_cast<size_t>(sp.texture_id)], smp,
                   uv0Name, uv1Name);
    if (neutralize3) { neutralize3[0] = neutralize3[1] = neutralize3[2] = 1.0f; }
  };

  DrawMaterialCPU dm;
  dm.name = rm.name;
  dm.absPath = rm.prim_path;
  dm.displayName = rm.name;

  // Load a normal-map slot (linear; default [0,1]->[-1,1] remap if unauthored).
  auto loadNormal = [&](const tydn::ShaderParam& sp) {
    if (sp.texture_id < 0) return;
    int t = LoadNextTexture(texCache, draw, scratch, sp.texture_id, false);
    if (t < 0) return;
    dm.normalTex = t;
    const tydn::RenderTexture& rt = scratch.textures[static_cast<size_t>(sp.texture_id)];
    FillNextSample(rt, &dm.normalSample, uv0Name, uv1Name);
    const bool defScale = rt.scale_value.x == 1.0f && rt.scale_value.y == 1.0f &&
                          rt.scale_value.z == 1.0f;
    const bool defBias = rt.bias.x == 0.0f && rt.bias.y == 0.0f && rt.bias.z == 0.0f;
    if (defScale && defBias) {
      dm.normalSample.scale[0] = dm.normalSample.scale[1] =
          dm.normalSample.scale[2] = 2.0f;
      dm.normalSample.bias[0] = dm.normalSample.bias[1] =
          dm.normalSample.bias[2] = -1.0f;
    }
  };
  // Pack metallic/roughness into the single metalRough slot. Roughness wins the
  // slot; a separate metallic texture is approximated onto the same slot (a
  // known Phase-2 limitation for non-packed ORM inputs).
  auto loadMetalRough = [&](const tydn::ShaderParam& metallic,
                            const tydn::ShaderParam& roughness) {
    if (roughness.texture_id >= 0) {
      int t = LoadNextTexture(texCache, draw, scratch, roughness.texture_id, false);
      if (t >= 0) {
        dm.metalRoughTex = t;
        dm.roughness = 1.0f;
        const tydn::RenderTexture& rt =
            scratch.textures[static_cast<size_t>(roughness.texture_id)];
        dm.roughnessChannel = NextScalarChannel(rt.output_channel);
        FillNextSample(rt, &dm.metalRoughSample, uv0Name, uv1Name);
      }
    }
    if (metallic.texture_id >= 0) {
      int t = LoadNextTexture(texCache, draw, scratch, metallic.texture_id, false);
      if (t >= 0) {
        const tydn::RenderTexture& rt =
            scratch.textures[static_cast<size_t>(metallic.texture_id)];
        if (dm.metalRoughTex < 0) {
          dm.metalRoughTex = t;
          FillNextSample(rt, &dm.metalRoughSample, uv0Name, uv1Name);
        }
        dm.metallic = 1.0f;
        dm.metallicChannel = NextScalarChannel(rt.output_channel);
      }
    }
  };

  // A material can author BOTH a UsdPreviewSurface and an OpenPBR/mtlx shader
  // (DCC exports, MaterialX-with-fallback); ConvertMaterial fills both but sets
  // shader_type to the last child (often OpenPBR). tydra-next resolves *direct*
  // UsdUVTexture connections into texture_ids but not MaterialX nodegraph image
  // nodes, so the two shaders can disagree on which textures resolved. Pick the
  // shader that actually resolved the most textures (tie -> UsdPreviewSurface,
  // the interop path). Falls back cleanly for single-shader materials.
  auto texCount = [](std::initializer_list<int> ids) {
    int n = 0; for (int i : ids) if (i >= 0) ++n; return n;
  };
  int pvTex = -1, opTex = -1;
  if (rm.preview_surface) {
    const tydn::PreviewSurfaceShader& s = *rm.preview_surface;
    pvTex = texCount({s.diffuse_color.texture_id, s.normal.texture_id,
                      s.emissive_color.texture_id, s.metallic.texture_id,
                      s.roughness.texture_id});
  }
  if (rm.openpbr) {
    const tydn::OpenPBRSurfaceShader& s = *rm.openpbr;
    opTex = texCount({s.base_color.texture_id, s.normal.texture_id,
                      s.emission_color.texture_id, s.base_metalness.texture_id,
                      s.base_roughness.texture_id});
  }
  const bool usePreview = rm.preview_surface && (!rm.openpbr || pvTex >= opTex);

  if (usePreview) {
    const tydn::PreviewSurfaceShader& s = *rm.preview_surface;
    dm.hasUsdPreviewSurface = true;
    setRGB(dm.baseColor, s.diffuse_color.value, 1.0f);
    dm.metallic = s.metallic.value.x;
    dm.roughness = s.roughness.value.x;
    setRGB(dm.emissive, s.emissive_color.value, 1.0f);
    dm.alpha = s.opacity.value.x;
    colorSlot(s.diffuse_color, true, &dm.baseColorTex, &dm.baseColorSample, dm.baseColor);
    colorSlot(s.emissive_color, true, &dm.emissiveTex, &dm.emissiveSample, dm.emissive);
    loadNormal(s.normal);
    loadMetalRough(s.metallic, s.roughness);
  } else if (rm.openpbr) {
    const tydn::OpenPBRSurfaceShader& s = *rm.openpbr;
    dm.hasOpenPBRSurface = true;
    dm.materialXNodeGraphJson = s.nodegraph_json;
    setRGB(dm.baseColor, s.base_color.value, s.base_weight.value.x);
    dm.metallic = s.base_metalness.value.x;
    dm.roughness = s.specular_roughness.value.x;
    setRGB(dm.emissive, s.emission_color.value, s.emission_luminance.value.x);
    dm.alpha = s.opacity.value.x;
    colorSlot(s.base_color, true, &dm.baseColorTex, &dm.baseColorSample, dm.baseColor);
    colorSlot(s.emission_color, true, &dm.emissiveTex, &dm.emissiveSample, dm.emissive);
    loadNormal(s.normal);
    loadMetalRough(s.base_metalness, s.base_roughness);
  } else {
    return -1;  // no PreviewSurface/OpenPBR -- fall back to default material
  }

  // AlphaMode enums line up 1:1 (Opaque=0, Mask=1, Blend=2).
  dm.alphaMode = static_cast<int>(rm.alpha_mode);
  dm.alphaCutoff = rm.alpha_cutoff;

  BakeLightRtOpenPBR(&dm);  // derive lightRtOpenPBR from the baked constants

  draw->materials.push_back(std::move(dm));
  return static_cast<int>(draw->materials.size() - 1);
}

}  // namespace

bool FindNextCamera(const tnext::Stage& stage, const std::string& name,
                    double time, NextCameraPose* out) {
  for (const tnext::UsdPrim& root : stage.GetRootPrims()) {
    if (FindNextCameraRec(stage, root, name, time, out)) return true;
  }
  return false;
}

bool BuildNextSkinningFrame(const tnext::Stage& stage, DrawScene* draw,
                            double time, SkinningFrameCPU* frame) {
  if (!draw || !frame) return false;
  if (draw->boneMatrixCount <= 0 || draw->nextSkels.empty()) return false;

  // One row per (skinned source mesh, joint), addressed absolutely by
  // DrawMeshCPU::jointIdx. Identity is the safe default: it renders the vertex
  // at its world-baked REST position, so a skeleton that stops resolving degrades
  // to the rest pose instead of collapsing the mesh to the origin.
  const size_t rows = static_cast<size_t>(draw->boneMatrixCount);
  std::vector<matrix4d> bones(rows, matrix4d::identity());
  // Skeletons are shared between meshes far more often than not; pose each once.
  std::map<std::pair<std::string, std::string>, std::vector<matrix4d>> posed;

  for (const DrawScene::NextSkelBinding& nb : draw->nextSkels) {
    const auto key = std::make_pair(nb.skelPath, nb.animPath);
    auto it = posed.find(key);
    if (it == posed.end()) {
      std::vector<matrix4d> sm;
      if (!PoseNextSkeleton(stage, nb.skelPath, nb.animPath, time, &sm)) continue;
      it = posed.emplace(key, std::move(sm)).first;
    }
    const std::vector<matrix4d>& sm = it->second;
    const matrix4d G = Mat4dFromArray(nb.geomBind);
    const matrix4d W = Mat4dFromArray(nb.world);
    const matrix4d invG = ::tinyusdz::inverse(G);
    const matrix4d invW = ::tinyusdz::inverse(W);
    const size_t nj =
        std::min(sm.size(), static_cast<size_t>(std::max(0, nb.numJoints)));
    for (size_t j = 0; j < nj; ++j) {
      const size_t row = static_cast<size_t>(nb.matrixBase) + j;
      if (row >= rows) break;
      // Row-vector: undo the world bake, LBS in bind space, re-apply the world.
      bones[row] = invW * (G * sm[j] * invG) * W;
    }
  }

  // Pack straight from `bones` rather than walking draw->meshes (as the Tydra
  // path does): the next loader frees each mesh's CPU geometry after GPU upload,
  // so the per-vertex skin attributes are no longer resident here -- only the GL
  // buffers hold them.
  frame->matrixCount = draw->boneMatrixCount;
  frame->rgba32f.assign(rows * 16, 0.0f);
  frame->enabled = true;
  for (size_t row = 0; row < rows; ++row) {
    for (int r = 0; r < 4; ++r) {
      for (int c = 0; c < 4; ++c) {
        frame->rgba32f[row * 16 + static_cast<size_t>(r) * 4 +
                       static_cast<size_t>(c)] =
            static_cast<float>(bones[row].m[r][c]);
      }
    }
  }
  return true;
}

void BuildNextMorphWeights(
    const tnext::Stage& stage, const DrawScene& draw, double time,
    const std::unordered_map<std::string, float>* blendOverride,
    std::vector<std::pair<int, std::vector<float>>>* out) {
  out->clear();
  for (size_t mi = 0; mi < draw.meshes.size(); ++mi) {
    const DrawMeshCPU& dm = draw.meshes[mi];
    if (dm.morphChannelCount <= 0 || dm.morphTargetChannels.empty()) continue;

    // Animated weights from the mesh's bound SkelAnimation, then manual overrides.
    std::unordered_map<std::string, float> weights =
        ResolveBlendWeights(stage, stage.GetPrimAtPath(dm.absPath), time);
    if (blendOverride)
      for (const auto& kv : *blendOverride) weights[kv.first] = kv.second;

    // Per-channel coefficients (== EvalMorphChannelCoeffs): each target's weight
    // brackets two channels (rest index 0 contributes nothing) with (1-t) / t.
    std::vector<float> coeff(static_cast<size_t>(dm.morphChannelCount), 0.0f);
    for (const MorphTargetChannelsCPU& tc : dm.morphTargetChannels) {
      if (tc.usdWeights.empty()) continue;
      auto it = weights.find(tc.name);
      if (it == weights.end() || it->second == 0.0f) continue;
      const std::vector<float> ibW(tc.usdWeights.begin(), tc.usdWeights.end() - 1);
      const MorphBracket br = FindMorphBracket(ibW, it->second);
      if (br.lo >= 1 && size_t(br.lo - 1) < tc.channelIds.size())
        coeff[tc.channelIds[br.lo - 1]] += (1.0f - br.t);
      if (br.hi >= 1 && size_t(br.hi - 1) < tc.channelIds.size())
        coeff[tc.channelIds[br.hi - 1]] += br.t;
    }
    out->emplace_back(static_cast<int>(mi), std::move(coeff));
  }
}

// IEEE binary16 -> float32 (EXR half envmaps).
static float NextHalfToFloat(uint16_t h) {
  const uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
  uint32_t exp = (h >> 10) & 0x1Fu;
  uint32_t man = h & 0x3FFu;
  uint32_t bits;
  if (exp == 0) {
    if (man == 0) {
      bits = sign;
    } else {
      exp = 127 - 15 + 1;
      while (!(man & 0x400u)) {
        man <<= 1;
        --exp;
      }
      man &= 0x3FFu;
      bits = sign | (exp << 23) | (man << 13);
    }
  } else if (exp == 31) {
    bits = sign | 0x7F800000u | (man << 13);
  } else {
    bits = sign | ((exp - 15 + 127) << 23) | (man << 13);
  }
  float f;
  std::memcpy(&f, &bits, sizeof(f));
  return f;
}

// DomeLight support for the `next` path: walk the stage for DomeLight prims,
// decode the envmap to float RGB (8-bit treated as linear, matching the tydra
// dome loader), and bake the split-sum IBL so raster ambient / instanced
// ambient / RT miss backgrounds light up on the large-scene path too.
void BuildNextLights(const tnext::Stage& stage, const std::string& usdPath,
                     double time, const TextureRuntimeOptions& texOpts,
                     DrawScene* draw) {
  const std::string baseDir = tinyusdz::io::GetBaseDir(usdPath);

  std::function<void(const tnext::UsdPrim&)> rec = [&](const tnext::UsdPrim& p) {
    if (p.GetTypeName() == "DomeLight" || p.GetTypeName() == "DomeLight_1") {
      DrawLightCPU light;
      light.type = DrawLightCPU::Type::Dome;
      light.name = p.GetName();
      light.absPath = p.GetPath().str();

      double w16[16];
      if (tydn::ComputeWorldTransform(stage, p, w16, time)) {
        for (int i = 0; i < 16; ++i) {
          light.transform[i] = static_cast<float>(w16[i]);
        }
      } else {
        for (int i = 0; i < 16; ++i) light.transform[i] = (i % 5 == 0) ? 1.f : 0.f;
      }

      auto readF = [&](const char* name, float fallback) {
        if (const tnext::Value* v = p.GetPropertyValue(name)) {
          if (const float* f = v->as_float()) return *f;
        }
        return fallback;
      };
      light.intensity = readF("inputs:intensity", 1.0f);
      light.exposure = readF("inputs:exposure", 0.0f);
      if (const tnext::Value* v = p.GetPropertyValue("inputs:color")) {
        if (const float* c = v->as_float3()) {
          light.color[0] = c[0];
          light.color[1] = c[1];
          light.color[2] = c[2];
        }
      }
      const float scale = light.intensity * std::pow(2.0f, light.exposure);
      for (int c = 0; c < 3; ++c) {
        light.effectiveColor[c] = light.color[c] * scale;
        light.normalizedColor[c] = light.color[c];
      }
      light.effectiveIntensity = scale;

      light.domeTextureFormat = DrawLightCPU::DomeTextureFormat::Automatic;
      if (const tnext::Value* v = p.GetPropertyValue("inputs:texture:format")) {
        if (const std::string* tk = v->as_token()) {
          if (*tk == "latlong")
            light.domeTextureFormat = DrawLightCPU::DomeTextureFormat::Latlong;
          else if (*tk == "mirroredBall")
            light.domeTextureFormat = DrawLightCPU::DomeTextureFormat::MirroredBall;
          else if (*tk == "angular")
            light.domeTextureFormat = DrawLightCPU::DomeTextureFormat::Angular;
        }
      }

      const tnext::Value* fv = p.GetPropertyValue("inputs:texture:file");
      const std::string* ap = fv ? fv->as_asset_path() : nullptr;
      if (ap && !ap->empty()) {
        light.textureFile = *ap;
        std::string tpath = *ap;
        if (!tpath.empty() && tpath[0] != '/' && !baseDir.empty()) {
          tpath = baseDir + "/" + tpath;
        }
        if (texOpts.domeIbl > 0 && TexToolsAvailable()) {
          const auto t0 = std::chrono::steady_clock::now();
          std::vector<float> rgb;
          int ew = 0, eh = 0;
          auto res = tinyusdz::image::LoadImageFromFile(tpath);
          if (res) {
            const tinyusdz::Image& img = res.value().image;
            const size_t npix =
                static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
            const int ch = img.channels;
            if (img.width > 0 && img.height > 0 && ch >= 1) {
              rgb.resize(npix * 3);
              bool decoded = true;
              if (img.format == tinyusdz::Image::PixelFormat::Float &&
                  img.bpp == 32) {
                const float* px = reinterpret_cast<const float*>(img.data.data());
                for (size_t i = 0; i < npix; ++i) {
                  const float c0 = px[i * ch + 0];
                  rgb[i * 3 + 0] = c0;
                  rgb[i * 3 + 1] = ch > 1 ? px[i * ch + 1] : c0;
                  rgb[i * 3 + 2] = ch > 2 ? px[i * ch + 2] : c0;
                }
              } else if (img.format == tinyusdz::Image::PixelFormat::Float &&
                         img.bpp == 16) {
                const uint16_t* px =
                    reinterpret_cast<const uint16_t*>(img.data.data());
                for (size_t i = 0; i < npix; ++i) {
                  const float c0 = NextHalfToFloat(px[i * ch + 0]);
                  rgb[i * 3 + 0] = c0;
                  rgb[i * 3 + 1] = ch > 1 ? NextHalfToFloat(px[i * ch + 1]) : c0;
                  rgb[i * 3 + 2] = ch > 2 ? NextHalfToFloat(px[i * ch + 2]) : c0;
                }
              } else if (img.bpp == 8) {
                const uint8_t* px = img.data.data();
                for (size_t i = 0; i < npix; ++i) {
                  const float c0 = static_cast<float>(px[i * ch + 0]) / 255.0f;
                  rgb[i * 3 + 0] = c0;
                  rgb[i * 3 + 1] =
                      ch > 1 ? static_cast<float>(px[i * ch + 1]) / 255.0f : c0;
                  rgb[i * 3 + 2] =
                      ch > 2 ? static_cast<float>(px[i * ch + 2]) / 255.0f : c0;
                }
              } else {
                decoded = false;
              }
              if (decoded) {
                ew = img.width;
                eh = img.height;
                if (light.domeTextureFormat ==
                        DrawLightCPU::DomeTextureFormat::MirroredBall ||
                    light.domeTextureFormat ==
                        DrawLightCPU::DomeTextureFormat::Angular) {
                  std::vector<float> eq;
                  int eqH = 0;
                  const int eqW = std::min(2048, std::max(256, 2 * ew));
                  if (TexToolsProbeToEquirect(
                          rgb.data(), ew, eh,
                          static_cast<int>(light.domeTextureFormat), eqW, &eq,
                          &eqH)) {
                    rgb = std::move(eq);
                    ew = eqW;
                    eh = eqH;
                  } else {
                    decoded = false;
                  }
                }
              }
              if (decoded &&
                  TexToolsBuildDomeIbl(rgb.data(), ew, eh, texOpts.domeIbl >= 2,
                                       &light.ibl)) {
                const double ms =
                    std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - t0)
                        .count();
                fprintf(stderr,
                        "[tusdview] dome IBL bake (next) '%s': %dx%d -> spec %d/irr %d in %.0f ms\n",
                        light.name.c_str(), ew, eh, light.ibl.specFaceSize,
                        light.ibl.irrFaceSize, ms);
              }
            }
          } else {
            fprintf(stderr, "[tusdview] dome envmap load failed (next): %s\n",
                    tpath.c_str());
          }
        }
      }
      draw->lights.push_back(std::move(light));
    } else {
      // Non-dome lights: enough for the raster preview key-light derivation
      // (UpdatePreviewLight uses a Distant light's direction, else a finite
      // light's position). Type name -> DrawLightCPU::Type.
      const std::string ty = p.GetTypeName();
      DrawLightCPU::Type lt = DrawLightCPU::Type::Point;
      bool isLight = true;
      if (ty == "DistantLight" || ty == "DistantLight_1")
        lt = DrawLightCPU::Type::Distant;
      else if (ty == "SphereLight")
        lt = DrawLightCPU::Type::Sphere;
      else if (ty == "RectLight")
        lt = DrawLightCPU::Type::Rect;
      else if (ty == "DiskLight")
        lt = DrawLightCPU::Type::Disk;
      else if (ty == "CylinderLight")
        lt = DrawLightCPU::Type::Cylinder;
      else
        isLight = false;

      if (isLight) {
        DrawLightCPU light;
        light.type = lt;
        light.name = p.GetName();
        light.absPath = p.GetPath().str();

        double w16[16];
        const bool haveXf = tydn::ComputeWorldTransform(stage, p, w16, time);
        if (haveXf) {
          for (int i = 0; i < 16; ++i) {
            light.transform[i] = static_cast<float>(w16[i]);
          }
          // Row 3 = translation (position); light faces local -Z, so the
          // emission direction is -(row 2). Matches the tydra RenderLight
          // derivation (render-data.cc).
          light.position[0] = static_cast<float>(w16[12]);
          light.position[1] = static_cast<float>(w16[13]);
          light.position[2] = static_cast<float>(w16[14]);
          light.direction[0] = -static_cast<float>(w16[8]);
          light.direction[1] = -static_cast<float>(w16[9]);
          light.direction[2] = -static_cast<float>(w16[10]);
        }

        auto readF = [&](const char* name, float fallback) {
          if (const tnext::Value* v = p.GetPropertyValue(name)) {
            if (const float* f = v->as_float()) return *f;
          }
          return fallback;
        };
        light.intensity = readF("inputs:intensity", 1.0f);
        light.exposure = readF("inputs:exposure", 0.0f);
        if (const tnext::Value* v = p.GetPropertyValue("inputs:color")) {
          if (const float* c = v->as_float3()) {
            light.color[0] = c[0];
            light.color[1] = c[1];
            light.color[2] = c[2];
          }
        }
        const float sc = light.intensity * std::pow(2.0f, light.exposure);
        for (int c = 0; c < 3; ++c) {
          light.effectiveColor[c] = light.color[c] * sc;
          light.normalizedColor[c] = light.color[c];
        }
        light.effectiveIntensity = sc;
        draw->lights.push_back(std::move(light));
      }
    }
    for (const tnext::UsdPrim& child : p.GetChildren()) rec(child);
  };
  for (const tnext::UsdPrim& root : stage.GetRootPrims()) rec(root);
}

// UsdVol volumes for the `next` path: walk the stage, find Volume prims,
// resolve each `field:*` relationship to its field-asset prim, load the .vdb
// (relative to the USD file dir), and emit a DrawVolumeCPU. Extends `bounds`
// with the volume world-AABB so the camera frames it.
void BuildNextVolumes(const tnext::Stage& stage, const std::string& usdPath,
                      double time, DrawScene* draw, Bounds* bounds) {
  const std::string baseDir = tinyusdz::io::GetBaseDir(usdPath);

  std::function<void(const tnext::UsdPrim&)> rec = [&](const tnext::UsdPrim& p) {
    if (p.GetTypeName() == "Volume") {
      double w16[16];
      tydn::ComputeWorldTransform(stage, p, w16, time);

      for (const std::string& relName : p.GetRelationshipNames()) {
        if (relName.rfind("field:", 0) != 0) continue;
        const std::vector<tnext::Path>* targets = p.GetRelationship(relName);
        if (!targets || targets->empty()) continue;
        tnext::UsdPrim field = stage.GetPrimAtPath((*targets)[0]);
        if (!field) continue;

        const tnext::Value* fp = field.GetPropertyValue("filePath");
        const std::string* ap = fp ? fp->as_asset_path() : nullptr;
        if (!ap || ap->empty()) continue;
        std::string fieldName = relName.substr(std::strlen("field:"));
        if (const tnext::Value* fn = field.GetPropertyValue("fieldName")) {
          if (const std::string* tk = fn->as_token()) fieldName = *tk;
        }

        // Resolve the asset path relative to the USD file directory.
        std::string vpath = *ap;
        if (!vpath.empty() && vpath[0] != '/' && !baseDir.empty()) {
          vpath = baseDir + "/" + vpath;
        }
        std::vector<tinyusdz::usdVol::VDBGrid> grids;
        std::string vw, ve;
        if (!tinyusdz::usdVol::ReadVDBFromFile(vpath, &grids, &vw, &ve) || grids.empty()) {
          continue;
        }
        const tinyusdz::usdVol::VDBGrid* g = nullptr;
        for (const auto& gg : grids)
          if (gg.name == fieldName) { g = &gg; break; }
        if (!g) g = &grids[0];
        if (g->data.empty() || g->dim[0] <= 0 || g->dim[1] <= 0 || g->dim[2] <= 0)
          continue;

        DrawVolumeCPU dv;
        dv.name = p.GetName();
        for (int k = 0; k < 16; ++k) dv.world[k] = static_cast<float>(w16[k]);
        dv.density = g->data;
        for (int a = 0; a < 3; ++a) {
          dv.dim[a] = g->dim[a];
          dv.aabbMin[a] = float(g->origin[a]) * float(g->voxel_size[a]) +
                          float(g->world_translation[a]);
          dv.aabbMax[a] = float(g->origin[a] + g->dim[a]) * float(g->voxel_size[a]) +
                          float(g->world_translation[a]);
        }
        dv.background = g->background;

        // Extend scene bounds with the volume world-AABB (8 corners). World
        // matrix is stored row-major-USD in w16 (p' = p * M), matching meshes.
        for (int corner = 0; corner < 8; ++corner) {
          float lp[3] = {(corner & 1) ? dv.aabbMax[0] : dv.aabbMin[0],
                         (corner & 2) ? dv.aabbMax[1] : dv.aabbMin[1],
                         (corner & 4) ? dv.aabbMax[2] : dv.aabbMin[2]};
          float wp[3];
          for (int c = 0; c < 3; ++c)
            wp[c] = lp[0] * float(w16[0 * 4 + c]) + lp[1] * float(w16[1 * 4 + c]) +
                    lp[2] * float(w16[2 * 4 + c]) + float(w16[3 * 4 + c]);
          bounds->add(wp);
        }
        draw->volumes.push_back(std::move(dv));
      }
    }
    for (const tnext::UsdPrim& c : p.GetChildren()) rec(c);
  };
  for (const tnext::UsdPrim& r : stage.GetRootPrims()) rec(r);
}

bool LoadUSDViaNext(const std::string& path, const LoadOptions& opts,
                    DrawScene* draw, std::string* warn, std::string* err,
                    LoadControl* ctrl,
                    std::shared_ptr<tnext::StageSession>* out_session) {
  // --- 1. Open a persistent next document. Parsed dependency layers remain in
  // the PCP cache for payload and variant edits instead of being reparsed. ---
  auto session = (out_session && *out_session)
                     ? *out_session
                     : std::make_shared<tnext::StageSession>();
  tnext::StageSessionOptions session_options;
  session_options.compose = opts.composition;
  session_options.max_total_memory = opts.maxMemoryBytes;
  if (opts.maxMemoryBytes > 0) {
    session_options.cache_retention = tnext::CacheRetention::LayersOnly;
  }
  session_options.resolver.allow_parent_paths = opts.allowParentRelativePaths;
  session_options.composition.variant_overrides_by_path = opts.variantOverrides;
  if (opts.payloadPolicy == PayloadPolicy::DeferAll) {
    session_options.composition.load_payloads = false;
  } else if (opts.payloadPolicy == PayloadPolicy::Whitelist) {
    session_options.composition.load_payloads = false;
    const std::set<std::string> whitelist = opts.payloadWhitelist;
    session_options.composition.payload_policy =
        [whitelist](const tnext::Path& prim_path, const std::string&) {
          return whitelist.count(prim_path.str()) != 0;
        };
  }
  if (ctrl) {
    session_options.progress_callback =
        [ctrl](const tnext::ProgressEvent& event) {
          ctrl->stage.store(static_cast<int>(event.phase));
          return !ctrl->cancel.load();
        };
  }
  bool opened = session->IsOpen();
  if (opened) {
    if (session->GetVariantSelections() != opts.variantOverrides) {
      opened = session->SetVariantSelections(opts.variantOverrides);
    }
    if (opened && opts.payloadPolicy == PayloadPolicy::Whitelist) {
      std::vector<tnext::Path> payload_paths;
      payload_paths.reserve(opts.payloadWhitelist.size());
      for (const std::string& payload_path : opts.payloadWhitelist) {
        payload_paths.emplace_back(payload_path);
      }
      opened = session->LoadPayloads(payload_paths);
    }
  } else {
    opened = session->OpenFile(path, session_options);
  }
  if (!opened) {
    if (err) *err = "next: compose failed: " + session->GetError();
    return false;
  }
  if (out_session) *out_session = session;
  if (warn && !session->GetWarning().empty()) *warn = session->GetWarning();
  const tnext::Stage& stage = session->GetStage();
  const std::vector<tnext::Path> deferredPayloads =
      session->GetDeferredPayloadPaths();
  if (!deferredPayloads.empty()) {
    std::string deferredSummary;
    const size_t shown = std::min<size_t>(deferredPayloads.size(), 8);
    for (size_t i = 0; i < shown; ++i) {
      if (!deferredSummary.empty()) deferredSummary += ", ";
      deferredSummary += deferredPayloads[i].str();
    }
    LOGI("next: %zu payloads deferred%s%s", deferredPayloads.size(),
         deferredSummary.empty() ? "" : ": ", deferredSummary.c_str());
  }
  const double time = std::isnan(opts.timecode) ? 0.0 : opts.timecode;

  // --- 2. A per-mesh converter (NOT a full-scene Convert). We triangulate each
  //        mesh on demand (ConvertMesh) as we walk the stage and free it right
  //        after baking it into a batch, so the whole RenderScene's geometry
  //        (~half the load peak) is never resident at once. ---
  tydn::ConverterConfig cfg;
  cfg.mesh.triangulate = true;
  cfg.mesh.triangulation_method = tydn::MeshConfig::TriangulationMethod::Fan;
  cfg.mesh.compute_normals = true;
  cfg.mesh.build_vertex_indices = true;
  // Keep the converter from decoding texture pixels: it records RenderTexture
  // metadata (asset path, wrap, scale/bias, channel) regardless, and we decode
  // the pixels ourselves in LoadNextTexture (base dir / .usdz aware).
  cfg.material.load_textures = false;
  cfg.material.allow_missing_textures = true;
  cfg.time_code = time;
  tydn::RenderSceneConverter conv(cfg);
  draw->upAxis = (stage.GetUpAxis() == "Z" || stage.GetUpAxis() == "z") ? "Z" : "Y";

  draw->meshes.clear();
  draw->materials.clear();
  draw->textures.clear();
  draw->skipped.clear();
  draw->triangleCount = 0;
  draw->truncated = false;
  draw->materials.emplace_back();  // default gray material (index 0)

  Bounds bounds;
  const std::size_t triCap =
      ctrl ? ctrl->maxTriangles : std::numeric_limits<std::size_t>::max();

  // --- 3a. PointInstancer pass: emit one GPU-instanced DrawMeshCPU per prototype
  //         mesh. Prototype geometry lives at the converter's authored location,
  //         so we re-express it relative to the prototype root and bake each
  //         instance's placement into a per-instance model matrix. ---
  std::unordered_set<std::string> consumed;  // proto mesh paths (skip in 3b)
  long long instTotal = 0;
  long long effectiveTris = 0;
  // Optional cap on total emitted instances (VRAM budget / headless software-GL
  // testing). Each instance matrix is 64 B, so e.g. 50M ~= 3.2 GB.
  size_t instBudget = std::numeric_limits<size_t>::max();
  if (opts.gpuGeometryBudgetBytes > 0) {
    instBudget = opts.gpuGeometryBudgetBytes / sizeof(matrix4d);
  }
  if (const char* mc = std::getenv("TUSDVIEW_NEXT_MAX_INSTANCES")) {
    instBudget = std::min(
        instBudget, static_cast<size_t>(std::strtoull(mc, nullptr, 10)));
  }

  std::function<void(const tnext::UsdPrim&)> walk = [&](const tnext::UsdPrim& p) {
    if (p.GetTypeName() == "PointInstancer") {
      double iw16[16];
      tydn::ComputeWorldTransform(stage, p, iw16, time);
      const matrix4d instancer_world = Mat4dFromArray(iw16);

      tydn::ValueArrayRead<float> positions;
      tydn::ReadFloatArray(p, "positions", time, &positions);
      const size_t n = positions.size() / 3;
      tydn::ValueArrayRead<int32_t> protoIdx;
      tydn::ReadIntArray(p, "protoIndices", time, &protoIdx);
      tydn::ValueArrayRead<float> orients;
      tydn::ReadFloatArray(p, "orientations", time, &orients);
      tydn::ValueArrayRead<float> scales;
      tydn::ReadFloatArray(p, "scales", time, &scales);
      tydn::ValueArrayRead<int64_t> invis;
      tydn::ReadInt64Array(p, "invisibleIds", time, &invis);
      tydn::ValueArrayRead<int64_t> inactive;
      tydn::ReadInt64Array(p, "inactiveIds", time, &inactive);
      tydn::ValueArrayRead<int64_t> ids;
      tydn::ReadInt64Array(p, "ids", time, &ids);
      std::unordered_set<int64_t> hiddenSet(invis.begin(), invis.end());
      hiddenSet.insert(inactive.begin(), inactive.end());
      // Optional per-instance displayColor on the instancer (rgb/instance).
      tydn::ValueArrayRead<float> instCol;
      tydn::ReadFloatArray(p, "primvars:displayColor", time, &instCol);
      const bool perInstColor = (instCol.size() == 3 * n && n > 0);

      const std::vector<tnext::Path>* protos = p.GetRelationship("prototypes");
      if (protos) {
        // Bucket instance indices by prototype for an O(instances) pass.
        std::vector<std::vector<uint32_t>> byProto(protos->size());
        for (size_t i = 0; i < n; ++i) {
          if (PointInstanceHidden(i, n, ids, hiddenSet)) continue;
          int pi = (i < protoIdx.size()) ? protoIdx[i] : 0;
          if (pi >= 0 && pi < int(protos->size())) byProto[pi].push_back(uint32_t(i));
        }
        static const float kIdentQuat[4] = {0, 0, 0, 1};
        static const float kUnitScale[3] = {1, 1, 1};

        for (size_t pi = 0; pi < protos->size(); ++pi) {
          tnext::UsdPrim protoRoot = stage.GetPrimAtPath((*protos)[pi]);
          if (!protoRoot.IsValid()) continue;
          // Consume ALL of this prototype's mesh paths (including nested-instancer
          // ones) so the static-batching pass never draws them as base geometry --
          // prototypes can live outside the instancer subtree.
          std::vector<tnext::UsdPrim> protoMeshes;
          tydn::GatherMeshPrims(protoRoot, &protoMeshes);
          for (const tnext::UsdPrim& mp : protoMeshes)
            consumed.insert(mp.GetPath().str());
          if (byProto[pi].empty()) continue;
          // One world placement (+ optional per-instance color) per visible
          // instance; EmitInstancedProto bakes mesh_rel*placement and recurses into
          // any nested instancers under the prototype.
          std::vector<matrix4d> placements;
          std::vector<float> colors;
          placements.reserve(byProto[pi].size());
          if (perInstColor) colors.reserve(byProto[pi].size() * 3);
          for (uint32_t i : byProto[pi]) {
            const float* q =
                (orients.size() >= (i + 1) * 4) ? &orients[i * 4] : kIdentQuat;
            const float* s =
                (scales.size() >= (i + 1) * 3) ? &scales[i * 3] : kUnitScale;
            placements.push_back(
                Mul4(InstanceTRS(&positions[i * 3], q, s), instancer_world));
            if (perInstColor) {
              colors.push_back(instCol[i * 3 + 0]);
              colors.push_back(instCol[i * 3 + 1]);
              colors.push_back(instCol[i * 3 + 2]);
            }
          }
          EmitInstancedProto(stage, conv, protoRoot, placements,
                             perInstColor ? &colors : nullptr, time,
                             opts.gpuSkinning, draw, &bounds, &instTotal,
                             &effectiveTris, instBudget, &consumed);
        }
      }
      return;  // do not descend into a PointInstancer's prototypes as geometry
    }
    for (const tnext::UsdPrim& c : p.GetChildren()) walk(c);
  };
  for (const tnext::UsdPrim& r : stage.GetRootPrims()) walk(r);

  // --- 3a-native. Scenegraph (instanceable) instances: prims that share an
  //     instance_prototype are flattened by the converter (one mesh set per
  //     instance). Group them and GPU-instance the prototype's geometry instead;
  //     the prototype prim itself still renders via 3b. ---
  {
    std::map<std::string, std::vector<matrix4d>> nativeGroups;
    std::function<void(const tnext::UsdPrim&)> wn = [&](const tnext::UsdPrim& p) {
      if (p.GetTypeName() == "PointInstancer") return;  // handled above
      const auto* s = p.GetPrimSpec();
      if (s && !s->meta().instance_prototype().empty()) {
        double w16[16];
        tydn::ComputeWorldTransform(stage, p, w16, time);
        nativeGroups[s->meta().instance_prototype()].push_back(Mat4dFromArray(w16));
        // The instance's children are proxies of the prototype; the converter
        // flattened them, so consume those mesh paths (excluded from 3b).
        // An instance's children ARE the prototype's children (UsdPrim follows
        // instance_prototype), so these paths are the PROTOTYPE's mesh paths.
        // Consuming them keeps the static-batching pass from drawing the
        // prototype's geometry a second time -- every placement, including the
        // prototype prim's own, is emitted below.
        std::vector<tnext::UsdPrim> ms;
        tydn::GatherMeshPrims(p, &ms);
        for (const tnext::UsdPrim& m : ms) consumed.insert(m.GetPath().str());
        return;
      }
      for (const tnext::UsdPrim& c : p.GetChildren()) wn(c);
    };
    for (const tnext::UsdPrim& r : stage.GetRootPrims()) wn(r);

    for (const auto& kv : nativeGroups) {
      tnext::UsdPrim protoRoot = stage.GetPrimAtPath(kv.first);
      if (!protoRoot.IsValid()) continue;
      // The prototype of a native-instance group is ITSELF one of the authored
      // instanceable prims (the pcp cache designates the first sibling and points
      // the others at it), so it needs its own placement here. It cannot fall back
      // to the static-batching pass: every sibling's mesh paths resolve to the
      // prototype's, so they were consumed above -- without this, one instance
      // (all of them, for a 2-instance group) silently vanished.
      std::vector<matrix4d> placements;
      placements.reserve(kv.second.size() + 1);
      double pw16[16];
      tydn::ComputeWorldTransform(stage, protoRoot, pw16, time);
      placements.push_back(Mat4dFromArray(pw16));
      placements.insert(placements.end(), kv.second.begin(), kv.second.end());
      // GPU-instance the prototype's geometry at each placement; EmitInstancedProto
      // recurses into any nested instancers under the prototype.
      EmitInstancedProto(stage, conv, protoRoot, placements,
                         /*placementColors=*/nullptr, time, opts.gpuSkinning, draw,
                         &bounds, &instTotal, &effectiveTris, instBudget,
                         /*consumed=*/nullptr);
    }
  }

  // --- 3b. Non-instanced meshes: STATIC BATCHING. Each mesh's vertices are baked
  //         to world space and merged into a few big buffers keyed by (purpose,
  //         geometric-normal), so a 33k-mesh scene draws in a handful of calls
  //         (one VAO/VBO/EBO per batch) instead of 33k -- far less draw-call + GL
  //         object overhead. Purpose stays per-batch so the GUI toggles still
  //         work; per-mesh pick/hide is not a goal of the flat large-scene path.
  struct Batch {
    DrawMeshCPU dm;
    bool anyColor = false;
    // A batch gains skin attributes the first time a SKINNED mesh joins it; the
    // vertices already in it (and every unskinned mesh that joins later) get
    // zero weights, which the vertex shader passes through unskinned. Joint
    // indices are absolute bone-texture rows, so one batch can draw vertices
    // posed by several different skeletons.
    bool anySkin = false;
    // True when a SKINNED mesh joined this batch under CPU skinning: its vertices
    // are one baked pose, so they do not bound the rig over the animation (the GPU
    // path signals the same thing through anySkin).
    bool anyCpuSkin = false;
    int matId = 0;
  };
  // key = (purpose, geometricNormal, materialId) -> current batch. Keying by
  // material keeps per-material draws distinct so each batch can reference its
  // own DrawMaterialCPU instead of the single default gray material.
  std::map<std::tuple<std::string, bool, int>, Batch> open;

  // Texture cache for material building: resolve texture assets against the
  // source directory and, for a .usdz package, its embedded entries. The size
  // cap and byte budget are applied while decoding, so a large scene never
  // materializes every texture at full resolution.
  NextTexCache texCache;
  tydn::TextureDecodeOptions texOpts;
  texOpts.base_dir = tinyusdz::io::GetBaseDir(path);
  texOpts.max_edge = opts.textureOptions.maxTextureSize > 0
                         ? uint32_t(opts.textureOptions.maxTextureSize)
                         : 0u;
  texOpts.budget_bytes =
      opts.textureOptions.textureBudgetMB > 0
          ? uint64_t(opts.textureOptions.textureBudgetMB) * 1024ull * 1024ull
          : 0ull;
  tinyusdz::next::USDZReader usdzArchive;
  if (path.size() >= 5 && path.compare(path.size() - 5, 5, ".usdz") == 0 &&
      usdzArchive.OpenFile(path)) {
    texOpts.usdz = &usdzArchive;
  }
  texCache.decoder = std::make_unique<tydn::TextureDecoder>(texOpts);

  // Resolve a material prim path to a DrawScene material index (cached by path).
  // Unbound / unconvertible -> 0 (default gray material).
  std::unordered_map<std::string, int> matIndexByPath;
  // A material is built ONCE and shared by every mesh that binds it, but the
  // UV-set names are the MESH's. Resolve the texture->UV-set routing at the first
  // mesh that binds the material, and warn if a later mesh would have resolved it
  // differently (two meshes binding one material with differently-named secondary
  // sets cannot both be satisfied without splitting the material -- rare enough
  // that reporting beats silently rendering one of them with the wrong UVs).
  std::unordered_map<std::string, std::string> matUv1ByPath;
  auto resolveMaterialPath = [&](const std::string& mpath,
                                 const std::string& uv0Name,
                                 const std::string& uv1Name) -> int {
    if (mpath.empty()) return 0;
    auto it = matIndexByPath.find(mpath);
    if (it != matIndexByPath.end()) {
      const auto uit = matUv1ByPath.find(mpath);
      if (uit != matUv1ByPath.end() && uit->second != uv1Name) {
        LOGW("material '%s' is bound by meshes with different secondary UV sets "
             "('%s' vs '%s'); keeping the first. Textures routed to the second "
             "UV set may sample the wrong coordinates on the later mesh.",
             mpath.c_str(), uit->second.c_str(), uv1Name.c_str());
      }
      return it->second;
    }
    tnext::UsdPrim matPrim = stage.GetPrimAtPath(mpath);
    int idx = matPrim.IsValid() ? BuildNextMaterial(stage, conv, matPrim, draw,
                                                    texCache, uv0Name, uv1Name)
                                : -1;
    if (idx < 0) idx = 0;
    matIndexByPath[mpath] = idx;
    matUv1ByPath[mpath] = uv1Name;
    return idx;
  };
  // Full UsdShade binding semantics: the purpose fallback chain
  // (material:binding:preview -> material:binding -> material:binding:full) AND
  // inheritance from ancestors. Production scenes (ALab) bind purpose-scoped on
  // an ancestor Xform and never author a plain `material:binding` on the Mesh —
  // reading only the Mesh's own `material:binding` dropped every material (and
  // so every texture) on those scenes.
  auto resolveMeshMaterial = [&](const tnext::UsdPrim& mp,
                                 const std::string& uv0Name = std::string(),
                                 const std::string& uv1Name = std::string()) -> int {
    return resolveMaterialPath(
        tnext::GetInheritedBoundMaterialPath(stage, mp.GetPath().str()), uv0Name,
        uv1Name);
  };

  // A clone of material `base` with its alpha replaced, made once per distinct
  // (material, opacity) pair. Lets a mesh's `displayOpacity` render through the
  // existing material alpha without mutating a material other meshes share.
  std::map<std::pair<int, int>, int> matAlphaVariants;
  size_t varyingOpacityMeshes = 0;
  auto materialWithAlpha = [&](int base, float alpha) -> int {
    if (base < 0 || static_cast<size_t>(base) >= draw->materials.size()) return base;
    // Quantize so near-identical opacities share one variant.
    const int key = static_cast<int>(std::lround(alpha * 1000.0f));
    auto it = matAlphaVariants.find({base, key});
    if (it != matAlphaVariants.end()) return it->second;
    DrawMaterialCPU variant = draw->materials[static_cast<size_t>(base)];
    variant.alpha = alpha;
    if (variant.alphaMode == static_cast<int>(AlphaMode::Opaque)) {
      variant.alphaMode = static_cast<int>(AlphaMode::Blend);
    }
    const int idx = static_cast<int>(draw->materials.size());
    draw->materials.push_back(std::move(variant));
    matAlphaVariants[{base, key}] = idx;
    return idx;
  };

  // GeomSubset per-face materials: when a mesh has `face` GeomSubset children
  // bound to materials, produce a per-triangle material id (else leave *triMat
  // empty -> the caller uses the whole-mesh material). tydra-next's Convert()
  // never fills material_subsets, so we read the GeomSubsets off the stage and
  // reconstruct the triangle->face mapping from the original face vertex counts
  // (fan/earcut both emit c-2 triangles per face, in face order).
  auto buildTriMaterials = [&](const tnext::UsdPrim& mp, const tydn::RenderMesh& m,
                               size_t numTris, int wholeMat,
                               std::vector<int>* triMat) {
    triMat->clear();
    struct Sub { std::vector<int32_t> faces; int mat; };
    std::vector<Sub> subs;
    for (const tnext::UsdPrim& c : mp.GetChildren()) {
      if (c.GetTypeName() != "GeomSubset") continue;
      bool isFace = true;  // elementType defaults to "face"
      if (const tnext::Value* et = c.GetPropertyValue("elementType"))
        if (const std::string* t = et->as_token())
          isFace = t->empty() || *t == "face";
      if (!isFace) continue;
      // Purpose chain, but no ancestor walk: a subset that binds nothing itself
      // must keep falling back to the whole-mesh material.
      const std::string bind = tnext::GetBoundMaterialPath(c);
      if (bind.empty()) continue;
      std::vector<int32_t> faces = ReadInts(c, "indices", time);
      if (faces.empty()) continue;
      subs.push_back({std::move(faces),
                      resolveMaterialPath(bind, m.texcoords_0_name,
                                          m.texcoords_1_name)});
    }
    if (subs.empty()) return;

    const std::vector<uint32_t> fvc = m.face_vertex_counts.flatten();
    std::vector<int> triFace;
    triFace.reserve(numTris);
    for (size_t f = 0; f < fvc.size(); ++f)
      for (uint32_t k = 2; k < fvc[f]; ++k) triFace.push_back(static_cast<int>(f));
    if (triFace.size() != numTris) return;  // triangulation mismatch -> whole-mesh

    std::vector<int> faceMat(fvc.size(), wholeMat);
    for (const Sub& s : subs)
      for (int32_t f : s.faces)
        if (f >= 0 && static_cast<size_t>(f) < faceMat.size()) faceMat[f] = s.mat;

    std::vector<int> tm(numTris);
    bool split = false;
    for (size_t t = 0; t < numTris; ++t) {
      tm[t] = faceMat[triFace[t]];
      if (tm[t] != wholeMat) split = true;
    }
    if (split) *triMat = std::move(tm);  // uniform -> leave empty
  };
  const size_t bytesPerBatchVertex = sizeof(DrawVertex) + 3 * sizeof(uint32_t);
  const size_t stagingVertexCap =
      opts.uploadStagingBytes > 0
          ? std::max<size_t>(size_t(64) << 10,
                             opts.uploadStagingBytes / bytesPerBatchVertex)
          : (size_t(8) << 20);
  const size_t kBatchVtxCap =
      std::min<size_t>(size_t(8) << 20, stagingVertexCap);

  auto flushBatch = [&](Batch& b) {
    if (b.dm.vertices.empty()) return;
    if (!b.anyColor) b.dm.vertexColors.clear();
    if (b.anySkin && b.dm.jointIdx.size() == b.dm.vertices.size() * 4) {
      // Bone rows are absolute and geomBind/world are already folded into them
      // (BuildNextSkinningFrame), so the batch needs no per-mesh bind matrix and
      // no row offset. skelId only has to be valid for the skinning frame to
      // pick this mesh up; the next path indexes DrawScene::nextSkels, not
      // RenderScene::skeletons.
      b.dm.skelId = 0;
      b.dm.skinMatrixBase = 0;
      std::memset(b.dm.skinGeomBind, 0, sizeof(b.dm.skinGeomBind));
      b.dm.skinGeomBind[0] = b.dm.skinGeomBind[5] = b.dm.skinGeomBind[10] =
          b.dm.skinGeomBind[15] = 1.0f;
    } else {
      b.dm.jointIdx.clear();
      b.dm.jointWt.clear();
    }
    // This batch's OWN world bounds, over its (already world-baked) vertices.
    // Copying the running scene-bounds accumulator here instead -- as this used to
    // -- gives every static mesh the scene-spanning box, which makes both the
    // per-mesh frustum cull and raster LOD no-ops: nothing is ever outside the
    // frustum or small on screen.
    //
    // Skinned batches keep the conservative scene box (either skinning mode): the
    // vertices here are a single pose -- rest for GPU skinning, one sampled time
    // for CPU skinning -- so they do not bound the mesh over the animation, and a
    // tight box would pop it out of view as the rig moves.
    if ((b.anySkin || b.anyCpuSkin) && bounds.has) {
      for (int k = 0; k < 3; ++k) {
        b.dm.aabbMin[k] = bounds.mn[k]; b.dm.aabbMax[k] = bounds.mx[k];
      }
    } else {
      float lo[3] = {b.dm.vertices[0].px, b.dm.vertices[0].py,
                     b.dm.vertices[0].pz};
      float hi[3] = {lo[0], lo[1], lo[2]};
      for (const DrawVertex& v : b.dm.vertices) {
        lo[0] = std::min(lo[0], v.px); hi[0] = std::max(hi[0], v.px);
        lo[1] = std::min(lo[1], v.py); hi[1] = std::max(hi[1], v.py);
        lo[2] = std::min(lo[2], v.pz); hi[2] = std::max(hi[2], v.pz);
      }
      for (int k = 0; k < 3; ++k) {
        b.dm.aabbMin[k] = lo[k] - b.dm.morphExtent[k];
        b.dm.aabbMax[k] = hi[k] + b.dm.morphExtent[k];
      }
    }
    b.dm.submeshes.push_back(
        DrawSubmesh{0, static_cast<uint32_t>(b.dm.indices.size()), b.matId});
    std::memset(b.dm.world, 0, sizeof(b.dm.world));
    b.dm.world[0] = b.dm.world[5] = b.dm.world[10] = b.dm.world[15] = 1.0f;
    draw->triangleCount += b.dm.indices.size() / 3;
    draw->meshes.push_back(std::move(b.dm));
    b = Batch();
  };

  // Gather the non-prototype mesh prims by walking the stage (there is no
  // RenderScene node list now); convert + bake each one streaming.
  struct PendingMesh {
    tnext::UsdPrim prim;
    bool forceProxy = false;
  };
  std::vector<PendingMesh> meshPrims;
  std::unordered_set<std::string> pendingPaths;
  float deferredTranslationMin[3] = {1e30f, 1e30f, 1e30f};
  float deferredTranslationMax[3] = {-1e30f, -1e30f, -1e30f};
  size_t deferredTranslationCount = 0;
  {
    tydn::RenderExtractOptions xopts;
    xopts.time_code = time;
    xopts.stop_at_point_instancers = true;
    xopts.stop_at_native_instances = true;
    tydn::RenderExtractResult xres;
    tydn::CollectRenderPrims(stage, xopts, &xres);
    meshPrims.reserve(xres.meshes.size());
    for (const tydn::RenderPrimRecord& rec : xres.meshes) {
      if (!consumed.count(rec.path)) {
        meshPrims.push_back(PendingMesh{rec.prim, false});
        pendingPaths.insert(rec.path);
      }
    }
    for (const tnext::Path& deferred : deferredPayloads) {
      if (pendingPaths.count(deferred.str()) != 0) continue;
      tnext::UsdPrim prim = stage.GetPrimAtPath(deferred);
      if (!prim.IsValid()) continue;
      meshPrims.push_back(PendingMesh{prim, true});
      pendingPaths.insert(deferred.str());
      double world[16];
      tydn::ComputeWorldTransform(stage, prim, world, time);
      for (size_t axis = 0; axis < 3; ++axis) {
        const float translation = static_cast<float>(world[12 + axis]);
        deferredTranslationMin[axis] =
            std::min(deferredTranslationMin[axis], translation);
        deferredTranslationMax[axis] =
            std::max(deferredTranslationMax[axis], translation);
      }
      ++deferredTranslationCount;
    }
  }
  float deferredProxyHalfSize = 1.0f;
  if (deferredTranslationCount > 1) {
    float diagonal2 = 0.0f;
    for (size_t axis = 0; axis < 3; ++axis) {
      const float span =
          deferredTranslationMax[axis] - deferredTranslationMin[axis];
      diagonal2 += span * span;
    }
    deferredProxyHalfSize = std::max(1.0f, std::sqrt(diagonal2) / 200.0f);
  }

  bool capped = false;
  long long totalTris = 0;
  size_t admittedGeometryBytes = 0;
  size_t proxyMeshCount = 0;
  // Weld effectiveness: emitted vertices vs authored points. ~1.0 means the
  // faceVarying split cost nothing; a ratio near the corners-per-point count
  // means the weld is not catching (see FillFlatGeometry).
  size_t weldedVertices = 0;
  size_t sourcePoints = 0;
  size_t deferredProxyCount = 0;
  for (const PendingMesh& pending : meshPrims) {
    const tnext::UsdPrim& mp = pending.prim;
    if (ctrl && ctrl->cancel.load()) break;
    if (capped) break;
    tydn::RenderMesh m;
    const tydn::GeometryInfo geometry =
        conv.GetGeometryInfo(mp, tydn::GeometryKind::Mesh);
    const bool overGeometryBudget =
        opts.gpuGeometryBudgetBytes > 0 &&
        (admittedGeometryBytes >= opts.gpuGeometryBudgetBytes ||
         geometry.estimated_resident_bytes >
             opts.gpuGeometryBudgetBytes - admittedGeometryBytes);
    if (pending.forceProxy || overGeometryBudget) {
      bool proxyConverted = conv.ConvertExtentProxy(mp, &m);
      if (!proxyConverted && pending.forceProxy) {
        const tydn::Float3 mn(-deferredProxyHalfSize,
                             -deferredProxyHalfSize,
                             -deferredProxyHalfSize);
        const tydn::Float3 mx(deferredProxyHalfSize,
                             deferredProxyHalfSize,
                             deferredProxyHalfSize);
        proxyConverted = conv.ConvertBoundsProxy(mp, mn, mx, &m);
      }
      if (!proxyConverted) {
        draw->skipped.push_back("GPU budget skipped mesh without extent: " +
                                mp.GetPath().str());
        draw->truncated = true;
        continue;
      }
      ++proxyMeshCount;
      if (pending.forceProxy) ++deferredProxyCount;
      draw->truncated = true;
    } else if (!conv.ConvertRenderableMesh(stage, mp, &m)) {
      continue;
    }
    DrawMeshCPU loc;
    std::vector<uint32_t> vertexToPoint;
    if (!FillFlatGeometry(m, &loc, &vertexToPoint)) continue;
    weldedVertices += loc.vertices.size();
    sourcePoints += m.point_count();
    admittedGeometryBytes +=
        loc.vertices.size() * sizeof(DrawVertex) +
        loc.indices.size() * sizeof(uint32_t) +
        loc.vertexColors.size() * sizeof(float) +
        loc.vertexAlpha.size() * sizeof(float) +
        loc.uv1.size() * sizeof(float) +
        loc.tangents.size() * sizeof(float) +
        loc.binormals.size() * sizeof(float);
    const std::string purpose = ResolveNextPurpose(stage, mp.GetPath().str());
    int wholeMat = resolveMeshMaterial(mp, m.texcoords_0_name, m.texcoords_1_name);
    double mw16[16];
    tydn::ComputeWorldTransform(stage, mp, mw16, time);
    // Skeletal skinning, before the vertices are world-baked into the batch.
    // GPU: keep the rest pose and emit per-vertex joint attributes (the shader
    // poses every frame). CPU: bake the static pose at `time` into the geometry.
    // Both no-op for unskinned meshes.
    bool cpuSkinned = false;
    if (opts.gpuSkinning) {
      SetupGpuSkinNext(stage, mp, time, &loc, vertexToPoint, m.point_count(),
                       mw16, draw);
    } else {
      cpuSkinned =
          BakeSkinning(stage, mp, time, &loc, vertexToPoint, m.point_count());
    }
    float Mf[16];
    for (int k = 0; k < 16; ++k) Mf[k] = static_cast<float>(mw16[k]);
    const float* M = Mf;  // row-major, p*M (same as the converter's node xform)

    // World-transform vertices in place (positions + normals), so both the
    // single- and multi-material append paths just copy the vertex.
    for (DrawVertex& v : loc.vertices) {
      float wp[3], wn[3];
      for (int c = 0; c < 3; ++c) {
        wp[c] = v.px * M[0 * 4 + c] + v.py * M[1 * 4 + c] + v.pz * M[2 * 4 + c] +
                M[3 * 4 + c];
        wn[c] = v.nx * M[0 * 4 + c] + v.ny * M[1 * 4 + c] + v.nz * M[2 * 4 + c];
      }
      v.px = wp[0]; v.py = wp[1]; v.pz = wp[2];
      const float nl = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
      if (nl > 1e-12f) { v.nx = wn[0] / nl; v.ny = wn[1] / nl; v.nz = wn[2] / nl; }
      else { v.nx = 0; v.ny = 0; v.nz = 0; }
    }
    // USD `primvars:displayOpacity`. No renderer here samples a per-vertex alpha
    // attribute, but the overwhelmingly common authoring is one opacity for the
    // whole mesh -- so when it does not actually vary, fold it into an
    // alpha-adjusted MATERIAL VARIANT (a clone of the bound material, made once
    // per distinct opacity). That renders correctly through the existing
    // material alpha, and folding it into the shared material in place would be
    // wrong the moment two meshes with different opacities share one material.
    // A genuinely per-vertex opacity keeps its buffer and is reported below.
    if (!loc.vertexAlpha.empty()) {
      float lo = loc.vertexAlpha[0], hi = loc.vertexAlpha[0];
      for (float a : loc.vertexAlpha) { lo = std::min(lo, a); hi = std::max(hi, a); }
      if (hi - lo <= 1e-6f) {
        if (lo < 1.0f - 1e-6f) wholeMat = materialWithAlpha(wholeMat, lo);
        loc.vertexAlpha.clear();
        loc.vertexAlpha.shrink_to_fit();
      } else {
        ++varyingOpacityMeshes;
      }
    }

    const bool hasC = !loc.vertexColors.empty();
    const bool hasSkin = loc.jointIdx.size() == loc.vertices.size() * 4 &&
                         loc.jointWt.size() == loc.vertices.size() * 4;
    // Give `b` skin attribute arrays sized to the vertices it already holds
    // (zero-weight = unskinned), so the two arrays stay parallel to b.dm.vertices.
    auto openSkin = [&](Batch& b) {
      if (cpuSkinned) b.anyCpuSkin = true;
      if (!hasSkin || b.anySkin) return;
      b.dm.jointIdx.assign(b.dm.vertices.size() * 4, 0u);
      b.dm.jointWt.assign(b.dm.vertices.size() * 4, 0.0f);
      b.anySkin = true;
    };
    // Append vertex `i`'s influences (or zeros when this mesh is unskinned but
    // the batch is already carrying skin attributes).
    auto pushSkin = [&](Batch& b, size_t i) {
      if (!b.anySkin) return;
      for (size_t k = 0; k < 4; ++k) {
        b.dm.jointIdx.push_back(hasSkin ? loc.jointIdx[i * 4 + k] : 0u);
        b.dm.jointWt.push_back(hasSkin ? loc.jointWt[i * 4 + k] : 0.0f);
      }
    };

    // Per-triangle materials from face GeomSubsets (empty => uniform wholeMat).
    std::vector<int> triMat;
    buildTriMaterials(mp, m, loc.indices.size() / 3, wholeMat, &triMat);

    if (triMat.empty()) {
      // --- Single-material fast path: append the whole mesh to one batch. ---
      Batch& b = open[{purpose, loc.geometricNormal, wholeMat}];
      b.matId = wholeMat;
      if (!b.dm.vertices.empty() &&
          b.dm.vertices.size() + loc.vertices.size() > kBatchVtxCap) {
        flushBatch(b);  // resets b in the map slot
      }
      b.dm.purpose = purpose;
      b.dm.geometricNormal = loc.geometricNormal;
      // Allocate the batch color buffer only once a mesh actually contributes a
      // color: back-fill white for the vertices already in the batch. No-color
      // batches (e.g. the hotel) then never allocate a 12 B/vertex white buffer.
      if (hasC && !b.anyColor) {
        b.dm.vertexColors.assign(b.dm.vertices.size() * 3, 1.0f);
        b.anyColor = true;
      }
      openSkin(b);
      // NOTE: rely on the vectors' amortized (doubling) growth -- an exact
      // reserve(size()+n) per mesh would reallocate the whole batch (O(N^2)).
      const uint32_t vbase = static_cast<uint32_t>(b.dm.vertices.size());
      for (size_t i = 0; i < loc.vertices.size(); ++i) {
        b.dm.vertices.push_back(loc.vertices[i]);
        pushSkin(b, i);
        if (b.anyColor) {
          if (hasC) {
            b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 0]);
            b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 1]);
            b.dm.vertexColors.push_back(loc.vertexColors[3 * i + 2]);
          } else {
            b.dm.vertexColors.push_back(1.0f);
            b.dm.vertexColors.push_back(1.0f);
            b.dm.vertexColors.push_back(1.0f);
          }
        }
      }
      for (uint32_t idx : loc.indices) b.dm.indices.push_back(vbase + idx);
      for (uint32_t widx : loc.wireframeIndices)
        b.dm.wireframeIndices.push_back(vbase + widx);
    } else {
      // --- Multi-material (GeomSubset) path: route each triangle to its
      //     material's batch, appending only the vertices that batch references
      //     (compacted per group so batches don't carry unused vertices). ---
      std::set<int> groups(triMat.begin(), triMat.end());
      const size_t numTris = loc.indices.size() / 3;
      bool firstGroup = true;
      for (int gm : groups) {
        Batch& b = open[{purpose, loc.geometricNormal, gm}];
        b.matId = gm;
        if (!b.dm.vertices.empty() &&
            b.dm.vertices.size() + loc.vertices.size() > kBatchVtxCap) {
          flushBatch(b);
        }
        b.dm.purpose = purpose;
        b.dm.geometricNormal = loc.geometricNormal;
        if (hasC && !b.anyColor) {
          b.dm.vertexColors.assign(b.dm.vertices.size() * 3, 1.0f);
          b.anyColor = true;
        }
        openSkin(b);
        std::vector<int> remap(loc.vertices.size(), -1);
        auto vtx = [&](uint32_t vi) -> uint32_t {
          if (remap[vi] < 0) {
            remap[vi] = static_cast<int>(b.dm.vertices.size());
            b.dm.vertices.push_back(loc.vertices[vi]);
            pushSkin(b, vi);
            if (b.anyColor) {
              if (hasC) {
                b.dm.vertexColors.push_back(loc.vertexColors[3 * vi + 0]);
                b.dm.vertexColors.push_back(loc.vertexColors[3 * vi + 1]);
                b.dm.vertexColors.push_back(loc.vertexColors[3 * vi + 2]);
              } else {
                b.dm.vertexColors.push_back(1.0f);
                b.dm.vertexColors.push_back(1.0f);
                b.dm.vertexColors.push_back(1.0f);
              }
            }
          }
          return static_cast<uint32_t>(remap[vi]);
        };
        for (size_t t = 0; t < numTris; ++t) {
          if (triMat[t] != gm) continue;
          b.dm.indices.push_back(vtx(loc.indices[3 * t + 0]));
          b.dm.indices.push_back(vtx(loc.indices[3 * t + 1]));
          b.dm.indices.push_back(vtx(loc.indices[3 * t + 2]));
        }
        // Attach the mesh's wireframe once (to the first group's batch, mapped
        // through vtx so its vertices exist there) -- avoids cross-batch dupes.
        if (firstGroup) {
          for (uint32_t widx : loc.wireframeIndices)
            b.dm.wireframeIndices.push_back(vtx(widx));
          firstGroup = false;
        }
      }
    }

    // World-space AABB from the 8 local-bbox corners (scene bounds for framing).
    const tydn::Float3& lo = m.bbox_min;
    const tydn::Float3& hi = m.bbox_max;
    for (int corner = 0; corner < 8; ++corner) {
      float lp[3] = {(corner & 1) ? hi.x : lo.x, (corner & 2) ? hi.y : lo.y,
                     (corner & 4) ? hi.z : lo.z};
      float wp[3];
      for (int c = 0; c < 3; ++c)
        wp[c] = lp[0] * M[0 * 4 + c] + lp[1] * M[1 * 4 + c] +
                lp[2] * M[2 * 4 + c] + M[3 * 4 + c];
      bounds.add(wp);
    }
    totalTris += static_cast<long long>(loc.indices.size() / 3);
    if (static_cast<std::size_t>(totalTris) > triCap) {
      draw->truncated = true;
      capped = true;
    }
  }

  if (proxyMeshCount > 0 && warn) {
    if (!warn->empty()) *warn += "\n";
    *warn += "next: emitted " + std::to_string(proxyMeshCount) +
             " geometry proxies before full payload materialization";
  }
  if (deferredProxyCount > 0) {
    LOGI("next: first frame uses %zu deferred-payload proxies",
         deferredProxyCount);
  }
  for (auto& kv : open) flushBatch(kv.second);

  // UsdVol volumes (OpenVDB): emit DrawVolumeCPU + extend bounds.
  BuildNextVolumes(stage, path, time, draw, &bounds);
  BuildNextLights(stage, path, time, opts.textureOptions, draw);

  if (bounds.has) {
    for (int k = 0; k < 3; ++k) {
      draw->aabbMin[k] = bounds.mn[k]; draw->aabbMax[k] = bounds.mx[k];
    }
    draw->hasBounds = true;
  }

  // Derive the raster preview key light from the lights BuildNextLights added
  // (after bounds, so finite-light directions use the scene center).
  UpdatePreviewLight(draw);

  // Purpose breakdown (so the GUI's purpose toggles have something to hide).
  size_t nGuide = 0, nProxy = 0, nRender = 0;
  for (const DrawMeshCPU& dm : draw->meshes) {
    if (dm.purpose == "guide") ++nGuide;
    else if (dm.purpose == "proxy") ++nProxy;
    else if (dm.purpose == "render") ++nRender;
  }
  LOGI("next: '%s' -> %zu draws (%zu guide, %zu proxy, %zu render), %lld instances, "
       "%zu unique tris (%lld effective), %zu materials, %zu textures, "
       "instXform VRAM ~%.2f GB, up=%s%s",
       path.c_str(), draw->meshes.size(), nGuide, nProxy, nRender, instTotal,
       draw->triangleCount, effectiveTris, draw->materials.size(),
       draw->textures.size(), double(instTotal) * 48.0 / 1e9,
       draw->upAxis.c_str(), draw->truncated ? " (truncated)" : "");
  if (sourcePoints > 0) {
    LOGI("next: weld %zu vertices from %zu points (%.2fx)", weldedVertices,
         sourcePoints,
         double(weldedVertices) / double(sourcePoints));
  }
  if (varyingOpacityMeshes > 0 && warn) {
    if (!warn->empty()) *warn += "\n";
    *warn += "next: " + std::to_string(varyingOpacityMeshes) +
             " mesh(es) author a per-vertex displayOpacity; it is carried on "
             "DrawMeshCPU::vertexAlpha but no renderer samples a per-vertex "
             "alpha attribute yet, so they render at their material alpha";
  }
  if (texCache.decoder && !draw->textures.empty()) {
    const tydn::TextureDecoder& dec = *texCache.decoder;
    LOGI("next: textures %zu, decoded %.1f MB (cap %u px, budget %.0f MB, "
         "%llu downscaled)",
         draw->textures.size(),
         double(dec.decoded_bytes()) / (1024.0 * 1024.0),
         dec.options().max_edge,
         double(dec.options().budget_bytes) / (1024.0 * 1024.0),
         static_cast<unsigned long long>(dec.downscaled_count()));
  }

  if (draw->meshes.empty() && draw->volumes.empty()) {
    if (err) *err = "next: no renderable mesh produced";
    return false;
  }
  return true;
}

}  // namespace tusdview
