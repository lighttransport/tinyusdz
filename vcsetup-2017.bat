rmdir /s /q build
mkdir build

cmake -G "Visual Studio 15 2017" -A x64 -DTINYUSDZ_WITH_OPENSUBDIV=On -Bbuild -H.
