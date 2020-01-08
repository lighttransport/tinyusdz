# Tiny USDZ loader

tinyusdz is dependency-free(depends only on STL. Yes, you don't need USD library!) USDZ loader library written in C++11

## Requirements

* C++11

## USDZ file format

USDZ is a uncompressed zip file.
USDZ contains usdc(binary) and resources(e.g. image file)

## Build

### Meson

```
$ meson builddir
$ cd builddir
$ ninja
```

### Build options

* TINYUSDZ_USE_SYSTEM_LIBZ : Use zlib installed on the system instead of miniz

## TODO

* [ ] USDZ saver
* [ ] Support compressed USDZ?

## License

MIT license

### Third party licenses

* miniz : public domain.
* USD : Apache 2.0 license. https://github.com/PixarAnimationStudios/USD
* lz4 : BSD-2 license. http://www.lz4.org
* cnpy(uncompressed ZIP decode/encode) : MIT license https://github.com/rogersce/cnpy
