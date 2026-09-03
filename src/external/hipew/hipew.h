/*
 * hipew.h — minimal runtime loader for the HIP runtime + hiprtc (ROCm).
 *
 * Modeled on cuew (examples/common/cuew) but intentionally tiny: it declares
 * only the handful of HIP driver/runtime and hiprtc entry points the LightRT HIP
 * backend and lusdview's HipRayTracer need. Like cuew/vkew, it opens the shared
 * libraries at runtime (dlopen / LoadLibrary) so there is NO link-time ROCm
 * dependency and a build with no ROCm installed still runs — hipewInit() just
 * returns an error and callers fall back / skip.
 *
 * The HIP types below are ABI-compatible re-declarations of the ROCm headers
 * (opaque handles + the few enum values we use); we deliberately do NOT include
 * <hip/hip_runtime_api.h> so this compiles without the ROCm SDK present.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTRT_HIPEW_H
#define LIGHTRT_HIPEW_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* hipewInit() result codes. */
#define HIPEW_SUCCESS 0
#define HIPEW_ERROR_OPEN_FAILED -1
#define HIPEW_ERROR_NOT_INITIALIZED -2

/* What to load. */
#define HIPEW_INIT_HIP 1
#define HIPEW_INIT_HIPRTC 2

/* --- HIP types (ABI-compatible with ROCm's hip/driver_types.h) ------------ */
typedef int hipError_t; /* hipSuccess == 0 */
typedef int hipDevice_t;
typedef void *hipDeviceptr_t;
typedef struct ihipModule_t *hipModule_t;
typedef struct ihipModuleSymbol_t *hipFunction_t;
typedef struct ihipStream_t *hipStream_t;
typedef struct ihipEvent_t *hipEvent_t;

typedef enum hipMemcpyKind {
  hipMemcpyHostToHost = 0,
  hipMemcpyHostToDevice = 1,
  hipMemcpyDeviceToHost = 2,
  hipMemcpyDeviceToDevice = 3,
  hipMemcpyDefault = 4
} hipMemcpyKind;

#define hipSuccess 0

/* --- hiprtc types --------------------------------------------------------- */
typedef int hiprtcResult; /* HIPRTC_SUCCESS == 0 */
typedef struct _hiprtcProgram *hiprtcProgram;

#define HIPRTC_SUCCESS 0

/* --- function pointer typedefs -------------------------------------------- */
typedef hipError_t (*thipInit)(unsigned int flags);
typedef hipError_t (*thipDriverGetVersion)(int *driverVersion);
typedef hipError_t (*thipGetDeviceCount)(int *count);
typedef hipError_t (*thipSetDevice)(int deviceId);
typedef hipError_t (*thipDeviceGet)(hipDevice_t *device, int ordinal);
typedef hipError_t (*thipDeviceGetName)(char *name, int len, hipDevice_t device);
typedef hipError_t (*thipMalloc)(void **ptr, size_t size);
typedef hipError_t (*thipFree)(void *ptr);
typedef hipError_t (*thipMemGetInfo)(size_t *free, size_t *total);
typedef hipError_t (*thipMemcpyHtoD)(hipDeviceptr_t dst, const void *src,
                                     size_t sizeBytes);
typedef hipError_t (*thipMemcpyDtoH)(void *dst, hipDeviceptr_t src,
                                     size_t sizeBytes);
typedef hipError_t (*thipMemcpyDtoHAsync)(void *dst, hipDeviceptr_t src,
                                          size_t sizeBytes,
                                          hipStream_t stream);
typedef hipError_t (*thipHostMalloc)(void **ptr, size_t sizeBytes,
                                     unsigned int flags);
typedef hipError_t (*thipHostFree)(void *ptr);
typedef hipError_t (*thipStreamCreate)(hipStream_t *stream);
typedef hipError_t (*thipStreamDestroy)(hipStream_t stream);
typedef hipError_t (*thipStreamSynchronize)(hipStream_t stream);
typedef hipError_t (*thipEventCreate)(hipEvent_t *event);
typedef hipError_t (*thipEventDestroy)(hipEvent_t event);
typedef hipError_t (*thipEventRecord)(hipEvent_t event, hipStream_t stream);
typedef hipError_t (*thipEventElapsedTime)(float *ms, hipEvent_t start,
                                           hipEvent_t stop);
typedef hipError_t (*thipModuleLoadData)(hipModule_t *module, const void *image);
typedef hipError_t (*thipModuleUnload)(hipModule_t module);
typedef hipError_t (*thipModuleGetFunction)(hipFunction_t *function,
                                            hipModule_t module,
                                            const char *kname);
typedef hipError_t (*thipModuleLaunchKernel)(
    hipFunction_t f, unsigned int gridDimX, unsigned int gridDimY,
    unsigned int gridDimZ, unsigned int blockDimX, unsigned int blockDimY,
    unsigned int blockDimZ, unsigned int sharedMemBytes, hipStream_t stream,
    void **kernelParams, void **extra);
typedef hipError_t (*thipDeviceSynchronize)(void);
typedef const char *(*thipGetErrorString)(hipError_t hipError);

typedef hiprtcResult (*thiprtcCreateProgram)(hiprtcProgram *prog, const char *src,
                                             const char *name, int numHeaders,
                                             const char *const *headers,
                                             const char *const *includeNames);
typedef hiprtcResult (*thiprtcCompileProgram)(hiprtcProgram prog, int numOptions,
                                              const char *const *options);
typedef hiprtcResult (*thiprtcGetCodeSize)(hiprtcProgram prog,
                                           size_t *codeSizeRet);
typedef hiprtcResult (*thiprtcGetCode)(hiprtcProgram prog, char *code);
typedef hiprtcResult (*thiprtcGetProgramLogSize)(hiprtcProgram prog,
                                                 size_t *logSizeRet);
