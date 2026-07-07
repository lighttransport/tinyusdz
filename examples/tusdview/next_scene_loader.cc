// SPDX-License-Identifier: Apache-2.0
// tusdview - `next` loader -> tydra-next RenderScene -> DrawScene adapter, plus
// PointInstancer extraction into GPU-instanced draws.

#include "next_scene_loader.hh"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "log.hh"

// `next` + tydra-next (built on demand; see CMakeLists.txt).
#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-extract.hh"
#include "tydra/next/scene-access.hh"  // ComputeWorldTransform
#include "value-types.hh"              // value::matrix4d / quatf / double3
#include "xform.hh"                    // to_matrix3x3 / to_matrix / inverse
#include "io-util.hh"                  // io::GetBaseDir
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

// Build interleaved DrawVertex geometry (mesh-LOCAL space) + indices from a
// tydra-next RenderMesh. Returns false if there is no renderable geometry.
bool FillFlatGeometry(const tydn::RenderMesh& m, DrawMeshCPU* dm) {
  const size_t np = m.point_count();
  if (np == 0 || m.triangulated_indices.size() < 3) return false;
  // Authored vertex normals -> smooth shading; otherwise shade geometrically in
  // the shader (screen-derivative normal), which reads correctly on hard
  // surfaces instead of being smeared by averaged smooth normals.
  const bool authoredNormals = m.has_normals() &&
                               m.normals_interp == tydn::Interpolation::Vertex &&
                               m.normals.size() == 3 * np;
  dm->geometricNormal = !authoredNormals;

  const bool hasUV = m.has_texcoords() &&
                     m.texcoords_0_interp == tydn::Interpolation::Vertex &&
                     m.texcoords_0.size() == 2 * np;

  // Per-vertex displayColor: Vertex (per-point) directly; Constant broadcast.
  if (!m.colors.empty()) {
    if (m.colors_interp == tydn::Interpolation::Vertex &&
        m.colors.size() >= 3 * np) {
      dm->vertexColors.resize(3 * np);
      for (size_t i = 0; i < np; ++i) {
        dm->vertexColors[3 * i + 0] = m.colors[3 * i + 0];
        dm->vertexColors[3 * i + 1] = m.colors[3 * i + 1];
        dm->vertexColors[3 * i + 2] = m.colors[3 * i + 2];
      }
    } else if (m.colors_interp == tydn::Interpolation::Constant &&
               m.colors.size() >= 3) {
      dm->vertexColors.resize(3 * np);
      for (size_t i = 0; i < np; ++i) {
        dm->vertexColors[3 * i + 0] = m.colors[0];
        dm->vertexColors[3 * i + 1] = m.colors[1];
        dm->vertexColors[3 * i + 2] = m.colors[2];
      }
    }
  }

  dm->name = m.name;
  dm->absPath = m.prim_path;
  dm->vertices.resize(np);
  for (size_t i = 0; i < np; ++i) {
    DrawVertex& v = dm->vertices[i];
    v.px = m.points[3 * i + 0];
    v.py = m.points[3 * i + 1];
    v.pz = m.points[3 * i + 2];
    v.nx = authoredNormals ? m.normals[3 * i + 0] : 0.0f;
    v.ny = authoredNormals ? m.normals[3 * i + 1] : 0.0f;
    v.nz = authoredNormals ? m.normals[3 * i + 2] : 0.0f;
    v.u = hasUV ? m.texcoords_0[2 * i + 0] : 0.0f;
    v.v = hasUV ? m.texcoords_0[2 * i + 1] : 0.0f;
  }
  dm->indices.resize(m.triangulated_indices.size());
  for (size_t i = 0; i < m.triangulated_indices.size(); ++i) {
    dm->indices[i] = m.triangulated_indices[i];
  }
  dm->submeshes.push_back(
      DrawSubmesh{0, static_cast<uint32_t>(dm->indices.size()), 0});

  // Original-polygon wireframe edges: the perimeter of each USD face, from the
  // pre-triangulation topology. This shows quads/ngons (not triangulation
  // diagonals) and is correct even for double-sided meshes (whose triangulation
  // doubles the tri count, defeating any per-triangle scheme). Indices match the
  // vertex buffer because FillFlatGeometry keeps vertex i == point i.
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
          const uint32_t a = fvi[off + k];
          const uint32_t b = fvi[off + (k + 1u) % c];
          if (a == b || a >= np || b >= np) continue;
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
void BakeBlendShapes(const tnext::Stage& stage, const tnext::UsdPrim& meshPrim,
                     double time, DrawMeshCPU* dm) {
  const std::vector<std::string> shapeNames =
      ReadTokens(meshPrim, "skel:blendShapes", time);
  const std::vector<tnext::Path>* targets =
      meshPrim.GetRelationship("skel:blendShapeTargets");
  if (shapeNames.empty() || !targets || targets->empty()) return;

  const std::unordered_map<std::string, float> weights =
      ResolveBlendWeights(stage, meshPrim, time);
  if (weights.empty()) return;

  const size_t np = dm->vertices.size();
  if (np == 0) return;
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
  for (size_t i = 0; i < np; ++i) {
    dm->vertices[i].px += delta[3 * i + 0];
    dm->vertices[i].py += delta[3 * i + 1];
    dm->vertices[i].pz += delta[3 * i + 2];
  }

  // Recompute smooth vertex normals from the baked positions so authored-normal
  // (smooth-shaded) meshes don't keep stale rest normals. Geometric-shaded meshes
  // re-derive normals in the shader and ignore the attribute, so skip them.
  if (dm->geometricNormal) return;
  for (size_t i = 0; i < np; ++i) {
    dm->vertices[i].nx = dm->vertices[i].ny = dm->vertices[i].nz = 0.0f;
  }
  for (size_t t = 0; t + 2 < dm->indices.size(); t += 3) {
    const uint32_t a = dm->indices[t], b = dm->indices[t + 1],
                   c = dm->indices[t + 2];
    if (a >= np || b >= np || c >= np) continue;
    const DrawVertex& va = dm->vertices[a];
    const DrawVertex& vb = dm->vertices[b];
    const DrawVertex& vc = dm->vertices[c];
    // Cross product of two edges = area-weighted face normal (accumulated to
    // each vertex for an area-weighted smooth normal).
    const float e1x = vb.px - va.px, e1y = vb.py - va.py, e1z = vb.pz - va.pz;
    const float e2x = vc.px - va.px, e2y = vc.py - va.py, e2z = vc.pz - va.pz;
    const float fnx = e1y * e2z - e1z * e2y;
    const float fny = e1z * e2x - e1x * e2z;
    const float fnz = e1x * e2y - e1y * e2x;
    for (uint32_t v : {a, b, c}) {
      dm->vertices[v].nx += fnx;
      dm->vertices[v].ny += fny;
      dm->vertices[v].nz += fnz;
    }
  }
  for (size_t i = 0; i < np; ++i) {
    DrawVertex& v = dm->vertices[i];
    const float len = std::sqrt(v.nx * v.nx + v.ny * v.ny + v.nz * v.nz);
    if (len > 1e-12f) {
      const float inv = 1.0f / len;
      v.nx *= inv; v.ny *= inv; v.nz *= inv;
    }
  }
}

// Build GPU-morph CSR channels for a prototype mesh, so the instanced raster
// shader morphs per-frame from a tiny per-channel coefficient buffer instead of
// the morph being baked into geometry. Mirrors BuildMorphChannels in
// mesh_build.cc, but point-indexed (FillFlatGeometry keeps vertex i == point i,
// so pointIndices map straight to vertices) and reading directly from the next
// stage. A channel = one delta stream (an in-between sample or the primary); per
// target the channels are [in-betweens ascending..., primary] with usdWeights
// [ibWeights..., 1.0], matching EvalMorphChannelCoeffs' bracket eval. No-op (no
// channels) when the mesh has no resolvable blendshape targets.
void BuildMorphChannelsNext(const tnext::Stage& stage,
                            const tnext::UsdPrim& meshPrim, double time,
                            DrawMeshCPU* dm) {
  const std::vector<std::string> shapeNames =
      ReadTokens(meshPrim, "skel:blendShapes", time);
  const std::vector<tnext::Path>* targets =
      meshPrim.GetRelationship("skel:blendShapeTargets");
  if (shapeNames.empty() || !targets || targets->empty()) return;

  const size_t np = dm->vertices.size();
  if (np == 0) return;

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

  // entry's target vertex (or -1 to skip).
  auto vtxOf = [np](const Chan& c, size_t e) -> int64_t {
    const int64_t v = c.pidx->empty() ? int64_t(e) : int64_t((*c.pidx)[e]);
    return (v >= 0 && size_t(v) < np && e * 3 + 2 < c.offsets.size()) ? v : -1;
  };
  // Pass 1: count entries per vertex. M = offsets/3 (== pidx size when present).
  std::vector<uint32_t> count(np, 0u);
  for (const Chan& c : chans) {
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e)
      if (vtxOf(c, e) >= 0) count[size_t(vtxOf(c, e))]++;
  }
  // Prefix-sum into morphOffsetCount (offset,count per vertex).
  dm->morphOffsetCount.assign(np * 2, 0u);
  uint64_t total = 0;
  for (size_t v = 0; v < np; ++v) {
    dm->morphOffsetCount[v * 2 + 0] = static_cast<uint32_t>(total);
    dm->morphOffsetCount[v * 2 + 1] = count[v];
    total += count[v];
  }
  // Pass 2: scatter [channelId, dx, dy, dz] halfs + the uint16 channelId side
  // buffer (the shader's active-channel skip pre-check).
  auto h = [](float f) { return tinyusdz::value::float_to_half_full(f).value; };
  dm->morphDeltaHalf.assign(total * 4, 0);
  dm->morphChannelId.assign(total, 0);
  std::vector<uint32_t> cursor(np, 0u);
  for (const Chan& c : chans) {
    const uint16_t chHalf = h(static_cast<float>(c.id));
    const uint16_t chId = static_cast<uint16_t>(c.id);
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e) {
      const int64_t v = vtxOf(c, e);
      if (v < 0) continue;
      const uint64_t slot = dm->morphOffsetCount[size_t(v) * 2 + 0] + cursor[size_t(v)]++;
      uint16_t* o = &dm->morphDeltaHalf[slot * 4];
      o[0] = chHalf;
      o[1] = h(c.offsets[e * 3 + 0]);
      o[2] = h(c.offsets[e * 3 + 1]);
      o[3] = h(c.offsets[e * 3 + 2]);
      dm->morphChannelId[slot] = chId;
    }
  }

  // Max per-axis morph displacement, to pad protoAabb for per-instance culling.
  // Conservative: per vertex, sum each axis's positive and negative deltas across
  // ALL channels (worst case = every channel at full coefficient), then take the
  // largest absolute swing over vertices. Safe superset (over-pads, never culls a
  // visible morphed instance); small overdrive (weight > 1) is not bounded.
  std::vector<float> sumPos(np * 3, 0.0f), sumNeg(np * 3, 0.0f);
  for (const Chan& c : chans) {
    const size_t m = c.offsets.size() / 3;
    for (size_t e = 0; e < m; ++e) {
      const int64_t v = vtxOf(c, e);
      if (v < 0) continue;
      for (int a = 0; a < 3; ++a) {
        const float d = c.offsets[e * 3 + a];
        (d >= 0.0f ? sumPos : sumNeg)[size_t(v) * 3 + a] += d;
      }
    }
  }
  for (size_t v = 0; v < np; ++v)
    for (int a = 0; a < 3; ++a)
      dm->morphExtent[a] = std::max(
          dm->morphExtent[a],
          std::max(sumPos[v * 3 + a], -sumNeg[v * 3 + a]));
}

