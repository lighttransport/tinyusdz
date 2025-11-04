# Installation Guide

## Quick Install (Local User)

```bash
# Build and install to local directory (no root needed)
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=$HOME/.local ..
make
make install
```

Headers will be in: `$HOME/.local/include/crate/`
Library will be in: `$HOME/.local/lib/`

## System-Wide Install (if you have permissions)

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/usr/local ..
make
make install
```

## Custom Install Location

```bash
mkdir build && cd build
cmake -DCMAKE_INSTALL_PREFIX=/path/to/your/location ..
make
make install
```

## In-Source Install (Default)

If you don't specify CMAKE_INSTALL_PREFIX, files install to:
```
sandbox/path-sort-and-encode-crate/install/
├── include/crate/
└── lib/
```

```bash
mkdir build && cd build
cmake ..  # Installs to ../install by default
make
make install
```

## Using Installed Library

### CMake

```cmake
# Add to your CMakeLists.txt
find_library(CRATE_ENCODING crate-encoding
    HINTS $ENV{HOME}/.local/lib)

find_path(CRATE_ENCODING_INCLUDE crate/path_interface.hh
    HINTS $ENV{HOME}/.local/include)

target_link_libraries(your_target ${CRATE_ENCODING})
target_include_directories(your_target PUBLIC ${CRATE_ENCODING_INCLUDE})
```

### Compiler Flags

```bash
g++ -std=c++17 \
    -I$HOME/.local/include \
    -L$HOME/.local/lib \
    -lcrate-encoding \
    your_code.cc -o your_app
```

## No Installation Required

You can also use the library without installing:

### Copy Files Directly

```bash
# Copy to your project
cp -r include/crate /your/project/include/
cp src/*.cc /your/project/src/

# Add to your build
g++ -std=c++17 -I/your/project/include \
    /your/project/src/path_sort.cc \
    /your/project/src/tree_encode.cc \
    your_code.cc -o your_app
```

### Use as Git Submodule

```bash
cd your_project
git submodule add <repo-url> third_party/crate-encoding
```

In CMakeLists.txt:
```cmake
add_subdirectory(third_party/crate-encoding)
target_link_libraries(your_app crate-encoding)
```

## Uninstall

```bash
cd build
cat install_manifest.txt | xargs rm
```

Or manually remove:
```bash
rm -rf $HOME/.local/include/crate
rm -f $HOME/.local/lib/libcrate-encoding.a
```
