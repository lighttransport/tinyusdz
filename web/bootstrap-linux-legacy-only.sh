# Legacy-only WASM32 build: the tinyusdz module WITHOUT the next core
# (TINYUSDZ_WASM_LEGACY_ONLY=ON). Drops the nextFlatten* low-memory flatten
# pipeline and the RenderStream streaming API; the classic loadFromBinary/
# Tydra RenderScene path is unchanged. Output: js/src/tinyusdz/
# tinyusdz_legacy.js/.wasm (distinct name so it never clobbers the default
# legacy+next tinyusdz.js/.wasm).
# Build with: `ninja -C build_legacy_only` (or `cmake --build build_legacy_only`).
rm -rf build_legacy_only
mkdir build_legacy_only

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DTINYUSDZ_WASM_LEGACY_ONLY=ON -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild_legacy_only
