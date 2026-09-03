rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DLIGHTUSD_WASM_COROUTINE=On  -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild
