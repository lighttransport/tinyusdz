builddir=build_64
rm -rf ${builddir}
mkdir ${builddir}

emcmake cmake -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -DTINYUSDZ_WASM_DEMODEV=1 -DTINYUSDZ_WASM64=1 -B${builddir}
