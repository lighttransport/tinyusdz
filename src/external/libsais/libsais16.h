/*--

This file is a part of libsais, a library for linear time suffix array,
longest common prefix array and burrows wheeler transform construction.

   Copyright (c) 2021-2022 Ilya Grebnov <ilya.grebnov@gmail.com>

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

Please see the file LICENSE for full copyright information.

--*/

#ifndef LIBSAIS16_H
#define LIBSAIS16_H 1

#define LIBSAIS16_VERSION_MAJOR   2
#define LIBSAIS16_VERSION_MINOR   7
#define LIBSAIS16_VERSION_PATCH   3
#define LIBSAIS16_VERSION_STRING  "2.7.3"

#ifdef _WIN32
    #ifdef LIBSAIS_SHARED
        #ifdef LIBSAIS_EXPORTS
            #define LIBSAIS_API __declspec(dllexport)
        #else
            #define LIBSAIS_API __declspec(dllimport)
        #endif
    #else
        #define LIBSAIS_API
    #endif
#else
    #define LIBSAIS_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

    #include <stdint.h>

    /**
    * Creates the libsais16 context that allows reusing allocated memory with each libsais16 operation. 
    * In multi-threaded environments, use one context per thread for parallel executions.
    * @return the libsais16 context, NULL otherwise.
    */
    LIBSAIS_API void * libsais16_create_ctx(void);

#if defined(LIBSAIS_OPENMP)
    /**
    * Creates the libsais16 context that allows reusing allocated memory with each parallel libsais16 operation using OpenMP. 
    * In multi-threaded environments, use one context per thread for parallel executions.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return the libsais16 context, NULL otherwise.
    */
    LIBSAIS_API void * libsais16_create_ctx_omp(int32_t threads);
#endif

    /**
    * Destroys the libsass context and free previusly allocated memory.
    * @param ctx The libsais16 context (can be NULL).
    */
    LIBSAIS_API void libsais16_free_ctx(void * ctx);

    /**
    * Constructs the suffix array of a given 16-bit string.
    * @param T [0..n-1] The input 16-bit string.
    * @param SA [0..n-1+fs] The output array of suffixes.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of SA array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16(const uint16_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);

    /**
    * Constructs the suffix array of a given 16-bit string using libsais16 context.
    * @param ctx The libsais16 context.
    * @param T [0..n-1] The input 16-bit string.
    * @param SA [0..n-1+fs] The output array of suffixes.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of SA array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_ctx(const void * ctx, const uint16_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);

#if defined(LIBSAIS_OPENMP)
    /**
    * Constructs the suffix array of a given 16-bit string in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param SA [0..n-1+fs] The output array of suffixes.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of SA array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_omp(const uint16_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq, int32_t threads);
#endif

    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @return The primary index if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq);

    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string with auxiliary indexes.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The output auxiliary indexes.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt_aux(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq, int32_t r, int32_t * I);

    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string using libsais16 context.
    * @param ctx The libsais16 context.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @return The primary index if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt_ctx(const void * ctx, const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq);

    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string with auxiliary indexes using libsais16 context.
    * @param ctx The libsais16 context.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The output auxiliary indexes.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt_aux_ctx(const void * ctx, const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq, int32_t r, int32_t * I);

#if defined(LIBSAIS_OPENMP)
    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return The primary index if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt_omp(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq, int32_t threads);

    /**
    * Constructs the burrows-wheeler transformed 16-bit string (BWT) of a given 16-bit string with auxiliary indexes in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given 16-bit string.
    * @param fs The extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..65535] The output 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The output auxiliary indexes.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_bwt_aux_omp(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq, int32_t r, int32_t * I, int32_t threads);
#endif

    /**
    * Creates the libsais16 reverse BWT context that allows reusing allocated memory with each libsais16_unbwt_* operation. 
    * In multi-threaded environments, use one context per thread for parallel executions.
    * @return the libsais16 context, NULL otherwise.
    */
    LIBSAIS_API void * libsais16_unbwt_create_ctx(void);

#if defined(LIBSAIS_OPENMP)
    /**
    * Creates the libsais16 reverse BWT context that allows reusing allocated memory with each parallel libsais16_unbwt_* operation using OpenMP. 
    * In multi-threaded environments, use one context per thread for parallel executions.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return the libsais16 context, NULL otherwise.
    */
    LIBSAIS_API void * libsais16_unbwt_create_ctx_omp(int32_t threads);
