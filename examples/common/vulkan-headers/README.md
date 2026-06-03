# Vulkan headers (vendored)

Khronos Vulkan API headers, used so tusdview's Vulkan backend builds without a
Vulkan SDK installed. Paired with the vendored [volk](../volk) meta-loader,
which resolves the actual entry points at runtime.

- `include/vulkan/*.h` + `include/vk_video/*.h` — the C registry headers only.
  The C++ bindings (`vulkan.hpp`, `vulkan_*.hpp`, `vulkan.cppm`; ~14 MB) are
  intentionally NOT vendored — volk is pure C and nothing here includes them.
- Version: **VK_HEADER_VERSION 296** (Vulkan 1.3.296)
- License: Apache-2.0 (see the SPDX headers in each file)

Vendored from the Khronos `Vulkan-Headers` repo at tag `vulkan-sdk-1.3.296.0`
(the same lockstep tag as volk). Compiled with
`VK_NO_PROTOTYPES` (set by `vk/vulkan.cmake`) so no entry-point symbols are
referenced at link time.

If you bump this version, keep volk at a release ≤ this header version — see
[../volk/README.md](../volk/README.md).
