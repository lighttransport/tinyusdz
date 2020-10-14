# pxr USD library API experiment

## Build pxr USD

Build USD in some way.
I recommend to use USD-build-aarch64(it also supports the build on x86 linux)

https://github.com/syoyo/USD-build-aarch64

## Build

Set path to prebult USD to `EXAMPLE_USD_DIR` cmake argument in `bootstrap-linux.sh`, then

```
$ ./bootstrap-linux.sh
$ cd build
$ cmake
```

EoL.
