# Running ThreadSanitizer (TSan)

How to build and run the TSan configuration, plus the two environment traps
that make TSan fail *before your code even runs* — both produce cryptic
startup errors that look like bugs in tinyusdz but are not.

## Build

```bash
cmake -B build_tsan -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DCMAKE_CXX_FLAGS=-fsanitize=thread \
      -DCMAKE_C_FLAGS=-fsanitize=thread \
      -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread \
      -DTINYUSDZ_ENABLE_THREAD=ON \
      -DTINYUSDZ_BUILD_EXAMPLES=ON -DTINYUSDZ_WITH_TYDRA=ON
cmake --build build_tsan -j
```

Notes:

- `TINYUSDZ_ENABLE_THREAD=ON` is required — it enables the parallel code
  paths TSan is supposed to check (parallel USDC ReconstructStage, parallel
  stage→specs crate conversion, parallel LayerToStage, parallel tydra mesh /
  animation conversion).
- Do NOT combine with `TINYUSDZ_WITH_TCMALLOC` (jemalloc/tcmalloc link):
  sanitizers must own the allocator.
- `src/imageproc/simd.cc` self-disables its `target_clones`
  multi-versioning under TSan — ifunc resolvers run before the TSan runtime
  is initialized and crash otherwise. Nothing to do, just don't "fix" the
  missing MV dispatch under TSan.
- The `SpinMutex` used for diagnostics merging calls `sched_yield()` under
  TSan (see `src/spin-mutex.hh`); without it, contended spins livelock for
  minutes under the TSan scheduler.

## Trap 1: kernel ASLR entropy (`vm.mmap_rnd_bits`)

**Symptom** — every TSan binary, even a trivial one, dies at startup:

```
FATAL: ThreadSanitizer: unexpected memory mapping 0x5a77a7a67000-...
```

or

```
==PID==ERROR: ThreadSanitizer: out of memory: allocator is trying to allocate 0x7b bytes
==PID==ERROR: ThreadSanitizer: out of memory: failed to allocate 0x1000 (4096) bytes of InternalMmapVector (error code: 12)
ERROR: Failed to mmap
```

**Cause** — Ubuntu kernels ≥ 6.5 (observed on 6.8.0) default to
`vm.mmap_rnd_bits = 32`. The TSan runtime's fixed shadow-memory layout only
tolerates up to 28 bits of mmap randomization; with 32 bits, PIE images and
mappings land outside the ranges the runtime expects.

**Fix** (root required):

```bash
sudo sysctl vm.mmap_rnd_bits=28
# persist:
echo 'vm.mmap_rnd_bits=28' | sudo tee /etc/sysctl.d/99-tsan.conf
```

**Verify** with a trivial program before blaming the real target:

```bash
printf '#include <thread>\nint x; int main(){ std::thread t([]{x=1;}); t.join(); return x; }\n' > /tmp/t.cc
g++ -fsanitize=thread /tmp/t.cc -o /tmp/t && /tmp/t; echo "rc=$? (1 = OK, it returns x)"
```

`setarch $(uname -m) -R <cmd>` (disable ASLR for one run) is a per-run
workaround, but on some kernel/toolchain combinations it is not sufficient —
prefer the sysctl.

## Trap 2: `RLIMIT_AS` caps (self-imposed or inherited)

**Symptom** — the trivial program works, but a specific tool (historically
`tusdcat`) still dies with the same
`out of memory: failed to allocate ... InternalMmapVector` error, while other
binaries from the same build (e.g. `tydra_to_renderscene`) start fine.

**Cause** — TSan reserves tens of **terabytes** of virtual address space at
startup (shadow memory, internal allocators — reservations, not committed
RAM). Any `RLIMIT_AS` cap kills it. `tusdcat` sets a 32 GB `RLIMIT_AS` in
`main()` as an OOM/thrash guard; that guard is now compiled out under
`__SANITIZE_THREAD__` / `__SANITIZE_ADDRESS__` (see
`examples/tusdcat/main.cc`). If you add an address-space cap to another tool,
gate it the same way. The same applies to caps inherited from the
environment (`ulimit -v`, container limits).

## Running

```bash
# large scenes are slow under TSan (10-20x); start with mid-size models
./build_tsan/tusdcat -f model.usdz -o /tmp/out.usdc
echo $?   # 66 = TSan reported an error; grep the output for "WARNING: ThreadSanitizer"
```

- TSan exit code 66 means a race (or runtime error) was reported even if the
  tool otherwise completed.
- Byte-compare the output against a non-TSan build's output — a data race
  that corrupts data often shows up as a byte diff or run-to-run
  nondeterminism even when the race report itself is unclear.
- Complement TSan with a determinism check on the normal thread build
  (10 runs, compare hashes) — it has caught reordering bugs TSan cannot see
  (TSan checks synchronization, not output ordering).

## History

- 2026-06: parallel ReconstructStage TSan pass found real bugs (shared
  `GetPrimMeta` static clobber, crate-reader cursor race — see git log
  9550a9fc2).
- 2026-07: parallel USDC crate-reader fieldset race fixed (TSan-confirmed,
  dropped whole subtrees — "Parallel-parse data-loss race" in project notes).
- 2026-07-06: both traps above hit in one session (kernel had
  `mmap_rnd_bits=32`; tusdcat's own 32 GB `RLIMIT_AS` masked as a TSan OOM).
