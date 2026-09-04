// SPDX-License-Identifier: Apache-2.0
#include <iostream>
#include <string>

#include "cuda/optix_runtime.hh"

int main() {
  lusdview::OptixRuntime runtime;
  std::string err;
  if (!runtime.load(&err)) {
    std::cerr << "SKIP: " << err << "\n";
    return 77;
  }
  if (!runtime.loaded() || !runtime.functionTable() ||
      runtime.abiVersion() <= 0) {
    std::cerr << "OptiX loader returned an invalid function table\n";
    return 1;
  }
  // A null context is rejected without disturbing the successfully loaded
  // function table. A CUDA-backed attachment is covered by viewer GPU tests.
  if (runtime.attachCudaContext(nullptr, &err) || err.empty() ||
      !runtime.loaded()) {
    std::cerr << "OptiX null CUDA-context validation failed\n";
    return 1;
  }
  std::cout << "OptiX runtime ABI " << runtime.abiVersion() << " loaded\n";
  return 0;
}
