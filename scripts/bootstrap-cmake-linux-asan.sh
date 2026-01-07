curdir=`pwd`

builddir=${curdir}/build_asan

rm -rf ${builddir}
mkdir ${builddir}

# with lld linker
#  -DCMAKE_TOOLCHAIN_FILE=cmake/lld-linux.toolchain.cmake 

cd ${builddir} && CXX=clang++ CC=clang cmake \
  -DSANITIZE_ADDRESS=1 \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

