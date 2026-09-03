rem Assume Python is built with `ci-build-python-lib.bat`
rem

rmdir /s /q build
mkdir build

cmake -G "Visual Studio 17 2022" -A x64 ^
-DLIGHTUSD_WITH_OPENSUBDIV=On ^
-DLIGHTUSD_WITH_PYTHON=1 ^
-DLIGHTUSD_PREFER_LOCAL_PYTHON_INSTALLATION=1 ^
-DPython3_EXECUTABLE=%~dp0/ci/dist/python/Scripts/python.exe ^
-Bbuild -S.
