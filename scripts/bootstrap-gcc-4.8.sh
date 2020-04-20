curdir=`pwd`

builddir=${curdir}/build-gcc4.8

rm -rf ${builddir}
mkdir ${builddir}

cd ${builddir} && cmake \
  -DCMAKE_C_COMPILER=gcc-4.8 \
  -DCMAKE_CXX_COMPILER=g++-4.8 \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

