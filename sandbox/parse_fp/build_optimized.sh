#!/bin/bash

# Build optimized parser
echo "Building optimized float parser..."
g++ -O3 -std=c++17 -I../../src/external/fast_float/include \
    parse_fp_optimized.cc -o parse_fp_optimized \
    -march=native -ffast-math

if [ $? -eq 0 ]; then
    echo "Build successful!"
    echo ""
    echo "Running tests..."
    echo "==============="
    
    # Test float array
    echo "Test 1: Float array (16K elements)"
    ./parse_fp_optimized 16384 1 0
    echo ""
    
    # Test float2 array
    echo "Test 2: Float2 array (8K elements)"
    ./parse_fp_optimized 8192 1 1
    echo ""
    
    # Test float3 array
    echo "Test 3: Float3 array (8K elements)"
    ./parse_fp_optimized 8192 1 2
    echo ""
    
    # Test float4 array
    echo "Test 4: Float4 array (8K elements)"
    ./parse_fp_optimized 8192 1 3
    echo ""
    
    # Test special values
    echo "Test 5: Special values (inf, nan, etc.)"
    ./parse_fp_optimized 10 1 4
    echo ""
    
    # Test large array with custom chunk size
    echo "Test 6: Large float array (100K elements, 32K chunk size)"
    ./parse_fp_optimized 100000 1 0 32768
    
else
    echo "Build failed!"
    exit 1
fi