# tinyusdz

Tiny, dependency-free **USD** (Universal Scene Description) library for
Python: load and author USDA / USDC / USDZ, resolve composition, and extract
GPU-ready render data — with **zero-copy NumPy interop**.

Built on the TinyUSDZ *next* core (no pxrUSD install required). Wheels:

- `cp310-abi3` — one stable-ABI wheel covers CPython **3.10+**
- `cp314t` — native **free-threaded** (GIL-less) CPython 3.14 wheel

```bash
pip install tinyusdz
```

## Reading

```python
import numpy as np
import tinyusdz

stage = tinyusdz.load("scene.usdz")          # usda / usdc / usdz, composed
for prim in stage:                            # depth-first traversal
    print(prim.path, prim.type_name)

mesh = stage.prim_at("/World/Mesh")
points = np.asarray(mesh["points"])           # zero-copy (N, 3) float32
attr = mesh.attribute("xformOp:translate")
if attr.has_timesamples:
    value = attr.get(time=12.0)               # linear interpolation
print(mesh.relationship("material:binding").targets)
```

## Authoring

```python
st = tinyusdz.Stage.create()
st.up_axis = "Y"
grid = st.define_prim("/World/Grid", "Mesh")
grid.set("points", np.zeros((64, 3), np.float32), type="point3f[]")
grid.set("purpose", "render", uniform=True)
grid.set("xformOp:translate", (0.0, 1.0, 0.0), time=0.0)
st.set_default_prim("World")
st.save("out.usdc")                           # or .usda / .usdz
```

## Render extraction (tydra)

```python
from tinyusdz import tydra

scene = tydra.to_render_scene(stage)          # triangulated, GPU-friendly
for mesh in scene.meshes:
    vertices = np.asarray(mesh.points)                # (N, 3) float32
    indices = np.asarray(mesh.triangulated_indices)   # (T,) uint32
    material = scene.materials[mesh.material_id]
    base_color = material.param("diffuse_color")      # tuple or RenderTexture
```

## Notes

- NumPy is optional (interop only, not a dependency).
- `tinyusdz.flatten_file(src, dst)` runs a low-memory compose+flatten
  pipeline that streams large arrays through without decoding them.
- Free-threaded CPython: concurrent reads of one `Stage` are safe; do not
  author to a stage while other threads read it.

Apache 2.0. Part of [TinyUSDZ](https://github.com/lighttransport/tinyusdz).
