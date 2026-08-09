// SPDX-License-Identifier: Apache-2.0
// Windows-only: loads the CUDA/HIP trace kernel source out of the RCDATA
// resource embedded by raytracer_kernel_resource.rc (see that file and
// CMakeLists.txt for why MSVC uses a resource instead of a string literal).
#include "kernel_resource.hh"

#include <windows.h>

namespace tusdview {

const std::string& GetEmbeddedKernelSource() {
  static const std::string src = [] {
    HMODULE mod = GetModuleHandleA(nullptr);
    HRSRC res = FindResourceA(mod, MAKEINTRESOURCEA(IDR_RAYTRACER_KERNEL), RT_RCDATA);
    if (!res) return std::string();
    HGLOBAL data = LoadResource(mod, res);
    if (!data) return std::string();
    const char* ptr = static_cast<const char*>(LockResource(data));
    const DWORD size = SizeofResource(mod, res);
    if (!ptr || size == 0) return std::string();
    return std::string(ptr, size);
  }();
  return src;
}

}  // namespace tusdview
