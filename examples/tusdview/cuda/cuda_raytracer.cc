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
#include <limits>
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
#include "lightrt_mtlx_bridge.hh"
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
  float pathLimits[4]; // SSS events, volume events, motion segments, variance
  float context[4];    // MaterialX time, frame, reserved, reserved
};

// Trace kernel source, shared with the HIP backend (compiled at runtime by
// NVRTC here, hiprtc there).
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

// raytracer_kernel_src.txt also serves as the source of C++ raw-string chunks:
// three `)CUDA" ... R"CUDA(` boundaries keep each MSVC literal below C2026's
// size limit. The C++ compiler removes those wrappers from kKernelSrc; do the
// equivalent when a developer asks NVRTC to compile the on-disk file directly.
void NormalizeKernelSource(std::string* source) {
  constexpr const char* close = ")CUDA\"";
  constexpr const char* open = "R\"CUDA(";
  for (size_t at = source->find(close); at != std::string::npos;
       at = source->find(close, at)) {
    const size_t next = source->find(open, at + std::strlen(close));
    if (next == std::string::npos) break;
    source->erase(at, next + std::strlen(open) - at);
  }
}

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
  // Increment when the host/kernel launch ABI or packed GPU records change.
  // The source hash normally invalidates PTX, but an explicit schema keeps a
  // loadable kernel compiled against an older argument/record contract from
  // surviving an ABI migration through copied or otherwise stale caches.
  constexpr const char* kCacheSchema = "tusdview-cuda-ptx-v2";
  uint64_t hash = UINT64_C(14695981039346656037);
  hash = HashBytes(hash, kKernelSrc, std::strlen(kKernelSrc));
  const std::string settings = std::string(kCacheSchema) + ":" +
      std::to_string(nvrtcMajor) + "." +
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
  F(dMatLightRt_); F(dMatGraph_); F(dGeomPropDesc_); F(dGeomPropValues_); F(dMatTex_);
  F(dMatTexParam_); F(dLightParams_);
  F(dTexels_); F(dTextures_); F(dUV_); F(dUV1_); F(dInfl_); F(dFace_); F(dDomW_); F(dDomJoint_);
  F(dBlasNodes_); F(dTlasNodes_); F(dInstances_); F(dOut_); F(dAccum_);
  F(dVolDens_); F(dVolParams_);
  F(dPointCenters_); F(dPointMajorAxes_); F(dPointNormals_);
  F(dPointRadii_); F(dPointColors_);
  F(dPointOrder_); F(dPointBvh_); F(dPointChunks_);
  F(dPointDepth_);
  numVols_ = 0;
  numMats_ = 0;
  numLights_ = 0;
  numTextures_ = 0;
  pointCount_ = 0;
  pointChunkCount_ = 0;
  outCap_ = 0; accumCap_ = 0; pointDepthCap_ = 0;
  triCount_ = 0; nodeCount_ = 0;
  instCount_ = 0; blasNodeCount_ = 0; tlasNodeCount_ = 0;
  pointPaging_ = false;
  pointHost_.reset();
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
  compileArch_ = compileArch;

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
      kernelGeneration_ = 1;
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
  kernelGeneration_ = 1;
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

