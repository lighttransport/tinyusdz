// SPDX-License-Identifier: Apache-2.0
#include "hip_raytracer.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "hipew.h"
#include "displacement_bake.hh"
#include "rt_bvh.hh"  // Node, BuildBvh, BuildTlas (shared with the CUDA tracer)

namespace tusdview {

namespace {

// Camera/light block passed by value to the kernel (must match `Cam` there).
struct Cam {
  float invVP[16];
  float camPos[4];
  float lightDir[4];   // xyz light, w depthScale
  float clear[4];      // rgb clear, w RenderMode
  float sceneMin[4];   // position AOV bbox
  float sceneExtent[4];
};

// Per-instance record (must match `Inst` in the kernel). All 4-byte fields, so the
// layout is identical host/device with no padding.
struct Inst {
  float w2o[12];   // world->object (affine inverse of o2w)
  float o2w[12];   // object->world (row-major 3x4)
  float tint[3];   // per-instance color
  int blasRoot;    // global node index of this instance's BLAS root
  int instId;      // stable instance id (instance-id AOV)
};

// Trace kernel source, shared with the CUDA backend (compiled at runtime by
// hiprtc here, NVRTC there).
#include "raytracer_kernel.inc"

#define CU_OK(call, what)                                          \
  do {                                                             \
    hipError_t _r = (call);                                        \
    if (_r != hipSuccess) {                                        \
      const char* _s = hipGetErrorString ? hipGetErrorString(_r) : nullptr; \
      if (err) *err = std::string("HIP ") + (what) + ": " + (_s ? _s : "error"); \
      return false;                                                \
    }                                                              \
  } while (0)

// world (column-major mat4) * point.
inline void XformPt(const float m[16], float x, float y, float z, float o[3]) {
  o[0] = m[0] * x + m[4] * y + m[8] * z + m[12];
  o[1] = m[1] * x + m[5] * y + m[9] * z + m[13];
  o[2] = m[2] * x + m[6] * y + m[10] * z + m[14];
}
inline void XformN(const float m[16], float x, float y, float z, float o[3]) {
  o[0] = m[0] * x + m[4] * y + m[8] * z;
  o[1] = m[1] * x + m[5] * y + m[9] * z;
  o[2] = m[2] * x + m[6] * y + m[10] * z;
}
// 3x4 row-major o2w (instanceXforms) * point.
inline void O2WPt(const float o2w[12], float x, float y, float z, float o[3]) {
  o[0] = o2w[0] * x + o2w[1] * y + o2w[2] * z + o2w[3];
  o[1] = o2w[4] * x + o2w[5] * y + o2w[6] * z + o2w[7];
  o[2] = o2w[8] * x + o2w[9] * y + o2w[10] * z + o2w[11];
}
inline void O2WN(const float o2w[12], float x, float y, float z, float o[3]) {
  o[0] = o2w[0] * x + o2w[1] * y + o2w[2] * z;
  o[1] = o2w[4] * x + o2w[5] * y + o2w[6] * z;
  o[2] = o2w[8] * x + o2w[9] * y + o2w[10] * z;
}
// column-major mat4 (DrawMeshCPU.world) -> row-major 3x4 o2w (instance format).
inline void Mat4ToO2W(const float m[16], float o2w[12]) {
  o2w[0] = m[0]; o2w[1] = m[4]; o2w[2] = m[8];  o2w[3] = m[12];
  o2w[4] = m[1]; o2w[5] = m[5]; o2w[6] = m[9];  o2w[7] = m[13];
  o2w[8] = m[2]; o2w[9] = m[6]; o2w[10] = m[10]; o2w[11] = m[14];
}
// Affine inverse of a row-major 3x4 o2w (3x3 cofactor inverse, t' = -R^-1 t).
// Returns false (and leaves identity) if near-singular.
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
  // R^-1 (row-major 3x3): adjugate / det.
  const float r00 = A * inv,             r01 = (c * h - b * i) * inv, r02 = (b * f - c * e) * inv;
  const float r10 = B * inv,             r11 = (a * i - c * g) * inv, r12 = (c * d - a * f) * inv;
  const float r20 = C * inv,             r21 = (b * g - a * h) * inv, r22 = (a * e - b * d) * inv;
  const float tx = o2w[3], ty = o2w[7], tz = o2w[11];
  w2o[0] = r00; w2o[1] = r01; w2o[2] = r02; w2o[3] = -(r00 * tx + r01 * ty + r02 * tz);
  w2o[4] = r10; w2o[5] = r11; w2o[6] = r12; w2o[7] = -(r10 * tx + r11 * ty + r12 * tz);
  w2o[8] = r20; w2o[9] = r21; w2o[10] = r22; w2o[11] = -(r20 * tx + r21 * ty + r22 * tz);
  return true;
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

}  // namespace

HipRayTracer::~HipRayTracer() {
  if (ready_) {
    freeScene();
    if (module_) hipModuleUnload(reinterpret_cast<hipModule_t>(module_));
  }
}

void HipRayTracer::freeScene() {
  auto F = [](uintptr_t& p) { if (p) { hipFree(reinterpret_cast<void*>(p)); p = 0; } };
  F(dTris_); F(dNrms_); F(dCols_); F(dGeo_); F(dMat_); F(dMatPbr_); F(dUV_); F(dUV1_); F(dInfl_); F(dFace_); F(dDomW_); F(dDomJoint_);
  F(dBlasNodes_); F(dTlasNodes_); F(dInstances_); F(dOut_);
  F(dVolDens_); F(dVolParams_);
  numVols_ = 0;
  numMats_ = 0;
  outCap_ = 0; triCount_ = 0; nodeCount_ = 0;
  instCount_ = 0; blasNodeCount_ = 0; tlasNodeCount_ = 0;
}

bool HipRayTracer::init(std::string* err) {
  if (ready_) return true;
  if (hipewInit(HIPEW_INIT_HIP) != HIPEW_SUCCESS) {
    if (err) *err = "hipew: HIP runtime not available";
    return false;
  }
  if (hipewInit(HIPEW_INIT_HIPRTC) != HIPEW_SUCCESS) {
    if (err) *err = "hipew: hiprtc not available (needed for runtime kernel compile)";
    return false;
  }
  CU_OK(hipInit(0), "hipInit");
  int count = 0;
  CU_OK(hipGetDeviceCount(&count), "hipGetDeviceCount");
  if (count < 1) { if (err) *err = "no HIP device"; return false; }
  device_ = 0;
  CU_OK(hipSetDevice(device_), "hipSetDevice");
  hipDevice_t dev = 0;
  CU_OK(hipDeviceGet(&dev, device_), "hipDeviceGet");
  char name[256] = {0};
  hipDeviceGetName(name, sizeof(name), dev);
  deviceName_ = name;

  // hiprtc compile the kernel. No --offload-arch is passed: hiprtc targets the
  // current device automatically. (Unlike NVRTC there is no PTX step; hiprtc
  // emits a loadable code object directly.)
  hiprtcProgram prog;
  if (hiprtcCreateProgram(&prog, kKernelSrc, "trace.hip", 0, nullptr, nullptr) !=
      HIPRTC_SUCCESS) {
    if (err) *err = "hiprtcCreateProgram failed";
    return false;
  }
  const char* opts[] = {"-ffast-math"};
  hiprtcResult nr = hiprtcCompileProgram(prog, 1, opts);
  if (nr != HIPRTC_SUCCESS) {
    size_t logSize = 0;
    hiprtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    if (logSize) hiprtcGetProgramLog(prog, &log[0]);
    if (err) *err = "hiprtc compile failed:\n" + log;
    hiprtcDestroyProgram(&prog);
    return false;
  }
  size_t codeSize = 0;
  hiprtcGetCodeSize(prog, &codeSize);
  std::string code(codeSize, '\0');
  hiprtcGetCode(prog, &code[0]);
  hiprtcDestroyProgram(&prog);

  hipModule_t mod;
  CU_OK(hipModuleLoadData(&mod, code.c_str()), "hipModuleLoadData");
  module_ = mod;
  hipFunction_t fn;
  CU_OK(hipModuleGetFunction(&fn, mod, "trace"), "hipModuleGetFunction(trace)");
  kernel_ = fn;
  ready_ = true;
  return true;
}


bool HipRayTracer::build(const DrawScene& scene, size_t maxTris,
                         size_t maxInstances, std::string* err,
                         float displacementScale) {
  if (!ready_) { if (err) *err = "HIP not initialized"; return false; }
  CU_OK(hipSetDevice(device_), "hipSetDevice");
  freeScene();
  truncated_ = false;
  const size_t instCap = maxInstances ? maxInstances : ~size_t(0);

  // 2-level instancing: each DrawMeshCPU becomes one BLAS over its LOCAL-space
  // triangles (geometry stored ONCE per prototype). Instanced meshes add N
  // instances, non-instanced meshes a single identity-style instance. Per-tri SoA
  // is concatenated + indexed by GLOBAL tri id; BLAS nodes are concatenated +
  // rebased to global. A TLAS over per-instance world AABBs ties it together.
  std::vector<float> gTris, gNrms, gCols, gUV, gUV1, gInfl, gDomW;
  std::vector<uint8_t> gGeo;
  std::vector<int> gMat, gFace, gDomJoint;
  std::vector<Node> gBlas;
  std::vector<Inst> instances;
  std::vector<float> instAabb;  // 6 floats/instance (world lo,hi) for the TLAS
  // The cap now bounds UNIQUE prototype geometry (tiny vs the old flatten cap).
  const size_t cap = maxTris ? maxTris : (size_t(1) << 62);

  for (const DrawMeshCPU& m : scene.meshes) {
    if (m.vertices.empty() || m.indices.empty()) continue;
    if (gTris.size() / 9 >= cap) { truncated_ = true; break; }
    if (instances.size() >= instCap) { truncated_ = true; break; }
    const bool instanced = m.instanceCount() > 0;
    const bool hasVtxCol = m.vertexColors.size() == m.vertices.size() * 3;
    const bool hasUV1 = m.uv1.size() == m.vertices.size() * 2;
    const bool hasInfl = m.morphInfluence.size() == m.vertices.size();
    const bool hasSkin = m.jointIdx.size() == m.vertices.size() * 4 &&
                         m.jointWt.size() == m.vertices.size() * 4;
    const bool hasFace = m.sourceFaceId.size() == m.indices.size() / 3;
    // geo byte: bit0 = geometricNormal, bits1-2 = purpose, bits3-5 = kind.
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

    // --- Emit this mesh's LOCAL-space triangles into temp BLAS arrays. ---
    std::vector<float> lt, ln, lc, luv, luv1, linfl, ldomw;
    std::vector<uint8_t> lg;
    std::vector<int> lm, lf, ldomj;
    for (size_t t = 0; t + 2 < m.indices.size(); t += 3) {
      float wp[9], wn[9], wc[9], wuv[6], wuv1[6], winfl[3], wdomw[3];
      int domJoint = -1;
      // Non-instanced meshes bake the submesh material color into cols (single
      // instance); instanced prototypes keep only displayColor and apply the
      // per-instance tint in the kernel (Inst::tint).
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
        wp[k * 3 + 0] = vtx.px; wp[k * 3 + 1] = vtx.py; wp[k * 3 + 2] = vtx.pz;  // LOCAL
        wn[k * 3 + 0] = vtx.nx; wn[k * 3 + 1] = vtx.ny; wn[k * 3 + 2] = vtx.nz;  // LOCAL
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
      // Bake coarse displacement (ray tracing intersects real triangles): offset
      // each corner along its normal by the sampled height, then shade with the
      // displaced triangle's geometric normal (matches the raster displaced look).
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
              wp[k * 3 + 0] += nx * h;
              wp[k * 3 + 1] += ny * h;
              wp[k * 3 + 2] += nz * h;
            }
          }
          const float e1[3] = {wp[3] - wp[0], wp[4] - wp[1], wp[5] - wp[2]};
          const float e2[3] = {wp[6] - wp[0], wp[7] - wp[1], wp[8] - wp[2]};
          const float gn[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                               e1[2] * e2[0] - e1[0] * e2[2],
                               e1[0] * e2[1] - e1[1] * e2[0]};
          const float gl = std::sqrt(gn[0] * gn[0] + gn[1] * gn[1] + gn[2] * gn[2]);
          if (gl > 1e-12f) {
            for (int k = 0; k < 3; ++k) {
              wn[k * 3 + 0] = gn[0] / gl;
              wn[k * 3 + 1] = gn[1] / gl;
              wn[k * 3 + 2] = gn[2] / gl;
            }
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
    if (ltc == 0) continue;

    // Prototype-local AABB (transformed per instance for the TLAS).
    float lo[3] = {lt[0], lt[1], lt[2]}, hi[3] = {lt[0], lt[1], lt[2]};
    for (size_t i = 0; i < ltc; ++i)
      for (int v = 0; v < 3; ++v)
        for (int k = 0; k < 3; ++k) {
          float c = lt[i * 9 + v * 3 + k];
          lo[k] = std::min(lo[k], c); hi[k] = std::max(hi[k], c);
        }

    // Build this BLAS's BVH over local tris; reorder per-tri arrays to leaf order.
    std::vector<float> cent(ltc * 3);
    for (size_t i = 0; i < ltc; ++i) {
      const float* tv = &lt[i * 9];
      for (int k = 0; k < 3; ++k) cent[i * 3 + k] = (tv[k] + tv[3 + k] + tv[6 + k]) / 3.f;
    }
    std::vector<int> bidx(ltc);
    for (size_t i = 0; i < ltc; ++i) bidx[i] = static_cast<int>(i);
    std::vector<Node> bnodes;
    bnodes.reserve(ltc * 2);
    BuildBvh(bnodes, bidx, 0, static_cast<int>(ltc), cent, lt);

    const size_t triOff = gTris.size() / 9;
    const size_t nodeOff = gBlas.size();
    const int blasRoot = static_cast<int>(nodeOff);
    for (size_t i = 0; i < ltc; ++i) {
      int s = bidx[i];
      gTris.insert(gTris.end(), &lt[s * 9], &lt[s * 9] + 9);
      gNrms.insert(gNrms.end(), &ln[s * 9], &ln[s * 9] + 9);
      gCols.insert(gCols.end(), &lc[s * 9], &lc[s * 9] + 9);
      gUV.insert(gUV.end(), &luv[s * 6], &luv[s * 6] + 6);
      gUV1.insert(gUV1.end(), &luv1[s * 6], &luv1[s * 6] + 6);
      gInfl.insert(gInfl.end(), &linfl[s * 3], &linfl[s * 3] + 3);
      gDomW.insert(gDomW.end(), &ldomw[s * 3], &ldomw[s * 3] + 3);
      gGeo.push_back(lg[s]);
      gMat.push_back(lm[s]);
      gFace.push_back(lf[s]);
      gDomJoint.push_back(ldomj[s]);
    }
    // Rebase BLAS node child/leaf indices into the global arrays.
    for (Node& nd : bnodes) {
      if (nd.count > 0) nd.left += static_cast<int>(triOff);
      else { nd.left += static_cast<int>(nodeOff); nd.right += static_cast<int>(nodeOff); }
    }
    gBlas.insert(gBlas.end(), bnodes.begin(), bnodes.end());

    // --- Instances of this BLAS (geometry shared; placement-only). ---
    auto addInst = [&](const float o2w[12], const float tint[3]) {
      if (instances.size() >= instCap) { truncated_ = true; return; }
      Inst I{};
      for (int k = 0; k < 12; ++k) I.o2w[k] = o2w[k];
      Affine3x4Inverse(o2w, I.w2o);  // identity fallback on a singular transform
      I.tint[0] = tint[0]; I.tint[1] = tint[1]; I.tint[2] = tint[2];
      I.blasRoot = blasRoot;
      I.instId = static_cast<int>(instances.size());
      instances.push_back(I);
      float wlo[3], whi[3];
      O2WAabb(o2w, lo, hi, wlo, whi);
      instAabb.insert(instAabb.end(), wlo, wlo + 3);
      instAabb.insert(instAabb.end(), whi, whi + 3);
    };
    if (instanced) {
      const size_t ninst = m.instanceCount();
      const bool perColor = m.instanceColors.size() == ninst * 3;
      for (size_t k = 0; k < ninst; ++k) {
        const float tint[3] = {perColor ? m.instanceColors[k * 3 + 0] : m.flatColor[0],
                               perColor ? m.instanceColors[k * 3 + 1] : m.flatColor[1],
                               perColor ? m.instanceColors[k * 3 + 2] : m.flatColor[2]};
        addInst(&m.instanceXforms[k * 12], tint);
      }
    } else {
      float o2w[12];
      Mat4ToO2W(m.world, o2w);
      const float white[3] = {1.0f, 1.0f, 1.0f};
      addInst(o2w, white);
    }
  }

  triCount_ = gTris.size() / 9;
  instCount_ = instances.size();
  if (triCount_ == 0 || instCount_ == 0) { if (err) *err = "HIP: no geometry"; return false; }
  blasNodeCount_ = gBlas.size();

  // Build the TLAS over instance world AABBs (reorders the instance table to leaf
  // order; instId stays with each Inst so the AOV is deterministic).
  std::vector<float> tcent(instCount_ * 3);
  for (size_t i = 0; i < instCount_; ++i)
    for (int k = 0; k < 3; ++k)
      tcent[i * 3 + k] = 0.5f * (instAabb[i * 6 + k] + instAabb[i * 6 + 3 + k]);
  std::vector<int> tidx(instCount_);
  for (size_t i = 0; i < instCount_; ++i) tidx[i] = static_cast<int>(i);
  std::vector<Node> tnodes =
      BuildTlas(tidx, static_cast<int>(instCount_), tcent, instAabb);
  tlasNodeCount_ = tnodes.size();
  nodeCount_ = blasNodeCount_ + tlasNodeCount_;
  std::vector<Inst> orderedInsts(instCount_);
  for (size_t i = 0; i < instCount_; ++i) orderedInsts[i] = instances[tidx[i]];

  // Upload.
  auto up = [&](const void* host, size_t bytes, uintptr_t* dptr) -> bool {
    void* p = nullptr;
    CU_OK(hipMalloc(&p, bytes ? bytes : 1), "hipMalloc");
    if (bytes) CU_OK(hipMemcpyHtoD(p, host, bytes), "hipMemcpyHtoD");
    *dptr = reinterpret_cast<uintptr_t>(p);
    return true;
  };
  if (!up(gTris.data(), gTris.size() * sizeof(float), &dTris_)) return false;
  if (!up(gNrms.data(), gNrms.size() * sizeof(float), &dNrms_)) return false;
  if (!up(gCols.data(), gCols.size() * sizeof(float), &dCols_)) return false;
  if (!up(gGeo.data(), gGeo.size(), &dGeo_)) return false;
  if (!up(gMat.data(), gMat.size() * sizeof(int), &dMat_)) return false;
  if (!up(gFace.data(), gFace.size() * sizeof(int), &dFace_)) return false;
  if (!up(gUV.data(), gUV.size() * sizeof(float), &dUV_)) return false;
  if (!up(gUV1.data(), gUV1.size() * sizeof(float), &dUV1_)) return false;
  if (!up(gInfl.data(), gInfl.size() * sizeof(float), &dInfl_)) return false;
  if (!up(gDomW.data(), gDomW.size() * sizeof(float), &dDomW_)) return false;
  if (!up(gDomJoint.data(), gDomJoint.size() * sizeof(int), &dDomJoint_)) return false;
  if (!up(gBlas.data(), gBlas.size() * sizeof(Node), &dBlasNodes_)) return false;
  if (!up(tnodes.data(), tnodes.size() * sizeof(Node), &dTlasNodes_)) return false;
  if (!up(orderedInsts.data(), orderedInsts.size() * sizeof(Inst), &dInstances_)) return false;

  // UsdVol volumes: concatenate dense density grids + per-volume params
  // (layout must match `struct VolParam` in the kernel source).
  numVols_ = 0;
  {
    struct HostVolParam {
      float invModel[16];
      float bmin[4];
      float bmax[4];
      int dim[4];  // .xyz dims, .w = float offset into volDens
      float albedo[4];
      float emission[4];
    };
    std::vector<float> volDens;
    std::vector<HostVolParam> vps;
    for (const DrawVolumeCPU& dv : scene.volumes) {
      const size_t n = size_t(dv.dim[0]) * size_t(dv.dim[1]) * size_t(dv.dim[2]);
      if (n == 0 || dv.density.size() < n) continue;
      float o2w[12], w2o[12];
      Mat4ToO2W(dv.world, o2w);  // column-major mat4 -> row-major 3x4
      if (!Affine3x4Inverse(o2w, w2o)) continue;
      HostVolParam vp{};
      // row-major 3x4 w2o -> column-major mat4 invModel.
      for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) vp.invModel[c * 4 + r] = w2o[r * 4 + c];
      vp.invModel[3] = 0.f; vp.invModel[7] = 0.f; vp.invModel[11] = 0.f;
      vp.invModel[15] = 1.f;
      for (int a = 0; a < 3; a++) {
        vp.bmin[a] = dv.aabbMin[a];
        vp.bmax[a] = dv.aabbMax[a];
        vp.albedo[a] = dv.albedo[a];
        vp.emission[a] = dv.emission[a];
        vp.dim[a] = dv.dim[a];
      }
      vp.dim[3] = static_cast<int>(volDens.size());  // float offset
      vp.albedo[3] = dv.densityScale;
      vp.emission[3] = dv.background;
      volDens.insert(volDens.end(), dv.density.begin(), dv.density.begin() + n);
      vps.push_back(vp);
    }
    if (!vps.empty()) {
      if (!up(volDens.data(), volDens.size() * sizeof(float), &dVolDens_))
        return false;
      if (!up(vps.data(), vps.size() * sizeof(HostVolParam), &dVolParams_))
        return false;
      numVols_ = static_cast<int>(vps.size());
    }
  }

