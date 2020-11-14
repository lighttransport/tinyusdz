rm -rf build
mkdir build

cd build

CXX=clang++ CC=clang cmake \
   -DCMAKE_BUILD_TYPE=Debug \
   -DEXAMPLE_USD_DIR="~/work/USD-build-aarch64/dist" \
   ..
