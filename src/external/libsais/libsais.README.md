# libsais

The libsais is a library for fast (see [Benchmarks](#benchmarks) below) linear time suffix array, longest common prefix array and Burrows-Wheeler transform construction based on induced sorting algorithm described in the following papers: 
* Ge Nong, Sen Zhang, Wai Hong Chan *Two Efficient Algorithms for Linear Suffix Array Construction*, 2008
* Juha Karkkainen, Giovanni Manzini, Simon J. Puglisi *Permuted Longest-Common-Prefix Array*, 2009
* Nataliya Timoshevskaya, Wu-chun Feng *SAIS-OPT: On the characterization and optimization of the SA-IS algorithm for suffix array construction*, 2014
* Jing Yi Xie, Ge Nong, Bin Lao, Wentao Xu *Scalable Suffix Sorting on a Multicore Machine*, 2020

Copyright (c) 2021-2022 Ilya Grebnov <ilya.grebnov@gmail.com>

>The libsais is inspired by [libdivsufsort](https://github.com/y-256/libdivsufsort), [sais](https://sites.google.com/site/yuta256/sais) libraries by Yuta Mori and [msufsort](https://github.com/michaelmaniscalco/msufsort) by Michael Maniscalco.

## Introduction
The libsais provides simple C99 API to construct suffix array and Burrows-Wheeler transformed string from a given string over constant-size alphabet. The algorithm runs in a linear time using typically only ~16KB of extra memory (with 2n bytes as absolute worst-case; where n is the length of the string). OpenMP acceleration uses 200KB of addition memory per thread.

> * The libsais works with compilers from GNU, Microsoft and Intel, but I recommend Clang for best performance.
> * The libsais is sensitive to fast memory and software prefetching and might not be suitable for some workloads. Please benchmark yourself.

## License
The libsais is released under the [Apache License Version 2.0](LICENSE "Apache license")

## Changes
* April 21, 2023 (2.7.3)
  * CMake script for library build and integration with other projects.
* April 18, 2023 (2.7.2)
  * Fixed out-of-bound memory access issue for large inputs (libsais64).
* June 19, 2022 (2.7.1)
  * Improved cache coherence for ARMv8 architecture.
* April 12, 2022 (2.7.0)
  * Support for longest common prefix array (LCP) construction.
* January 1, 2022 (2.6.5)
  * Exposed functions to construct suffix array of a given integer array.
  * Improved detection of various compiler intrinsics.
  * Capped free space parameter to avoid crashing due to 32-bit integer overflow.
* October 21, 2021 (2.6.0)
  * libsais16 for 16-bit inputs.
* October 15, 2021 (2.5.0)
  * Support for optional symbol frequency tables.
* July 14, 2021 (2.4.0)
  * Reverse Burrows-Wheeler transform.
* June 23, 2021 (2.3.0)
  * Burrows-Wheeler transform with auxiliary indexes.
* April 27, 2021 (2.2.0)
  * libsais64 for inputs larger than 2GB.
* April 19, 2021 (2.1.0)
  * Additional OpenMP acceleration.
* April 4, 2021 (2.0.0)
  * OpenMP acceleration. 
* February 23, 2021 (1.0.0)
  * Initial release.

## Versions of the libsais library
* [libsais.c](src/libsais.c) (and corresponding [libsais.h](include/libsais.h)) is for suffix array, PLCP, LCP, forward BWT and reverse BWT construction over 8-bit inputs smaller than 2GB (2147483648 bytes).
  * This version of the library could also be used to construct suffix array of an integer array (with a caveat that input array must be mutable).
* [libsais64.c](src/libsais64.c) (and corresponding [libsais64.h](include/libsais64.h)) is optional extension of the library for inputs larger or equlas to 2GB (2147483648 bytes).
* [libsais16.c](src/libsais16.c) (and corresponding [libsais16.h](include/libsais16.h)) is independent version of the library for 16-bit inputs.

## Examples of APIs (see [libsais.h](include/libsais.h), [libsais16.h](include/libsais16.h) and [libsais64.h](include/libsais64.h) for complete APIs list)
```c
    /**
    * Constructs the suffix array of a given string.
    * @param T [0..n-1] The input string.
    * @param SA [0..n-1+fs] The output array of suffixes.
    * @param n The length of the given string.
    * @param fs Extra space available at the end of SA array (0 should be enough for most cases).
    * @param freq [0..255] The output symbol frequency table (can be NULL).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    int32_t libsais(const uint8_t * T, int32_t * SA, int32_t n, int32_t fs, int32_t * freq);

    /**
    * Constructs the suffix array of a given integer array.
    * Note, during construction input array will be modified, but restored at the end if no errors occurred.
    * @param T [0..n-1] The input integer array.
    * @param SA [0..n-1+fs] The output array of suffixes.
    * @param n The length of the integer array.
    * @param k The alphabet size of the input integer array.
    * @param fs Extra space available at the end of SA array (can be 0, but 4k or better 6k is recommended for optimal performance).
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    int32_t libsais_int(int32_t * T, int32_t * SA, int32_t n, int32_t k, int32_t fs);

    /**
    * Constructs the burrows-wheeler transformed string of a given string.
    * @param T [0..n-1] The input string.
    * @param U [0..n-1] The output string (can be T).
    * @param A [0..n-1+fs] The temporary array.
    * @param n The length of the given string.
    * @param fs Extra space available at the end of A array (0 should be enough for most cases).
    * @param freq [0..255] The output symbol frequency table (can be NULL).
    * @return The primary index if no error occurred, -1 or -2 otherwise.
    */
    int32_t libsais_bwt(const uint8_t * T, uint8_t * U, int32_t * A, int32_t n, int32_t fs, int32_t * freq);

    /**
    * Constructs the original string from a given burrows-wheeler transformed string with primary index.
    * @param T [0..n-1] The input string.
    * @param U [0..n-1] The output string (can be T).
    * @param A [0..n] The temporary array (NOTE, temporary array must be n + 1 size).
    * @param n The length of the given string.
    * @param freq [0..255] The input symbol frequency table (can be NULL).                	
    * @param i The primary index.
    * @return 0 if no error occurred, -1 or -2 otherwise.
    */
    int32_t libsais_unbwt(const uint8_t * T, uint8_t * U, int32_t * A, int32_t n, const int32_t * freq, int32_t i);
```

## Example installation using [CPM](https://github.com/cpm-cmake/CPM.cmake)
```cmake
CPMAddPackage(
  NAME libsais
  GITHUB_REPOSITORY IlyaGrebnov/libsais
  GIT_TAG v2.7.3
  OPTIONS
    "LIBSAIS_USE_OPENMP OFF"
    "LIBSAIS_BUILD_SHARED_LIB OFF"
)

target_link_libraries(<your target> libsais)
```

---

# Benchmarks

Full list of benchmarks are moved to own [Benchmarks.md](Benchmarks.md) file.

## Large pages and multi-core systems support

Large-pages and OpenMP improves the libsais performance. Here is an example of such improvements on Manzini Corpus.

| file            |      size | baseline| LP      | LP w 2c | LP w 3c | LP w 4c | LP w 5c | LP w 6c | LP w 7c | LP w 8c |
|:---------------:|:---------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|:-------:|
|       chr22.dna |  34553758 |43.50MB/s|50.18MB/s|61.20MB/s|73.66MB/s|78.91MB/s|81.20MB/s|81.49MB/s|81.52MB/s|80.42MB/s|
|         etext99 | 105277340 |32.96MB/s|40.73MB/s|50.19MB/s|59.34MB/s|62.97MB/s|64.06MB/s|62.83MB/s|63.08MB/s|62.49MB/s|
|     gcc-3.0.tar |  86630400 |44.32MB/s|50.13MB/s|58.51MB/s|68.85MB/s|73.82MB/s|75.76MB/s|76.14MB/s|75.85MB/s|75.24MB/s|
|           howto |  39422105 |42.78MB/s|48.10MB/s|57.38MB/s|67.75MB/s|71.91MB/s|73.67MB/s|73.61MB/s|73.17MB/s|72.38MB/s|
|          jdk13c |  69728899 |42.70MB/s|47.77MB/s|54.50MB/s|64.85MB/s|69.63MB/s|71.66MB/s|72.15MB/s|71.96MB/s|71.24MB/s|
| linux-2.4.5.tar | 116254720 |42.46MB/s|48.85MB/s|57.60MB/s|67.92MB/s|72.29MB/s|73.88MB/s|74.11MB/s|73.59MB/s|73.27MB/s|
|        rctail96 | 114711151 |36.39MB/s|43.19MB/s|50.96MB/s|60.60MB/s|64.33MB/s|65.43MB/s|65.79MB/s|65.78MB/s|65.18MB/s|
|             rfc | 116421901 |39.81MB/s|46.76MB/s|55.92MB/s|66.48MB/s|70.79MB/s|71.68MB/s|72.21MB/s|71.92MB/s|71.06MB/s|
|     sprot34.dat | 109617186 |36.09MB/s|45.06MB/s|53.26MB/s|61.60MB/s|59.69MB/s|62.25MB/s|67.20MB/s|66.84MB/s|66.38MB/s|
|            w3c2 | 104201579 |42.97MB/s|47.09MB/s|54.01MB/s|63.79MB/s|67.67MB/s|69.84MB/s|69.94MB/s|69.65MB/s|68.86MB/s|

Note, multi-core scalability is limited by RAM bandwidth and adding more RAM channels improves performance:
![enwik9 BWT throughput in MB/s on Azure DS14 v2 (Intel Xeon Platinum 8171M)](Azure_enwik9_benchmark.png?raw=true "enwik9 BWT throughput in MB/s on Azure DS14 v2 (Intel Xeon Platinum 8171M)")

## libsais64 for inputs larger than 2GB

Starting from version 2.2.0 libsais64 could process inputs larger than 2GB.

The times below are the minimum of five runs measuring **multi-threaded (MT)** performance of suffix array construction on Azure DS14 v2 (Intel Xeon Platinum 8171M).

|  file           |    size     |    libsais64 2.2.0  (MT)   |   divsufsort64 2.0.2 (MT)  |speedup (MT)|
|:---------------:|:-----------:|:--------------------------:|:--------------------------:|:----------:|
|         english |  2210395553 |  61.499 sec (  34.28 MB/s) | 435.199 sec (   4.84 MB/s) |**+607.65%**|
|   GRCh38.p13.fa |  3321586957 |  84.068 sec (  37.68 MB/s) | 782.938 sec (   4.05 MB/s) |**+831.32%**|
|         enwik10 | 10000000000 | 303.542 sec (  31.42 MB/s) |1927.351 sec (   4.95 MB/s) |**+534.95%**|

## Additional memory

The libsais reuses space allocated for suffix array during construction. Sometimes this free space is not sufficient for most optimal algorithm (this is uncommon) and libsais will need to fallback to less efficient one (libsais has 4 algorithms at different break-points point: 6k, 4k, 2k and 1k; where k is alphabet size). To improve performance for those cases you could allocating additional space at the end of suffix array.

|  file           |    size     |     libsais + O(n)  (ST)   |     libsais + O(1) (ST)    |speedup (ST)|    libsais + O(n)  (MT)    |     libsais + O(1) (ST)    |speedup (MT)|
|:---------------:|:-----------:|:--------------------------:|:--------------------------:|:----------:|:--------------------------:|:--------------------------:|:----------:|
|            osdb |    10085684 |   0.222 sec (  45.52 MB/s) |   0.228 sec (  44.20 MB/s) |  **+2.97%**|   0.150 sec (  67.30 MB/s) |   0.162 sec (  62.25 MB/s) |  **+8.11%**|
|           x-ray |     8474240 |   0.190 sec (  44.52 MB/s) |   0.217 sec (  39.11 MB/s) | **+13.82%**|   0.122 sec (  69.46 MB/s) |   0.156 sec (  54.16 MB/s) | **+28.25%**|
|             sao |     7251944 |   0.175 sec (  41.48 MB/s) |   0.182 sec (  39.75 MB/s) |  **+4.37%**|   0.127 sec (  57.26 MB/s) |   0.140 sec (  51.87 MB/s) | **+10.39%**|
|         ooffice |     6152192 |   0.113 sec (  54.55 MB/s) |   0.117 sec (  52.45 MB/s) |  **+4.01%**|   0.081 sec (  76.38 MB/s) |   0.088 sec (  70.30 MB/s) |  **+8.65%**|
|            abac |      200000 |   0.002 sec (  84.36 MB/s) |   0.003 sec (  73.63 MB/s) | **+14.56%**|   0.002 sec ( 105.08 MB/s) |   0.002 sec (  86.64 MB/s) | **+21.27%**|
|           test3 |     2097088 |   0.034 sec (  61.54 MB/s) |   0.037 sec (  56.45 MB/s) |  **+9.03%**|   0.028 sec (  75.76 MB/s) |   0.032 sec (  64.93 MB/s) | **+16.68%**|

> * All other files from [Benchmarks](#benchmarks) above do not suffer from this fallbacks.