  // Per-material PBR scalars indexed by tri matId: metal, rough, emitR/G/B, alpha.
  numMats_ = static_cast<int>(scene.materials.size());
  std::vector<float> matPbr(std::max<size_t>(scene.materials.size(), 1) * 6, 0.0f);
  for (size_t i = 0; i < scene.materials.size(); ++i) {
    const DrawMaterialCPU& dm = scene.materials[i];
    matPbr[i * 6 + 0] = dm.metallic;
    matPbr[i * 6 + 1] = dm.roughness;
    matPbr[i * 6 + 2] = dm.emissive[0];
    matPbr[i * 6 + 3] = dm.emissive[1];
    matPbr[i * 6 + 4] = dm.emissive[2];
    matPbr[i * 6 + 5] = dm.alpha;
  }
  if (!up(matPbr.data(), matPbr.size() * sizeof(float), &dMatPbr_)) return false;
  return true;
}

bool HipRayTracer::trace(const float invViewProj[16], const float camPos[3],
                          const float lightDir[3], const float clearColor[3],
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err, int spp) {
  if (!ready_ || !dTris_) { if (err) *err = "HIP scene not built"; return false; }
  CU_OK(hipSetDevice(device_), "hipSetDevice");
  const size_t bytes = size_t(w) * h * 4;
  if (outCap_ < bytes) {
    if (dOut_) hipFree(reinterpret_cast<void*>(dOut_));
    void* p = nullptr;
    CU_OK(hipMalloc(&p, bytes), "hipMalloc(out)");
    dOut_ = reinterpret_cast<uintptr_t>(p);
    outCap_ = bytes;
  }
  Cam cam{};
  std::memcpy(cam.invVP, invViewProj, 16 * sizeof(float));
  for (int i = 0; i < 3; ++i) {
    cam.camPos[i] = camPos[i];
    cam.lightDir[i] = lightDir[i];
    cam.clear[i] = clearColor[i];
  }
  cam.clear[3] = static_cast<float>(renderMode);
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  void* dT = reinterpret_cast<void*>(dTris_), *dN = reinterpret_cast<void*>(dNrms_),
        *dC = reinterpret_cast<void*>(dCols_), *dG = reinterpret_cast<void*>(dGeo_),
        *dM = reinterpret_cast<void*>(dMat_), *dMP = reinterpret_cast<void*>(dMatPbr_),
        *dU = reinterpret_cast<void*>(dUV_), *dU1 = reinterpret_cast<void*>(dUV1_),
        *dIn = reinterpret_cast<void*>(dInfl_), *dF = reinterpret_cast<void*>(dFace_),
        *dDw = reinterpret_cast<void*>(dDomW_), *dDj = reinterpret_cast<void*>(dDomJoint_),
        *dBl = reinterpret_cast<void*>(dBlasNodes_), *dTl = reinterpret_cast<void*>(dTlasNodes_),
        *dI = reinterpret_cast<void*>(dInstances_), *dO = reinterpret_cast<void*>(dOut_);
  void* dVD = reinterpret_cast<void*>(dVolDens_), *dVP = reinterpret_cast<void*>(dVolParams_);
  int numMats = numMats_;
  int numVols = numVols_;
  // ORDER MUST MATCH the kernel signature: tris,nrms,cols,geo,mats,matPbr,numMats,
  // uvs,uvs1,infls,faces,domw,domj,blas,tlas,insts,out,W,H,cam,
  // volDens,volParams,numVols.
  void* args[] = {&dT,  &dN,  &dC, &dG, &dM, &dMP, &numMats, &dU, &dU1, &dIn,
                  &dF,  &dDw, &dDj, &dBl, &dTl, &dI, &dO, &w, &h, &cam,
                  &dVD, &dVP, &numVols};
  unsigned gx = (w + 7) / 8, gy = (h + 7) / 8;
  const int samples = spp < 1 ? 1 : spp;
  rgba->resize(bytes);
  // 1 spp: launch once, read straight back. >1 spp: accumulate Halton-jittered
  // samples on the host and average (anti-aliasing for the screenshot path).
  std::vector<float> accum;
  std::vector<uint8_t> frame;
  if (samples > 1) { accum.assign(bytes, 0.0f); frame.resize(bytes); }
  for (int s = 0; s < samples; ++s) {
    float jx, jy;
    RtPixelJitter(s, samples, &jx, &jy);
    cam.sceneMin[3] = jx;
    cam.sceneExtent[3] = jy;
    CU_OK(hipModuleLaunchKernel(reinterpret_cast<hipFunction_t>(kernel_), gx, gy, 1,
                                8, 8, 1, 0, nullptr, args, nullptr),
          "hipModuleLaunchKernel");
    CU_OK(hipDeviceSynchronize(), "hipDeviceSynchronize");
    if (samples == 1) {
      CU_OK(hipMemcpyDtoH(rgba->data(), reinterpret_cast<void*>(dOut_), bytes),
            "hipMemcpyDtoH");
    } else {
      CU_OK(hipMemcpyDtoH(frame.data(), reinterpret_cast<void*>(dOut_), bytes),
            "hipMemcpyDtoH");
      for (size_t i = 0; i < bytes; ++i) accum[i] += float(frame[i]);
    }
  }
  if (samples > 1) {
    for (size_t i = 0; i < bytes; ++i)
      (*rgba)[i] = uint8_t(accum[i] / float(samples) + 0.5f);
    for (size_t i = 3; i < bytes; i += 4) (*rgba)[i] = 255;  // keep alpha opaque
  }
  return true;
}

}  // namespace tusdview
