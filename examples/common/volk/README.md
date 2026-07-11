# volk (vendored)

[volk](https://github.com/zeux/volk) is a meta-loader for Vulkan. tusdview uses
it to resolve the Vulkan API at runtime (dlopen `libvulkan.so.1` / `vulkan-1.dll`)
instead of linking the Vulkan SDK loader — the cuew/GLEW approach. This lets
tusdview build with no Vulkan SDK present and run wherever a Vulkan ICD/loader
is installed.

- Source: https://github.com/zeux/volk
- Version: **vulkan-sdk-1.3.296.0** (`VOLK_HEADER_VERSION 296`)
- License: MIT (see `LICENSE.md`)

## Version pin — keep volk ≤ the bundled Vulkan headers

volk's version **must not exceed** the Vulkan headers in
`../vulkan-headers/include` (currently `VK_HEADER_VERSION 296`, Vulkan 1.3.296).
volk.h declares every entry point of its version as a `PFN_*` function pointer;
if volk is *newer* than the headers it references `PFN_*` typedefs the headers
don't define, and the build fails to compile. A volk that is the same age or
*older* than the headers is always safe (it just loads a subset). volk and the
headers are vendored from the same `vulkan-sdk-1.3.296.0` lockstep tag, so they
match exactly.

If you upgrade the bundled Vulkan headers, you may bump volk to a matching (or
older) release.

## Usage

Built into the `tusdview` target by `vk/vulkan.cmake` with `VK_NO_PROTOTYPES`.
`vk_renderer.cc` calls `volkInitialize()` then `volkLoadInstance(instance)` /
`volkLoadDevice(device)`. imgui's Vulkan backend dispatches through the same
pointers via `IMGUI_IMPL_VULKAN_USE_VOLK`.
