#!/bin/bash

# Build WASM module with memory pool

em++ memory_pool.cpp \
    -o memory_pool.js \
    --bind \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ENVIRONMENT=node \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='MemoryPoolModule' \
    -O2

echo "Memory pool build complete. Generated files:"
echo "  memory_pool.js"
echo "  memory_pool.wasm"