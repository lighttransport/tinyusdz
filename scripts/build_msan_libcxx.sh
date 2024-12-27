# Reference
# https://stackoverflow.com/questions/56454026/building-libc-with-memorysanitizer-instrumentation-fails-due-to-memorysanitize

git clone --depth=1 https://github.com/llvm/llvm-project -b llvmorg-19.1.0

build_dir=libcxx_msan

rm -rf ${build_dir}
mkdir -p ${build_dir}

#
# Assume clang(17 or later required) is installed on your system.
#

CXX=clang++-18 CC=clang-18 cmake -G Ninja -DCMAKE_INSTALL_PREFIX=${build_dir}/dist -B ${build_dir} -S llvm-project/runtimes/ -DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi;libunwind" -DLLVM_USE_SANITIZER=Memory -DCMAKE_BUILD_TYPE=Release && \
ninja -C ${build_dir} && \
ninja -C ${build_dir} install \

