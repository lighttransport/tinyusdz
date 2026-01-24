# TinyUSDZ Project Overview

This document provides a comprehensive overview of the TinyUSDZ project, a C++14 library for handling USDZ, USDC, and USDA files. It is designed to be secure, portable, and dependency-free.

## Building and Running

The project uses CMake for building. Here are the key commands for building, running, and testing the project:

### Building the C++ library

To build the C++ library, you can use the following commands:

```bash
mkdir build
cd build
cmake ..
make
```

### Building the Python bindings

The Python bindings can be built using `scikit-build`.

```bash
python -m build .
```

Or, for development:

```bash
python setup.py build
```

### Running the examples

The project includes several examples in the `examples/` directory. For example, to run the `tusdcat` example, you can use the following command:

```bash
./build/examples/tusdcat/tusdcat <input_file>
```

### Running the tests

To run the tests, you can use the following command:

```bash
ctest --test-dir build
```

## Development Conventions

*   **Branching:** The `dev` branch is used for development. Pull requests should be submitted to this branch.
*   **Coding Style:** The project uses `.clang-format` to enforce a consistent coding style.
*   **Testing:** The project uses CTest for testing. Tests are located in the `tests/` directory.

## Project Structure

*   `src/`: The source code for the TinyUSDZ library.
*   `python/`: The Python bindings for the TinyUSDZ library.
*   `examples/`: Example applications that use the TinyUSDZ library.
*   `tests/`: Tests for the TinyUSDZ library.
*   `doc/`: Documentation for the TinyUSDZ library.
*   `models/`: Example USD models.
*   `cmake/`: CMake modules.
*   `external/`: Third-party dependencies.
