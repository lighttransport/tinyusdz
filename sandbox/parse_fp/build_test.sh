#!/bin/bash

echo "Building ASCII parser test with optimization enabled..."
echo "======================================================="

# Build with TINYUSDZ_PARSER_OPT enabled
g++ -O3 -std=c++14 \
    -I../../src \
    -I../../src/external \
    test_ascii_parser_opt.cc \
    ../../src/ascii-parser-basetype.cc \
    ../../src/io-util.cc \
    -o test_ascii_parser_opt \
    -march=native

if [ $? -eq 0 ]; then
    echo "✓ Build successful with optimization enabled!"
    echo ""
    echo "Running test..."
    echo "==============="
    ./test_ascii_parser_opt
else
    echo "✗ Build failed!"
    exit 1
fi

echo ""
echo "Building ASCII parser test with optimization disabled..."
echo "========================================================"

# Build without TINYUSDZ_PARSER_OPT (comment out the define)
sed 's/#define TINYUSDZ_PARSER_OPT/\/\/ #define TINYUSDZ_PARSER_OPT/' test_ascii_parser_opt.cc > test_ascii_parser_noopt.cc

g++ -O3 -std=c++14 \
    -I../../src \
    -I../../src/external \
    test_ascii_parser_noopt.cc \
    ../../src/ascii-parser-basetype.cc \
    ../../src/io-util.cc \
    -o test_ascii_parser_noopt \
    -march=native

if [ $? -eq 0 ]; then
    echo "✓ Build successful with optimization disabled!"
    echo ""
    echo "Running test..."
    echo "==============="
    ./test_ascii_parser_noopt
    
    echo ""
    echo "Comparing optimized vs non-optimized performance:"
    echo "================================================"
    echo "Optimized version:"
    time ./test_ascii_parser_opt > /dev/null
    echo ""
    echo "Non-optimized version:"
    time ./test_ascii_parser_noopt > /dev/null
    
else
    echo "✗ Build failed!"
    exit 1
fi