// Build a prototype mesh's local geometry (+ flat displayColor) from the
// converter, and its mesh-local -> proto-root-local transform `mesh_rel`. Shared
// by the PointInstancer and native-instance passes. Returns false if the mesh has
// no converter geometry.
bool BuildProtoMesh(const tnext::Stage& stage, tydn::RenderSceneConverter& conv,
                    const tnext::UsdPrim& mp, const matrix4d& inv_protoroot,
                    double time, DrawMeshCPU* dm, matrix4d* mesh_rel) {
  // Convert just this mesh on demand (streaming) -- avoids holding the whole
  // RenderScene in RAM.
  tydn::RenderMesh rm;
  if (!conv.ConvertMesh(mp, &rm)) return false;
  if (!FillFlatGeometry(rm, dm)) return false;
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
    BakeBlendShapes(stage, mp, time, dm);
  else
    BuildMorphChannelsNext(stage, mp, time, dm);
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
void EmitInstancedProto(const tnext::Stage& stage,
                        tydn::RenderSceneConverter& conv,
                        const tnext::UsdPrim& protoRoot,
                        const std::vector<matrix4d>& placements,
                        const std::vector<float>* placementColors, double time,
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
    if (!BuildProtoMesh(stage, conv, mp, inv_proto, time, &dm, &mesh_rel)) continue;
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
      const float tpos[3] = {static_cast<float>(fin.m[3][0]),
                             static_cast<float>(fin.m[3][1]),
                             static_cast<float>(fin.m[3][2])};
      bounds->add(tpos);
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
        EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time, draw,
                           bounds, instTotal, effectiveTris, instBudget, consumed);
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
      EmitInstancedProto(stage, conv, innerRoot, innerPl, nullptr, time, draw,
                         bounds, instTotal, effectiveTris, instBudget, consumed);
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

// Convert a bound material prim into a DrawMaterialCPU appended to `draw`, and
// return its index (>=1). Phase 1: baked PBR CONSTANTS only (base color,
// metallic, roughness, emissive, alpha) -- textures/GeomSubset are follow-ups.
// We reuse tusdview's own BakeLightRtOpenPBR so the --next path shades materials
// through the same path the legacy loader uses. Returns -1 if the prim has no
// usable surface shader (caller then keeps the default gray material, index 0).
int BuildNextMaterial(const tnext::Stage& stage, tydn::RenderSceneConverter& conv,
                      const tnext::UsdPrim& matPrim, DrawScene* draw) {
  tydn::RenderScene scratch;  // holds any texture metadata we don't consume yet
  tydn::RenderMaterial rm;
  if (!conv.ConvertMaterial(stage, matPrim, &rm, &scratch)) return -1;

  auto setRGB = [](float* dst, const tydn::Float4& v, float w) {
    dst[0] = v.x * w; dst[1] = v.y * w; dst[2] = v.z * w;
  };

  DrawMaterialCPU dm;
  dm.name = rm.name;
  dm.absPath = rm.prim_path;
  dm.displayName = rm.name;

  if (rm.shader_type == tydn::RenderMaterial::ShaderType::PreviewSurface &&
      rm.preview_surface) {
    const tydn::PreviewSurfaceShader& s = *rm.preview_surface;
    dm.hasUsdPreviewSurface = true;
    setRGB(dm.baseColor, s.diffuse_color.value, 1.0f);
    dm.metallic = s.metallic.value.x;
    dm.roughness = s.roughness.value.x;
    setRGB(dm.emissive, s.emissive_color.value, 1.0f);
    dm.alpha = s.opacity.value.x;
  } else if (rm.shader_type == tydn::RenderMaterial::ShaderType::OpenPBR &&
             rm.openpbr) {
    const tydn::OpenPBRSurfaceShader& s = *rm.openpbr;
    dm.hasOpenPBRSurface = true;
    dm.materialXNodeGraphJson = s.nodegraph_json;
    setRGB(dm.baseColor, s.base_color.value, s.base_weight.value.x);
    dm.metallic = s.base_metalness.value.x;
    dm.roughness = s.specular_roughness.value.x;
    setRGB(dm.emissive, s.emission_color.value, s.emission_luminance.value.x);
    dm.alpha = s.opacity.value.x;
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
                    std::shared_ptr<tnext::Stage>* out_stage) {
  // --- 1. Compose with the next loader (default options load payloads). ---
  // Heap-allocate the stage so the caller can keep it alive for per-frame
  // animation (the lazy arrays stay mmap-backed, not materialized).
  auto stagePtr = std::make_shared<tnext::Stage>();
  tnext::Stage& stage = *stagePtr;
  std::string lwarn, lerr;
  if (!tnext::LoadUSDComposed(path, &stage, &lwarn, &lerr, /*comp_opts=*/nullptr)) {
    if (err) *err = "next: compose failed: " + lerr;
    return false;
  }
  if (out_stage) *out_stage = stagePtr;
  if (warn && !lwarn.empty()) *warn = lwarn;
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
  // Phase 1 --next materials are constants-only: don't let the converter try to
  // load texture pixels (it has no custom loader here anyway).
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
  if (const char* mc = std::getenv("TUSDVIEW_NEXT_MAX_INSTANCES")) {
    instBudget = static_cast<size_t>(std::strtoull(mc, nullptr, 10));
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
                             perInstColor ? &colors : nullptr, time, draw, &bounds,
                             &instTotal, &effectiveTris, instBudget, &consumed);
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
        std::vector<tnext::UsdPrim> ms;
        tydn::GatherMeshPrims(p, &ms);
        for (const tnext::UsdPrim& m : ms) consumed.insert(m.GetPath().str());
        return;
      }
      for (const tnext::UsdPrim& c : p.GetChildren()) wn(c);
    };
    for (const tnext::UsdPrim& r : stage.GetRootPrims()) wn(r);

    for (const auto& kv : nativeGroups) {
      // A single instance is just the prototype shown twice; let 3b draw it.
      if (kv.second.size() < 2) continue;
      tnext::UsdPrim protoRoot = stage.GetPrimAtPath(kv.first);
      if (!protoRoot.IsValid()) continue;
      // GPU-instance the prototype's geometry at each native-instance placement;
      // EmitInstancedProto recurses into any nested instancers under the prototype.
      // consumed=nullptr: the prototype prim itself still renders via 3b (unchanged).
      EmitInstancedProto(stage, conv, protoRoot, kv.second, /*placementColors=*/nullptr,
                         time, draw, &bounds, &instTotal, &effectiveTris, instBudget,
                         /*consumed=*/nullptr);
    }
  }

  // --- 3b. Non-instanced meshes: STATIC BATCHING. Each mesh's vertices are baked
  //         to world space and merged into a few big buffers keyed by (purpose,
  //         geometric-normal), so a 33k-mesh scene draws in a handful of calls
  //         (one VAO/VBO/EBO per batch) instead of 33k -- far less draw-call + GL
  //         object overhead. Purpose stays per-batch so the GUI toggles still
  //         work; per-mesh pick/hide is not a goal of the flat large-scene path.
  struct Batch { DrawMeshCPU dm; bool anyColor = false; int matId = 0; };
  // key = (purpose, geometricNormal, materialId) -> current batch. Keying by
  // material keeps per-material draws distinct so each batch can reference its
  // own DrawMaterialCPU instead of the single default gray material.
  std::map<std::tuple<std::string, bool, int>, Batch> open;

  // Resolve a mesh's bound material to a DrawScene material index (cached by
  // material prim path). Unbound / unconvertible -> 0 (default gray material).
  std::unordered_map<std::string, int> matIndexByPath;
  auto resolveMeshMaterial = [&](const tnext::UsdPrim& mp) -> int {
    const std::vector<tnext::Path>* bind = mp.GetRelationship("material:binding");
    if (!bind || bind->empty()) return 0;
    const std::string mpath = (*bind)[0].str();
    auto it = matIndexByPath.find(mpath);
    if (it != matIndexByPath.end()) return it->second;
    tnext::UsdPrim matPrim = stage.GetPrimAtPath((*bind)[0]);
    int idx = matPrim.IsValid() ? BuildNextMaterial(stage, conv, matPrim, draw) : -1;
    if (idx < 0) idx = 0;
    matIndexByPath[mpath] = idx;
    return idx;
  };
  const size_t kBatchVtxCap = size_t(8) << 20;  // 8M verts/batch (indices stay 32-bit)

  auto flushBatch = [&](Batch& b) {
    if (b.dm.vertices.empty()) return;
    if (!b.anyColor) b.dm.vertexColors.clear();
    if (bounds.has)
      for (int k = 0; k < 3; ++k) {
        b.dm.aabbMin[k] = bounds.mn[k]; b.dm.aabbMax[k] = bounds.mx[k];
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
  std::vector<tnext::UsdPrim> meshPrims;
  {
    tydn::RenderExtractOptions xopts;
    xopts.time_code = time;
    xopts.stop_at_point_instancers = true;
    xopts.stop_at_native_instances = true;
    tydn::RenderExtractResult xres;
    tydn::CollectRenderPrims(stage, xopts, &xres);
    meshPrims.reserve(xres.meshes.size());
    for (const tydn::RenderPrimRecord& rec : xres.meshes) {
      if (!consumed.count(rec.path)) meshPrims.push_back(rec.prim);
    }
  }

  bool capped = false;
  for (const tnext::UsdPrim& mp : meshPrims) {
    if (ctrl && ctrl->cancel.load()) break;
    if (capped) break;
    tydn::RenderMesh m;
    if (!conv.ConvertMesh(mp, &m)) continue;
    DrawMeshCPU loc;
    if (!FillFlatGeometry(m, &loc)) continue;
    const std::string purpose = ResolveNextPurpose(stage, mp.GetPath().str());
    const int matId = resolveMeshMaterial(mp);
    double mw16[16];
    tydn::ComputeWorldTransform(stage, mp, mw16, time);
    float Mf[16];
    for (int k = 0; k < 16; ++k) Mf[k] = static_cast<float>(mw16[k]);
    const float* M = Mf;  // row-major, p*M (same as the converter's node xform)

    Batch& b = open[{purpose, loc.geometricNormal, matId}];
    b.matId = matId;
    if (!b.dm.vertices.empty() &&
        b.dm.vertices.size() + loc.vertices.size() > kBatchVtxCap) {
      flushBatch(b);  // resets b in the map slot
    }
    b.dm.purpose = purpose;
    b.dm.geometricNormal = loc.geometricNormal;
    const bool hasC = !loc.vertexColors.empty();
    // Allocate the batch color buffer only once a mesh actually contributes a
    // color: back-fill white for the vertices already in the batch. No-color
    // batches (e.g. the hotel) then never allocate a 12 B/vertex white buffer.
    if (hasC && !b.anyColor) {
      b.dm.vertexColors.assign(b.dm.vertices.size() * 3, 1.0f);
      b.anyColor = true;
    }

    // NOTE: rely on the vectors' amortized (doubling) growth -- an exact
    // reserve(size()+n) per mesh would reallocate the whole batch every mesh (O(N^2)).
    const uint32_t vbase = static_cast<uint32_t>(b.dm.vertices.size());
    for (size_t i = 0; i < loc.vertices.size(); ++i) {
      DrawVertex v = loc.vertices[i];
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
      b.dm.vertices.push_back(v);
      // Only emit per-vertex color once the batch has any (back-filled above);
      // white for this mesh when it has none, to stay aligned.
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
    // Carry the original-polygon wireframe edges into the batch (offset to the
    // batch's vertex range). Indices from different source meshes occupy disjoint
    // ranges, so no cross-mesh dedup is needed.
    for (uint32_t widx : loc.wireframeIndices)
      b.dm.wireframeIndices.push_back(vbase + widx);

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
    if (draw->triangleCount + b.dm.indices.size() / 3 > triCap) {
      draw->truncated = true;
      capped = true;
    }
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
       "%zu unique tris (%lld effective), %zu materials, instXform VRAM ~%.2f GB, up=%s%s",
       path.c_str(), draw->meshes.size(), nGuide, nProxy, nRender, instTotal,
       draw->triangleCount, effectiveTris, draw->materials.size(),
       double(instTotal) * 48.0 / 1e9,
       draw->upAxis.c_str(), draw->truncated ? " (truncated)" : "");

  if (draw->meshes.empty() && draw->volumes.empty()) {
    if (err) *err = "next: no renderable mesh produced";
    return false;
  }
  return true;
}

}  // namespace tusdview
