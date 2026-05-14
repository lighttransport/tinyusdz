# tinyusdz

Python bindings for [tinyusdz](https://github.com/lighttransport/tinyusdz) —
a tiny, dependency-free USD/USDA/USDC/USDZ loader.

- Single abi3 wheel per platform: works on CPython 3.10, 3.11, 3.12, 3.13+.
- No NumPy dependency; zero-copy via the Python buffer protocol.
- No Pixar USD install required.

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
```

## License

Apache 2.0
