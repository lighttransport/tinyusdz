# Simple viewer with SDL2

This viewer uses NanoRT(SW ray tracer) to render the model and display it using SDL2
(So no OpenGL dependency)

## Requirements 

* C++14 compiler
  * external library imgui_sdl requires C++14
* cmake
* X11 related package(Linux only)

## Build

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

## Run

```
$ ./usdz_view <input.usdz>
```

## TODO

* [ ] Subdivision surface

## Third party libraries

* imgui : MIT license.
* SDL2 : zlib license.
