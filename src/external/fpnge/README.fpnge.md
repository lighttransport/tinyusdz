# fpnge

Vendored from [veluca93/fpnge](https://github.com/veluca93/fpnge) — a very fast
SIMD PNG encoder (originally by Luca Versari / Google).

Files: `fpnge.h`, `fpnge.cc`, `LICENSE` (Apache License 2.0).

## Usage in TinyUSDZ

fpnge is used as the default PNG encoder for the image writer
(`src/image-writer.cc`) when the library is built with
`-DTINYUSDZ_WITH_FPNGE=ON`. PNG encoding then prefers fpnge and falls back to
`fpng` (and then `stb_image_write`) for inputs/targets fpnge cannot handle.

## SIMD code paths

fpnge upstream requires **SSE4.1 minimum** (AVX2 preferred); there is no SSE2 or
scalar code path (`fpnge.cc` emits `#error Requires SSE4.1 support minimum`).
The CMake option `TINYUSDZ_FPNGE_SIMD` selects the build behavior:

| value    | behavior |
|----------|----------|
| `avx2`   | compile fpnge with `-mavx2 -mpclmul` (default) |
| `sse41`  | compile fpnge with `-msse4.1 -mpclmul` |
| `sse2`   | do **not** compile fpnge; PNG encoding falls back to `fpng` |
| `scalar` | do **not** compile fpnge; PNG encoding falls back to `fpng` |

On MSVC the `-m*` flags are not passed (`/arch:AVX2` is used for `avx2`); fpnge.cc
self-defines `__SSE4_1__`/`__PCLMUL__` on MSVC.

PCLMUL is used for hardware CRC32 when available, with a software lookup-table
fallback otherwise.
