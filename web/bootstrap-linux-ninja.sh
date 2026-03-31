rm -rf build_ninja
mkdir build_ninj

emcmake cmake -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_VERBOSE_MAKEFILE=1 -Bbuild_ninja
