// SPDX-License-Identifier: Apache-2.0
#include "hip_raytracer.hh"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "hipew.h"
#include "displacement_bake.hh"
#include "rt_scene_build.hh"  // Node, Inst, HostScene, BuildHostScene (shared)

#if defined(_MSC_VER)
#include "kernel_resource.hh"
#endif

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
  float vp[16];        // world->clip (wireframe edge projection)
  float lens[4];       // focus distance, aperture radius, enabled, reserved
};

// Trace kernel source, shared with the CUDA backend (compiled at runtime by
// hiprtc here, NVRTC there).
//
// MSVC caps a single string literal at ~16,380 bytes (C2026), well under this
// kernel's size, so it loads the source from an embedded Win32 resource
// instead (see raytracer_kernel_resource.rc / kernel_resource.cc). GCC/Clang
// have no such limit; they compile the CMake-generated raw-string literal
// (see raytracer_kernel_src.txt in CMakeLists.txt).
#if defined(_MSC_VER)
static const char* kKernelSrc = GetEmbeddedKernelSource().c_str();
#else
#include "raytracer_kernel.inc"
#endif

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
  F(dTris_); F(dNrms_); F(dCols_); F(dGeo_); F(dEmask_); F(dMat_); F(dBackMat_);
  F(dMatPbr_); F(dMatBase_);
  F(dMatLightRt_); F(dMatTex_);
  F(dMatTexParam_); F(dLightParams_);
  F(dTexels_); F(dTextures_); F(dUV_); F(dUV1_); F(dInfl_); F(dFace_); F(dDomW_); F(dDomJoint_);
  F(dBlasNodes_); F(dTlasNodes_); F(dInstances_); F(dOut_); F(dAccum_);
  F(dVolDens_); F(dVolParams_);
  numVols_ = 0;
  numMats_ = 0;
  numLights_ = 0;
  numTextures_ = 0;
  outCap_ = 0; accumCap_ = 0; triCount_ = 0; nodeCount_ = 0;
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
                         float displacementScale, BuildProgress* progress,
                         bool retainForRefit) {
  if (!ready_) { if (err) *err = "HIP not initialized"; return false; }
  CU_OK(hipSetDevice(device_), "hipSetDevice");  // also sets the device on a worker thread
  freeScene();
  retained_.reset();
  refitMap_ = RefitMap{};

  // Build the host scene (parallel per-mesh geometry + TLAS) -- shared with CUDA.
  HostScene local;
  if (retainForRefit) retained_.reset(new HostScene());
  HostScene& hs = retained_ ? *retained_ : local;
  if (!BuildHostScene(scene, maxTris, maxInstances, displacementScale, &hs, err,
                      progress, retained_ ? &refitMap_ : nullptr)) {
    if (err) *err = "HIP: " + *err;
    retained_.reset();
    return false;
  }
  truncated_ = hs.truncated;
  triCount_ = hs.triCount;
  instCount_ = hs.instCount;
  blasNodeCount_ = hs.blasNodeCount;
  tlasNodeCount_ = hs.tlasNodeCount;
  nodeCount_ = blasNodeCount_ + tlasNodeCount_;
  numMats_ = hs.numMats;
  numLights_ = hs.numLights;
  numTextures_ = hs.numTextures;
  numVols_ = hs.numVols;

  if (progress) { progress->phase = 3; progress->done = 0; progress->total = 0; }  // upload
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
  if (!up(hs.emask.data(), hs.emask.size(), &dEmask_)) return false;
  if (!up(hs.mat.data(), hs.mat.size() * sizeof(int), &dMat_)) return false;
  if (!hs.backMat.empty() &&
      !up(hs.backMat.data(), hs.backMat.size() * sizeof(int), &dBackMat_))
    return false;
  if (!up(hs.face.data(), hs.face.size() * sizeof(int), &dFace_)) return false;
  if (!up(hs.uv.data(), hs.uv.size() * sizeof(float), &dUV_)) return false;
  if (!up(hs.uv1.data(), hs.uv1.size() * sizeof(float), &dUV1_)) return false;
  if (!up(hs.infl.data(), hs.infl.size() * sizeof(float), &dInfl_)) return false;
  if (!up(hs.domw.data(), hs.domw.size() * sizeof(float), &dDomW_)) return false;
  if (!up(hs.domj.data(), hs.domj.size() * sizeof(int), &dDomJoint_)) return false;
  if (!up(hs.blas.data(), hs.blas.size() * sizeof(Node), &dBlasNodes_)) return false;
  if (!up(hs.tlas.data(), hs.tlas.size() * sizeof(Node), &dTlasNodes_)) return false;
  if (!up(hs.instances.data(), hs.instances.size() * sizeof(Inst), &dInstances_)) return false;
  if (!up(hs.matTex.data(), hs.matTex.size() * sizeof(int), &dMatTex_)) return false;
  if (!up(hs.matTexParam.data(), hs.matTexParam.size() * sizeof(float),
          &dMatTexParam_)) return false;
  if (!up(hs.texels.data(), hs.texels.size(), &dTexels_)) return false;
  if (!up(hs.textures.data(), hs.textures.size() * sizeof(HostTextureDesc), &dTextures_)) return false;
  if (hs.numVols > 0) {
    if (!up(hs.volDens.data(), hs.volDens.size() * sizeof(float), &dVolDens_)) return false;
    if (!up(hs.volParams.data(), hs.volParams.size() * sizeof(HostVolParam), &dVolParams_))
      return false;
  }
  if (!up(hs.matPbr.data(), hs.matPbr.size() * sizeof(float), &dMatPbr_)) return false;
  if (!up(hs.matBase.data(), hs.matBase.size() * sizeof(float), &dMatBase_)) return false;
  if (!up(hs.matLightRt.data(), hs.matLightRt.size() * sizeof(float),
          &dMatLightRt_)) return false;
  if (!up(hs.lightParams.data(), hs.lightParams.size() * sizeof(float),
          &dLightParams_)) return false;
  if (progress) progress->phase = 4;  // done

  // Refit only needs tris/nrms/blas/tlas/instances; drop the pose-invariant
  // arrays (texels alone can be hundreds of MB) now that they are on the device.
  if (retained_) {
    HostScene& r = *retained_;
    auto drop = [](auto& v) { v.clear(); v.shrink_to_fit(); };
    drop(r.cols); drop(r.uv); drop(r.uv1); drop(r.infl); drop(r.domw);
    drop(r.geo); drop(r.emask); drop(r.mat); drop(r.backMat); drop(r.face);
    drop(r.domj);
    drop(r.matPbr); drop(r.matBase); drop(r.matLightRt); drop(r.matTex);
    drop(r.matTexParam);
    drop(r.texels); drop(r.textures); drop(r.lightParams);
    drop(r.volDens); drop(r.volParams);
  }
  return true;
}