#endif

    /**
    * Destroys the libsass reverse BWT context and free previusly allocated memory.
    * @param ctx The libsais16 context (can be NULL).
    */
    LIBSAIS_API void libsais16_unbwt_free_ctx(void * ctx);

    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with primary index.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param i The primary index.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t i);

    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with primary index using libsais16 reverse BWT context.
    * @param ctx The libsais16 reverse BWT context.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param i The primary index.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt_ctx(const void * ctx, const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t i);

    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with auxiliary indexes.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The input auxiliary indexes.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt_aux(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t r, const int32_t * I);

    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with auxiliary indexes using libsais16 reverse BWT context.
    * @param ctx The libsais16 reverse BWT context.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The input auxiliary indexes.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt_aux_ctx(const void * ctx, const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t r, const int32_t * I);

#if defined(LIBSAIS_OPENMP)
    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with primary index in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param i The primary index.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt_omp(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t i, int32_t threads);

    /**
    * Constructs the original 16-bit string from a given burrows-wheeler transformed 16-bit string (BWT) with auxiliary indexes in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param U [0..n-1] The output 16-bit string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given 16-bit string.
    * @param freq [0..65535] The input 16-bit symbol frequency table (can be NULL).
    * @param r The sampling rate for auxiliary indexes (must be power of 2).
    * @param I [0..(n-1)/r] The input auxiliary indexes.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    LIBSAIS_API int32_t libsais16_unbwt_aux_omp(const uint16_t * T, uint16_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t r, const int32_t * I, int32_t threads);
#endif

    /**
    * Constructs the permuted longest common prefix array (PLCP) of a given 16-bit string and a suffix array.
    * @param T [0..n-1] The input 16-bit string.
    * @param SA [0..n-1] The input suffix array.
    * @param PLCP [0..n-1] The output permuted longest common prefix array.
    * @param n The length of the 16-bit string and the suffix array.
    * @return 0 if no error occurred, -1 otherwise.
    */
    LIBSAIS_API int32_t libsais16_plcp(const uint16_t * T, const int32_t * SA, int32_t * PLCP, int32_t n);

    /**
    * Constructs the longest common prefix array (LCP) of a given permuted longest common prefix array (PLCP) and a suffix array.
    * @param PLCP [0..n-1] The input permuted longest common prefix array.
    * @param SA [0..n-1] The input suffix array.
    * @param LCP [0..n-1] The output longest common prefix array (can be SA).
    * @param n The length of the permuted longest common prefix array and the suffix array.
    * @return 0 if no error occurred, -1 otherwise.
    */
    LIBSAIS_API int32_t libsais16_lcp(const int32_t * PLCP, const int32_t * SA, int32_t * LCP, int32_t n);

#if defined(LIBSAIS_OPENMP)
    /**
    * Constructs the permuted longest common prefix array (PLCP) of a given 16-bit string and a suffix array in parallel using OpenMP.
    * @param T [0..n-1] The input 16-bit string.
    * @param SA [0..n-1] The input suffix array.
    * @param PLCP [0..n-1] The output permuted longest common prefix array.
    * @param n The length of the 16-bit string and the suffix array.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 otherwise.
    */
    LIBSAIS_API int32_t libsais16_plcp_omp(const uint16_t * T, const int32_t * SA, int32_t * PLCP, int32_t n, int32_t threads);

    /**
    * Constructs the longest common prefix array (LCP) of a given permuted longest common prefix array (PLCP) and a suffix array in parallel using OpenMP.
    * @param PLCP [0..n-1] The input permuted longest common prefix array.
    * @param SA [0..n-1] The input suffix array.
    * @param LCP [0..n-1] The output longest common prefix array (can be SA).
    * @param n The length of the permuted longest common prefix array and the suffix array.
    * @param threads The number of OpenMP threads to use (can be 0 for OpenMP default).
    * @return 0 if no error occurred, -1 otherwise.
    */
    LIBSAIS_API int32_t libsais16_lcp_omp(const int32_t * PLCP, const int32_t * SA, int32_t * LCP, int32_t n, int32_t threads);
#endif

#ifdef __cplusplus
}
#endif

#endif
