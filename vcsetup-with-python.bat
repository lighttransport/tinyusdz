rem assume

rmdir /s /q build_with_py
mkdir build_with_py

rem Specify absolute python.exe path if required.
rem Use `whre python` to find abosolute path to python.exe in cmd
rem Conda/Miniconda installed Python recommended.
rem
rem -DPython3_EXECUTABLE=C:\Users\%%HOME%%\miniconda3\envs\pylightusd\python.exe ^

cmake -G "Visual Studio 17 2022" -A x64 ^
-DLIGHTUSD_WITH_OPENSUBDIV=On ^
-DLIGHTUSD_WITH_PYTHON=1 ^
-DLIGHTUSD_PREFER_LOCAL_PYTHON_INSTALLATION=1 ^
-Bbuild_with_py -S.

