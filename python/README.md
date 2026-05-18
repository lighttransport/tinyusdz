# tinyusdz

Python bindings for [TinyUSDZ](https://github.com/lighttransport/tinyusdz), a
small USD/USDA/USDC/USDZ loader and authoring library that does not require a
Pixar OpenUSD install.

- Single abi3 wheel per platform for CPython 3.11+.
- No NumPy dependency; zero-copy buffer protocol support when NumPy is present.
- No Pixar USD install required.
- Includes Tydra render-scene conversion for renderer-friendly mesh, material,
  texture, light, camera, animation, and skeleton data.

## Install

```sh
pip install tinyusdz
```

## Quick start

```python
import tinyusdz

stage = tinyusdz.load("scene.usdz")
for prim in tinyusdz.traverse(stage):
    print(prim.type_name, prim.name)

mesh = stage.get_prim_at_path("/World/Mesh")
points = mesh.get_attribute("points").value

# Zero-copy NumPy view (optional — NumPy is not required)
import numpy as np
arr = np.asarray(points)
print(arr.shape, arr.dtype)

scene = tinyusdz.tydra.convert_to_render_scene(stage)
print(len(scene.meshes()), len(scene.materials()))
```

## License

Apache 2.0
