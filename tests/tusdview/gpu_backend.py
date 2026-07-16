"""Is tusdview's renderer a software rasterizer?

The GL screenshot tests below cannot run on a software GL stack. Under Mesa's
llvmpipe (what you get from Xvfb or an X11-forwarded DISPLAY, neither of which
has DRI) tusdview's mesh draw fetches only `aPosition` correctly: every other
vertex attribute -- aNormal, aUV, aUV1, the skin joints/weights -- reads back as
zero, and even `gl_VertexID` reads 0, while the very same VAO/VBO state renders
correctly on hardware GL (radeonsi) and on Vulkan. So a texture samples uv (0,0)
and a GPU-skinned vertex gets no joints: the render is wrong for reasons that
have nothing to do with the feature under test, and a failure here says nothing
about the code.

Rather than assert on garbage, the callers treat a software renderer as "this
backend cannot answer the question" and move on to a backend that can (Vulkan,
which is hardware here), or skip.

tusdview prints its device at startup, e.g.
  [tusdview] renderer: OpenGL, GPU: Mesa llvmpipe (LLVM 20.1.8, 256 bits), ...
  [tusdview] renderer: Vulkan, GPU: AMD Radeon RX 9070 XT (RADV GFX1201), ...
"""

import os
import re

_NVIDIA_GLVND_JSON = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json"


def nvidia_offload_env(base=None):
    """Environment for the Xvfb fallback, with NVIDIA PRIME render-offload.

    Xvfb has no DRI, so Mesa hands out llvmpipe even when the host has a
    discrete NVIDIA GPU. When the NVIDIA driver is installed, the GLVND
    render-offload variables route the GL context to the hardware device
    instead (doc/tusdview.md, "Headless HW-accelerated GL"). Without the
    driver the variables are NOT set -- forcing the nvidia GLX vendor on a
    non-NVIDIA host breaks GL outright -- so other hosts keep llvmpipe and
    the callers' software-renderer skip. Explicitly exported values win.
    """
    env = dict(os.environ if base is None else base)
    if os.path.exists("/proc/driver/nvidia/version"):
        env.setdefault("__NV_PRIME_RENDER_OFFLOAD", "1")
        env.setdefault("__GLX_VENDOR_LIBRARY_NAME", "nvidia")
        if os.path.exists(_NVIDIA_GLVND_JSON):
            env.setdefault("__EGL_VENDOR_LIBRARY_FILENAMES", _NVIDIA_GLVND_JSON)
    return env


_SOFTWARE = re.compile(
    r"llvmpipe|softpipe|swrast|SwiftShader|lavapipe|Software Rasterizer",
    re.IGNORECASE,
)


def is_software_renderer(viewer_output):
    """True when tusdview's startup banner names a software device."""
    if not viewer_output:
        return False
    for line in viewer_output.splitlines():
        if "renderer:" in line and _SOFTWARE.search(line):
            return True
    return False


def device_name(viewer_output):
    """The `GPU: ...` field of tusdview's banner (for log messages).

    The device string itself contains commas ("Mesa llvmpipe (LLVM 20.1.8, 256
    bits)"), so it runs to the next banner field rather than to the next comma.
    """
    if not viewer_output:
        return "unknown"
    for line in viewer_output.splitlines():
        if "renderer:" in line:
            m = re.search(r"GPU:\s*(.+?)(?:,\s*API:|$)", line)
            if m:
                return m.group(1).strip()
    return "unknown"
