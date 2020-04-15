curdir=`pwd`

builddir=${curdir}/build

rm -rf ${builddir}
mkdir ${builddir}


cd ${builddir} && cmake \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

