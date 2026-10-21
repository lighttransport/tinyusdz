# lightusd

Lightweight, dependency-free **USD** (Universal Scene Description) library for
Python: load and author USDA / USDC / USDZ, resolve composition, and extract
GPU-ready render data — with **zero-copy NumPy interop**.

Built on the LightUSD *next* core + *tydra-next* (no pxrUSD install required,
no legacy core in the wheel; native `cmake -S .` and WASM still
provide the legacy path via `web/binding.cc`). **v1.0.0**, npm `preview`
dist-tag. Wheels:

- `cp311-abi3` — one stable-ABI wheel covers CPython **3.11+**
- `cp314t` — native **free-threaded** (GIL-less) CPython 3.14 wheel

```bash
pip install lightusd
```

## Reading

```python
import numpy as np
import lightusd

stage = lightusd.load("scene.usdz")          # usda / usdc / usdz, composed
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
st = lightusd.Stage.create()
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
from lightusd import tydra

scene = tydra.to_render_scene(stage)          # triangulated, GPU-friendly
for mesh in scene.meshes:
    vertices = np.asarray(mesh.points)                # (N, 3) float32
    indices = np.asarray(mesh.triangulated_indices)   # (T,) uint32
    material = scene.materials[mesh.material_id]
    base_color = material.param("diffuse_color")      # tuple or RenderTexture
```

## Asset resolution

```python
from lightusd import AssetResolver

resolver = AssetResolver()
path, exists, is_package = resolver.resolve_new("sub/new.usda", "/proj/look/layer.usda")

with resolver.bind_context("/proj/assets"):
    path, exists, is_package = resolver.resolve("tex.png")

with resolver.cache_scope():
    resolver.resolve("tex.png")                # memoized for the scope

resolver.register_package(".pkg", lambda package, entry: f"virtual:{package}|{entry}")
path, exists, is_package = resolver.resolve("bundle.pkg[dir/entry.usda]")

resolver.register_scheme(
    "studio",
    lambda identifier, anchor, scheme_context: identifier,
    lambda identifier: b"#usda 1.0\n",
)
assert resolver.read("studio:layer.usda").startswith(b"#usda")

resolver.register_memory("usd-anon:generated.usda", b"#usda 1.0\n")
data = resolver.read("usd-anon:generated.usda")
report, ok = resolver.dependency_report("usd-anon:generated.usda")
count, bytes_used, peak = resolver.memory_stats

with resolver.bind_context("/proj/assets", scheme_contexts={"https": "/cache"}):
    path, exists, is_package = resolver.resolve("https://example.com/tex.png")

stage = lightusd.load("root.usda", resolver=resolver)
```

Resolver and package callbacks are retained until replaced, unregistered, or
the resolver is destroyed. Composition may invoke them from worker threads;
exceptions raised by callbacks are re-raised by `resolve`, `read`, or `load`.

## Notes

- Arrays implement Python's read-only buffer protocol. Use `memoryview(array)`
  or `np.asarray(array)`; `Array.memoryview()` and `__array_interface__` have
  been removed. A view retains its C11 storage after attribute replacement,
  stage closure, or render-scene deletion. Later writes detach shared storage.
- Numeric authoring accepts sequences and typed buffers, including strided and
  big-endian buffers. Shared native descriptors validate and convert components;
  integer overflow rejects the write without changing the attribute.
- NumPy is optional (interop only, not a dependency).
- `lightusd.flatten_file(src, dst)` runs a low-memory compose+flatten
  pipeline that streams large arrays through without decoding them.
- Free-threaded CPython: concurrent reads of one `Stage` are safe; do not
  author to a stage while other threads read it.
- Source builds can set `LIGHTUSD_PY_ENABLE_THREAD=0` to omit optional
  parser/composition/render worker threads while retaining API-level
  synchronization and free-threaded-CPython safety.

Apache 2.0. Part of [LightUSD](https://github.com/lighttransport/LightUSD).
