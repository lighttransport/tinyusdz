# AGENTS.md

Guidance for AI coding agents (Claude Code, Copilot, Cursor, etc.) working in this repository.

## Project Overview

TinyUSDZ is a secure, portable, dependency-free C++17 library for parsing and writing USD (Universal Scene Description) files in USDA (ASCII), USDC (binary/Crate), and USDZ (zip archive) formats. Security-focused alternative to Pixar's pxrUSD with minimal dependencies. No C++ exceptions; error handling via `nonstd::expected`.

## Repository Layout

```
src/                       Core library sources (~250 .cc/.hh files)
  ascii-parser*.{hh,cc}      Hand-written USDA parser (split across
                             entry / props / basetype / timesamples /
                             timesamples-array translation units)
  crate-reader*.{hh,cc}      USDC binary (Crate) reader (split across
                             arrays, paths, timesamples, values)
  crate-writer.{hh,cc}       USDC binary writer (experimental but
                             increasingly fidelity-locked)
  crate-format.{hh,cc}       Crate binary layout, ValueRep, indices
  crate-dump.{hh,cc}         Low-level crate inspection (`tusdcat
                             --dump-crate-fields`)
  crate-path-utils/          Path encoding helpers shared by reader/
                             writer
  usda-reader.{hh,cc}        High-level USDA reading
  usdc-reader*.{hh,cc}       High-level USDC reading (also -prim,
                             -property)
  usda-writer.{hh,cc}        USDA writer (production)
  usdc-writer.{hh,cc}        USDC writer entry (delegates to
                             crate-writer)
  pprinter*.{hh,cc}          Pretty-printer (Stage/Prim -> USDA text);
                             pprint-meta / pprint-shader / pprint-enum
                             / pprint-detail
  stage-converter.cc         Stage <-> CrateData (LayerSpec) bridge
                             used by writer
  composition*.{hh,cc}       Composition arcs (references, payloads,
                             inherits, specializes, variants),
                             reconstruction, graph
  prim-reconstruct*          Per-schema field reconstruction tables
  ascii-parser-entry.cc      Registers prim/attr meta names
  tinyusdz.{hh,cc}           Main API (LoadUSDFromFile, etc.)
  stage.{hh,cc}              USD Stage (scene graph)
  prim-types.{hh,cc}         Primitive type definitions
  value-types.{hh,cc}        Value type system + DEFINE_TYPE_TRAIT
  attribute-eval*.cc         Animatable<T> attribute evaluation
                             (split across many TUs to keep
                             template-instantiation costs sane)
  usdGeom.{hh,cc}            Geometry prims (Mesh, Sphere, etc.)
  usdShade.{hh,cc}           Materials and shaders
  usdSkel.{hh,cc}            Skeletal animation
  usdLux.{hh,cc}             Lights
  usdPhysics.{hh,cc}         UsdPhysics (rigid bodies, joints,
                             colliders) — physics-2026 branch focus
  usdMtlx.{hh,cc}            MaterialX nodegraphs
  usdAR.{hh,cc}              UsdAR (anchor / image / face)
  usdFbx, usdMedia, usdObj   Adjacent format/asset schemas
  c-tinyusd.{h}              C API surface (stable, MIT-friendly)
  c-tinyusd-helpers.{h,cc}   C API impl + helpers (composition arc
                             authoring, variant content, attribute
                             setters, etc.)
  c-tinyusd-tydra.{h,cc}     C API for Tydra scene access
  python/module.c            CPython abi3 extension entry (tinyusdz
                             Python module — Stage / Prim / Attribute
                             / Value / RenderScene types). Backed by
                             the C API in c-tinyusd-helpers.
  core/                      Schema-agnostic primitives split out for
                             tighter dependency graphs:
    attribute.hh, attr-metas.hh, prim-metas.hh, metadata-base.hh,
    composition-types.hh, list-op.hh, variant-types.hh, prim.hh,
    prim-spec.hh, layer-types.hh, animatable.hh, instance-key.{hh,cc},
    extent.hh, collection-api.hh, …
  tydra/                     Tydra framework (USD -> render-ready)
    render-data.{hh,cc}        Convert Stage to OpenGL/Vulkan scene
    scene-access.{hh,cc}       Scene traversal and query APIs (also
                               provides the introspection used by the
                               C / Python APIs — GetProperty,
                               GetPropertyNames, …)
    texture-util.{hh,cc}       Texture loading / colorspace
    attribute-eval-*           Tydra-side animatable evaluation
    variant-{converter,support}.cc  Variant authoring/dispatch helpers
  external/, nonstd/         Vendored deps (header-only): nonstd::
                             optional, expected, string_view, fmt,
                             stb_image, base122, miniz, …
  attic/, blender/, next/    Experimental / under-construction code
                             not built into the main library

python/                    CPython abi3 wheel (built from
                           src/python/module.c via setuptools +
                           CMake). Layout:
  pyproject.toml             Build config (declared at repo root)
  tinyusdz/                  Installed package (`import tinyusdz`)
    __init__.py              Public re-exports
    _core.pyi                Type stubs (kept in sync with module.c)
  tests/                     pytest suite (~40 files, ~780 tests)
  tutorial*.py               Hand-run examples
  README.md                  User-facing Python docs

tests/                     C++ tests + roundtrip + Python harness
  unit/                      Acutest-based unit tests (unit-*.cc, 580+
                             tests)
  usda/                      USDA test fixture files
  usdc/                      USDC test fixture files
  parse_usd/                 Python-driven parse test runner
  tydra_to_renderscene/      Tydra conversion tests
  feat/                      Feature-specific test miniprograms
  fuzzer/                    libFuzzer corpora + harness sources
  compare-usda.js            Roundtrip comparison (tusdcat vs usdcat)
  run-usdcat-compare.sh      Batch roundtrip test runner

examples/                  Standalone example apps (separate builds):
                           tusdcat, api_tutorial, asset_resolution,
                           c_api_example, mcp_server, openglviewer,
                           optixviewer, file_format, js-script,
                           progressive_composition, etc.
models/                    Test USD files for development
doc/                       Documentation (testing-cpp.md,
                           how-to-implement-feature.md, c-py-tasks.md,
                           crate-impl.md, ci.md — release/publish
                           procedure, etc.)
aousd/                     AOUSD spec text + crate-impl docs (NOT the
                           PDFs themselves — those are gitignored)
scripts/                   Build/bootstrap scripts for various
                           platforms
web/                       WebAssembly/JavaScript bindings and demos
sandbox/                   Experimental tooling and prototypes
```

