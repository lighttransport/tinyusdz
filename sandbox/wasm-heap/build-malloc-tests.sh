#!/bin/bash

# Build WASM with different malloc implementations to test memory reuse

echo "Building with different malloc implementations..."

# dlmalloc (default/general-purpose)
echo "Building with dlmalloc..."
em++ memory_test.cpp \
    -o memory_test_dlmalloc.js \
    --bind \
    -s MALLOC=dlmalloc \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ENVIRONMENT=node \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='MemoryTestModule' \
    -O2

# emmalloc (simple and compact)  
echo "Building with emmalloc..."
em++ memory_test.cpp \
    -o memory_test_emmalloc.js \
    --bind \
    -s MALLOC=emmalloc \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ENVIRONMENT=node \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='MemoryTestModule' \
    -O2

# mimalloc (multithreaded allocator)
echo "Building with mimalloc..."
em++ memory_test.cpp \
    -o memory_test_mimalloc.js \
    --bind \
    -s MALLOC=mimalloc \
    -s ALLOW_MEMORY_GROWTH=1 \
    -s EXPORTED_RUNTIME_METHODS='["ccall", "cwrap"]' \
    -s ENVIRONMENT=node \
    -s MODULARIZE=1 \
    -s EXPORT_NAME='MemoryTestModule' \
    -O2

echo "All malloc variants built successfully:"
echo "  dlmalloc: memory_test_dlmalloc.js/.wasm"
echo "  emmalloc: memory_test_emmalloc.js/.wasm" 
echo "  mimalloc: memory_test_mimalloc.js/.wasm"