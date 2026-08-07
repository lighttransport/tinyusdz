# Legacy-only WASM32 build: the tinyusdz module WITHOUT the next core
# (-DTINYUSDZ_WASM_PRODUCT=legacy). Drops the nextFlatten* low-memory flatten
# pipeline and the RenderStream streaming API; the classic loadFromBinary/
# Tydra RenderScene path is unchanged. Output: js/src/tinyusdz/
# tinyusdz.js/.wasm.
# Build with: `ninja -C build_legacy_only` (or `cmake --build build_legacy_only`).
rm -rf build_legacy_only
mkdir build_legacy_only

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DTINYUSDZ_WASM_PRODUCT=legacy -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild_legacy_only
