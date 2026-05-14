"""
Build driver for the tinyusdz PyPI wheel.

Flow:
  1. Invoke CMake on the top-level tinyusdz project with the C API enabled,
     Python / Tydra / examples / tests disabled, Position-Independent-Code on.
  2. Build only the `tinyusdz_static` and `c-tinyusd_static` targets.
  3. Compile `src/python/module.c` as a CPython stable-ABI (abi3) extension,
     linking against the two static archives.

The resulting wheel is tagged `cp310-abi3-<platform>` and is compatible with
CPython >= 3.10.
"""
from __future__ import annotations

import os
import pathlib
import platform
import shutil
import subprocess
import sys
import sysconfig
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


ROOT = pathlib.Path(__file__).parent.resolve()
CMAKE_BUILD_DIR = ROOT / "build_py_ext"

PY_LIMITED_API = 0x030B0000  # 3.11 (buffer protocol in stable ABI)


def _ext_suffix_abi3() -> str:
    """Return the ABI3 suffix setuptools will assign on this platform."""
    # setuptools handles .abi3.so / .pyd naming when py_limited_api=True.
    # We don't need to force it ourselves.
    return sysconfig.get_config_var("EXT_SUFFIX") or ""


def _cmake_configure_and_build() -> None:
    if CMAKE_BUILD_DIR.exists():
        # Incremental build is fine; CMake detects changes.
        pass
    CMAKE_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cmake_args = [
        "cmake",
        "-S", str(ROOT),
        "-B", str(CMAKE_BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DTINYUSDZ_WITH_C_API=ON",
        "-DTINYUSDZ_WITH_TYDRA=ON",
        "-DTINYUSDZ_WITH_PYTHON=OFF",
        "-DTINYUSDZ_BUILD_EXAMPLES=OFF",
        "-DTINYUSDZ_BUILD_TESTS=OFF",
        "-DTINYUSDZ_WITH_TOOL_USDA_PARSER=OFF",
        "-DTINYUSDZ_WITH_TOOL_USDC_PARSER=OFF",
    ]
    is_windows = platform.system() == "Windows"

    # On Windows, prefer the native Visual Studio generator so CMake
    # auto-locates MSVC without needing cl.exe on PATH. Using Ninja here
    # would require running inside a vcvarsall shell, which the PyPA
    # build frontend does not set up when it invokes cmake. Elsewhere
    # (Linux / macOS) Ninja is strictly faster if it is available.
    if not is_windows and shutil.which("ninja") is not None:
        cmake_args.extend(["-G", "Ninja"])

    if is_windows:
        cmake_args.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")

    env = os.environ.copy()
    # Respect MACOSX_DEPLOYMENT_TARGET from cibuildwheel if set.
    subprocess.check_call(cmake_args, env=env)

    build_args = [
        "cmake", "--build", str(CMAKE_BUILD_DIR),
        "--config", "Release",
        "--target", "tinyusdz_static",
        "--target", "c-tinyusd_static",
        "--parallel", str(os.cpu_count() or 2),
    ]
    subprocess.check_call(build_args, env=env)


def _find_static_lib(stem: str) -> pathlib.Path:
    """Locate a static library built by CMake with variable naming across
    platforms and generator types (multi-config vs single-config)."""
    candidates = [
        CMAKE_BUILD_DIR / f"lib{stem}.a",
        CMAKE_BUILD_DIR / f"{stem}.lib",
        CMAKE_BUILD_DIR / "Release" / f"{stem}.lib",
    ]
    candidates += list(CMAKE_BUILD_DIR.rglob(f"lib{stem}.a"))
    candidates += list(CMAKE_BUILD_DIR.rglob(f"{stem}.lib"))
    for c in candidates:
        if c.is_file():
            return c
    raise FileNotFoundError(
        f"Could not locate static library for target '{stem}' under "
        f"{CMAKE_BUILD_DIR}. Candidates tried: {candidates}")


class CMakeBuildExt(build_ext):
    """Drive CMake to produce static libs, then compile the C extension."""

    def run(self) -> None:
        _cmake_configure_and_build()

        tinyusdz_lib = _find_static_lib("tinyusdz_static")
        c_api_lib = _find_static_lib("c-tinyusd_static")

        for ext in self.extensions:
            # Order matters for static archives on POSIX: the C API layer
            # depends on the core, so it must come first.
            ext.extra_objects = [str(c_api_lib), str(tinyusdz_lib)] + list(ext.extra_objects)
        super().run()


extra_compile_args = []
extra_link_args = []
if platform.system() == "Linux":
    extra_link_args += ["-lstdc++", "-lm", "-lpthread"]
    extra_compile_args += ["-O2"]
elif platform.system() == "Darwin":
    extra_link_args += ["-lc++"]
    extra_compile_args += ["-O2"]
elif platform.system() == "Windows":
    # MSVC: stdc++ is linked implicitly for .cpp objects in the static lib.
    extra_compile_args += ["/O2"]


ext = Extension(
    name="tinyusdz._core",
    sources=["src/python/module.c"],
    include_dirs=[
        "src",
    ],
    define_macros=[
        ("Py_LIMITED_API", hex(PY_LIMITED_API)),
    ],
    py_limited_api=True,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    language="c",
)


setup(
    ext_modules=[ext],
    cmdclass={"build_ext": CMakeBuildExt},
    options={
        "bdist_wheel": {"py_limited_api": "cp311"},
    },
)
