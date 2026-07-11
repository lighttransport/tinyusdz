# textools Vendor Import (pristine snapshot)

Upstream: tinyexr `release` branch, `tools/` subtree
(local worktree: `/mnt/nvme02/work/tinyexr-tocio`,
https://github.com/syoyo/tinyexr).

Four pure-C11 texture libraries imported as a **pristine snapshot** (not a
fork, not a submodule). Initial import at upstream commit
`8b89eea948b221321df19773968d38735a611257` on 2026-07-05.

Re-synced 2026-07-11 from upstream branch `texcomp-ktx2-reader` commit
`14a2c9d` (adds the KTX2 reader / transcode-on-load API, the previously
missing `texcomp/src/texcomp_astc_decode.c`, KTX2 Zstd-supercompression read via
`tp_ktx2_read_zstd` + a host decompressor callback, and the PR #258 hardening of
the KTX2 parser against crafted headers — unsigned range checks on
pixelWidth/Height, levelCount/layerCount/faceCount, an in-bounds DFD check, and
64-bit `w*h*4` in the decoders. See "KTX2 reader" below).
Earlier imports: `b113e0e`, `1d6ad64`.

Keep it pristine: prefer fixing bugs upstream and re-syncing over patching
here. If a local patch becomes unavoidable, list it under "tinyusdz-local
patches" below.

## What the libraries are

- `resize/` — **tir**: content-aware separable image resizer (U8/U16/F16/F32,
  1–4 interleaved channels; box…lanczos3/kaiser filters; clamp/reflect/wrap
  edges; premultiplied-alpha filtering; normal-map/heightmap content modes;
  runtime x86 SIMD dispatch, NEON, opt-in SVE and threads). Header:
  `resize/include/tir.h`.
- `texcomp/` — GPU block-texture compression: BC1/3/5/6H/7, ETC2/EAC,
  ASTC LDR+HDR, `uni` transcodable intermediate, DDS/KTX memory writers.
  Header: `texcomp/include/texcomp.h`.
- `texpipe/` — mip/orchestration layer on tir + texcomp: sRGB-aware and
  alpha-coverage-preserving mip chains, normal-map mips + Toksvig roughness,
  dilate/gutter, cube/octa seam fixup, per-level compression, KTX2 array
  writer. Header: `texpipe/include/texpipe.h` (includes `tir.h`+`texcomp.h`).
  **KTX2 reader** (`tp_ktx2_read`, `tp_ktx2_decode_level_rgba8`,
  `tp_ktx2_write_uni`): the consumer side of the writers — parse a KTX2, map its
  VkFormat to a codec (or detect the Basis-free `uni` UASTC intermediate at
  `vkFormat==0`), then decode-to-RGBA8 (uni / BC7 / ASTC LDR) or hand the blocks
  to `texcomp`'s `tc_uni_transcode_*` for per-device transcoding.
  supercompressionScheme 0 only (Zstd/BasisLZ deferred — no zstd in this vendor).
- `envmap/` — IBL: equirect/cube/octahedral projections + conversion,
  GGX prefiltered specular cube mip chain, diffuse irradiance cube,
  split-sum BRDF LUT, spherical harmonics/gaussians. Header:
  `envmap/include/envmap.h` (includes `tir.h` for the allocator only).

Dependency graph: tir and texcomp are leaves; texpipe needs both; the envmap
library needs tir only. No dependency on tinyexr itself, zstd, or astcenc in
the imported library sources; no `<stdio.h>` in library code (upstream
enforces this).

## Imported / skipped files

Imported per tool: `include/*.h`, `src/*.c` (incl.
`texcomp/src/texcomp_astc_decode.c`, the ASTC LDR decoder — added in the
2026-07-11 re-sync) + internal headers/`.inc`,
license files, and the self-contained plain-`main()` test programs
(`resize/tests/{tir_test,tir_f16_test}.c`, `texcomp/test/test_texcomp.c` +
`*_ref_decode.h`, `texpipe/test/test_texpipe.c`,
`envmap/test/{test_envmap,test_pbr}.c`).

Skipped: all CLIs (`tir_resize_main.c`, `texcomp_cli.c`, `texpipe_cli.c`,
`envmap_cli.c` — they include tinyexr's `exr.h` and zstd), `bench/`, fuzzers,
test corpora, the optional astcenc C++ ASTC backend, and the zstd-based
xbc7/uni-KTX2 supercompression paths (CLI-only upstream).

## Build (tinyusdz-local; upstream has no CMake)

Gated by `TINYUSDZ_WITH_TEXTOOLS` (top-level `CMakeLists.txt`), which builds
the `tinyusdz_textools` static library from all four `src/` dirs with the
four `include/` dirs public. Notes:

- Only `resize/src/tir_kernels_sve.c` needs `-march=armv8-a+sve`
  (aarch64 + compiler-flag check; runtime HWCAP-gated, stub otherwise).
  All other x86/NEON dispatch is runtime/`__attribute__((target))`-based and
  needs no arch flags; on MSVC the target attributes compile out and
  intrinsics build unconditionally.
- Threads: `TIR_ENABLE_THREADS` + `TIR_THREADS_PTHREAD` are defined on UNIX;
  serial elsewhere.
- Consumers: `tusdview` (define `TUSDVIEW_WITH_TEXTOOLS`) via
  `examples/tusdview/texture_tools.{hh,cc}`; ctest targets `textools-*`.

## tinyusdz-local patches (must survive a re-sync)

None.

## Re-sync procedure

1. In the upstream worktree, note the new commit hash.
2. Re-copy the imported file list above (`rsync`/`cp` from
   `<upstream>/tools/{resize,texcomp,texpipe,envmap}`), excluding the skipped
   files.
3. Review `git diff`; re-apply any local patches listed above.
4. Rebuild and test:
   `cmake --build build_ninja --target tinyusdz_textools && ctest --test-dir build_ninja -R 'textools-' --output-on-failure`
   then `ctest --test-dir build_ninja -R 'tusdview-' --output-on-failure`.
5. Update the import commit + date at the top of this file.

## Licenses

- `resize/` (tir): **BSD-3-Clause** (SPDX tags in sources; `resize/LICENSE`
  is a copy of the tinyexr root LICENSE — upstream keeps no per-subdir
  license file for `tools/resize`).
- `texcomp/`, `texpipe/`, `envmap/`: **Apache-2.0** (per-subdir `LICENSE`;
  `texcomp/NOTICE.md` carries third-party attributions — astcenc port
  credits, bcdec/Basis Universal/QuickBC7 concept credits — and must be
  preserved).
