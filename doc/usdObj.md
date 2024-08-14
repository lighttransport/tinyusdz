# usdObj in TinyUSDZ

TinyUSDZ has built-in support of usdObj(wavefront .obj import).
Why built-in?

* It is not recommended to use plug-in architecture(e.g. separated dll) for mobile/wasm devices.
* People frequently want .obj support

.obj support is enabled by default. You can disable usdObj support by cmake flags(`TINYUSDZ_USE_USDOBJ=Off`)

## Data structure

* Shape(group/object) hierarchy is flattened to single mesh and no material.
* Texcoords and normals are decoded(expanded) as face-varying attribute.

## Limitations

* No material info is parsed
* No per-object and per-face material

## TODO

* [ ] Construct Indexed Primvar for texcoords and normals(more USD friendly than expanding it to face-varying data?)
* [ ] Preserve shape hierarchy
* [ ] per-face material
  * Decode to GeomSubset?
* [ ] Loading Skin weight(tinyobjloader's extension)
* [ ] Optional: Do triangulation on .obj load. 
