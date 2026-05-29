# WINE + clang-cl build of pure Win32/Win64 TinyUSDZ library

TinyUSDZ can be compiled with clang-cl + the MSVC SDK on Linux, and the
resulting pure-Windows binaries can be run on top of WINE — a Windows C/C++
build without a Windows machine.

Build assets:

- `scripts/bootstrap-clang-cl-wsl.sh` — bootstrap helper.
- `cmake/clang-cl-msvc-wsl.cmake` — the CMake toolchain file.

## MSVC SDK license

https://devblogs.microsoft.com/cppblog/updates-to-visual-studio-build-tools-license-for-c-and-cpp-open-source-projects/

The MSVC SDK (Visual Studio Build Tools / Microsoft C++ Build Tools) EULA has
been relaxed so it can be used (grab a copy) for OSS builds without a full
Visual Studio license. The Windows SDK itself does not require a VS license.

## Setup

Download an LLVM prebuilt for your HOST architecture (e.g. Ubuntu x86_64 for an
x64 Linux host) from https://github.com/llvm/llvm-project/releases, and install
the Ninja build tool.

### WSL

On WSL the easy path is to install the Visual Studio Build Tools on the Windows
side: https://visualstudio.microsoft.com/visual-cpp-build-tools/ . You can also
use `msvc-wine` (below).

### Linux (Ubuntu)

https://github.com/mstorsjo/msvc-wine

We recommend `msvc-wine`: it automates downloading and unpacking the MSVC SDK
and the Windows SDK. Note that recent `msvc-wine` lowercases folder/file names.

## Cross compile TinyUSDZ with clang-cl

See `scripts/bootstrap-clang-cl-wsl.sh`. The toolchain invocation looks like:

```
  cd build-clang-cl-wsl
  cmake -G Ninja -DCMAKE_TOOLCHAIN_FILE="cmake/clang-cl-msvc-wsl.cmake" \
    -DHOST_ARCH=x64 \
    -DLLVM_NATIVE_TOOLCHAIN="/path/to/clang+llvm-<ver>-x86_64-linux-gnu/" \
    -DMSVC_BASE:FILEPATH="/mnt/d/VC/Tools/MSVC/14.26.28801/" \
    -DWINSDK_BASE="/mnt/d/winsdk/10/" \
    -DWINSDK_VER="10.0.18362.0" \
    ..
```

Edit `LLVM_NATIVE_TOOLCHAIN`, `MSVC_BASE`, `WINSDK_BASE` (Windows paths work
when cross-compiling on WSL), and `WINSDK_VER` to match your MSVC SDK / Windows
SDK. Because recent `msvc-wine` lowercases names, you may also need to fix some
folder/file names (e.g. `Windows.h -> windows.h`) referenced in
`cmake/clang-cl-msvc-wsl.cmake`.

## Run on WINE

`vcruntime*.dll` and some MSVC/ucrt DLLs must be on the PATH; set it through
`WINEPATH`:

```
$ WINEPATH="%PATH%;C:\path\to\dlls" wine64 ./usda_parser.exe
```