## Build Commands

```bash
# Native build (Linux/macOS)
mkdir build && cd build
cmake .. -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
make -j16

# Or use bootstrap script
./scripts/bootstrap-cmake-linux.sh
cd build && make -j16

# WASM build
cd web && mkdir build && cd build
emcmake cmake ..
make
```

Build folder: `build/` (native), `web/build/` (WASM).

```bash
# Python extension (CPython abi3 wheel)
pip install -e . --no-build-isolation
# editable install builds into build_py_ext/ (gitignored).
# Re-run after touching src/python/module.c, c-tinyusd-helpers.{h,cc},
# or any header transitively included by them.

cd python && python3 -m pytest tests/ -q
```

### Key CMake Options

- `TINYUSDZ_BUILD_TESTS=ON` - Build unit tests
- `TINYUSDZ_BUILD_EXAMPLES=ON` - Build example apps
- `TINYUSDZ_PRODUCTION_BUILD=ON` - Disable debug logging
- `TINYUSDZ_WITH_TYDRA=ON` - Tydra framework (default ON)
- `TINYUSDZ_WITH_EXR=ON` - EXR/HDR texture support
- `TINYUSDZ_WITH_AUDIO=ON` - Audio file loading (mp3/wav)
- `TINYUSDZ_WITH_OPENSUBDIV=ON` - Subdivision surfaces

## Testing

