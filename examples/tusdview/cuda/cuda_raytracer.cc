// SPDX-License-Identifier: Apache-2.0
#include "cuda_raytracer.hh"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "cuew.h"
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
  float vp[16];        // world->clip (wireframe edge projection)
};

// Trace kernel source, shared with the HIP backend (compiled at runtime by
// NVRTC here, hiprtc there).
#include "raytracer_kernel.inc"

#define CU_OK(call, what)                                          \
  do {                                                             \
    CUresult _r = (call);                                          \
    if (_r != CUDA_SUCCESS) {                                      \
      const char* _s = nullptr;                                    \
      if (cuGetErrorString) cuGetErrorString(_r, &_s);             \
      if (err) *err = std::string("CUDA ") + (what) + ": " + (_s ? _s : "error"); \
      return false;                                                \
    }                                                              \
  } while (0)


}  // namespace

CudaRayTracer::~CudaRayTracer() {
  if (ctx_) {
    freeScene();
    if (module_) cuModuleUnload(reinterpret_cast<CUmodule>(module_));
    cuCtxDestroy(reinterpret_cast<CUcontext>(ctx_));
  }
}

void CudaRayTracer::freeScene() {
  auto F = [](uintptr_t& p) { if (p) { cuMemFree(static_cast<CUdeviceptr>(p)); p = 0; } };
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

bool CudaRayTracer::init(std::string* err) {
  if (ctx_) return true;
  if (cuewInit(CUEW_INIT_CUDA) != CUEW_SUCCESS) {
    if (err) *err = "cuew: CUDA driver not available";
    return false;
  }
  if (cuewInit(CUEW_INIT_NVRTC) != CUEW_SUCCESS) {
    if (err) *err = "cuew: NVRTC not available (needed for runtime kernel compile)";
    return false;
  }
  CU_OK(cuInit(0), "cuInit");
  int count = 0;
  CU_OK(cuDeviceGetCount(&count), "cuDeviceGetCount");
  if (count < 1) { if (err) *err = "no CUDA device"; return false; }
  CUdevice dev;
  CU_OK(cuDeviceGet(&dev, 0), "cuDeviceGet");
  device_ = dev;
  char name[256] = {0};
  cuDeviceGetName(name, sizeof(name), dev);
  deviceName_ = name;
  int major = 0, minor = 0;
  cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, dev);
  cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, dev);
  CUcontext ctx;
  CU_OK(cuCtxCreate(&ctx, 0, dev), "cuCtxCreate");
  ctx_ = ctx;

  // NVRTC compile the kernel for this device's architecture.
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, kKernelSrc, "trace.cu", 0, nullptr, nullptr) !=
      NVRTC_SUCCESS) {
    if (err) *err = "nvrtcCreateProgram failed";
    return false;
  }
  std::string arch = "--gpu-architecture=compute_" + std::to_string(major) +
                     std::to_string(minor);
  const char* opts[] = {arch.c_str(), "--use_fast_math"};
  nvrtcResult nr = nvrtcCompileProgram(prog, 2, opts);
  if (nr != NVRTC_SUCCESS) {
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    if (logSize) nvrtcGetProgramLog(prog, &log[0]);
    if (err) *err = "NVRTC compile failed:\n" + log;
    nvrtcDestroyProgram(&prog);
    return false;
  }
  size_t ptxSize = 0;
  nvrtcGetPTXSize(prog, &ptxSize);
  std::string ptx(ptxSize, '\0');
  nvrtcGetPTX(prog, &ptx[0]);
  nvrtcDestroyProgram(&prog);

  CUmodule mod;
  CU_OK(cuModuleLoadData(&mod, ptx.c_str()), "cuModuleLoadData");
  module_ = mod;
  CUfunction fn;
  CU_OK(cuModuleGetFunction(&fn, mod, "trace"), "cuModuleGetFunction(trace)");
  kernel_ = fn;
  return true;
}


