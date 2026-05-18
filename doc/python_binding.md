# TinyUSDZ Python Binding

TinyUSDZ ships a CPython extension package named `tinyusdz`. The package is a
thin Python facade over `src/python/module.c`, backed by the C API and Tydra
scene-access helpers.

End-user package documentation lives in [../python/README.md](../python/README.md).
This page is for source builds and maintainer notes.

## Package Status

- Package name: `tinyusdz`
- Python support: CPython 3.11+
- Wheel ABI: abi3, built from the CPython 3.11 stable ABI floor
- Runtime dependency on NumPy: none
- Optional test dependency: NumPy, used to validate buffer-protocol interop
- Version source: git tags through `setuptools_scm`

The Python API exposes:

- `tinyusdz.load(path, format=None)`, `loads(usda_text)`, and
  `load_bytes(data, format=None)`
- `Stage`, `Prim`, `Attribute`, and `Value`
- stage authoring and `Stage.save(...)`
- composition arc, variant, metadata, relationship, connection, and time-sample
  authoring helpers
- `tinyusdz.traverse(stage)` and `rewrite_asset_paths(...)`
- `tinyusdz.tydra.convert_to_render_scene(stage)` with zero-copy render buffers

The exact public signatures are tracked in
[../python/tinyusdz/_core.pyi](../python/tinyusdz/_core.pyi).

## Install From PyPI

```sh
python -m pip install tinyusdz
```

## Editable Source Build

From the repository root:

```sh
python -m pip install -e . --no-build-isolation
```

The build is driven by [../setup.py](../setup.py):

1. Configure CMake under `build_py_ext/`.
2. Build the static C++ library and C API library.
3. Compile `src/python/module.c` as an abi3 CPython extension.
4. Install the Python package from `python/tinyusdz`.

Rebuild after touching `src/python/module.c`, `src/c-tinyusd-helpers.*`, the C
API headers, or any transitively included C++ headers used by the extension.

## Wheel Build

The project uses `pyproject.toml`, `setuptools`, `setuptools_scm`, and `wheel`.

```sh
python -m pip install build
python -m build
```

CI wheels are built with cibuildwheel. The wheel version is derived from the git
tag; do not edit a Python version file by hand for a release.

## Tests

```sh
python -m pip install -e ".[test]" --no-build-isolation
python -m pytest python/tests -q
```

The tests cover loading, authoring, USDA/USDC/USDZ save paths, typed values,
buffer protocol behavior, Tydra render-scene extraction, variants, composition
arcs, relationships, metadata, and error handling.

## Minimal Usage

```python
import tinyusdz

stage = tinyusdz.load("scene.usdz")
for prim in tinyusdz.traverse(stage):
    print(prim.type_name, prim.name)

mesh = stage.get_prim_at_path("/World/Mesh")
if mesh is not None:
    points = mesh.get_attribute("points").value
    print(points.to_string())

stage.save("out.usda")
```

Render-scene conversion:

```python
import tinyusdz

stage = tinyusdz.load("scene.usda")
scene = tinyusdz.tydra.convert_to_render_scene(stage)

for mesh in scene.meshes():
    print(mesh.name, mesh.points)
```

NumPy can consume `Value` and `BufferView` objects through the buffer protocol,
but NumPy is optional:

```python
import numpy as np
import tinyusdz

stage = tinyusdz.load("scene.usda")
scene = tinyusdz.tydra.convert_to_render_scene(stage)
points = np.asarray(scene.meshes()[0].points)
```
