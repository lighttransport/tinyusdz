# MMap-Based USD Loading

This note describes TinyUSDZ's mmap-based file loading path and the optional
USDC zero-copy array mode used by Tydra.

## Summary

TinyUSDZ can load USD files through OS-backed mmap when mmap support is
available. This avoids first copying the whole file into a heap buffer. For
USDC payloads, `USDLoadOptions::mmap_zero_copy` can additionally defer selected
large uncompressed arrays and let Tydra read them from the mapped file when it
builds render data.

The normal file-loading APIs are:

```cpp
tinyusdz::Stage stage;
std::string warn;
std::string err;

tinyusdz::USDLoadOptions options;
options.mmap_zero_copy = true;

bool ok = tinyusdz::LoadUSDFromFile("scene.usdz", &stage, &warn, &err, options);
if (!ok) {
  // inspect err
}
```

The same option works with `LoadUSDCFromFile` and `LoadUSDZFromFile`.
`LoadUSDFromFile` auto-detects USDA, USDC, and USDZ.

## Ownership And Lifetime

For file-based loads, TinyUSDZ owns the backing storage after a successful load
when zero-copy arrays were deferred:

- mmap path: the OS mapping is adopted by `Stage` and unmapped when the `Stage`
  is destroyed, assigned over, or `Stage::clear_mmap_data()` is called.
- non-mmap fallback path: the whole-file byte buffer is moved into `Stage` and
  kept alive for the same lifetime.

This means Tydra can safely access deferred array bytes while converting from
the loaded `Stage`.

For memory-based loads, TinyUSDZ does not own the caller's input buffer:

```cpp
std::vector<uint8_t> bytes = read_file_somewhere();

tinyusdz::Stage stage;
tinyusdz::USDLoadOptions options;
options.mmap_zero_copy = true;

bool ok = tinyusdz::LoadUSDCFromMemory(
    bytes.data(), bytes.size(), "scene.usdc", &stage, &warn, &err, options);

// bytes must remain alive while stage may use zero-copy array refs.
```

Use file-based loaders when you want `Stage` to manage the backing lifetime.
If you use `LoadUSDFromMemory`, `LoadUSDCFromMemory`, or `LoadUSDZFromMemory`
with `mmap_zero_copy = true`, keep the original input bytes alive until any
Tydra conversion or direct zero-copy access is finished.

Copying a `Stage` does not copy mmap zero-copy state. Moving a `Stage` transfers
that state.

## What Gets Deferred

Only USDC arrays are eligible. USDA is text and is fully parsed into the Stage.
USDZ is a ZIP container; if the selected root asset is USDC, that embedded USDC
can use the same deferred-array path.

Eligible arrays are large, uncompressed numeric arrays such as:

- `float`, `double`, `half`
- `float2/3/4`, `double2/3/4`, `half2/3/4`
- `matrix2d`, `matrix3d`, `matrix4d`

Small arrays and compressed integer arrays are materialized normally. Time
samples are currently materialized normally; the zero-copy path covers default
values.

## Tydra Behavior

Tydra checks `Stage::has_mmap_zero_copy()` during render-scene conversion. For
supported mesh attributes such as points, normals, and texture coordinates, it
validates the recorded byte range and alignment through `MMapDataSource`, then
copies the data into render buffers. If validation fails or the attribute is not
eligible, Tydra falls back to the normal Stage attribute evaluation path.

Keep the source `Stage` alive until Tydra has finished any work that may read
from deferred arrays.

## Zstd-Compressed USD

File-level zstd-compressed USD is decompressed into a temporary buffer before
format dispatch. The zero-copy option is disabled for that decompressed recursive
load, because the temporary buffer cannot safely back a long-lived `Stage`.

## Security Notes

MMap zero-copy still validates bounds before returning typed pointers:

- byte offset and element count arithmetic is overflow-checked,
- requested byte ranges must lie inside the backing source,
- element size and pointer alignment must match the requested C++ type.

Use `USDLoadOptions::max_memory_limit_in_mb` for untrusted input. The memory
limit still applies during parsing and to materialized data; deferred array bytes
remain in the mapped file or Stage-owned backing buffer instead of being copied
into Stage arrays.
