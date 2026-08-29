# TinyUSDZ Python Binding

TinyUSDZ ships a CPython extension package named `tinyusdz` **v1.0.0**
(next core, `preview` npm dist-tag), built on the **next core**
(`src/next/` + `src/tydra/next/`) through the C API (`src/c-api/`). The
legacy tinyusdz core is not linked into the Python wheel. Native C++ (`cmake
-S .`) and WASM (`web/CMakeLists.txt` / `web/binding.cc` with
`TINYUSDZ_WASM_PRODUCT=legacy/next/combined`) still build and ship the legacy
path.

End-user package documentation lives in [../python/README.md](../python/README.md).
This page is for source builds and maintainer notes.

## Package Status

- Package name: `tinyusdz`
- Python support: CPython 3.10+ (stable ABI) and free-threaded CPython 3.14
- Wheels: `cp310-abi3` (one wheel covers 3.10+) and `cp314t` (free-threaded,
  `Py_mod_gil = Py_MOD_GIL_NOT_USED`)
- Runtime dependency on NumPy: none (zero-copy interop when present, via
  `__array_interface__` on the abi3 build and the buffer protocol on cp314t)
- Version source: git tags through `setuptools_scm`

## Architecture

```
python/tinyusdz/__init__.py   pure-python facade (pathlib, value normalizer)
python/tinyusdz/tydra.py      render-scene shim
src/python/py-*.c             raw CPython C-API extension (tinyusdz._core)
src/c-api/tinyusdz-c.*        core C API (tusd_*): stage/prim/attr/authoring
src/c-api/tinyusdz-render-c.* tydra render C API (buffers, materials, nodes)
src/next/                     next core (parser, crate, composition, writers)
src/tydra/next/               render-scene converter
```

Single extension source, two build configurations:

- **abi3** (default): `Py_LIMITED_API=0x030A0000`. The buffer protocol is not
  in the limited API before 3.11, so zero-copy numpy interop goes through
  `__array_interface__` / `Array.memoryview()`.
- **free-threaded** (`Py_GIL_DISABLED` interpreters): non-limited build with
  real buffer-protocol slots. All extension state lives in module state
  (multi-phase init, no static globals); stage reads are thread-safe (lazy
  crate-array materialization is serialized inside the C API), authoring must
  not race reads of the same stage.

## Building from source

```bash
pip install -e .          # drives CMake on src/next, then builds the extension
pytest python/tests -q
```

Environment overrides: `TINYUSDZ_PY_LIMITED_API=0` (force a non-abi3 dev
build), `TINYUSDZ_CMAKE_ARGS` (extra CMake args),
`TINYUSDZ_TEST_ASSETS` (pytest asset dir).

Wheels are built by `.github/workflows/wheels.yml` with cibuildwheel (v3.x,
`enable = ["cpython-freethreading"]`); configuration lives in
`[tool.cibuildwheel]` in `pyproject.toml`. Publishing uses PyPI Trusted
Publishing (OIDC), unchanged.

## C API notes

`src/c-api/tinyusdz-c.h` is a standalone C11 FFI surface usable from any
language (Rust/C#/Deno/...): opaque owning handles + by-value `tusd_prim`
handles, thread-local `tusd_last_error()`, zero-copy `tusd_value_view` /
`tusd_buffer_view` views, batched authoring calls. Smoke-tested from pure C
by `tests/c-api/test_tinyusdz_c.c` (ctest: `next_test_c_api`).
