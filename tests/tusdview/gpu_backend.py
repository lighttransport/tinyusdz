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
import shutil
import subprocess

_NVIDIA_GLVND_JSON = "/usr/share/glvnd/egl_vendor.d/10_nvidia.json"

_gpu_vendor_cache = "unprobed"


def detect_gpu():
    """The GPU vendor whose kernel driver is loaded: "nvidia", "amd", or None.

    - "nvidia": the proprietary driver is up (/proc/driver/nvidia/version).
    - "amd": the amdgpu KMS driver is loaded (/sys/module/amdgpu), or the ROCm
      compute node exists (/dev/kfd -- amdkfd, what HIP enumerates).
    - None: no hardware GPU driver; GL/Vulkan can only be software.

    Driver presence, not device health: a wedged GPU still detects. That is
    fine for the tests here -- the render either works or fails/skips loudly.
    """
    global _gpu_vendor_cache
    if _gpu_vendor_cache == "unprobed":
        if os.path.exists("/proc/driver/nvidia/version"):
            _gpu_vendor_cache = "nvidia"
        elif os.path.exists("/sys/module/amdgpu") or os.path.exists("/dev/kfd"):
            _gpu_vendor_cache = "amd"
        else:
            _gpu_vendor_cache = None
    return _gpu_vendor_cache


def vk_device_args(backend):
    """Extra tusdview args selecting the hardware Vulkan device, when present.

    Inside Xvfb (or a sandboxed session) the default Vulkan device can be
    llvmpipe/lavapipe even though the hardware ICD enumerates fine -- explicit
    selection routes the backend to the hardware device (doc/tusdview.md,
    "Vulkan on NVIDIA PRIME/offload under Xvfb"). tusdview's --vk-device does
    a case-insensitive substring match over device name + driver, so "nvidia"
    and "amd" (RADV reports "AMD Radeon ...") each select the right ICD. On a
    host with a real display this is a no-op (the hardware device would win
    the auto-selection anyway); with no GPU driver no args are added, and the
    default (possibly software) device keeps the callers' skip behavior.
    """
    vendor = detect_gpu()
    if backend in ("vk", "vulkan") and vendor:
        return ["--vk-device", vendor]
    return []


def gpu_offload_env(base=None):
    """Environment for the Xvfb fallback, with GL routed to the hardware GPU.

    Xvfb has no DRI, so Mesa hands out llvmpipe regardless of the installed
    GPU. On NVIDIA, the GLVND PRIME render-offload variables route the GL
    context to the hardware device anyway (doc/tusdview.md, "Headless
    HW-accelerated GL"). AMD has no equivalent escape hatch -- Mesa's radeonsi
    needs DRI on the X server itself -- so there (and with no GPU) the
    environment is returned unchanged and GL under Xvfb stays llvmpipe, which
    the callers already detect and skip. Forcing the nvidia GLX vendor on a
    non-NVIDIA host would break GL outright, hence the vendor gate; explicitly
    exported values win (setdefault).
    """
    env = dict(os.environ if base is None else base)
    if detect_gpu() == "nvidia":
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


def software_only_vulkan():
    """True when vulkaninfo proves every visible Vulkan device is a CPU.

    Mesa's llvmpipe can advertise ray-query extensions but take minutes (or
    never complete) on even tiny RT captures. Driver-presence checks are not
    enough here: a host may have a kernel GPU module loaded while the sandbox
    exposes only the CPU ICD. Return False when vulkaninfo is unavailable or
    inconclusive so a real/virtual device still gets the normal render probe.
    """
    vulkaninfo = shutil.which("vulkaninfo")
    if not vulkaninfo:
        return False
    try:
        r = subprocess.run(
            [vulkaninfo, "--summary"], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, timeout=10, check=False)
    except (OSError, subprocess.TimeoutExpired):
        return False
    output = r.stdout.decode(errors="replace")
    if r.returncode != 0:
        return False
    has_cpu = "PHYSICAL_DEVICE_TYPE_CPU" in output
    has_non_cpu = any(kind in output for kind in (
        "PHYSICAL_DEVICE_TYPE_DISCRETE_GPU",
        "PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU",
        "PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU",
        "PHYSICAL_DEVICE_TYPE_OTHER",
    ))
    return has_cpu and not has_non_cpu
