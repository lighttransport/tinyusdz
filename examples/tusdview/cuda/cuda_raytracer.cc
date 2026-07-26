// SPDX-License-Identifier: Apache-2.0
#include "cuda_raytracer.hh"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#if defined(_WIN32)
#include <direct.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <sys/types.h>
#endif

#include "cuew.h"
#include "displacement_bake.hh"
#include "log.hh"
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
  float lens[4];       // focus distance, aperture radius, enabled, reserved
};

// Trace kernel source, shared with the HIP backend (compiled at runtime by
// NVRTC here, hiprtc there).
#include "raytracer_kernel.inc"

char CachePathSeparator() {
#if defined(_WIN32)
  return '\\';
#else
  return '/';
#endif
}

bool IsPathSeparator(char c) { return c == '/' || c == '\\'; }

std::string JoinCachePath(const std::string& base, const std::string& child) {
  if (base.empty()) return child;
  if (child.empty()) return base;
  return IsPathSeparator(base.back()) ? base + child
                                      : base + CachePathSeparator() + child;
}

std::string CacheParentPath(const std::string& path) {
  const size_t separator = path.find_last_of("/\\");
  return separator == std::string::npos ? std::string() : path.substr(0, separator);
}

bool CachePathIsDirectory(const std::string& path) {
#if defined(_WIN32)
  struct _stat info {};
  if (_stat(path.c_str(), &info) != 0) return false;
  return (info.st_mode & _S_IFDIR) != 0;
#else
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) return false;
  return S_ISDIR(info.st_mode);
#endif
}

bool CachePathIsRegularFile(const std::string& path) {
#if defined(_WIN32)
  struct _stat info {};
  if (_stat(path.c_str(), &info) != 0) return false;
  return (info.st_mode & _S_IFREG) != 0;
#else
  struct stat info {};
  if (stat(path.c_str(), &info) != 0) return false;
  return S_ISREG(info.st_mode);
#endif
}

bool CreateCacheDirectory(const std::string& path) {
#if defined(_WIN32)
  const int result = _mkdir(path.c_str());
#else
  const int result = mkdir(path.c_str(), 0700);
#endif
  return result == 0 || (errno == EEXIST && CachePathIsDirectory(path));
}

bool CreateCacheDirectories(const std::string& path, std::string* warning) {
  if (path.empty()) return true;
  for (size_t i = 0; i <= path.size(); ++i) {
    if (i != path.size() && !IsPathSeparator(path[i])) continue;
    const std::string prefix = path.substr(0, i);
    if (prefix.empty() || prefix.back() == ':') continue;
    if (!CreateCacheDirectory(prefix)) {
      *warning = "cannot create " + prefix + ": " + std::strerror(errno);
      return false;
    }
  }
  return true;
}

std::string DefaultCudaCacheDirectory() {
  auto envPath = [](const char* name) -> std::string {
    const char* value = std::getenv(name);
    return (value && *value) ? std::string(value) : std::string();
  };
#if defined(_WIN32)
  std::string base = envPath("LOCALAPPDATA");
  if (base.empty()) base = envPath("USERPROFILE");
  return base.empty() ? std::string()
                      : JoinCachePath(JoinCachePath(base, "tusdview"), "cuda");
#elif defined(__APPLE__)
  const std::string home = envPath("HOME");
  return home.empty()
             ? std::string()
             : JoinCachePath(
                   JoinCachePath(
                       JoinCachePath(JoinCachePath(home, "Library"), "Caches"),
                       "tusdview"),
                   "cuda");
#else
  std::string base = envPath("XDG_CACHE_HOME");
  if (!base.empty()) return JoinCachePath(JoinCachePath(base, "tusdview"), "cuda");
  const std::string home = envPath("HOME");
  return home.empty()
             ? std::string()
             : JoinCachePath(JoinCachePath(JoinCachePath(home, ".cache"),
                                           "tusdview"),
                             "cuda");
#endif
}

