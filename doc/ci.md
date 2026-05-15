# CI / Release Procedure

Version-bump, tagging, and publish procedure for TinyUSDZ.

## Version sources

| Component | File | How version is set |
|---|---|---|
| C++ library (compile-time constants) | `src/tinyusdz.hh` lines 50–53 | Hand-edit |
| Python wheel (`tinyusdz` on PyPI) | derived from git tag via `setuptools_scm` (config in `pyproject.toml` line 52–53; written to `python/tinyusdz/_version.py` at build time) | Automatic from tag |
| NPM package (`tinyusdz` on npmjs.org) | `web/npm/package.json` line 4 (source-of-truth) and `web/js/package.json` line 4 | Hand-edit; workflow can override via `--release-version=` |

There is no `CHANGELOG.md` / release-notes file in the repo. GitHub Release notes are written on the GitHub UI when cutting the release.

## 1. Pre-release: bump versions on the release branch

Work on the `release` branch (PRs target `release`, see `AGENTS.md`).

### 1a. C++ version constants

Edit `src/tinyusdz.hh`:

```cpp
constexpr int version_major = 0;
constexpr int version_minor = 9;
constexpr int version_micro = 2;    // bump
constexpr auto version_rev = "";    // e.g. "rc.1" for pre-releases
```

These are the only C++ version constants; `CMakeLists.txt` does not hardcode a version.

### 1b. NPM package version

Edit both:

- `web/npm/package.json` — `"version": "x.y.z"`
- `web/js/package.json`  — `"version": "x.y.z"`

These are the source-of-truth for the npm package. The `wasmPublish.yml` workflow can override the published version via its `release_version` workflow input (see step 4); when used, `web/npm/scripts/stage-package.mjs` rewrites the staged manifest at publish time. Keep the checked-in files in sync with the version you actually publish.

### 1c. Python version

Do **not** edit any file. `setuptools_scm` derives the Python wheel version from the `v*.*.*` git tag pushed in step 3. Confirm `pyproject.toml` still has:

```toml
dynamic = ["version"]
[tool.setuptools_scm]
version_file = "python/tinyusdz/_version.py"
```

### 1d. Commit

```bash
git checkout release
git pull
# edit src/tinyusdz.hh, web/npm/package.json, web/js/package.json
git add src/tinyusdz.hh web/npm/package.json web/js/package.json
git commit -m "Bump version to x.y.z"
git push origin release
```

## 2. Sanity checks before tagging

```bash
# Native build + tests
cmake -S . -B build -DTINYUSDZ_BUILD_TESTS=ON
cmake --build build -j16
cd build && ctest --output-on-failure && cd ..

# Roundtrip
bash tests/run-usdcat-compare.sh

# Python wheel (local smoke test; full matrix runs in CI)
python -m pip install --upgrade build
python -m build --sdist
```

## 3. Tag and push — triggers PyPI publish

The PyPI workflow (`.github/workflows/wheels.yml`) is triggered by **any push of a tag matching `v*.*.*`**.

```bash
git tag -a vX.Y.Z -m "Release vX.Y.Z"
git push origin vX.Y.Z
```

What this triggers in `wheels.yml`:

1. `build_wheels` matrix — builds CPython abi3 wheels via cibuildwheel for:
   - `ubuntu-latest` × x86_64
   - `ubuntu-24.04-arm` × aarch64
   - `macos-14` × arm64
   - `windows-latest` × AMD64
   - `windows-11-arm` × ARM64
2. `build_sdist` — produces source distribution.
3. `publish` job (only on `push` of `refs/tags/v*`):
   - Uses **PyPI Trusted Publishing (OIDC)** — no API token in repo secrets.
   - The pypi.org project must have a matching publisher registered:
     - owner: `lighttransport`, repo: `tinyusdz`, workflow: `wheels.yml`, environment: `pypi`.
   - GitHub environment `pypi` must exist on the repo with `id-token: write` allowed.
   - Emits PEP 740 attestations.

Watch the run at: https://github.com/lighttransport/tinyusdz/actions/workflows/wheels.yml

If the publish step fails after wheels build successfully, wheels are kept as workflow artifacts — re-running just the `publish` job is safe (PyPI rejects duplicates).

### Pre-release / RC tags

PyPI accepts PEP 440 pre-releases (`vX.Y.ZrcN`, `vX.Y.Z.devN`, etc.). The tag pattern `v*.*.*` matches `v0.9.2rc1` as well as `v0.9.2`. To make a real RC:

1. Set `version_rev = "rc.1"` in `src/tinyusdz.hh` (cosmetic; C++ side only).
2. Push tag `v0.9.2rc1`.
3. Install with `pip install --pre tinyusdz`.

## 4. NPM publish — manual workflow dispatch

The npm publish workflow (`.github/workflows/wasmPublish.yml`) is **not** triggered by tags. Run it manually:

GitHub UI: Actions → "Build and publish wasm" → Run workflow.

Inputs:

- `release_version` — semver, e.g. `0.9.9` (rewrites the staged `package.json`; should match what you committed in 1b).
- `npm_tag` — npm dist-tag, e.g. `latest` for a real release, `preview` / `next` / `rc` for pre-releases. **Default is `preview`** — change it to `latest` for stable releases.

The job:

1. Sets up Node 24 with `registry-url: https://registry.npmjs.org`.
2. Installs Emscripten 4.0.9 and builds WASM32 + WASM64 (MinSizeRel).
3. `npm run build:stage -- --release-version=<input>` (runs `web/npm/scripts/stage-package.mjs`).
4. `npm run validate` (runs `web/npm/scripts/validate-package.mjs`).
5. `npm publish --provenance --access public --tag <npm_tag>` from `web/npm/dist`.

Publishing requires npm provenance (OIDC, `id-token: write`) — the npmjs `tinyusdz` package must trust this workflow. No npm token in repo secrets.

To publish locally instead (not recommended; loses provenance attestation):

```bash
cd web/npm
npm ci
./00-build-package.sh           # builds wasm + stages dist
npm run build:stage -- --release-version=X.Y.Z
npm run validate
cd dist && npm publish --access public --tag latest
```

## 5. GitHub Release

After the PyPI run is green:

```bash
gh release create vX.Y.Z --title "vX.Y.Z" --notes "…release notes…" --target release
```

Or via the GitHub UI on the tag page. The release-notes body is hand-written (no auto-generated CHANGELOG in the repo).

## 6. Post-release

- Bump `version_micro` (or `version_minor`) in `src/tinyusdz.hh` on the `dev` branch to the next pre-release (e.g. `0.9.3` with `version_rev = ""`).
- Optionally bump `web/npm/package.json` + `web/js/package.json` to a `-dev` version to make accidental publishes of intermediate builds easy to spot.

## Summary: files touched per release

```
src/tinyusdz.hh              # C++ version constants
web/npm/package.json         # npm package version
web/js/package.json          # JS module version (keep in sync with web/npm)
```

Files **not** touched by a version bump (handled automatically):

```
pyproject.toml               # dynamic version via setuptools_scm
python/tinyusdz/_version.py  # generated at build time
CMakeLists.txt               # no hardcoded version
```

## Workflow reference

| Workflow file | Trigger | Publishes to | Auth |
|---|---|---|---|
| `.github/workflows/wheels.yml` | push tag `v*.*.*` (or `workflow_dispatch`) | PyPI `tinyusdz` | OIDC trusted publisher (env `pypi`) |
| `.github/workflows/wasmPublish.yml` | `workflow_dispatch` only | npmjs `tinyusdz` | OIDC provenance |
