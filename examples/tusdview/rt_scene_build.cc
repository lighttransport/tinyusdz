// SPDX-License-Identifier: Apache-2.0
// tusdview — shared host-side scene build (see rt_scene_build.hh).
#include "rt_scene_build.hh"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

#include "displacement_bake.hh"  // SampleTextureRed

namespace tusdview {

namespace {

// column-major mat4 (DrawMeshCPU.world) -> row-major 3x4 o2w (instance format).
inline void Mat4ToO2W(const float m[16], float o2w[12]) {
  o2w[0] = m[0]; o2w[1] = m[4]; o2w[2] = m[8];  o2w[3] = m[12];
  o2w[4] = m[1]; o2w[5] = m[5]; o2w[6] = m[9];  o2w[7] = m[13];
  o2w[8] = m[2]; o2w[9] = m[6]; o2w[10] = m[10]; o2w[11] = m[14];
}
// Affine inverse of a row-major 3x4 o2w; identity fallback when near-singular.
inline bool Affine3x4Inverse(const float o2w[12], float w2o[12]) {
  const float a = o2w[0], b = o2w[1], c = o2w[2];
  const float d = o2w[4], e = o2w[5], f = o2w[6];
  const float g = o2w[8], h = o2w[9], i = o2w[10];
  const float A = e * i - f * h, B = f * g - d * i, C = d * h - e * g;
  float det = a * A + b * B + c * C;
  if (std::fabs(det) < 1e-20f) {
    for (int k = 0; k < 12; ++k) w2o[k] = 0.0f;
    w2o[0] = w2o[5] = w2o[10] = 1.0f;
    return false;
  }
  const float inv = 1.0f / det;
  const float r00 = A * inv, r01 = (c * h - b * i) * inv, r02 = (b * f - c * e) * inv;
  const float r10 = B * inv, r11 = (a * i - c * g) * inv, r12 = (c * d - a * f) * inv;
  const float r20 = C * inv, r21 = (b * g - a * h) * inv, r22 = (a * e - b * d) * inv;
  const float tx = o2w[3], ty = o2w[7], tz = o2w[11];
  w2o[0] = r00; w2o[1] = r01; w2o[2] = r02; w2o[3] = -(r00 * tx + r01 * ty + r02 * tz);
  w2o[4] = r10; w2o[5] = r11; w2o[6] = r12; w2o[7] = -(r10 * tx + r11 * ty + r12 * tz);
  w2o[8] = r20; w2o[9] = r21; w2o[10] = r22; w2o[11] = -(r20 * tx + r21 * ty + r22 * tz);
  return true;
}
inline void O2WPt(const float o2w[12], float x, float y, float z, float o[3]) {
  o[0] = o2w[0] * x + o2w[1] * y + o2w[2] * z + o2w[3];
  o[1] = o2w[4] * x + o2w[5] * y + o2w[6] * z + o2w[7];
  o[2] = o2w[8] * x + o2w[9] * y + o2w[10] * z + o2w[11];
}
// World AABB = transform the 8 corners of a local AABB by a row-major 3x4 o2w.
inline void O2WAabb(const float o2w[12], const float lo[3], const float hi[3],
                    float wlo[3], float whi[3]) {
  for (int k = 0; k < 3; ++k) { wlo[k] = 1e30f; whi[k] = -1e30f; }
  for (int c = 0; c < 8; ++c) {
    float p[3] = {(c & 1) ? hi[0] : lo[0], (c & 2) ? hi[1] : lo[1],
                  (c & 4) ? hi[2] : lo[2]};
    float w[3];
    O2WPt(o2w, p[0], p[1], p[2], w);
    for (int k = 0; k < 3; ++k) { wlo[k] = std::min(wlo[k], w[k]); whi[k] = std::max(whi[k], w[k]); }
  }
}

// Run f(i) for i in [0,n) across hardware threads (serial for small n).
template <class F>
void ParallelFor(size_t n, F f) {
  unsigned nt = std::max(1u, std::thread::hardware_concurrency());
  if (n < 64 || nt <= 1) { for (size_t i = 0; i < n; ++i) f(i); return; }
  nt = static_cast<unsigned>(std::min<size_t>(nt, n));
  const size_t chunk = (n + nt - 1) / nt;
  std::vector<std::thread> ts;
  for (unsigned t = 0; t < nt; ++t) {
    const size_t b = size_t(t) * chunk, e = std::min(n, b + chunk);
    if (b >= e) break;
    ts.emplace_back([&f, b, e] { for (size_t i = b; i < e; ++i) f(i); });
  }
  for (auto& th : ts) th.join();
}

// Per-mesh build result (leaf-order geometry + a LOCAL-ref BLAS + placements).
struct MeshBuild {
  bool valid = false;
  std::vector<float> tris, nrms, cols, uv, uv1, infl, domw;
  std::vector<uint8_t> geo;
  std::vector<int> mat, face, domj;
  std::vector<Node> blas;  // local node/leaf refs (rebased during assembly)
  float lo[3] = {0, 0, 0}, hi[3] = {0, 0, 0};
  std::vector<float> instO2W;   // 12 floats/placement (1 for non-instanced)
  std::vector<float> instTint;  // 3 floats/placement
};

// Flatten one mesh into local-space leaf-order triangle SoA + per-prototype BLAS.
// Pure (reads scene/mesh only), so it runs in parallel across meshes.
MeshBuild BuildOneMesh(const DrawScene& scene, const DrawMeshCPU& m,
                       float displacementScale) {
  MeshBuild mb;
  if (m.vertices.empty() || m.indices.empty()) return mb;
  const bool instanced = m.instanceCount() > 0;
  const bool hasVtxCol = m.vertexColors.size() == m.vertices.size() * 3;
  const bool hasUV1 = m.uv1.size() == m.vertices.size() * 2;
  const bool hasInfl = m.morphInfluence.size() == m.vertices.size();
  const bool hasSkin = m.jointIdx.size() == m.vertices.size() * 4 &&
                       m.jointWt.size() == m.vertices.size() * 4;
  const bool hasFace = m.sourceFaceId.size() == m.indices.size() / 3;
  const uint8_t g = static_cast<uint8_t>((m.geometricNormal ? 1 : 0) |
                                         ((PurposeId(m.purpose) & 3) << 1) |
                                         ((m.kindId & 7) << 3));
  auto submeshMat = [&](uint32_t triIdx0) -> const DrawMaterialCPU* {
    for (const DrawSubmesh& s : m.submeshes)
      if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
        return (s.materialId >= 0 && size_t(s.materialId) < scene.materials.size())
                   ? &scene.materials[s.materialId] : nullptr;
    return nullptr;
  };
  auto submeshMatId = [&](uint32_t triIdx0) -> int {
    for (const DrawSubmesh& s : m.submeshes)
      if (triIdx0 >= s.indexOffset && triIdx0 < s.indexOffset + s.indexCount)
        return s.materialId;
    return -1;
  };

  std::vector<float> lt, ln, lc, luv, luv1, linfl, ldomw;
  std::vector<uint8_t> lg;
  std::vector<int> lm, lf, ldomj;
  for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
    float wp[9], wn[9], wc[9], wuv[6], wuv1[6], winfl[3], wdomw[3];
    int domJoint = -1;
    float curTint[3] = {0.6f, 0.6f, 0.6f};
    if (instanced) {
      curTint[0] = curTint[1] = curTint[2] = 1.0f;
    } else if (const DrawMaterialCPU* mat = submeshMat(static_cast<uint32_t>(t))) {
      curTint[0] = mat->baseColor[0];
      curTint[1] = mat->baseColor[1];
      curTint[2] = mat->baseColor[2];
    }
    for (int k = 0; k < 3; ++k) {
      const uint32_t vidx = m.indices[t + k];
      const DrawVertex& vtx = m.vertices[vidx];
      wp[k * 3 + 0] = vtx.px; wp[k * 3 + 1] = vtx.py; wp[k * 3 + 2] = vtx.pz;
      wn[k * 3 + 0] = vtx.nx; wn[k * 3 + 1] = vtx.ny; wn[k * 3 + 2] = vtx.nz;
      wuv[k * 2 + 0] = vtx.u; wuv[k * 2 + 1] = vtx.v;
      wuv1[k * 2 + 0] = hasUV1 ? m.uv1[vidx * 2 + 0] : 0.0f;
      wuv1[k * 2 + 1] = hasUV1 ? m.uv1[vidx * 2 + 1] : 0.0f;
      winfl[k] = hasInfl ? m.morphInfluence[vidx] : 0.0f;
      int dj = -1; float dw = 0.0f;
      if (hasSkin)
        for (int b = 0; b < 4; ++b) {
          const float wb = m.jointWt[vidx * 4 + b];
          if (wb > dw) { dw = wb; dj = static_cast<int>(m.jointIdx[vidx * 4 + b]); }
        }
      wdomw[k] = dw;
      if (k == 0) domJoint = dj;
      float dc[3] = {1, 1, 1};
      if (hasVtxCol) {
        const float* c = &m.vertexColors[vidx * 3];
        dc[0] = c[0]; dc[1] = c[1]; dc[2] = c[2];
      }
      wc[k * 3 + 0] = curTint[0] * dc[0];
      wc[k * 3 + 1] = curTint[1] * dc[1];
      wc[k * 3 + 2] = curTint[2] * dc[2];
    }
    if (displacementScale != 0.0f) {
      const DrawMaterialCPU* dmat = submeshMat(static_cast<uint32_t>(t));
      if (dmat && dmat->hasDisplacement()) {
        for (int k = 0; k < 3; ++k) {
          float h = dmat->displacementTex >= 0
                        ? SampleTextureRed(scene, dmat->displacementTex,
                                           wuv[k * 2], wuv[k * 2 + 1]) *
                                  dmat->displacementTexScale +
                              dmat->displacementTexBias
                        : dmat->displacementConst;
          h *= displacementScale;
          float nx = wn[k * 3], ny = wn[k * 3 + 1], nz = wn[k * 3 + 2];
          const float nl = std::sqrt(nx * nx + ny * ny + nz * nz);
          if (nl > 1e-12f) {
            nx /= nl; ny /= nl; nz /= nl;
            wp[k * 3 + 0] += nx * h; wp[k * 3 + 1] += ny * h; wp[k * 3 + 2] += nz * h;
          }
        }
        const float e1[3] = {wp[3] - wp[0], wp[4] - wp[1], wp[5] - wp[2]};
        const float e2[3] = {wp[6] - wp[0], wp[7] - wp[1], wp[8] - wp[2]};
        const float gn[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                             e1[0] * e2[1] - e1[1] * e2[0]};
        const float gl = std::sqrt(gn[0] * gn[0] + gn[1] * gn[1] + gn[2] * gn[2]);
        if (gl > 1e-12f)
          for (int k = 0; k < 3; ++k) {
            wn[k * 3 + 0] = gn[0] / gl; wn[k * 3 + 1] = gn[1] / gl; wn[k * 3 + 2] = gn[2] / gl;
          }
      }
    }
    lt.insert(lt.end(), wp, wp + 9);
    ln.insert(ln.end(), wn, wn + 9);
    lc.insert(lc.end(), wc, wc + 9);
    luv.insert(luv.end(), wuv, wuv + 6);
    luv1.insert(luv1.end(), wuv1, wuv1 + 6);
    linfl.insert(linfl.end(), winfl, winfl + 3);
    ldomw.insert(ldomw.end(), wdomw, wdomw + 3);
    ldomj.push_back(domJoint);
    lg.push_back(g);
    lm.push_back(submeshMatId(static_cast<uint32_t>(t)));
    lf.push_back(hasFace ? static_cast<int>(m.sourceFaceId[t / 3]) : -1);
  }
  const size_t ltc = lt.size() / 9;
  if (ltc == 0) return mb;