uint64_t HashBytes(uint64_t hash, const void* data, size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= UINT64_C(1099511628211);
  }
  return hash;
}

std::string CacheKey(int nvrtcMajor, int nvrtcMinor, int compileArch) {
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = HashBytes(hash, kKernelSrc, std::strlen(kKernelSrc));
  const std::string settings = std::to_string(nvrtcMajor) + "." +
      std::to_string(nvrtcMinor) + ":compute_" + std::to_string(compileArch) +
      ":--use_fast_math";
  hash = HashBytes(hash, settings.data(), settings.size());
  char hex[17] = {};
  std::snprintf(hex, sizeof(hex), "%016llx",
                static_cast<unsigned long long>(hash));
  return "trace-nvrtc" + std::to_string(nvrtcMajor) + "." +
      std::to_string(nvrtcMinor) + "-compute" + std::to_string(compileArch) +
      "-" + hex + ".ptx";
}

bool ReadCacheFile(const std::string& path, std::string* data) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return false;
  stream.seekg(0, std::ios::end);
  const std::streamoff size = stream.tellg();
  if (size <= 0 || size > 64 * 1024 * 1024) return false;
  stream.seekg(0, std::ios::beg);
  data->resize(static_cast<size_t>(size));
  return static_cast<bool>(stream.read(data->data(),
                                       static_cast<std::streamsize>(size)));
}

bool WriteCacheFile(const std::string& path, const std::string& data,
                    std::string* warning) {
  if (!CreateCacheDirectories(CacheParentPath(path), warning)) return false;
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::string temporary = path + ".tmp." + std::to_string(stamp);
  {
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(data.data(),
                                 static_cast<std::streamsize>(data.size()))) {
      *warning = "cannot write " + temporary;
      std::remove(temporary.c_str());
      return false;
    }
  }
  if (std::rename(temporary.c_str(), path.c_str()) != 0) {
    const int renameError = errno;
    // Another process may have won the same atomic cache population race.
    if (!CachePathIsRegularFile(path)) {
      *warning = "cannot install " + path + ": " + std::strerror(renameError);
      std::remove(temporary.c_str());
      return false;
    }
    std::remove(temporary.c_str());
  }
  return true;
}

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
  freeRuntime();
}