See `doc/testing-cpp.md` for full details on the C++ test infrastructure, and use [the Regression Test Procedure](doc/testing-cpp.md#regression-test-procedure) before merging/refactoring.

### Pre-merge checklist

Before merging refactors or feature branches, confirm all required checks pass:

1. Validate clean build and native regression coverage

```bash
cmake -S . -B build -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build build -j16
cd build
ctest --output-on-failure
ctest -R unit --output-on-failure
ctest -R roundtrip --output-on-failure
ctest -R feat --output-on-failure
```

2. Run web/WASM checks when web or JS-facing code changed

```bash
cd web
emcmake cmake -S . -B build
cmake --build build -j16
ctest --test-dir build --output-on-failure
```

3. Run Pixar compatibility regression if available

```bash
USDCAT_PATH=~/local/USD/dist/bin/usdcat TUSDCAT_PATH=./build/tusdcat \
  bash tests/run-usdcat-compare.sh
```

4. Check docs and commit hygiene

- Confirm any behavior-impacting changes are covered in [doc/testing-cpp.md](doc/testing-cpp.md)
- Verify no unrelated artifacts are left uncommitted for review

Do not merge if any command in steps 1–3 fails.

Copy-paste pre-merge script:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(git rev-parse --show-toplevel)}"
cd "$ROOT_DIR"
JOBS="${JOBS:-16}"

cmake -S "$ROOT_DIR" -B "$ROOT_DIR/build" \
  -DTINYUSDZ_BUILD_TESTS=ON -DTINYUSDZ_BUILD_EXAMPLES=ON
cmake --build "$ROOT_DIR/build" -j"$JOBS"

cd "$ROOT_DIR/build"
ctest --output-on-failure
ctest -R unit --output-on-failure
ctest -R roundtrip --output-on-failure
ctest -R feat --output-on-failure

cd "$ROOT_DIR/web"
if [ -f "$ROOT_DIR/web/CMakeLists.txt" ]; then
  emcmake cmake -S . -B build
  cmake --build build -j"$JOBS"
  ctest --test-dir build --output-on-failure
fi

if [ -x "$ROOT_DIR/tests/run-usdcat-compare.sh" ]; then
  USDCAT_PATH="${USDCAT_PATH:-$HOME/local/USD/dist/bin/usdcat}"
  TUSDCAT_PATH="${TUSDCAT_PATH:-$ROOT_DIR/build/tusdcat}"
  USDCAT_PATH="$USDCAT_PATH" TUSDCAT_PATH="$TUSDCAT_PATH" \
    bash "$ROOT_DIR/tests/run-usdcat-compare.sh"
fi
```

```bash
# Run all ctest-registered tests (from build/)
ctest --output-on-failure

# Run only unit tests
ctest -R unit-test-tinyusdz --output-on-failure

# Run only roundtrip tests
ctest -R roundtrip --output-on-failure

# Run a single Acutest unit test by name
./build/unit-test-tinyusdz crate_writer_cone_test

# Roundtrip comparison: tusdcat vs pxrUSD usdcat
USDCAT_PATH=~/local/USD/dist/bin/usdcat TUSDCAT_PATH=./build/tusdcat \
  bash tests/run-usdcat-compare.sh

# Compare individual file
node tests/compare-usda.js --detailed-diff \
  --tusdcat ./build/tusdcat --usdcat ~/local/USD/dist/bin/usdcat \
  tests/usda/somefile.usda
