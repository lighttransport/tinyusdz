# AddressSanitizer Notes

This page documents sanitizer build policy and the known i386 AddressSanitizer
limit for `tusdcat`.

## Configure ASan

Use `SANITIZE_ADDRESS` when configuring CMake:

```sh
cmake -S . -B build_asan -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSANITIZE_ADDRESS=1 \
  -DTINYUSDZ_BUILD_TESTS=ON \
  -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build_asan
ctest --test-dir build_asan --output-on-failure
```

For the 32-bit Linux diagnostic build used to reproduce the `tusdcat` issue:

```sh
cmake -S . -B build_clang21_m32_asan -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=cmake/linux_i386.toolchain.cmake \
  -DCMAKE_C_COMPILER=/usr/bin/clang-21 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-21 \
  -DCMAKE_BUILD_TYPE=Debug \
  -DSANITIZE_ADDRESS=1 \
  -DTINYUSDZ_BUILD_TESTS=ON \
  -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build_clang21_m32_asan --target tusdcat unit-test-tinyusdz
```

## Known i386 `tusdcat` Limit

`tusdcat` is too large for the i386 ASan runtime/allocator address-space layout.
With clang-21, a 32-bit ASan build of the full `tusdcat` binary aborts at
startup or first output, before it can do useful validation work:

```text
ERROR: AddressSanitizer: out of memory: failed to allocate 0x110000
bytes of InternalMmapVector
ERROR: Failed to mmap
```

This is not a single large TinyUSDZ allocation, and it is not specific to GCC
libasan. The same failure reproduces with clang-21/compiler-rt ASan. Debugging
showed the primary allocation failure was a normal 4 KiB libc stdout buffer
allocation reached from `print_help()`. The larger `InternalMmapVector` failure
is a secondary failure while ASan is trying to report the original allocator
OOM.

The failure is caused by the ASan runtime needing large shadow, metadata,
quarantine, fake-stack, and allocator regions inside the small 32-bit virtual
address space, while the full `tusdcat` executable also carries a large text,
data, and bss footprint. In the reproduced clang-21 build, the binary was an
ELF32 executable with roughly:

| Section | Size |
| --- | ---: |
| `.text` | 43 MB |
| `.data` | 474 KB |
| `.bss` | 9 MB |
| total | 53 MB |

ASan startup mapped large low/high shadow regions, then normal allocator mmap
requests failed with `ENOMEM`.

## Mitigations Tested

These options did not make full i386 `tusdcat` ASan validation reliable:

- `ASAN_OPTIONS=quarantine_size_mb=0`
- `ASAN_OPTIONS=detect_leaks=0`
- `ASAN_OPTIONS=malloc_context_size=5`
- `ASAN_OPTIONS=detect_stack_use_after_return=0`
- disabling ASan global instrumentation with `-mllvm -asan-globals=0`
- building non-PIE with `-fno-pie` and `-no-pie`
- `setarch i386` / `linux32` address-layout variants

`ASAN_OPTIONS=allocator_may_return_null=1` can sometimes let trivial `--help`
output continue, but it only converts the ASan allocator abort into null-return
behavior. It does not make real `tusdcat` parse or validation paths reliable and
should not be used as a CI fix.

## Repo Policy

The supported coverage split is:

- Use full ASan `tusdcat` validation on 64-bit builds.
- Use 32-bit non-ASan builds for i386 tool coverage.
- Use 32-bit ASan builds for smaller tests such as `unit-test-tinyusdz` and
  parser-focused executables.

`CMakeLists.txt` skips the `tusdcat` end-to-end validation tests when both
conditions are true:

- `CMAKE_SIZEOF_VOID_P EQUAL 4`
- `SANITIZE_ADDRESS` is enabled

The validation logic is still exercised on non-ASan 32-bit builds and on 64-bit
ASan builds. If i386 ASan validation coverage is required, add a smaller
dedicated validation executable that links only the parser and validation code
needed for the target fixture set, instead of using full `tusdcat`.

Possible size-reduction flags such as `-O1`,
`-fsanitize-address-use-after-return=never`, or
`-fsanitize-address-outline-instrumentation` may be useful for diagnostics, but
they should be treated as experimental until the resulting binary is verified
with the actual i386 ASan validation workload.
