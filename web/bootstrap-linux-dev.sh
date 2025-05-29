rm -rf build
mkdir build

emcmake cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild -DTINYUSDZ_WASM_DEV=1
