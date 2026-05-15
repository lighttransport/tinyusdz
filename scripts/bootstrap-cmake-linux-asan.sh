curdir=`pwd`

builddir=${curdir}/build_asan

rm -rf ${builddir}
mkdir ${builddir}

# with lld linker
#  -DCMAKE_TOOLCHAIN_FILE=cmake/lld-linux.toolchain.cmake 

cd ${builddir} && CXX=clang++ CC=clang cmake \
  -DSANITIZE_ADDRESS=1 \
  -DTINYUSDZ_NO_WERROR=On \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

# -DTINYUSDZ_NO_WERROR=On: gcc's static analyzer under ASan
# instrumentation generates false-positive -Warray-bounds /
# -Wstringop-truncation warnings on vendored stb_image_write.h and
# strncpy() use in crate-writer.cc that don't fire in non-ASan builds.
# This is an instrumented debug build, not production validation, so
# turning off -Werror is the right tradeoff.