void CudaRayTracer::freeRuntime() {
  if (ctx_) {
    freeScene();
    if (module_) cuModuleUnload(reinterpret_cast<CUmodule>(module_));
    cuCtxDestroy(reinterpret_cast<CUcontext>(ctx_));
  }
  kernel_ = nullptr;
  module_ = nullptr;
  ctx_ = nullptr;
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

  int compileArch = major * 10 + minor;
  int nvrtcMajor = 0;
  int nvrtcMinor = 0;
  if (nvrtcVersion) nvrtcVersion(&nvrtcMajor, &nvrtcMinor);
  // The CUDA driver may support a newer GPU than the separately installed
  // runtime compiler. PTX is forward-compatible, so ask NVRTC for the newest
  // virtual architecture it actually supports without exceeding the device.
  // Otherwise a machine with (for example) a new driver/GPU and an older NVRTC
  // fails compilation even though that compiler can produce valid PTX for it.
  std::vector<int> supported;
  if (nvrtcGetNumSupportedArchs && nvrtcGetSupportedArchs) {
    int countArchs = 0;
    if (nvrtcGetNumSupportedArchs(&countArchs) == NVRTC_SUCCESS &&
        countArchs > 0) {
      supported.resize(static_cast<size_t>(countArchs));
      if (nvrtcGetSupportedArchs(supported.data()) == NVRTC_SUCCESS) {
        int compatible = 0;
        for (int candidate : supported) {
          if (candidate <= compileArch) compatible = std::max(compatible, candidate);
        }
        if (compatible > 0) compileArch = compatible;
      } else {
        supported.clear();
      }
    }
  }
  // NVRTC 13.x has a severe optimizer regression for virtual architectures
  // 100 and newer on this kernel (>90 s and >1 GiB versus ~4 s for compute_90).
  // The kernel uses no architecture-specific features, and PTX is explicitly
  // forward-compatible, so retain optimized code while this compiler release is
  // in use by targeting the newest pre-regression architecture it advertises.
  if (nvrtcMajor == 13 && compileArch >= 100 &&
      std::find(supported.begin(), supported.end(), 90) != supported.end()) {
    compileArch = 90;
  }

  const std::string cacheDirectory =
      cacheDirectory_.empty() ? DefaultCudaCacheDirectory() : cacheDirectory_;
  const std::string cachePath =
      cacheDirectory.empty()
          ? std::string()
          : JoinCachePath(cacheDirectory,
                          CacheKey(nvrtcMajor, nvrtcMinor, compileArch));
  auto loadModule = [&](const std::string& ptx) -> bool {
    CUmodule mod = nullptr;
    if (cuModuleLoadData(&mod, ptx.c_str()) != CUDA_SUCCESS) return false;
    CUfunction fn = nullptr;
    if (cuModuleGetFunction(&fn, mod, "trace") != CUDA_SUCCESS) {
      cuModuleUnload(mod);
      return false;
    }
    module_ = mod;
    kernel_ = fn;
    return true;
  };
  std::string ptx;
  if (!cachePath.empty() && ReadCacheFile(cachePath, &ptx)) {
    if (loadModule(ptx)) {
      LOGI("CUDA kernel cache hit: %s", cachePath.c_str());
      return true;
    }
    LOGW("ignoring invalid CUDA kernel cache entry: %s",
         cachePath.c_str());
    ptx.clear();
    if (std::remove(cachePath.c_str()) != 0 && errno != ENOENT) {
      LOGW("could not remove invalid CUDA kernel cache entry: %s",
           std::strerror(errno));
    }
  }

  // Compile the kernel only after a validated persistent-cache miss.
  nvrtcProgram prog;
  if (nvrtcCreateProgram(&prog, kKernelSrc, "trace.cu", 0, nullptr, nullptr) !=
      NVRTC_SUCCESS) {
    if (err) *err = "nvrtcCreateProgram failed";
    freeRuntime();
    return false;
  }
  std::string arch =
      "--gpu-architecture=compute_" + std::to_string(compileArch);
  LOGI("CUDA kernel cache miss; compiling with NVRTC %d.%d for compute_%d",
       nvrtcMajor, nvrtcMinor, compileArch);
  const char* opts[] = {arch.c_str(), "--use_fast_math"};
  nvrtcResult nr = nvrtcCompileProgram(prog, 2, opts);
  if (nr != NVRTC_SUCCESS) {
    size_t logSize = 0;
    nvrtcGetProgramLogSize(prog, &logSize);
    std::string log(logSize, '\0');
    if (logSize) nvrtcGetProgramLog(prog, &log[0]);
    if (err) *err = "NVRTC compile failed:\n" + log;
    nvrtcDestroyProgram(&prog);
    freeRuntime();
    return false;
  }
  size_t ptxSize = 0;
  nvrtcGetPTXSize(prog, &ptxSize);
  ptx.resize(ptxSize);
  nvrtcGetPTX(prog, &ptx[0]);
  nvrtcDestroyProgram(&prog);

  if (!loadModule(ptx)) {
    if (err) *err = "CUDA compiled PTX failed module validation";
    freeRuntime();
    return false;
  }
  if (!cachePath.empty()) {
    std::string warning;
    if (WriteCacheFile(cachePath, ptx, &warning)) {
      LOGI("CUDA kernel cached: %s", cachePath.c_str());
    } else {
      LOGW("CUDA kernel cache write skipped: %s", warning.c_str());
    }
  }
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
                          float exposure,
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err, int spp,
                          const RtCameraLens* lens) {
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
  cam.camPos[3] = exposure;
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  if (lens && lens->enabled()) {
    cam.lens[0] = lens->focusDistance;
    cam.lens[1] = lens->apertureRadius;
    cam.lens[2] = 1.0f;
  }
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
