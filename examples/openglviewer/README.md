# Simple OpenGL viewer

## Status 

Not yet working.

## Supported USD files

USDC(binary), USDA(ascii) and USDZ(zip container)

## Requirements 

* C++11 compiler
* OpenGL 2.x
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

## TODO

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