```

### ctest targets

| Name | What It Tests |
|------|---------------|
| `unit-test-tinyusdz` | 140+ Acutest unit tests (parser, writer, math, materials, etc.) |
| `usda-parser-unit-test` | Load all `tests/usda/*.usda` + expected-failure cases |
| `usdc-parser-unit-test` | Load all `tests/usdc/*.usdc` files |
| `usda-roundtrip-test` | USDA parse -> export -> reparse -> compare |
| `usdc-roundtrip-test` | USDA -> USDC -> reparse -> compare |

### Adding a new unit test

1. Declare in `tests/unit/unit-<module>.h`
2. Implement in `tests/unit/unit-<module>.cc`
3. Register in `tests/unit/unit-main.cc` (`TEST_LIST` array)
4. Rebuild and verify: `make -j16 && ctest -R unit-test-tinyusdz --output-on-failure`

## Key Data Flow

1. **Load**: `LoadUSDFromFile()` -> format detection -> parser -> `Stage`
2. **Compose**: `Stage` -> composition arcs -> flattened scene graph
3. **Convert**: `Stage` -> Tydra -> `RenderScene` (for rendering)
4. **Write**: `Stage` -> writer -> output file (USDA or USDC)

## Coding Conventions

- C++17 baseline (C++20 for coroutine support)
- `.cc`/`.hh` extensions
- No C++ exceptions (`nonstd::expected` for errors)
- Build with `-Weverything -Werror` (clang); suppress specific warnings via pragmas
- PascalCase for types, camelCase for functions
- `DCOUT()` macro for debug logging (compiled out in production builds)
- Headers must be self-contained

## Adding a New USD Schema / Prim Type

See **[doc/how-to-implement-feature.md](doc/how-to-implement-feature.md)** for the full step-by-step procedure covering research, file-by-file implementation checklist, type mapping, testing, and common pitfalls. The Physics (`src/usdPhysics.hh`) and AR (`src/usdAR.hh`) implementations are the canonical references.

## Security

- Memory budget controls: `USDLoadOptions::max_memory_limit_in_mb`
- Bounds checking in all parsers
- Fuzzer targets in `tests/fuzzer/`
- Always validate untrusted USD input with memory limits

## Commit Style

Concise imperative subjects (e.g. "Fix double-quoting in USDC metadata"). Body optional for context. Reference issues with `#123`. Default branch for PRs: `release`.

## Release / Versioning

Cutting a release (version bump, git tag, PyPI wheel publish, npm package publish) is documented in **[doc/ci.md](doc/ci.md)**. Read it before bumping any version or pushing a `v*.*.*` tag — the tag push triggers an automated PyPI publish via `.github/workflows/wheels.yml` (OIDC trusted publishing), and the npm publish is a manual `workflow_dispatch` on `.github/workflows/wasmPublish.yml`. The version sources that need hand-editing are `src/tinyusdz.hh` (C++ constants) and `web/{npm,js}/package.json` (npm packages); the Python wheel version is derived from the git tag by `setuptools_scm` and must NOT be edited by hand.

### Versioning and tagging checklist

Use this for release preparation and tag creation:

1. Decide bump level (MAJOR/MINOR/PATCH) and update all version sources in the same commit:
   - `src/tinyusdz.hh`
   - `web/npm/package.json`
   - `web/js/package.json`
2. Run the standard regression checks above (pre-merge checklist).
3. Verify generated artifacts are clean and consistent:
   - `git diff --stat`
   - `git status --short`
4. Create signed or annotated tag:

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z"
```

5. Push tag only after approval:

```bash
git push origin vX.Y.Z
```

6. Confirm CI/publish flows:
   - Verify `.github/workflows/wheels.yml` completed for PyPI.
   - Trigger/verify WebAssembly publish flow (`.github/workflows/wasmPublish.yml`) if npm packages changed.

Copy-paste versioning script:

```bash
#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="${ROOT_DIR:-$(git rev-parse --show-toplevel)}"
cd "$ROOT_DIR"
VERSION="${1:?Usage: $0 <semver>}"
if ! printf '%s\n' "$VERSION" | grep -Eq '^[0-9]+\.[0-9]+\.[0-9]+$'; then
  echo "ERROR: version must be X.Y.Z" >&2
  exit 1
fi

python3 - "$ROOT_DIR" "$VERSION" <<'PY'
import json
import pathlib
import re
import sys

root = pathlib.Path(sys.argv[1]).resolve()
version = sys.argv[2]
major, minor, patch = version.split(".")

header = root / "src/tinyusdz.hh"
text = header.read_text()
text = re.sub(r"#define TINYUSDZ_VERSION_MAJOR +[0-9]+", f"#define TINYUSDZ_VERSION_MAJOR {major}", text)
text = re.sub(r"#define TINYUSDZ_VERSION_MINOR +[0-9]+", f"#define TINYUSDZ_VERSION_MINOR {minor}", text)
text = re.sub(r"#define TINYUSDZ_VERSION_PATCH +[0-9]+", f"#define TINYUSDZ_VERSION_PATCH {patch}", text)
header.write_text(text)

for rel in ("web/npm/package.json", "web/js/package.json"):
    p = root / rel
    data = json.loads(p.read_text())
    data["version"] = version
    p.write_text(json.dumps(data, indent=2) + "\n")
PY

git add "$ROOT_DIR/src/tinyusdz.hh" "$ROOT_DIR/web/npm/package.json" "$ROOT_DIR/web/js/package.json"
echo "Version bump prepared for $VERSION"

USDCAT_PATH="${USDCAT_PATH:-$HOME/local/USD/dist/bin/usdcat}" \
TUSDCAT_PATH="${TUSDCAT_PATH:-$ROOT_DIR/build/tusdcat}" \
  bash "$ROOT_DIR/tests/run-usdcat-compare.sh"

git commit -m "Bump version to $VERSION"
git tag -a "v$VERSION" -m "Release v$VERSION"
```

## Git Push Policy (mandatory pre-push checklist)

Pushing rewrites shared state. Before **any** `git push` (regular, force, or `--force-with-lease`), an agent must complete every step below. Skipping a step is a defect — pre-push hygiene is one of the few places in this repo where "ask first" beats "act first" by default, because once a 100 MB binary or a leaked credential lands on `origin` it is forever in the public Git history.

### 1. Audit the commits about to leave the machine

Always inspect the exact range you are pushing — `git log --oneline @{upstream}..HEAD` for a regular push, or `git log --oneline <remote-tip>..HEAD` for a force/lease push — and run the four checks below against **every** commit in that range, not just `HEAD`. Any single offending commit blocks the push; resolve via `git rebase -i` (drop / edit) or `git filter-repo` (path removal) before continuing.

#### Check 1 — Credentials & sensitive data

Never push a commit that contains, or has ever contained at any point in its history within the push range:

- API keys, bearer tokens, OAuth client secrets, AWS / GCP / Azure access keys, SSH private keys, PGP private keys, `.netrc`, `.env`, `.npmrc` with `_authToken`.
- Email/password pairs, JWTs, Slack/Discord/GitHub webhook URLs.
- Internal hostnames, VPN configs, customer asset paths that aren't already public.
- Personal user paths (`/home/<someone>/…`, `C:\Users\<someone>\…`) baked into source comments — embarrassing rather than dangerous, but still gets stripped.

How to check (run from the repo root, against the exact push range):

```bash
RANGE="$(git rev-parse --abbrev-ref --symbolic-full-name @{upstream})..HEAD"
git diff "$RANGE" -- ':!**/*.md' ':!**/*.txt' \
  | grep -nIE 'AKIA[0-9A-Z]{16}|ASIA[0-9A-Z]{16}|AIza[0-9A-Za-z_\-]{35}|(?i)(api[_-]?key|secret|token|password|passwd|bearer|private[_-]?key)\s*[:=]' \
  || echo "No credential-shaped strings found in $RANGE"
```

This is a heuristic, not a guarantee. If a commit touches anything that *talks* to an external service (CI, package upload, asset server) eyeball the diff manually too.

#### Check 2 — No build artifacts

Build outputs do not belong in Git. They explode the pack size, cause merge conflicts on every rebuild, and routinely contain absolute paths from the developer's machine. If you find any of these in the push range, drop the file (and ignore the path going forward).

The common culprits in this repo:

- `build/`, `build_py/`, `build_py_ext/`, `build_test/`, `web/build/`, `web/build_64*/` — CMake / ninja output trees.
- `*.a`, `*.o`, `*.obj`, `*.so`, `*.dylib`, `*.dll`, `*.lib`, `*.pdb` — compiled object files / libraries.
- `python/tinyusdz/_core.abi3.so` — the editable-install Python extension binary; regenerated by `pip install -e .`.
- `*.ninja_deps`, `*.ninja_log`, `build.ninja`, `CMakeCache.txt`, `CMakeFiles/`, `CTestTestfile.cmake`, `compile_commands.json` — CMake/ninja state.
- `__pycache__/`, `*.pyc`, `node_modules/`, `dist/`, `*.egg-info/`.

Run from the repo root:

```bash
git diff --name-only "$RANGE" \
  | grep -E '^(build|build_py|build_py_ext|build_test|web/build)/|/CMakeFiles/|\.(a|o|obj|so|dylib|dll|lib|pdb|pyc|ninja_deps|ninja_log)$|/CMakeCache\.txt$|/CTestTestfile\.cmake$|/build\.ninja$|/_core\.abi3\.so$' \
  && { echo "ABORT: build artifacts staged for push"; exit 1; } \
  || echo "No obvious build artifacts in $RANGE"
