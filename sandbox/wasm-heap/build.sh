#!/bin/bash

# Build WASM module with embind and allow_memory_growth

em++ memory_test.cpp \
    -o memory_test.js \
    --bind \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ENVIRONMENT=node \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='MemoryTestModule' \
    -O2

echo "Build complete. Generated files:"
echo "  memory_test.js"
echo "  memory_test.wasm"