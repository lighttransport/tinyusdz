# Next-only WASM32 build: next-core + tydra-next module
# (-DTINYUSDZ_WASM_PRODUCT=next), no legacy loader. Output:
# js/src/tinyusdz/tinyusdz_next.js/.wasm.
# Build with: `ninja -C build_next_only` (or `cmake --build build_next_only`).
rm -rf build_next_only
mkdir build_next_only

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DTINYUSDZ_WASM_PRODUCT=next -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild_next_only
