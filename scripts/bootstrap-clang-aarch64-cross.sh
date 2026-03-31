curdir=`pwd`

builddir=${curdir}/build-clang-aarch64

rm -rf ${builddir}
mkdir ${builddir}

cd ${builddir} && cmake \
  -DCMAKE_TOOLCHAIN_FILE=cmake/aarch64-linux-clang.toolchain.cmake \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  -DTINYUSDZ_BUILD_TESTS=On \
  -DTINYUSDZ_BUILD_EXAMPLES=On \
  ..
