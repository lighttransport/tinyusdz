curdir=`pwd`

builddir=${curdir}/build

rm -rf ${builddir}
mkdir ${builddir}

# Use environment CC/CXX if set, otherwise default to clang
: ${CC:=clang}
: ${CXX:=clang++}

# with lld linker
#  -DCMAKE_TOOLCHAIN_FILE=cmake/lld-linux.toolchain.cmake

cd ${builddir} && CC=${CC} CXX=${CXX} cmake \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

