rm -rf build_emcc
mkdir build_emcc

# Assume emsdk env has been setup
emcmake cmake -Bbuild_emcc -DCMAKE_BUILD_TYPE=MinSizeRel
