rem assume 

rmdir /s /q build
mkdir build

rem Specify absolute python.exe path if required.
rem Use `whre python` to find abosolute path to python.exe in cmd

cmake -G "Visual Studio 17 2022" -A x64 ^
-DTINYUSDZ_WITH_OPENSUBDIV=On ^
-DTINYUSDZ_WITH_PYTHON=1 ^
-DTINYUSDZ_PREFER_LOCAL_PYTHON_INSTALLATION=1 ^
-DPython3_EXECUTABLE=C:\Users\%%HOME%%\miniconda3\envs\pytinyusdz\python.exe
-Bbuild -S.

