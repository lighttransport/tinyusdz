/*
 * hipew.h — minimal runtime loader for the HIP runtime + hiprtc (ROCm).
 *
 * Modeled on cuew (examples/common/cuew) but intentionally tiny: it declares
 * only the handful of HIP driver/runtime and hiprtc entry points the LightRT HIP
 * backend and tusdview's HipRayTracer need. Like cuew/vkew, it opens the shared
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
typedef hipError_t (*thipMemcpyHtoD)(hipDeviceptr_t dst, const void *src,
                                     size_t sizeBytes);
typedef hipError_t (*thipMemcpyDtoH)(void *dst, hipDeviceptr_t src,
                                     size_t sizeBytes);
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
extern thipInit hipInit;
extern thipDriverGetVersion hipDriverGetVersion;
extern thipGetDeviceCount hipGetDeviceCount;
extern thipSetDevice hipSetDevice;
extern thipDeviceGet hipDeviceGet;
extern thipDeviceGetName hipDeviceGetName;
extern thipMalloc hipMalloc;
extern thipFree hipFree;
extern thipMemcpyHtoD hipMemcpyHtoD;
extern thipMemcpyDtoH hipMemcpyDtoH;
extern thipModuleLoadData hipModuleLoadData;
extern thipModuleUnload hipModuleUnload;
extern thipModuleGetFunction hipModuleGetFunction;
extern thipModuleLaunchKernel hipModuleLaunchKernel;
extern thipDeviceSynchronize hipDeviceSynchronize;
extern thipGetErrorString hipGetErrorString;

extern thiprtcCreateProgram hiprtcCreateProgram;
extern thiprtcCompileProgram hiprtcCompileProgram;
extern thiprtcGetCodeSize hiprtcGetCodeSize;
extern thiprtcGetCode hiprtcGetCode;
extern thiprtcGetProgramLogSize hiprtcGetProgramLogSize;
extern thiprtcGetProgramLog hiprtcGetProgramLog;
extern thiprtcDestroyProgram hiprtcDestroyProgram;
extern thiprtcGetErrorString hiprtcGetErrorString;

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
