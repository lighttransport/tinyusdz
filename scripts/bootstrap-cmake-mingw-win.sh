# llvm-mingw + mintty bash(git for Windows)
# Assume Ninja is installed on your system
curdir=`pwd`

builddir=${curdir}/build

rm -rf ${builddir}
mkdir ${builddir}

# Change the path to llvm-mingw to fit into your system.
cd ${builddir} && cmake \
  -G "Ninja" \
  -DCMAKE_CXX_COMPILER=/d/local/llvm-mingw/bin/x86_64-w64-mingw32-g++.exe \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  ..

