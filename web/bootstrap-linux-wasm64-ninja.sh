builddir=build_64_ninja
rm -rf ${builddir}
mkdir ${builddir}

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -DTINYUSDZ_WASM64=1 -B${builddir}
