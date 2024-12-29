# Reference
# https://stackoverflow.com/questions/56454026/building-libc-with-memorysanitizer-instrumentation-fails-due-to-memorysanitize

git clone --depth=1 https://github.com/llvm/llvm-project -b llvmorg-19.1.0

dist_dir=llvm_project_msan_dist
libcxx_build_dir=libcxx_msan
libc_build_dir=libc_msan

rm -rf ${build_dir}
mkdir -p ${build_dir}

#
# Assume clang(17 or later required) is installed on your system.
#

CXX=clang++-18 CC=clang-18 cmake -G Ninja -DCMAKE_INSTALL_PREFIX=${dist_dir} -B ${libcxx_build_dir} -S llvm-project/runtimes/ -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" -DLLVM_USE_SANITIZER=MemoryWithOrigins -DCMAKE_BUILD_TYPE=Release && \
ninja -C ${libcxx_build_dir} && \
ninja -C ${libcxx_build_dir} install 

#
# TODO:  build libc without msan
#
#CXX=clang++-18 CC=clang-18 cmake -G Ninja -DCMAKE_INSTALL_PREFIX=${dist_dir} -B ${libc_build_dir} -S llvm-project/runtimes/ -DLLVM_ENABLE_RUNTIMES="libc" -DCMAKE_BUILD_TYPE=Release && \
#ninja -C ${libc_build_dir} && \
#ninja -C ${libc_build_dir} install 
