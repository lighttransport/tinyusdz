rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -DTINYUSDZ_WASM64=1 -Bbuild