bool CudaRayTracer::reloadKernel(const std::string& sourcePath,
                                 std::string* err) {
  if (!ctx_ || !module_ || compileArch_ <= 0) {
    if (err) *err = "CUDA runtime must be initialized before kernel reload";
    return false;
  }
  std::string source;
  if (!ReadCacheFile(sourcePath, &source)) {
    if (err) *err = "cannot read CUDA kernel source: " + sourcePath;
    return false;
  }
  NormalizeKernelSource(&source);
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  const auto started = std::chrono::steady_clock::now();
  nvrtcProgram program = nullptr;
  if (nvrtcCreateProgram(&program, source.c_str(), sourcePath.c_str(), 0,
                         nullptr, nullptr) != NVRTC_SUCCESS) {
    if (err) *err = "nvrtcCreateProgram failed for " + sourcePath;
    return false;
  }
  const std::string arch =
      "--gpu-architecture=compute_" + std::to_string(compileArch_);
  const char* options[] = {arch.c_str(), "--use_fast_math"};
  const nvrtcResult compileResult = nvrtcCompileProgram(program, 2, options);
  if (compileResult != NVRTC_SUCCESS) {
    size_t logSize = 0;
    nvrtcGetProgramLogSize(program, &logSize);
    std::string log(logSize, '\0');
    if (logSize) nvrtcGetProgramLog(program, log.data());
    if (!log.empty() && log.back() == '\0') log.pop_back();
    nvrtcDestroyProgram(&program);
    if (err) *err = "NVRTC live compile failed:\n" + log;
    return false;
  }
  size_t ptxSize = 0;
  if (nvrtcGetPTXSize(program, &ptxSize) != NVRTC_SUCCESS || ptxSize == 0) {
    nvrtcDestroyProgram(&program);
    if (err) *err = "NVRTC live compile produced no PTX";
    return false;
  }
  std::string ptx(ptxSize, '\0');
  if (nvrtcGetPTX(program, ptx.data()) != NVRTC_SUCCESS) {
    nvrtcDestroyProgram(&program);
    if (err) *err = "NVRTC live PTX extraction failed";
    return false;
  }
  nvrtcDestroyProgram(&program);

  CUmodule replacement = nullptr;
  if (cuModuleLoadData(&replacement, ptx.c_str()) != CUDA_SUCCESS) {
    if (err) *err = "CUDA driver rejected live-compiled PTX";
    return false;
  }
  CUfunction replacementKernel = nullptr;
  if (cuModuleGetFunction(&replacementKernel, replacement, "trace") !=
      CUDA_SUCCESS) {
    cuModuleUnload(replacement);
    if (err) *err = "live CUDA module has no trace entry point";
    return false;
  }
  if (cuCtxSynchronize() != CUDA_SUCCESS) {
    cuModuleUnload(replacement);
    if (err) *err = "CUDA synchronization failed before kernel swap";
    return false;
  }
  CUmodule previous = reinterpret_cast<CUmodule>(module_);
  module_ = replacement;
  kernel_ = replacementKernel;
  cuModuleUnload(previous);
  ++kernelGeneration_;
  lastKernelCompileMs_ = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - started)
                             .count();
  LOGI("CUDA kernel live reload committed: generation %llu, %.1f ms (%s)",
       static_cast<unsigned long long>(kernelGeneration_),
       lastKernelCompileMs_, sourcePath.c_str());
  return true;
}


