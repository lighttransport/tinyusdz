"""
Build driver for the tinyusdz PyPI wheel.

Flow:
  1. Invoke CMake on the standalone `src/next` project (next-core), building
     the `tinyusdz_next`, `tydra_next` and `tinyusdz_c` static libraries.
     The legacy tinyusdz core is NOT built or linked.
  2. Compile the `src/python/py-*.c` sources into the `tinyusdz._core`
     extension, linking those static archives.

Two build flavors from the same sources:
  - Regular CPython: stable-ABI (abi3) extension with
    Py_LIMITED_API=0x030A0000; the wheel is tagged cp310-abi3 and covers
    CPython >= 3.10.
  - Free-threaded CPython (Py_GIL_DISABLED, e.g. 3.14t): the limited API is
    unavailable there, so a regular version-specific extension is built
    (wheel tag cp314t-...). The module declares Py_mod_gil=Py_MOD_GIL_NOT_USED.
"""
from __future__ import annotations

import os
import pathlib
import platform
import shutil
import subprocess
import sysconfig
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext


ROOT = pathlib.Path(__file__).parent.resolve()
CMAKE_BUILD_DIR = ROOT / "build_py_ext_next"

PY_LIMITED_API = 0x030A0000  # CPython 3.10 floor for the abi3 wheel

# The stable ABI cannot be used on free-threaded CPython builds.
FREE_THREADED = bool(sysconfig.get_config_var("Py_GIL_DISABLED"))
USE_LIMITED = (not FREE_THREADED and
               os.environ.get("TINYUSDZ_PY_LIMITED_API", "1") != "0")


def _cmake_configure_and_build() -> None:
    CMAKE_BUILD_DIR.mkdir(parents=True, exist_ok=True)

    cmake_args = [
        "cmake",
        "-S", str(ROOT / "src" / "next"),
        "-B", str(CMAKE_BUILD_DIR),
        "-DCMAKE_BUILD_TYPE=MinSizeRel",
        "-DCMAKE_POSITION_INDEPENDENT_CODE=ON",
        "-DTINYUSDZ_NEXT_ENABLE_THREAD=ON",
        "-DTINYUSDZ_NEXT_BUILD_TESTS=OFF",
        "-DTINYUSDZ_NEXT_BUILD_PYTHON=OFF",
    ]
    is_windows = platform.system() == "Windows"

    # On Windows, prefer the native Visual Studio generator so CMake
    # auto-locates MSVC without needing cl.exe on PATH. Elsewhere Ninja is
    # strictly faster if available.
    if not is_windows and shutil.which("ninja") is not None:
        cmake_args.extend(["-G", "Ninja"])
    if is_windows:
        cmake_args.append("-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL")

    extra = os.environ.get("TINYUSDZ_CMAKE_ARGS", "")
    if extra:
        cmake_args.extend(extra.split())

    env = os.environ.copy()
    subprocess.check_call(cmake_args, env=env)

    build_args = [
        "cmake", "--build", str(CMAKE_BUILD_DIR),
        "--config", "MinSizeRel",
        "--target", "tinyusdz_next",
        "--target", "tydra_next",
        "--target", "tinyusdz_c",
        "--parallel", str(os.cpu_count() or 2),
    ]
    subprocess.check_call(build_args, env=env)


def _find_static_lib(stem: str) -> pathlib.Path:
    """Locate a static library built by CMake with variable naming across
    platforms and generator types (multi-config vs single-config)."""
    candidates = [
        CMAKE_BUILD_DIR / f"lib{stem}.a",
        CMAKE_BUILD_DIR / f"{stem}.lib",
        CMAKE_BUILD_DIR / "MinSizeRel" / f"{stem}.lib",
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

        # Link order matters for static archives on POSIX: dependents first.
        c_api = _find_static_lib("tinyusdz_c")
        tydra = _find_static_lib("tydra_next")
        core = _find_static_lib("tinyusdz_next")

        for ext in self.extensions:
            ext.extra_objects = [str(c_api), str(tydra), str(core)] + list(
                ext.extra_objects)
        super().run()


extra_compile_args = []
extra_link_args = []
if platform.system() == "Linux":
    extra_compile_args += ["-O2", "-ffunction-sections", "-fdata-sections"]
    extra_link_args += ["-lstdc++", "-lm", "-lpthread",
                        "-Wl,--gc-sections", "-Wl,-s"]
elif platform.system() == "Darwin":
    extra_compile_args += ["-O2"]
    extra_link_args += ["-lc++", "-Wl,-dead_strip", "-Wl,-x"]
elif platform.system() == "Windows":
    extra_compile_args += ["/O2"]


define_macros = []
if USE_LIMITED:
    define_macros.append(("Py_LIMITED_API", hex(PY_LIMITED_API)))

ext = Extension(
    name="tinyusdz._core",
    sources=[
        "src/python/py-module.c",
        "src/python/py-array.c",
        "src/python/py-prim.c",
        "src/python/py-render.c",
        "src/python/py-stage.c",
    ],
    include_dirs=[
        "src/c-api",
        "src/python",
    ],
    define_macros=define_macros,
    py_limited_api=USE_LIMITED,
    extra_compile_args=extra_compile_args,
    extra_link_args=extra_link_args,
    language="c",
)


options = {}
if USE_LIMITED:
    options["bdist_wheel"] = {"py_limited_api": "cp310"}

setup(
    ext_modules=[ext],
    cmdclass={"build_ext": CMakeBuildExt},
    options=options,
)
