/*
 * hipew.c — implementation of the minimal HIP / hiprtc runtime loader.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "hipew.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef HMODULE hipew_lib_t;
#else
#include <dlfcn.h>
typedef void *hipew_lib_t;
#endif

#include <stddef.h>

/* --- entry-point definitions ---------------------------------------------- */
thipInit hipInit;
thipDriverGetVersion hipDriverGetVersion;
thipGetDeviceCount hipGetDeviceCount;
thipSetDevice hipSetDevice;
thipDeviceGet hipDeviceGet;
thipDeviceGetName hipDeviceGetName;
thipMalloc hipMalloc;
thipFree hipFree;
thipMemcpyHtoD hipMemcpyHtoD;
thipMemcpyDtoH hipMemcpyDtoH;
thipModuleLoadData hipModuleLoadData;
thipModuleUnload hipModuleUnload;
thipModuleGetFunction hipModuleGetFunction;
thipModuleLaunchKernel hipModuleLaunchKernel;
thipDeviceSynchronize hipDeviceSynchronize;
thipGetErrorString hipGetErrorString;

thiprtcCreateProgram hiprtcCreateProgram;
thiprtcCompileProgram hiprtcCompileProgram;
thiprtcGetCodeSize hiprtcGetCodeSize;
thiprtcGetCode hiprtcGetCode;
thiprtcGetProgramLogSize hiprtcGetProgramLogSize;
thiprtcGetProgramLog hiprtcGetProgramLog;
thiprtcDestroyProgram hiprtcDestroyProgram;
thiprtcGetErrorString hiprtcGetErrorString;

static hipew_lib_t g_hip_lib = NULL;
static hipew_lib_t g_hiprtc_lib = NULL;
static int g_inited_mask = 0;
static int g_result = HIPEW_ERROR_NOT_INITIALIZED;

static hipew_lib_t hipew_dlopen(const char *const *names) {
  for (int i = 0; names[i]; i++) {
#ifdef _WIN32
    hipew_lib_t h = LoadLibraryA(names[i]);
#else
    hipew_lib_t h = dlopen(names[i], RTLD_NOW | RTLD_GLOBAL);
#endif
    if (h) return h;
  }
  return NULL;
}

static void *hipew_dlsym(hipew_lib_t lib, const char *name) {
#ifdef _WIN32
  return (void *)GetProcAddress(lib, name);
#else
  return dlsym(lib, name);
#endif
}

