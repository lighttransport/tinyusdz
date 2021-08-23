curdir=`pwd`

builddir=${curdir}/build-gcc4.9

rm -rf ${builddir}
mkdir ${builddir}

cd ${builddir} && cmake \
  -DCMAKE_C_COMPILER=gcc-4.9 \
  -DCMAKE_CXX_COMPILER=g++-4.9 \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

