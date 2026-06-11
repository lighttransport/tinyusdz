# Default WASM32 build: Ninja generator + MinSizeRel.
#
# MinSizeRel applies emscripten's link-time -Oz (and drops runtime assertions),
# which is what keeps the .wasm small (~5MB vs ~13MB for a plain Release link).
# Build with: `ninja -C build` (or `cmake --build build`).
# For a speed-over-size build instead, use bootstrap-linux-release.sh.
rm -rf build
mkdir build

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild
