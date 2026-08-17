# tusdview GPU texture benchmark

This is a standalone, headless texture-processing benchmark. It does not link
the tusdview renderer and is safe to build while renderer work is in progress.

```sh
cmake -S . -B build_ninja -G Ninja \
  -DTINYUSDZ_BUILD_TEXTURE_GPU_BENCH=ON \
  -DTINYUSDZ_WITH_TEXTOOLS=ON
cmake --build build_ninja --target tusdview_texture_gpu_bench

build_ninja/tusdview_texture_gpu_bench \
  --backend vulkan --root /mnt/disk1/data/alab \
  --report alab-vulkan.json
```

The input corpus is external and must be supplied at runtime. A single image
file may also be passed as `--root` for bounded probes. Missing hardware,
drivers, runtimes, or corpus data return CTest-style skip code `77`. LDR and
EXR/HDR images are accepted. HDR images are Reinhard-tonemapped to RGBA8 for
BC1/BC3/BC5/BC7 and ASTC; BC6H preserves the HDR float data. Vulkan measures
resize plus BC1/BC3/BC5/BC7 and has a fast unsigned BC6H mode-11 compute
encoder. The BC6H prototype uses nearest source sampling and a single
endpoint-line search; the vendored CPU encoder remains the higher-quality
reference. ASTC uses a Vulkan 4x3-weight interpolated block path with a
deliberately small endpoint/weight search; it favors throughput over the
quality of the vendored CPU encoder.

Use `--synthetic --allow-software` for a small smoke run on llvmpipe. `--filter
bilinear` selects the fast filter; Mitchell is the default and matches the
viewer’s general-purpose CPU filter semantics more closely. The HIP adapter
uses native ROCm kernels for resize and BC1/BC3/BC5/BC7 compression. It selects
the first HIP device and supports BC6H mode-11 HDR encoding; use
`--backend hip` to benchmark it. Set `LD_LIBRARY_PATH` to the ROCm library
directory when the HIP runtime is not on the system loader path. The CUDA
adapter uses native CUDA kernels for resize and BC1/BC3/BC5/BC7 compression.
Its default architecture is `120` for Blackwell-class GPUs and can be changed
with `-DTUSDVIEW_TEXTURE_CUDA_ARCHITECTURES=...`; select it with
`--backend cuda`. CUDA BC6H remains an explicit skip for now.

Use `--mips N` to benchmark and validate N output mip levels; `--mips 0`
generates the complete chain down to 1x1. LDR and HIP BC6H HDR levels are chained from the
previous GPU-generated level. Each level is reported separately (`bc7@mip0`,
`bc7@mip1`, …) and GPU time is averaged over the requested iterations.
