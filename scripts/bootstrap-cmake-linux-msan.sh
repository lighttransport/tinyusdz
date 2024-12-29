curdir=`pwd`

builddir=${curdir}/build_msan

rm -rf ${builddir}
mkdir ${builddir}


# path to libcxx(built with msan)
LIBCXX_MSAN_DIR=${curdir}/llvm_project_msan_dist

# Specify sanitizer flag directly to CXX compiler

export CXX="clang++-18 -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer "
export CC="clang-18 -fsanitize=memory -fsanitize-memory-track-origins -fno-omit-frame-pointer "

#
# Use isystem for custom built libcxx to suppress clang's warnings.
#

cd ${builddir} && cmake \
  -DCMAKE_TOOLCHAIN_FILE=cmake/lld-linux.toolchain.cmake \
  -DCMAKE_CXX_FLAGS="-fsanitize=memory -fno-omit-frame-pointer -fsanitize-memory-track-origins -isystem ${LIBCXX_MSAN_DIR}/include -isystem ${LIBCXX_MSAN_DIR}/include/c++/v1 -nostdinc++ " \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,-rpath,${LIBCXX_MSAN_DIR}/lib -lc++ -lc++abi -lunwind " \
  -DCMAKE_AR=/usr/bin/llvm-ar \
  -DCMAKE_VERBOSE_MAKEFILE=1 \
  -DTINYUSDZ_WITH_OPENSUBDIV=0 \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DTINYUSDZ_MSAN=1 \
  -DTINYUSDZ_WITH_C_API=1 \
  -DTINYUSDZ_WITH_BUILTIN_IMAGE_LOADER=0 \
  -DTINYUSDZ_WITH_EXR=0 \
  -DTINYUSDZ_WITH_TIFF=0 \
  -DTINYUSDZ_WITH_PYTHON=0 \
  -DTINYUSDZ_WITH_JSON=0 \
  -DTINYUSDZ_WITH_USDMTLX=0 \
  -DTINYUSDZ_WITH_USDOBJ=0 \
  -DTINYUSDZ_WITH_USDVOX=0 \
  -DTINYUSDZ_WITH_USDFBX=0 \
  -DTINYUSDZ_BUILD_TESTS=1 \
  -DTINYUSDZ_BUILD_BENCHMARKS=0 \
  -DTINYUSDZ_COMPILE_TIME_TRACE=0 \
  -DTINYUSDZ_DEBUG_PRINT=0 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  ..