bool HipRayTracer::refit(const DrawScene& scene, std::string* err) {
  if (!ready_ || !dTris_) { if (err) *err = "HIP scene not built"; return false; }
  if (!canRefit()) {
    if (err) *err = "scene was not built with refit retained";
    return false;
  }
  CU_OK(hipSetDevice(device_), "hipSetDevice");
  HostScene& hs = *retained_;
  if (!RefitHostScene(scene, refitMap_, &hs, err)) return false;
  // Only the pose-dependent buffers move; sizes are unchanged, so copy into the
  // existing allocations (no realloc, no touch of materials/textures/uvs).
  CU_OK(hipMemcpyHtoD(reinterpret_cast<void*>(dTris_), hs.tris.data(),
                      hs.tris.size() * sizeof(float)), "hipMemcpyHtoD(tris)");
  CU_OK(hipMemcpyHtoD(reinterpret_cast<void*>(dNrms_), hs.nrms.data(),
                      hs.nrms.size() * sizeof(float)), "hipMemcpyHtoD(nrms)");
  CU_OK(hipMemcpyHtoD(reinterpret_cast<void*>(dBlasNodes_), hs.blas.data(),
                      hs.blas.size() * sizeof(Node)), "hipMemcpyHtoD(blas)");
  CU_OK(hipMemcpyHtoD(reinterpret_cast<void*>(dTlasNodes_), hs.tlas.data(),
                      hs.tlas.size() * sizeof(Node)), "hipMemcpyHtoD(tlas)");
  return true;
}