  mb.lo[0] = mb.hi[0] = lt[0]; mb.lo[1] = mb.hi[1] = lt[1]; mb.lo[2] = mb.hi[2] = lt[2];
  for (size_t i = 0; i < ltc; ++i)
    for (int v = 0; v < 3; ++v)
      for (int k = 0; k < 3; ++k) {
        float c = lt[i * 9 + v * 3 + k];
        mb.lo[k] = std::min(mb.lo[k], c); mb.hi[k] = std::max(mb.hi[k], c);
      }

  std::vector<float> cent(ltc * 3);
  for (size_t i = 0; i < ltc; ++i) {
    const float* tv = &lt[i * 9];
    for (int k = 0; k < 3; ++k) cent[i * 3 + k] = (tv[k] + tv[3 + k] + tv[6 + k]) / 3.f;
  }
  std::vector<int> bidx(ltc);
  for (size_t i = 0; i < ltc; ++i) bidx[i] = static_cast<int>(i);
  mb.blas.reserve(ltc * 2);
  BuildBvh(mb.blas, bidx, 0, static_cast<int>(ltc), cent, lt);

  // Emit per-tri arrays in BVH leaf order (so the local blas leaf refs are valid).
  mb.tris.reserve(ltc * 9); mb.nrms.reserve(ltc * 9); mb.cols.reserve(ltc * 9);
  mb.uv.reserve(ltc * 6); mb.uv1.reserve(ltc * 6); mb.infl.reserve(ltc * 3);
  mb.domw.reserve(ltc * 3); mb.geo.reserve(ltc); mb.mat.reserve(ltc);
  mb.face.reserve(ltc); mb.domj.reserve(ltc);
  for (size_t i = 0; i < ltc; ++i) {
    int s = bidx[i];
    mb.tris.insert(mb.tris.end(), &lt[s * 9], &lt[s * 9] + 9);
    mb.nrms.insert(mb.nrms.end(), &ln[s * 9], &ln[s * 9] + 9);
    mb.cols.insert(mb.cols.end(), &lc[s * 9], &lc[s * 9] + 9);
    mb.uv.insert(mb.uv.end(), &luv[s * 6], &luv[s * 6] + 6);
    mb.uv1.insert(mb.uv1.end(), &luv1[s * 6], &luv1[s * 6] + 6);
    mb.infl.insert(mb.infl.end(), &linfl[s * 3], &linfl[s * 3] + 3);
    mb.domw.insert(mb.domw.end(), &ldomw[s * 3], &ldomw[s * 3] + 3);
    mb.geo.push_back(lg[s]);
    mb.mat.push_back(lm[s]);
    mb.face.push_back(lf[s]);
    mb.domj.push_back(ldomj[s]);
  }

