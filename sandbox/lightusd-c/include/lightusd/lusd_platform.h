/*
 * lusd_platform.h - Platform detection, export macros, C11 compatibility
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LUSD_PLATFORM_H
#define LUSD_PLATFORM_H

/* C11 conformance */
#if defined(__cplusplus)
  #define LUSD_EXTERN_C_BEGIN extern "C" {
  #define LUSD_EXTERN_C_END   }
#else
  #define LUSD_EXTERN_C_BEGIN
  #define LUSD_EXTERN_C_END
#endif

/* Export/import macros */
#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef LUSD_BUILDING_DLL
    #define LUSD_API __declspec(dllexport)
  #elif defined(LUSD_DLL)
    #define LUSD_API __declspec(dllimport)
  #else
    #define LUSD_API
  #endif
#elif defined(__GNUC__) || defined(__clang__)
  #define LUSD_API __attribute__((visibility("default")))
#else
  #define LUSD_API
#endif

/* Inline hint */
#if defined(_MSC_VER)
  #define LUSD_INLINE __forceinline
#elif defined(__GNUC__) || defined(__clang__)
  #define LUSD_INLINE static inline __attribute__((always_inline))
#else
  #define LUSD_INLINE static inline
#endif

/* Alignment */
#if defined(_MSC_VER)
  #define LUSD_ALIGNAS(n) __declspec(align(n))
#else
  #define LUSD_ALIGNAS(n) _Alignas(n)
#endif

/* Static assert */
#if defined(__cplusplus) && (__cplusplus >= 201103L)
  #define LUSD_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(_MSC_VER)
  #define LUSD_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #define LUSD_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
  #define LUSD_STATIC_ASSERT(cond, msg) \
    typedef char lusd_static_assert_##__LINE__[(cond) ? 1 : -1]
#endif

/* Null pointer */
#ifndef NULL
  #ifdef __cplusplus
    #define NULL nullptr
  #else
    #define NULL ((void*)0)
  #endif
#endif

/* Boolean */
#if !defined(__cplusplus) && defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
  #include <stdbool.h>
#elif !defined(__cplusplus)
  #ifndef bool
    #define bool  int
    #define true  1
    #define false 0
  #endif
#endif

/* Fixed-width integers */
#include <stdint.h>
#include <stddef.h>

/* Platform detection */
#if defined(_WIN32)
  #define LUSD_PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
  #define LUSD_PLATFORM_APPLE 1
#elif defined(__linux__)
  #define LUSD_PLATFORM_LINUX 1
#elif defined(__EMSCRIPTEN__)
  #define LUSD_PLATFORM_WASM 1
#endif

/* Architecture */
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__) || defined(_M_ARM64)
  #define LUSD_ARCH_64BIT 1
#else
  #define LUSD_ARCH_32BIT 1
#endif

/* Unused parameter suppression */
#define LUSD_UNUSED(x) (void)(x)

#endif /* LUSD_PLATFORM_H */