bool CudaRayTracer::build(const DrawScene& scene, size_t maxTris,
                          size_t maxInstances, std::string* err,
                          float displacementScale, BuildProgress* progress) {
  if (!ctx_) { if (err) *err = "CUDA not initialized"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  freeScene();

  // Build the host scene (parallel per-mesh geometry + TLAS) -- shared with HIP.
  HostScene hs;
  if (!BuildHostScene(scene, maxTris, maxInstances, displacementScale, &hs, err,
                      progress)) {
    if (err) *err = "CUDA: " + *err;
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
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, bytes ? bytes : 1), "cuMemAlloc");
    if (bytes) CU_OK(cuMemcpyHtoD(p, host, bytes), "cuMemcpyHtoD");
    *dptr = static_cast<uintptr_t>(p);
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
  return true;
}

bool CudaRayTracer::trace(const float invViewProj[16], const float viewProj[16],
                          const float camPos[3],
                          const float lightDir[3], const float clearColor[3],
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err, int spp) {
  if (!ctx_ || !dTris_) { if (err) *err = "CUDA scene not built"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  const size_t bytes = size_t(w) * h * 4;
  if (outCap_ < bytes) {
    if (dOut_) cuMemFree(static_cast<CUdeviceptr>(dOut_));
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, bytes), "cuMemAlloc(out)");
    dOut_ = static_cast<uintptr_t>(p);
    outCap_ = bytes;
  }
  // Supersample accumulator (float per channel), only for spp > 1.
  const size_t accumBytes = (spp > 1) ? bytes * sizeof(float) : 0;
  if (accumBytes && accumCap_ < accumBytes) {
    if (dAccum_) cuMemFree(static_cast<CUdeviceptr>(dAccum_));
    CUdeviceptr p;
    CU_OK(cuMemAlloc(&p, accumBytes), "cuMemAlloc(accum)");
    dAccum_ = static_cast<uintptr_t>(p);
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
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  CUdeviceptr dT = dTris_, dN = dNrms_, dC = dCols_, dG = dGeo_, dM = dMat_,
              dBM = dBackMat_,
              dMP = dMatPbr_, dMB = dMatBase_, dML = dMatLightRt_, dMT = dMatTex_,
              dTx = dTexels_, dTD = dTextures_,
              dU = dUV_, dU1 = dUV1_, dIn = dInfl_, dF = dFace_,
              dDw = dDomW_, dDj = dDomJoint_, dBl = dBlasNodes_, dTl = dTlasNodes_,
              dI = dInstances_, dO = dOut_;
  CUdeviceptr dMTP = dMatTexParam_;
  CUdeviceptr dLP = dLightParams_;
  CUdeviceptr dVD = dVolDens_, dVP = dVolParams_, dEm = dEmask_;
  int numMats = numMats_;
  int numLights = numLights_;
  int numTextures = numTextures_;
  int numVols = numVols_;
  const int samples = spp < 1 ? 1 : spp;
  CUdeviceptr dAcc = (samples > 1) ? dAccum_ : 0;
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
  // and resolves into the RGBA8 output on the last sample. One synchronize + one
  // readback for the whole frame instead of one per sample.
  for (int s = 0; s < samples; ++s) {
    float jx, jy;
    RtPixelJitter(s, samples, &jx, &jy);
    cam.sceneMin[3] = jx;
    cam.sceneExtent[3] = jy;
    sampleIdx = s;
    CU_OK(cuLaunchKernel(reinterpret_cast<CUfunction>(kernel_), gx, gy, 1, 8, 8, 1, 0,
                         nullptr, args, nullptr),
          "cuLaunchKernel");
  }
  CU_OK(cuCtxSynchronize(), "cuCtxSynchronize");
  CU_OK(cuMemcpyDtoH(rgba->data(), static_cast<CUdeviceptr>(dOut_), bytes),
        "cuMemcpyDtoH");
  return true;
}

}  // namespace tusdview