  // Placements (instanced: N; non-instanced: one identity-style placement).
  if (instanced) {
    const size_t ninst = m.instanceCount();
    const bool perColor = m.instanceColors.size() == ninst * 3;
    mb.instO2W.reserve(ninst * 12);
    mb.instTint.reserve(ninst * 3);
    for (size_t k = 0; k < ninst; ++k) {
      mb.instO2W.insert(mb.instO2W.end(), &m.instanceXforms[k * 12], &m.instanceXforms[k * 12] + 12);
      mb.instTint.push_back(perColor ? m.instanceColors[k * 3 + 0] : m.flatColor[0]);
      mb.instTint.push_back(perColor ? m.instanceColors[k * 3 + 1] : m.flatColor[1]);
      mb.instTint.push_back(perColor ? m.instanceColors[k * 3 + 2] : m.flatColor[2]);
    }
  } else {
    float o2w[12];
    Mat4ToO2W(m.world, o2w);
    mb.instO2W.insert(mb.instO2W.end(), o2w, o2w + 12);
    mb.instTint.push_back(1.0f); mb.instTint.push_back(1.0f); mb.instTint.push_back(1.0f);
  }
  mb.valid = true;
  return mb;
}

}  // namespace

bool BuildHostScene(const DrawScene& scene, size_t maxTris, size_t maxInstances,
                    float displacementScale, HostScene* out, std::string* err) {
  const size_t cap = maxTris ? maxTris : (size_t(1) << 62);
  const size_t instCap = maxInstances ? maxInstances : ~size_t(0);

  using Clock = std::chrono::steady_clock;
  auto t0 = Clock::now();
  auto ms = [&](Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
  };

  // Phase A: build every mesh's geometry in parallel (the dominant cost on
  // heavily-prototyped scenes). Each writes its own results slot.
  std::vector<MeshBuild> mbs(scene.meshes.size());
  ParallelFor(scene.meshes.size(), [&](size_t i) {
    mbs[i] = BuildOneMesh(scene, scene.meshes[i], displacementScale);
  });
  auto tA = Clock::now();

  // Phase B1: assemble geometry in mesh order (offsets/caps identical to the
  // serial build) and collect a flat list of instance sources. The per-instance
  // matrix math (inverse + world AABB) is deferred to a parallel pass (B2) since
  // it dominates on massively-instanced scenes (e.g. Moana Island).
  struct InstSrc {
    const float* o2w;   // points into mbs (kept alive until after B2)
    float tint[3];
    int blasRoot;
    int proto;          // index into protoBox
  };
  std::vector<InstSrc> isrc;
  std::vector<std::array<float, 6>> protoBox;  // {lo.xyz, hi.xyz} per accepted mesh
  for (MeshBuild& mb : mbs) {
    if (!mb.valid) continue;
    if (out->tris.size() / 9 >= cap) { out->truncated = true; break; }
    if (isrc.size() >= instCap) { out->truncated = true; break; }
    const size_t triOff = out->tris.size() / 9;
    const size_t nodeOff = out->blas.size();
    const int blasRoot = static_cast<int>(nodeOff);
    out->tris.insert(out->tris.end(), mb.tris.begin(), mb.tris.end());
    out->nrms.insert(out->nrms.end(), mb.nrms.begin(), mb.nrms.end());
    out->cols.insert(out->cols.end(), mb.cols.begin(), mb.cols.end());
    out->uv.insert(out->uv.end(), mb.uv.begin(), mb.uv.end());
    out->uv1.insert(out->uv1.end(), mb.uv1.begin(), mb.uv1.end());
    out->infl.insert(out->infl.end(), mb.infl.begin(), mb.infl.end());
    out->domw.insert(out->domw.end(), mb.domw.begin(), mb.domw.end());
    out->geo.insert(out->geo.end(), mb.geo.begin(), mb.geo.end());
    out->mat.insert(out->mat.end(), mb.mat.begin(), mb.mat.end());
    out->face.insert(out->face.end(), mb.face.begin(), mb.face.end());
    out->domj.insert(out->domj.end(), mb.domj.begin(), mb.domj.end());
    for (Node nd : mb.blas) {
      if (nd.count > 0) nd.left += static_cast<int>(triOff);
      else { nd.left += static_cast<int>(nodeOff); nd.right += static_cast<int>(nodeOff); }
      out->blas.push_back(nd);
    }
    const int proto = static_cast<int>(protoBox.size());
    protoBox.push_back({mb.lo[0], mb.lo[1], mb.lo[2], mb.hi[0], mb.hi[1], mb.hi[2]});
    const size_t np = mb.instTint.size() / 3;
    for (size_t k = 0; k < np && isrc.size() < instCap; ++k) {
      InstSrc s;
      s.o2w = &mb.instO2W[k * 12];
      s.tint[0] = mb.instTint[k * 3 + 0];
      s.tint[1] = mb.instTint[k * 3 + 1];
      s.tint[2] = mb.instTint[k * 3 + 2];
      s.blasRoot = blasRoot;
      s.proto = proto;
      isrc.push_back(s);
    }
    if (np > 0 && isrc.size() >= instCap) out->truncated = true;
  }

  // Phase B2: per-instance matrix math in parallel (instId == output index, so the
  // result is identical to the serial sequential assignment).
  std::vector<float> instAabb(isrc.size() * 6);
  out->instances.resize(isrc.size());
  ParallelFor(isrc.size(), [&](size_t i) {
    const InstSrc& s = isrc[i];
    Inst& I = out->instances[i];
    for (int j = 0; j < 12; ++j) I.o2w[j] = s.o2w[j];
    Affine3x4Inverse(s.o2w, I.w2o);
    I.tint[0] = s.tint[0]; I.tint[1] = s.tint[1]; I.tint[2] = s.tint[2];
    I.blasRoot = s.blasRoot;
    I.instId = static_cast<int>(i);
    const auto& box = protoBox[s.proto];
    float wlo[3], whi[3];
    O2WAabb(s.o2w, box.data(), box.data() + 3, wlo, whi);
    for (int k = 0; k < 3; ++k) { instAabb[i * 6 + k] = wlo[k]; instAabb[i * 6 + 3 + k] = whi[k]; }
  });
  isrc.clear();
  isrc.shrink_to_fit();
  mbs.clear();
  mbs.shrink_to_fit();

  out->triCount = out->tris.size() / 9;
  out->instCount = out->instances.size();
  if (out->triCount == 0 || out->instCount == 0) {
    if (err) *err = "no geometry";
    return false;
  }
  out->blasNodeCount = out->blas.size();

  // TLAS over instance world AABBs (reorders the instance table to leaf order).
  std::vector<float> tcent(out->instCount * 3);
  for (size_t i = 0; i < out->instCount; ++i)
    for (int k = 0; k < 3; ++k)
      tcent[i * 3 + k] = 0.5f * (instAabb[i * 6 + k] + instAabb[i * 6 + 3 + k]);
  std::vector<int> tidx(out->instCount);
  for (size_t i = 0; i < out->instCount; ++i) tidx[i] = static_cast<int>(i);
  out->tlas = BuildTlas(tidx, static_cast<int>(out->instCount), tcent, instAabb);
  out->tlasNodeCount = out->tlas.size();
  std::vector<Inst> ordered(out->instCount);
  for (size_t i = 0; i < out->instCount; ++i) ordered[i] = out->instances[tidx[i]];
  out->instances.swap(ordered);

  // UsdVol volumes.
  for (const DrawVolumeCPU& dv : scene.volumes) {
    const size_t n = size_t(dv.dim[0]) * size_t(dv.dim[1]) * size_t(dv.dim[2]);
    if (n == 0 || dv.density.size() < n) continue;
    float o2w[12], w2o[12];
    Mat4ToO2W(dv.world, o2w);
    if (!Affine3x4Inverse(o2w, w2o)) continue;
    HostVolParam vp{};
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 4; c++) vp.invModel[c * 4 + r] = w2o[r * 4 + c];
    vp.invModel[3] = 0.f; vp.invModel[7] = 0.f; vp.invModel[11] = 0.f; vp.invModel[15] = 1.f;
    for (int a = 0; a < 3; a++) {
      vp.bmin[a] = dv.aabbMin[a]; vp.bmax[a] = dv.aabbMax[a];
      vp.albedo[a] = dv.albedo[a]; vp.emission[a] = dv.emission[a]; vp.dim[a] = dv.dim[a];
    }
    vp.dim[3] = static_cast<int>(out->volDens.size());
    vp.albedo[3] = dv.densityScale;
    vp.emission[3] = dv.background;
    out->volDens.insert(out->volDens.end(), dv.density.begin(), dv.density.begin() + n);
    out->volParams.push_back(vp);
  }
  out->numVols = static_cast<int>(out->volParams.size());

  // Per-material PBR scalars.
  auto tB = Clock::now();
  if (std::getenv("TUSDVIEW_RT_TIMING"))
    std::fprintf(stderr,
                 "[rt_scene_build] %zu meshes -> %zu tris / %zu instances: "
                 "phaseA(geom) %lld ms, phaseB(assemble+TLAS) %lld ms\n",
                 scene.meshes.size(), out->triCount, out->instCount,
                 (long long)ms(t0, tA), (long long)ms(tA, tB));

  out->numMats = static_cast<int>(scene.materials.size());
  out->matPbr.assign(std::max<size_t>(scene.materials.size(), 1) * 6, 0.0f);
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    const DrawMaterialCPU& dm = scene.materials[i];
    out->matPbr[i * 6 + 0] = dm.metallic;
    out->matPbr[i * 6 + 1] = dm.roughness;
    out->matPbr[i * 6 + 2] = dm.emissive[0];
    out->matPbr[i * 6 + 3] = dm.emissive[1];
    out->matPbr[i * 6 + 4] = dm.emissive[2];
    out->matPbr[i * 6 + 5] = dm.alpha;
  }
  return true;
}

}  // namespace tusdview