bool HipRayTracer::trace(const float invViewProj[16], const float viewProj[16],
                          const float camPos[3],
                          const float lightDir[3], const float clearColor[3],
                          float exposure,
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err, int spp,
                          const RtCameraLens* lens) {
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
  // Supersample accumulator (float per channel), only for spp > 1.
  const size_t accumBytes = (spp > 1) ? bytes * sizeof(float) : 0;
  if (accumBytes && accumCap_ < accumBytes) {
    if (dAccum_) hipFree(reinterpret_cast<void*>(dAccum_));
    void* p = nullptr;
    CU_OK(hipMalloc(&p, accumBytes), "hipMalloc(accum)");
    dAccum_ = reinterpret_cast<uintptr_t>(p);
    accumCap_ = accumBytes;
  }
  Cam cam{};
  std::memcpy(cam.invVP, invViewProj, 16 * sizeof(float));
  std::memcpy(cam.vp, viewProj, 16 * sizeof(float));  // world->clip (wireframe projection)
  for (int i = 0; i < 3; ++i) {
    cam.camPos[i] = camPos[i];
    cam.lightDir[i] = lightDir[i];
    cam.clear[i] = clearColor[i];
  }
  cam.clear[3] = static_cast<float>(renderMode);
  cam.camPos[3] = exposure;
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  if (lens && lens->enabled()) {
    cam.lens[0] = lens->focusDistance;
    cam.lens[1] = lens->apertureRadius;
    cam.lens[2] = 1.0f;
  }
  void* dT = reinterpret_cast<void*>(dTris_), *dN = reinterpret_cast<void*>(dNrms_),
        *dC = reinterpret_cast<void*>(dCols_), *dG = reinterpret_cast<void*>(dGeo_),
        *dM = reinterpret_cast<void*>(dMat_), *dBM = reinterpret_cast<void*>(dBackMat_),
        *dMP = reinterpret_cast<void*>(dMatPbr_), *dMB = reinterpret_cast<void*>(dMatBase_),
        *dML = reinterpret_cast<void*>(dMatLightRt_),
        *dLP = reinterpret_cast<void*>(dLightParams_),
        *dMT = reinterpret_cast<void*>(dMatTex_), *dTx = reinterpret_cast<void*>(dTexels_),
        *dMTP = reinterpret_cast<void*>(dMatTexParam_),
        *dTD = reinterpret_cast<void*>(dTextures_),
        *dU = reinterpret_cast<void*>(dUV_), *dU1 = reinterpret_cast<void*>(dUV1_),
        *dIn = reinterpret_cast<void*>(dInfl_), *dF = reinterpret_cast<void*>(dFace_),
        *dDw = reinterpret_cast<void*>(dDomW_), *dDj = reinterpret_cast<void*>(dDomJoint_),
        *dBl = reinterpret_cast<void*>(dBlasNodes_), *dTl = reinterpret_cast<void*>(dTlasNodes_),
        *dI = reinterpret_cast<void*>(dInstances_), *dO = reinterpret_cast<void*>(dOut_);
  void* dVD = reinterpret_cast<void*>(dVolDens_), *dVP = reinterpret_cast<void*>(dVolParams_);
  void* dEm = reinterpret_cast<void*>(dEmask_);
  int numMats = numMats_;
  int numLights = numLights_;
  int numTextures = numTextures_;
  int numVols = numVols_;
  const int samples = spp < 1 ? 1 : spp;
  void* dAcc = (samples > 1) ? reinterpret_cast<void*>(dAccum_) : nullptr;
  int sampleIdx = 0;
  int numSamples = samples;
  // ORDER MUST MATCH the kernel signature: tris,nrms,cols,geo,mats,backMats,matPbr,matBase,
  // matLightRt,numMats,lightParams,numLights,matTex,matTexParam,texels,textures,
  // numTextures,uvs,uvs1,infls,faces,domw,domj,blas,tlas,insts,out,W,H,cam,
  // volDens,volParams,numVols,emask,accum,sampleIdx,numSamples.
  void* args[] = {&dT,  &dN,  &dC, &dG, &dM, &dBM, &dMP, &dMB, &dML, &numMats, &dLP,
                  &numLights, &dMT, &dMTP, &dTx, &dTD, &numTextures, &dU, &dU1,
                  &dIn, &dF,  &dDw,
                  &dDj, &dBl, &dTl, &dI, &dO, &w, &h, &cam,
                  &dVD, &dVP, &numVols, &dEm, &dAcc, &sampleIdx, &numSamples};
  unsigned gx = (w + 7) / 8, gy = (h + 7) / 8;
  rgba->resize(bytes);
  // Launch one kernel per Halton-jittered sample; the kernel accumulates on the
  // device (float per channel, same math as the old host loop -- byte-identical)
  // and resolves into the RGBA8 output on the last sample. One readback for the
  // whole frame instead of one per sample.
  //
  // TUSDVIEW_RT_TIMING=1: report the isolated per-pass GPU trace time (launch +
  // blocking sync, excluding the DtoH readback) -- the cost an interactive
  // per-frame retrace would pay once the scene is already built/uploaded. The
  // per-pass sync exists only for that timing; the untimed path syncs once.
  const bool rtTiming = std::getenv("TUSDVIEW_RT_TIMING") != nullptr;
  double traceMsTotal = 0.0;
  for (int s = 0; s < samples; ++s) {
    float jx, jy;
    RtPixelJitter(s, samples, &jx, &jy);
    cam.sceneMin[3] = jx;
    cam.sceneExtent[3] = jy;
    sampleIdx = s;
    auto t0 = std::chrono::steady_clock::now();
    CU_OK(hipModuleLaunchKernel(reinterpret_cast<hipFunction_t>(kernel_), gx, gy, 1,
                                8, 8, 1, 0, nullptr, args, nullptr),
          "hipModuleLaunchKernel");
    if (rtTiming) {
      CU_OK(hipDeviceSynchronize(), "hipDeviceSynchronize");
      auto t1 = std::chrono::steady_clock::now();
      double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
      traceMsTotal += ms;
      std::fprintf(stderr, "[hip_raytracer] trace pass %d/%d: %.1f ms (%dx%d)\n",
                   s + 1, samples, ms, w, h);
    }
  }
  CU_OK(hipDeviceSynchronize(), "hipDeviceSynchronize");
  CU_OK(hipMemcpyDtoH(rgba->data(), reinterpret_cast<void*>(dOut_), bytes),
        "hipMemcpyDtoH");
  if (rtTiming) {
    std::fprintf(stderr,
                 "[hip_raytracer] %d pass(es): %.1f ms total, %.1f ms/pass (%dx%d)\n",
                 samples, traceMsTotal, traceMsTotal / double(samples), w, h);
  }
  return true;
}

}  // namespace tusdview