bool CudaRayTracer::build(const DrawScene& scene, size_t maxTris,
                          size_t maxInstances, std::string* err,
                          float displacementScale, BuildProgress* progress) {
  if (!ctx_) { if (err) *err = "CUDA not initialized"; return false; }
  cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_));
  freeScene();
  deviceUsedBytes_ = 0;
  deviceTotalBytes_ = 0;

  // Build the host scene (parallel per-mesh geometry + TLAS) -- shared with HIP.
  HostScene hs;
  if (!BuildHostScene(scene, maxTris, maxInstances, displacementScale, &hs, err,
                      progress, nullptr, textureBudgetBytes_)) {
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
  if (hs.pointCount > static_cast<size_t>(std::numeric_limits<int>::max())) {
    if (err) *err = "CUDA: Gaussian point count exceeds kernel limit (" +
                    std::to_string(hs.pointCount) + ")";
    return false;
  }
  // Keep an oversized analytic-point field on the host and page one BVH chunk
  // at trace time. The normal path still keeps all records resident for speed.
  const uint64_t gaussianBytes =
      uint64_t(hs.pointCenters.size()) * sizeof(float) +
      uint64_t(hs.pointMajorAxes.size()) * sizeof(float) +
      uint64_t(hs.pointNormals.size()) * sizeof(float) +
      uint64_t(hs.pointRadii.size()) * sizeof(float) +
      uint64_t(hs.pointColors.size()) * sizeof(float) +
      uint64_t(hs.pointOrder.size()) * sizeof(int) +
      uint64_t(hs.pointBvh.size()) * sizeof(Node) +
      uint64_t(hs.pointChunks.size()) * sizeof(PointBvhChunk);
  uint64_t gaussianBudget = 0;
  if (const char* value = std::getenv("TUSDVIEW_GAUSSIAN_GPU_BUDGET_MB")) {
    char* end = nullptr;
    const unsigned long long mb = std::strtoull(value, &end, 10);
    if (value[0] != '\0' && end != value && *end == '\0' && mb > 0 &&
        mb <= (std::numeric_limits<uint64_t>::max)() / (1024ull * 1024ull))
      gaussianBudget = mb * 1024ull * 1024ull;
  }
  if (gaussianBudget == 0 && cuMemGetInfo) {
    size_t freeBytes = 0, totalBytes = 0;
    if (cuMemGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS) {
      constexpr uint64_t kHeadroom = 256ull * 1024ull * 1024ull;
      if (freeBytes > kHeadroom) gaussianBudget = freeBytes - kHeadroom;
    }
  }
  if (gaussianBudget != 0 && gaussianBytes > gaussianBudget) {
    pointPaging_ = true;
    pointHost_.reset(new HostScene());
    pointHost_->pointCenters = std::move(hs.pointCenters);
    pointHost_->pointMajorAxes = std::move(hs.pointMajorAxes);
    pointHost_->pointNormals = std::move(hs.pointNormals);
    pointHost_->pointRadii = std::move(hs.pointRadii);
    pointHost_->pointColors = std::move(hs.pointColors);
    pointHost_->pointOrder = std::move(hs.pointOrder);
    pointHost_->pointBvh = std::move(hs.pointBvh);
    pointHost_->pointChunks = std::move(hs.pointChunks);
    pointHost_->pointCount = hs.pointCount;
    hs.pointCount = 0;
    std::fprintf(stderr,
                 "CUDA Gaussian buffers: %.1f MiB exceed %.1f MiB; paging %zu chunk(s)\n",
                 double(gaussianBytes) / (1024.0 * 1024.0),
                 double(gaussianBudget) / (1024.0 * 1024.0),
                 pointHost_->pointChunks.size());
  }
  if (gaussianBytes > 0) {
    std::fprintf(stderr, "CUDA Gaussian buffers: %.1f MiB%s\n",
                 double(gaussianBytes) / (1024.0 * 1024.0),
                 pointPaging_ ? " (paged)"
                              : (gaussianBudget ? " (within GPU budget)" : ""));
  }
  pointCount_ = static_cast<int>(pointPaging_ ? pointHost_->pointCount
                                              : hs.pointCount);

  if (progress) { progress->phase = 3; progress->done = 0; progress->total = 0; }  // upload
  // Upload every array to the device.
  auto up = [&](const void* host, size_t bytes, uintptr_t* dptr) -> bool {
    size_t freeBytes = 0, totalBytes = 0;
    const bool haveMemInfo = cuMemGetInfo &&
                             cuMemGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS;
    CUdeviceptr p;
    const CUresult ar = cuMemAlloc(&p, bytes ? bytes : 1);
    if (ar != CUDA_SUCCESS) {
      const char* s = nullptr;
      if (cuGetErrorString) cuGetErrorString(ar, &s);
      if (err) {
        *err = "CUDA scene upload allocation failed (" +
               std::to_string(bytes) + " bytes";
        if (haveMemInfo)
          *err += ", " + std::to_string(freeBytes) + " bytes free / " +
                  std::to_string(totalBytes) + " total";
        *err += std::string("): ") + (s ? s : "error");
      }
      freeScene();
      return false;
    }
    if (bytes) {
      const CUresult cr = cuMemcpyHtoD(p, host, bytes);
      if (cr != CUDA_SUCCESS) {
        const char* s = nullptr;
        if (cuGetErrorString) cuGetErrorString(cr, &s);
        cuMemFree(p);
        if (err) *err = std::string("CUDA scene upload copy failed: ") +
                        (s ? s : "error");
        freeScene();
        return false;
      }
    }
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
  if (!pointPaging_) {
    if (!up(hs.pointCenters.data(), hs.pointCenters.size() * sizeof(float), &dPointCenters_)) return false;
    if (!up(hs.pointMajorAxes.data(), hs.pointMajorAxes.size() * sizeof(float), &dPointMajorAxes_)) return false;
    if (!up(hs.pointNormals.data(), hs.pointNormals.size() * sizeof(float), &dPointNormals_)) return false;
    if (!up(hs.pointRadii.data(), hs.pointRadii.size() * sizeof(float), &dPointRadii_)) return false;
    if (!up(hs.pointColors.data(), hs.pointColors.size() * sizeof(float), &dPointColors_)) return false;
    if (!up(hs.pointOrder.data(), hs.pointOrder.size() * sizeof(int), &dPointOrder_)) return false;
    if (!up(hs.pointBvh.data(), hs.pointBvh.size() * sizeof(Node), &dPointBvh_)) return false;
    pointChunkCount_ = static_cast<int>(hs.pointChunks.size());
    if (!up(hs.pointChunks.data(), hs.pointChunks.size() * sizeof(PointBvhChunk),
            &dPointChunks_)) return false;
  }
  if (!up(hs.matPbr.data(), hs.matPbr.size() * sizeof(float), &dMatPbr_)) return false;
  if (!up(hs.matBase.data(), hs.matBase.size() * sizeof(float), &dMatBase_)) return false;
  if (!up(hs.matLightRt.data(), hs.matLightRt.size() * sizeof(float),
          &dMatLightRt_)) return false;
  if (!up(hs.matGraph.data(), hs.matGraph.size() * sizeof(float),
          &dMatGraph_)) return false;
  {
    std::vector<HostGeomPropDesc> desc;
    std::vector<float> values;
    for (const HostGeomProp& prop : hs.geomProps) {
      if (((prop.components < 1 || prop.components > 4) &&
           prop.components != 9 && prop.components != 16) ||
          prop.values.size() != hs.triCount * 3u * prop.components) continue;
      HostGeomPropDesc d;
      d.hash = MaterialXGeomPropHash(prop.name);
      d.components = prop.components;
      d.valueOffset = static_cast<uint32_t>(values.size());
      desc.push_back(d);
      values.insert(values.end(), prop.values.begin(), prop.values.end());
    }
    geomPropCount_ = static_cast<int>(desc.size());
    if (geomPropCount_ > 0) {
      if (!up(desc.data(), desc.size() * sizeof(HostGeomPropDesc), &dGeomPropDesc_) ||
          !up(values.data(), values.size() * sizeof(float), &dGeomPropValues_)) return false;
    }
  }
  if (!up(hs.lightParams.data(), hs.lightParams.size() * sizeof(float),
          &dLightParams_)) return false;
  if (cuMemGetInfo) {
    size_t freeBytes = 0, totalBytes = 0;
    if (cuMemGetInfo(&freeBytes, &totalBytes) == CUDA_SUCCESS) {
      deviceUsedBytes_ = totalBytes - freeBytes;
      deviceTotalBytes_ = totalBytes;
      std::fprintf(stderr,
                   "CUDA RT VRAM: %.1f MiB used / %.1f MiB total, "
                   "%.1f MiB free\n",
                   double(totalBytes - freeBytes) / (1024.0 * 1024.0),
                   double(totalBytes) / (1024.0 * 1024.0),
                   double(freeBytes) / (1024.0 * 1024.0));
    }
  }
  if (progress) progress->phase = 4;  // done
  return true;
}

bool CudaRayTracer::updateMaterialConstants(
    int materialId, const DrawMaterialCPU& material, std::string* err) {
  if (!ctx_ || materialId < 0 || materialId >= numMats_ || !dMatPbr_ ||
      !dMatBase_ || !dMatLightRt_) {
    if (err) *err = "CUDA material index is out of range or scene is not built";
    return false;
  }
  CU_OK(cuCtxSetCurrent(reinterpret_cast<CUcontext>(ctx_)),
        "cuCtxSetCurrent(material update)");
  float pbr[6], base[3], lightRt[kLightRtOpenPBRFloats];
  PackRtMaterialConstants(material, pbr, base, lightRt);
  const CUdeviceptr pbrDst = static_cast<CUdeviceptr>(dMatPbr_) +
                             static_cast<size_t>(materialId) * sizeof(pbr);
  const CUdeviceptr baseDst = static_cast<CUdeviceptr>(dMatBase_) +
                              static_cast<size_t>(materialId) * sizeof(base);
  const CUdeviceptr lightRtDst = static_cast<CUdeviceptr>(dMatLightRt_) +
      static_cast<size_t>(materialId) * sizeof(lightRt);
  CU_OK(cuMemcpyHtoD(pbrDst, pbr, sizeof(pbr)), "material PBR upload");
  CU_OK(cuMemcpyHtoD(baseDst, base, sizeof(base)), "material base upload");
  CU_OK(cuMemcpyHtoD(lightRtDst, lightRt, sizeof(lightRt)),
        "material OpenPBR upload");
  return true;
}

bool CudaRayTracer::trace(const float invViewProj[16], const float viewProj[16],
                          const float camPos[3],
                          const float lightDir[3], const float clearColor[3],
                          float exposure,
                          int renderMode, float depthScale, const float sceneMin[3],
                          const float sceneExtent[3], int w, int h,
                          std::vector<uint8_t>* rgba, std::string* err, int spp,
                          const RtCameraLens* lens,
                          const PathTraceSettings* pathTrace,
                          std::vector<float>* linearRgba,
                          uint32_t* renderedSamples,
                          float sceneTime, float sceneFrame) {
  if (!ctx_ || (!dTris_ && pointCount_ == 0)) { if (err) *err = "CUDA scene not built"; return false; }
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
  const bool productionPath = pathTrace && pathTrace->enabled;
  const size_t accumBytes = (spp > 1 || productionPath) ? bytes * sizeof(float) : 0;
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
  cam.context[0] = sceneTime;
  cam.context[1] = sceneFrame;
  cam.camPos[3] = exposure;
  cam.lightDir[3] = depthScale;  // depth AOV normalizer
  for (int i = 0; i < 3; ++i) { cam.sceneMin[i] = sceneMin[i]; cam.sceneExtent[i] = sceneExtent[i]; }
  if (lens && lens->enabled()) {
    cam.lens[0] = lens->focusDistance;
    cam.lens[1] = lens->apertureRadius;
    cam.lens[2] = 1.0f;
  }
  if (pathTrace && pathTrace->enabled) {
    cam.lens[3] = static_cast<float>(
        0x800000u | (pathTrace->maxDepth & 255u) |
        ((pathTrace->russianRouletteDepth & 255u) << 8u) |
        ((pathTrace->seed & 127u) << 16u));
    cam.pathLimits[0] = static_cast<float>(pathTrace->maxSubsurfaceEvents);
    cam.pathLimits[1] = static_cast<float>(pathTrace->maxVolumeEvents);
    cam.pathLimits[2] = static_cast<float>(pathTrace->motionSegments);
    cam.pathLimits[3] = pathTrace->varianceThreshold;
  }
  CUdeviceptr dT = triCount_ ? dTris_ : 0,
              dN = triCount_ ? dNrms_ : 0, dC = triCount_ ? dCols_ : 0,
              dG = triCount_ ? dGeo_ : 0, dM = triCount_ ? dMat_ : 0,
              dBM = dBackMat_,
              dMP = dMatPbr_, dMB = dMatBase_, dML = dMatLightRt_, dMG = dMatGraph_, dMT = dMatTex_,
              dTx = dTexels_, dTD = dTextures_,
              dU = dUV_, dU1 = dUV1_, dIn = dInfl_, dF = dFace_,
              dDw = dDomW_, dDj = dDomJoint_, dBl = dBlasNodes_, dTl = dTlasNodes_,
              dI = instCount_ ? dInstances_ : 0, dO = dOut_;
  CUdeviceptr dMTP = dMatTexParam_;
  CUdeviceptr dLP = dLightParams_;
  CUdeviceptr dVD = dVolDens_, dVP = dVolParams_, dEm = dEmask_;
  CUdeviceptr dPC = dPointCenters_, dPA = dPointMajorAxes_, dPN = dPointNormals_,
              dPR = dPointRadii_, dPCol = dPointColors_, dPO = dPointOrder_,
              dPB = dPointBvh_;
  CUdeviceptr dPChunks = dPointChunks_;
  CUdeviceptr dDepth = 0;
  int numMats = numMats_;
  int numLights = numLights_;
  int numTextures = numTextures_;
  int numVols = numVols_;
  const int samples = spp < 1 ? 1 : spp;
  if (renderedSamples) *renderedSamples = static_cast<uint32_t>(samples);
  CUdeviceptr dAcc = (samples > 1 || productionPath) ? dAccum_ : 0;
  int sampleIdx = 0;
  int numSamples = samples;
  // ORDER MUST MATCH the kernel signature: tris,nrms,cols,geo,mats,backMats,matPbr,matBase,
  // matLightRt,numMats,lightParams,numLights,matTex,matTexParam,texels,textures,
  // numTextures,uvs,uvs1,infls,faces,domw,domj,blas,tlas,insts,out,W,H,cam,
  // volDens,volParams,numVols,emask,accum,sampleIdx,numSamples,hitDepth.
  void* args[] = {&dT,  &dN,  &dC, &dG, &dM, &dBM, &dMP, &dMB, &dML, &dMG, &numMats, &dLP,
                  &numLights, &dMT, &dMTP, &dTx, &dTD, &numTextures, &dU, &dU1,
                  &dIn, &dF,  &dDw,
                  &dDj, &dBl, &dTl, &dI, &dO, &w, &h, &cam,
                  &dVD, &dVP, &numVols, &dEm, &dPC, &dPA, &dPN, &dPR, &dPCol,
                  &dPO, &dPB, &pointCount_, &dPChunks, &pointChunkCount_,
                  &dAcc, &sampleIdx, &numSamples, &dDepth,
                  &dGeomPropDesc_, &dGeomPropValues_, &geomPropCount_};
  unsigned gx = (w + 7) / 8, gy = (h + 7) / 8;
  rgba->resize(bytes);
  if (pointPaging_ && pointHost_ && !pointHost_->pointChunks.empty()) {
    const size_t pixels = size_t(w) * size_t(h);
    if (pointDepthCap_ < pixels) {
      if (dPointDepth_) cuMemFree(static_cast<CUdeviceptr>(dPointDepth_));
      CUdeviceptr p = 0;
      if (cuMemAlloc(&p, pixels * sizeof(float)) != CUDA_SUCCESS) {
        if (err) *err = "CUDA Gaussian paging depth allocation failed";
        freeScene();
        return false;
      }
      dPointDepth_ = static_cast<uintptr_t>(p);
      pointDepthCap_ = pixels;
    }
    dDepth = static_cast<CUdeviceptr>(dPointDepth_);
    std::vector<uint8_t> best(bytes), chunkRgba(bytes);
    std::vector<uint8_t> bestSet(pixels);
    std::vector<float> bestDepth(pixels), chunkDepth(pixels);
    std::vector<uint32_t> sums(pixels * 3, 0);
    auto allocCopy = [&](const void* src, size_t n, CUdeviceptr* dst) {
      *dst = 0;
      if (cuMemAlloc(dst, n ? n : 1) != CUDA_SUCCESS) return false;
      if (n && cuMemcpyHtoD(*dst, src, n) != CUDA_SUCCESS) {
        cuMemFree(*dst); *dst = 0; return false;
      }
      return true;
    };
    auto freePointChunk = [&]() {
      if (dPC) cuMemFree(dPC);
      if (dPA) cuMemFree(dPA);
      if (dPN) cuMemFree(dPN);
      if (dPR) cuMemFree(dPR);
      if (dPCol) cuMemFree(dPCol);
      if (dPO) cuMemFree(dPO);
      if (dPB) cuMemFree(dPB);
      if (dPChunks) cuMemFree(dPChunks);
      dPC = dPA = dPN = dPR = dPCol = dPO = dPB = dPChunks = 0;
    };
    const int totalPointCount = pointCount_;
    const int totalPointChunks = pointChunkCount_;
    dAcc = 0;
    numSamples = 1;
    pointChunkCount_ = 1;
    for (int s = 0; s < samples; ++s) {
      std::fill(bestDepth.begin(), bestDepth.end(),
                std::numeric_limits<float>::infinity());
      std::fill(bestSet.begin(), bestSet.end(), uint8_t{0});
      RtPixelJitter(s, samples, &cam.sceneMin[3], &cam.sceneExtent[3]);
      for (const PointBvhChunk& srcChunk : pointHost_->pointChunks) {
        const size_t first = static_cast<size_t>(srcChunk.first);
        const size_t count = static_cast<size_t>(srcChunk.count);
        const size_t orderFirst = static_cast<size_t>(srcChunk.orderFirst);
        const size_t bvhFirst = static_cast<size_t>(srcChunk.bvhFirst);
        const size_t bvhCount = static_cast<size_t>(srcChunk.bvhCount);
        std::vector<int> localOrder(count);
        for (size_t i = 0; i < count; ++i)
          localOrder[i] = pointHost_->pointOrder[orderFirst + i] -
                          static_cast<int>(first);
        std::vector<Node> localBvh(
            pointHost_->pointBvh.begin() + bvhFirst,
            pointHost_->pointBvh.begin() + bvhFirst + bvhCount);
        const PointBvhChunk localChunk{0, static_cast<int>(count), 0, 0,
                                       static_cast<int>(bvhCount)};
        freePointChunk();
        if (!allocCopy(pointHost_->pointCenters.data() + first * 3,
                       count * 3 * sizeof(float), &dPC) ||
            !allocCopy(pointHost_->pointMajorAxes.data() + first * 3,
                       count * 3 * sizeof(float), &dPA) ||
            !allocCopy(pointHost_->pointNormals.data() + first * 3,
                       count * 3 * sizeof(float), &dPN) ||
            !allocCopy(pointHost_->pointRadii.data() + first * 2,
                       count * 2 * sizeof(float), &dPR) ||
            !allocCopy(pointHost_->pointColors.data() + first * 4,
                       count * 4 * sizeof(float), &dPCol) ||
            !allocCopy(localOrder.data(), localOrder.size() * sizeof(int), &dPO) ||
            !allocCopy(localBvh.data(), localBvh.size() * sizeof(Node), &dPB) ||
            !allocCopy(&localChunk, sizeof(localChunk), &dPChunks)) {
          if (err) *err = "CUDA Gaussian paging chunk upload failed";
          freePointChunk(); freeScene(); return false;
        }
        pointCount_ = static_cast<int>(count);
        sampleIdx = s;
        CU_OK(cuLaunchKernel(reinterpret_cast<CUfunction>(kernel_), gx, gy, 1,
                             8, 8, 1, 0, nullptr, args, nullptr),
              "cuLaunchKernel Gaussian paging");
        CU_OK(cuCtxSynchronize(), "cuCtxSynchronize Gaussian paging");
        CU_OK(cuMemcpyDtoH(chunkRgba.data(), static_cast<CUdeviceptr>(dOut_), bytes),
              "cuMemcpyDtoH Gaussian paging color");
        CU_OK(cuMemcpyDtoH(chunkDepth.data(), dDepth,
                           pixels * sizeof(float)),
              "cuMemcpyDtoH Gaussian paging depth");
        for (size_t pidx = 0; pidx < pixels; ++pidx) {
          if (!bestSet[pidx] || chunkDepth[pidx] < bestDepth[pidx]) {
            bestDepth[pidx] = chunkDepth[pidx];
            bestSet[pidx] = 1;
            std::memcpy(&best[pidx * 4], &chunkRgba[pidx * 4], 4);
          }
        }
      }
      for (size_t pidx = 0; pidx < pixels; ++pidx) {
        sums[pidx * 3 + 0] += best[pidx * 4 + 0];
        sums[pidx * 3 + 1] += best[pidx * 4 + 1];
        sums[pidx * 3 + 2] += best[pidx * 4 + 2];
      }
    }
    freePointChunk();
    pointCount_ = totalPointCount;
    pointChunkCount_ = totalPointChunks;
    for (size_t pidx = 0; pidx < pixels; ++pidx) {
      (*rgba)[pidx * 4 + 0] = static_cast<uint8_t>(sums[pidx * 3 + 0] / samples);
      (*rgba)[pidx * 4 + 1] = static_cast<uint8_t>(sums[pidx * 3 + 1] / samples);
      (*rgba)[pidx * 4 + 2] = static_cast<uint8_t>(sums[pidx * 3 + 2] / samples);
      (*rgba)[pidx * 4 + 3] = 255;
    }
    return true;
  }
  // Launch one kernel per Halton-jittered sample; the kernel accumulates on the
  // device. Normally this is one synchronize + one readback for the whole
  // frame. Production renders optionally checkpoint at a coarse interval so a
  // converged CUDA render can stop before its maximum sample budget.
  int completedSamples = samples;
  std::vector<float> convergencePrevious;
  std::vector<float> convergenceCurrent;
  const bool adaptive = productionPath && linearRgba && pathTrace &&
                        pathTrace->varianceThreshold > 0.0f &&
                        samples > static_cast<int>(pathTrace->minSamples);
  const int convergenceInterval =
      std::max(8, static_cast<int>(pathTrace ? pathTrace->minSamples / 4u : 8u));
  for (int s = 0; s < samples; ++s) {
    float jx, jy;
    RtPixelJitter(s, samples, &jx, &jy);
    cam.sceneMin[3] = jx;
    cam.sceneExtent[3] = jy;
    sampleIdx = s;
    CU_OK(cuLaunchKernel(reinterpret_cast<CUfunction>(kernel_), gx, gy, 1, 8, 8, 1, 0,
                         nullptr, args, nullptr),
          "cuLaunchKernel");
    const int completed = s + 1;
    if (adaptive && completed >= static_cast<int>(pathTrace->minSamples) &&
        completed % convergenceInterval == 0) {
      CU_OK(cuCtxSynchronize(), "cuCtxSynchronize convergence");
      convergenceCurrent.resize(size_t(w) * size_t(h) * 4u);
      CU_OK(cuMemcpyDtoH(convergenceCurrent.data(),
                         static_cast<CUdeviceptr>(dAccum_),
                         convergenceCurrent.size() * sizeof(float)),
            "cuMemcpyDtoH convergence");
      const float inv = 1.0f / static_cast<float>(completed);
      for (size_t i = 0; i < convergenceCurrent.size(); i += 4) {
        convergenceCurrent[i] *= inv;
        convergenceCurrent[i + 1] *= inv;
        convergenceCurrent[i + 2] *= inv;
        convergenceCurrent[i + 3] = 1.0f;
      }
      const double change =
          PathTraceRelativeChange(convergenceCurrent, convergencePrevious);
      convergencePrevious.swap(convergenceCurrent);
      if (change <= static_cast<double>(pathTrace->varianceThreshold)) {
        completedSamples = completed;
        LOGI("CUDA path trace converged at %d samples "
             "(relative RMS %.6f <= %.6f)",
             completed, change, pathTrace->varianceThreshold);
        break;
      }
    }
  }
  CU_OK(cuCtxSynchronize(), "cuCtxSynchronize");
  CU_OK(cuMemcpyDtoH(rgba->data(), static_cast<CUdeviceptr>(dOut_), bytes),
        "cuMemcpyDtoH");
  if (linearRgba && pathTrace && pathTrace->enabled) {
    linearRgba->resize(size_t(w) * size_t(h) * 4u);
    CU_OK(cuMemcpyDtoH(linearRgba->data(), static_cast<CUdeviceptr>(dAccum_),
                       linearRgba->size() * sizeof(float)),
          "cuMemcpyDtoH(linear accumulation)");
    const float inv = 1.0f / static_cast<float>(completedSamples);
    for (size_t i = 0; i < linearRgba->size(); i += 4) {
      (*linearRgba)[i] *= inv;
      (*linearRgba)[i + 1] *= inv;
      (*linearRgba)[i + 2] *= inv;
      (*linearRgba)[i + 3] = 1.0f;
    }
  }
  if (renderedSamples)
    *renderedSamples = static_cast<uint32_t>(completedSamples);
  return true;
}

}  // namespace tusdview