```

`.gitignore` already covers all of the above; if a file shows up here it usually means it was either `git add`-ed with `-f`, or a previous mistake was rebased forward. Drop it; do not push it.

#### Check 3 — No unintended binary / asset data

USD asset files, Blender scenes, captured payloads, large images, and other binary art belong in external storage (Git LFS, Lighttransport's S3 bucket, the artist drop folder), not in the main repo history. Specifically reject:

- `*.blend`, `*.fbx`, `*.glb`, `*.gltf` larger than a few KB (small spec fixtures are OK), `*.abc`, `*.usdz` (zip), arbitrary `*.mb` / `*.ma`.
- `*.usd`, `*.usda`, `*.usdc` outside `tests/usda/`, `tests/usdc/`, `tests/feat/`, `models/`, `python/tests/` — these directories hold curated, deliberately-small fixtures; any ad-hoc capture goes elsewhere.
- `*.png`, `*.jpg`, `*.jpeg`, `*.exr`, `*.hdr`, `*.tif` over ~256 KB — texture captures are gitignored by default; small icons / spec test images are fine.
- Captured `.pdf` design docs (the AOUSD PDFs are explicitly gitignored under `aousd/*.pdf`; do not check them back in).
- `.codex`, `.claude/` and other AI tool scratch dirs.
- Anything you can't trivially regenerate from source.

Run from the repo root:

```bash
git diff --stat "$RANGE" \
  | awk '$3 ~ /^[0-9]+$/ && $3+0 > 256 { print }' \
  | head
# Inspect anything large; binaries show as "Bin <n> -> <m> bytes".

git diff --name-only "$RANGE" \
  | grep -iE '\.(blend|fbx|glb|gltf|abc|mb|ma|exr|hdr|tif|tiff|pdf)$|^data/.*\.usd[acz]?$|/captures?/' \
  && { echo "ABORT: unexpected binary/asset paths in $RANGE"; exit 1; } \
  || echo "No flagged binary/asset paths in $RANGE"
```

If a binary really must ship with the repo, the answer is git-lfs or an external asset bucket — never a plain `git add`. When in doubt, ask the user.

#### Check 4 — User permission (always, no exceptions)

After the three audits above pass, **stop and ask the user before pushing** — even if the user previously said "push when done", "go ahead and push", or has approved many pushes today. Authorization is single-shot and scoped to the immediate request. The default mode is "audit, summarize, ask, then push", in that exact order.

When asking, summarize what is about to be pushed:

- the branch name and remote (`origin/physics-2026`, etc.),
- whether it is a fast-forward or a force-push (force-pushes also need to spell out which previously-public commit hashes are being orphaned),
- the count of commits and a one-line per-commit summary,
- the audit results from checks 1–3 (e.g. "No credential-shaped strings, no build artifacts, no flagged binaries"),
- any pre-push test results worth surfacing (last unit / pytest run).

Do **not** assume "auto mode" or any prior `--yes`-style flag covers a push. Force-pushes especially need explicit, fresh confirmation; prefer `--force-with-lease` over `--force` when the user agrees to a force push, because lease refuses if the remote moved since the last fetch (avoiding the classic "I just clobbered a teammate's commit" failure).

### 2. After the push

- If the push was a force-push, immediately note the previous remote tip in the conversation so a teammate can recover their work (`git reflog show origin/<branch>` on their side, or `git fetch origin <old-sha>:refs/heads/recovered-<branch>`).
- Open / update the PR if one exists; the project's default PR base is `release`.
- Do not delete branches the user did not explicitly ask to delete.

### 3. If a check fails

- For a credential leak — **stop**, surface the leak to the user, and treat the credential as compromised even if the commit is still local. Rotate first, scrub history second; never push to "clean it up later."
- For a stray binary / artifact — drop it from history with `git filter-repo --invert-paths --path <p>` (or `git rebase -i` if it's a single recent commit), update `.gitignore`, then re-run the full checklist.
- For unintended assets — confirm with the user whether the asset belongs in Git LFS, an external bucket, or should simply be removed.

The Pre-push checklist is non-negotiable. A single 100 MB binary in `origin` permanently bloats every future clone of this repo; a single leaked key in `origin` is a security incident regardless of how fast it gets rotated.
