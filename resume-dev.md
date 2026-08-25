# Resume: completed headless MaterialX/OpenPBR parity

Work in this repository and follow `AGENTS.md`. Continue from the existing
worktree. Do not rewrite published history. Do not touch or add the unrelated
untracked `run.sh` or `usd-assets` paths.

## Repository state

- Branch: `dev`
- Public remote: `https://github.com/lighttransport/tinyusdz.git`
- Public tip: `origin/dev` at `e14812b52` (`Bound semantic renderer regression time`)
- Local tip: `9e9ae2464` (`Complete OpenPBR parity across GPU backends`)
- Local branch is two linear commits ahead of `origin/dev`:
  - `eafbe798e Expand headless MaterialX renderer parity`
  - `9e9ae2464 Complete OpenPBR parity across GPU backends`
- Expected untracked paths: `resume-dev.md`, `run.sh`, and `usd-assets`.
  `resume-dev.md` is this handoff document; the other two remain untouched.
- No push has been performed for the two local commits.

## Completed renderer work

- Expanded headless `tusdrender` MaterialX/OpenPBR coverage for base, coat,
  transmission, subsurface, emission, opacity, texture/UDIM variants, normal
  and coat-normal maps, UV routing, colorspace, executable graphs, and
  displacement response.
- Shared the exact generated semantic fixtures with `tusdview` instead of
  maintaining duplicate USDA and texture generators.
- Fixed coat-normal propagation through the next loader, resolved materials,
  and CUDA/HIP shared renderer sampling data.
- Fixed displacement propagation for CUDA/HIP and baked displaced geometry
  before Vulkan's indexed hardware-RT acceleration-structure build.
- Added `tusdrender -vkDevice N` / `--vk-device N` for explicit Vulkan physical
  device selection.
- Added strict validation controls:
  - `TUSDR_PARITY_REQUIRE_BACKENDS=vkr,cuda,hip`
  - `TUSDR_PARITY_REQUIRE_HARDWARE=1`
  - `TUSDR_PARITY_VULKAN_DEVICES=0:NVIDIA,1:AMD`
- Strict runs reject software fallback and proved CUDA on NVIDIA, HIP on AMD,
  and Vulkan ray query on NVIDIA and AMD physical devices.
- Made NVIDIA OpenGL/Xvfb regression deterministic with a build-local driver
  shader cache and an ordered CTest warm-up fixture. A cold full shader compile
  takes about 65 seconds; cached launches take about one second.

## Latest verification

- `cmake --build build_ninja -j16`: passed.
- Strict expanded `tusdrender` OpenPBR hardware matrix: passed in about 61–65
  seconds across NVIDIA/AMD Vulkan RT, CUDA, and HIP.
- Focused formerly failing GL cases all execute and pass:
  - raster multilight: 3.21 s
  - GL semantic AOV: 8.25 s
  - GL/Vulkan parity: 8.43 s
  - raster shadow map: 8.33 s
  - raster carrier shadow: 5.51 s
  - alpha/instancer shadow: 0.85 s
- Full native regression using documented NVIDIA offload and shared Xvfb
  passed all 291 registered tests in 274.93 seconds (4m 35s):

  ```sh
  xvfb-run -a -s '-screen 0 1280x800x24' \
    ctest --test-dir build_ninja --output-on-failure -j4
  ```

  Optional external-asset, corpus, validation-layer, and real-texture tests
  reported expected capability/fixture skips; there were zero failures.
- Standalone stable-next regression passed 39/39 in 18.86 seconds, with one
  expected AOUSD reference-data capability skip:

  ```sh
  cmake -S src/next -B build-next-ninja -G Ninja \
    -DTINYUSDZ_NEXT_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
  cmake --build build-next-ninja -j16
  ctest --test-dir build-next-ninja --output-on-failure
  ```

- Completion hygiene passed: `git diff --check`, Bash syntax checks, Python
  bytecode compilation, and scans for personal paths, credential-shaped text,
  `.wasm`, PDFs, and build artifacts.

## Remaining work

The requested implementation scope is complete. Only release mechanics remain:

1. Fetch `origin` and perform the mandatory `AGENTS.md` pre-push audit over the
   exact `origin/dev..HEAD` range, covering both local commits.
2. Confirm the remote has not moved and the update is a regular fast-forward.
3. Summarize the audit and ask the user for fresh push authorization.
4. Push `dev` only after that new authorization. Do not force-push.

Do not commit this handoff unless the user explicitly asks to include it.