/* Resolve `sym` from `lib` into `*dst`; set the local `ok` flag to 0 on miss. */
#define HIPEW_FIND(lib, dst, type, sym)            \
  do {                                             \
    *(void **)(&dst) = hipew_dlsym((lib), #sym);   \
    if (!dst) ok = 0;                              \
  } while (0)

int hipewInit(unsigned int mask) {
  /* Already attempted: return the cached outcome (only re-try parts not yet
   * satisfied is overkill — callers pass a fixed mask). */
  if (g_inited_mask == (int)mask && g_result != HIPEW_ERROR_NOT_INITIALIZED) {
    return g_result;
  }

  int ok = 1;

  if (mask & HIPEW_INIT_HIP) {
    if (!g_hip_lib) {
      static const char *const hip_names[] = {
#ifdef _WIN32
          "amdhip64.dll", "amdhip64_7.dll", "amdhip64_6.dll",
#else
          "libamdhip64.so", "libamdhip64.so.7", "libamdhip64.so.6",
          "libamdhip64.so.5",
#endif
          NULL};
      g_hip_lib = hipew_dlopen(hip_names);
    }
    if (!g_hip_lib) {
      g_inited_mask = (int)mask;
      g_result = HIPEW_ERROR_OPEN_FAILED;
      return g_result;
    }
    HIPEW_FIND(g_hip_lib, hipInit, thipInit, hipInit);
    HIPEW_FIND(g_hip_lib, hipDriverGetVersion, thipDriverGetVersion,
               hipDriverGetVersion);
    HIPEW_FIND(g_hip_lib, hipGetDeviceCount, thipGetDeviceCount,
               hipGetDeviceCount);
    HIPEW_FIND(g_hip_lib, hipSetDevice, thipSetDevice, hipSetDevice);
    HIPEW_FIND(g_hip_lib, hipDeviceGet, thipDeviceGet, hipDeviceGet);
    HIPEW_FIND(g_hip_lib, hipDeviceGetName, thipDeviceGetName, hipDeviceGetName);
    HIPEW_FIND(g_hip_lib, hipMalloc, thipMalloc, hipMalloc);
    HIPEW_FIND(g_hip_lib, hipFree, thipFree, hipFree);
    HIPEW_FIND(g_hip_lib, hipMemcpyHtoD, thipMemcpyHtoD, hipMemcpyHtoD);
    HIPEW_FIND(g_hip_lib, hipMemcpyDtoH, thipMemcpyDtoH, hipMemcpyDtoH);
    HIPEW_FIND(g_hip_lib, hipModuleLoadData, thipModuleLoadData,
               hipModuleLoadData);
    HIPEW_FIND(g_hip_lib, hipModuleUnload, thipModuleUnload, hipModuleUnload);
    HIPEW_FIND(g_hip_lib, hipModuleGetFunction, thipModuleGetFunction,
               hipModuleGetFunction);
    HIPEW_FIND(g_hip_lib, hipModuleLaunchKernel, thipModuleLaunchKernel,
               hipModuleLaunchKernel);
    HIPEW_FIND(g_hip_lib, hipDeviceSynchronize, thipDeviceSynchronize,
               hipDeviceSynchronize);
    HIPEW_FIND(g_hip_lib, hipGetErrorString, thipGetErrorString,
               hipGetErrorString);
  }

  if (mask & HIPEW_INIT_HIPRTC) {
    if (!g_hiprtc_lib) {
      static const char *const rtc_names[] = {
#ifdef _WIN32
          "hiprtc.dll", "hiprtc0507.dll", "hiprtc0506.dll",
#else
          "libhiprtc.so", "libhiprtc.so.7", "libhiprtc.so.6",
#endif
          NULL};
      g_hiprtc_lib = hipew_dlopen(rtc_names);
    }
    if (!g_hiprtc_lib) {
      g_inited_mask = (int)mask;
      g_result = HIPEW_ERROR_OPEN_FAILED;
      return g_result;
    }
    HIPEW_FIND(g_hiprtc_lib, hiprtcCreateProgram, thiprtcCreateProgram,
               hiprtcCreateProgram);
    HIPEW_FIND(g_hiprtc_lib, hiprtcCompileProgram, thiprtcCompileProgram,
               hiprtcCompileProgram);
    HIPEW_FIND(g_hiprtc_lib, hiprtcGetCodeSize, thiprtcGetCodeSize,
               hiprtcGetCodeSize);
    HIPEW_FIND(g_hiprtc_lib, hiprtcGetCode, thiprtcGetCode, hiprtcGetCode);
    HIPEW_FIND(g_hiprtc_lib, hiprtcGetProgramLogSize, thiprtcGetProgramLogSize,
               hiprtcGetProgramLogSize);
    HIPEW_FIND(g_hiprtc_lib, hiprtcGetProgramLog, thiprtcGetProgramLog,
               hiprtcGetProgramLog);
    HIPEW_FIND(g_hiprtc_lib, hiprtcDestroyProgram, thiprtcDestroyProgram,
               hiprtcDestroyProgram);
    HIPEW_FIND(g_hiprtc_lib, hiprtcGetErrorString, thiprtcGetErrorString,
               hiprtcGetErrorString);
  }

  g_inited_mask = (int)mask;
  g_result = ok ? HIPEW_SUCCESS : HIPEW_ERROR_OPEN_FAILED;
  return g_result;
}