typedef hiprtcResult (*thiprtcGetProgramLog)(hiprtcProgram prog, char *log);
typedef hiprtcResult (*thiprtcDestroyProgram)(hiprtcProgram *prog);
typedef const char *(*thiprtcGetErrorString)(hiprtcResult result);

/* --- resolved entry points (valid after a successful hipewInit) ----------- */
extern thipInit hipew_hipInit;
extern thipDriverGetVersion hipew_hipDriverGetVersion;
extern thipGetDeviceCount hipew_hipGetDeviceCount;
extern thipSetDevice hipew_hipSetDevice;
extern thipDeviceGet hipew_hipDeviceGet;
extern thipDeviceGetName hipew_hipDeviceGetName;
extern thipMalloc hipew_hipMalloc;
extern thipFree hipew_hipFree;
extern thipMemGetInfo hipew_hipMemGetInfo;
extern thipMemcpyHtoD hipew_hipMemcpyHtoD;
extern thipMemcpyDtoH hipew_hipMemcpyDtoH;
extern thipMemcpyDtoHAsync hipew_hipMemcpyDtoHAsync;
extern thipHostMalloc hipew_hipHostMalloc;
extern thipHostFree hipew_hipHostFree;
extern thipStreamCreate hipew_hipStreamCreate;
extern thipStreamDestroy hipew_hipStreamDestroy;
extern thipStreamSynchronize hipew_hipStreamSynchronize;
extern thipEventCreate hipew_hipEventCreate;
extern thipEventDestroy hipew_hipEventDestroy;
extern thipEventRecord hipew_hipEventRecord;
extern thipEventElapsedTime hipew_hipEventElapsedTime;
extern thipModuleLoadData hipew_hipModuleLoadData;
extern thipModuleUnload hipew_hipModuleUnload;
extern thipModuleGetFunction hipew_hipModuleGetFunction;
extern thipModuleLaunchKernel hipew_hipModuleLaunchKernel;
extern thipDeviceSynchronize hipew_hipDeviceSynchronize;
extern thipGetErrorString hipew_hipGetErrorString;

extern thiprtcCreateProgram hipew_hiprtcCreateProgram;
extern thiprtcCompileProgram hipew_hiprtcCompileProgram;
extern thiprtcGetCodeSize hipew_hiprtcGetCodeSize;
extern thiprtcGetCode hipew_hiprtcGetCode;
extern thiprtcGetProgramLogSize hipew_hiprtcGetProgramLogSize;
extern thiprtcGetProgramLog hipew_hiprtcGetProgramLog;
extern thiprtcDestroyProgram hipew_hiprtcDestroyProgram;
extern thiprtcGetErrorString hipew_hiprtcGetErrorString;

/* Compatibility names for existing hipew clients. The variables themselves
 * remain in the hipew_ namespace so they cannot interpose on libamdhip64's
 * real HIP entry points when another translation unit links the HIP runtime. */
#define hipInit hipew_hipInit
#define hipDriverGetVersion hipew_hipDriverGetVersion
#define hipGetDeviceCount hipew_hipGetDeviceCount
#define hipSetDevice hipew_hipSetDevice
#define hipDeviceGet hipew_hipDeviceGet
#define hipDeviceGetName hipew_hipDeviceGetName
#define hipMalloc hipew_hipMalloc
#define hipFree hipew_hipFree
#define hipMemGetInfo hipew_hipMemGetInfo
#define hipMemcpyHtoD hipew_hipMemcpyHtoD
#define hipMemcpyDtoH hipew_hipMemcpyDtoH
#define hipMemcpyDtoHAsync hipew_hipMemcpyDtoHAsync
#define hipHostMalloc hipew_hipHostMalloc
#define hipHostFree hipew_hipHostFree
#define hipStreamCreate hipew_hipStreamCreate
#define hipStreamDestroy hipew_hipStreamDestroy
#define hipStreamSynchronize hipew_hipStreamSynchronize
#define hipEventCreate hipew_hipEventCreate
#define hipEventDestroy hipew_hipEventDestroy
#define hipEventRecord hipew_hipEventRecord
#define hipEventElapsedTime hipew_hipEventElapsedTime
#define hipModuleLoadData hipew_hipModuleLoadData
#define hipModuleUnload hipew_hipModuleUnload
#define hipModuleGetFunction hipew_hipModuleGetFunction
#define hipModuleLaunchKernel hipew_hipModuleLaunchKernel
#define hipDeviceSynchronize hipew_hipDeviceSynchronize
#define hipGetErrorString hipew_hipGetErrorString
#define hiprtcCreateProgram hipew_hiprtcCreateProgram
#define hiprtcCompileProgram hipew_hiprtcCompileProgram
#define hiprtcGetCodeSize hipew_hiprtcGetCodeSize
#define hiprtcGetCode hipew_hiprtcGetCode
#define hiprtcGetProgramLogSize hipew_hiprtcGetProgramLogSize
#define hiprtcGetProgramLog hipew_hiprtcGetProgramLog
#define hiprtcDestroyProgram hipew_hiprtcDestroyProgram
#define hiprtcGetErrorString hipew_hiprtcGetErrorString

/* Load libamdhip64 + libhiprtc and resolve the entry points above. `mask` is a
 * bitwise-OR of HIPEW_INIT_HIP / HIPEW_INIT_HIPRTC. Returns HIPEW_SUCCESS, or an
 * HIPEW_ERROR_* code if a requested library or symbol could not be found.
 * Idempotent and safe to call repeatedly (subsequent calls return the cached
 * result). */
int hipewInit(unsigned int mask);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTRT_HIPEW_H */
