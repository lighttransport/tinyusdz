# Simple OpenGL viewer

## Status 

Not yet working.

## Supported USD files

USDC(binary), USDA(ascii) and USDZ(zip container)

## Requirements 

* C++14 compiler
* OpenGL/GLES 3.x
* cmake

## Build

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Run

```
$ ./usdz_glview <input.usdz>
```

### Optional: Embed shaders(requires Python)

If you modify shader frag/vert, please update shader source code by running

```
$ python embed_shaders.py
```

Alternatively you can use `xxd -i input.frag` to generate embeddable shader code.


## TODO

* [ ] Embed shader codes to C++ source code.
* [ ] Subdivision surface
* [ ] Animation
  * Timesamples animation
    * [ ] Xform
    * [ ] GeomMesh(Vertex animation) 
    * [ ] Camera, Light, etc.
  * usdSkel
    * [ ] Vertex skinning
    * [ ] Morphing(Blend shapes)

## Third party libraries

* imgui : MIT license.
* glfw3 : zlib license.
