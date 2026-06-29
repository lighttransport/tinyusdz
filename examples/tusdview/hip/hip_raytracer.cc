// SPDX-License-Identifier: Apache-2.0
#include "hip_raytracer.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "hipew.h"
#include "displacement_bake.hh"
#include "rt_scene_build.hh"  // Node, Inst, HostScene, BuildHostScene (shared)

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

  // Build the host scene (parallel per-mesh geometry + TLAS) -- shared with CUDA.
  HostScene hs;
  if (!BuildHostScene(scene, maxTris, maxInstances, displacementScale, &hs, err)) {
    if (err) *err = "HIP: " + *err;
    return false;
  }
  truncated_ = hs.truncated;
  triCount_ = hs.triCount;
  instCount_ = hs.instCount;
  blasNodeCount_ = hs.blasNodeCount;
  tlasNodeCount_ = hs.tlasNodeCount;
  nodeCount_ = blasNodeCount_ + tlasNodeCount_;
  numMats_ = hs.numMats;
  numVols_ = hs.numVols;

  // Upload every array to the device.
  auto up = [&](const void* host, size_t bytes, uintptr_t* dptr) -> bool {
    void* p = nullptr;
    CU_OK(hipMalloc(&p, bytes ? bytes : 1), "hipMalloc");
    if (bytes) CU_OK(hipMemcpyHtoD(p, host, bytes), "hipMemcpyHtoD");
    *dptr = reinterpret_cast<uintptr_t>(p);
    return true;
  };
  if (!up(hs.tris.data(), hs.tris.size() * sizeof(float), &dTris_)) return false;
  if (!up(hs.nrms.data(), hs.nrms.size() * sizeof(float), &dNrms_)) return false;
  if (!up(hs.cols.data(), hs.cols.size() * sizeof(float), &dCols_)) return false;
  if (!up(hs.geo.data(), hs.geo.size(), &dGeo_)) return false;
  if (!up(hs.mat.data(), hs.mat.size() * sizeof(int), &dMat_)) return false;
  if (!up(hs.face.data(), hs.face.size() * sizeof(int), &dFace_)) return false;
  if (!up(hs.uv.data(), hs.uv.size() * sizeof(float), &dUV_)) return false;
  if (!up(hs.uv1.data(), hs.uv1.size() * sizeof(float), &dUV1_)) return false;
  if (!up(hs.infl.data(), hs.infl.size() * sizeof(float), &dInfl_)) return false;
  if (!up(hs.domw.data(), hs.domw.size() * sizeof(float), &dDomW_)) return false;
  if (!up(hs.domj.data(), hs.domj.size() * sizeof(int), &dDomJoint_)) return false;
  if (!up(hs.blas.data(), hs.blas.size() * sizeof(Node), &dBlasNodes_)) return false;
  if (!up(hs.tlas.data(), hs.tlas.size() * sizeof(Node), &dTlasNodes_)) return false;
  if (!up(hs.instances.data(), hs.instances.size() * sizeof(Inst), &dInstances_)) return false;
  if (hs.numVols > 0) {
    if (!up(hs.volDens.data(), hs.volDens.size() * sizeof(float), &dVolDens_)) return false;
    if (!up(hs.volParams.data(), hs.volParams.size() * sizeof(HostVolParam), &dVolParams_))
      return false;
  }
  if (!up(hs.matPbr.data(), hs.matPbr.size() * sizeof(float), &dMatPbr_)) return false;
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
