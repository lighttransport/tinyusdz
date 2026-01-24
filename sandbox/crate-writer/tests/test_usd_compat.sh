#!/bin/bash
# SPDX-License-Identifier: Apache 2.0
# Copyright 2025, Light Transport Entertainment Inc.
#
# OpenUSD Compatibility Test Script
#
# Tests that files written by crate-writer can be read by official OpenUSD tools:
# - usdcat (convert to USDA)
# - usddumpcrate (inspect crate structure)
#
# Usage: ./test_usd_compat.sh [path/to/openusd]
#

set -e  # Exit on error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Configuration
TEST_DIR="/tmp/crate_writer_compat_tests"
BUILD_DIR="$(cd "$(dirname "$0")/../build" && pwd)"

# Check if OpenUSD path is provided
if [ -n "$1" ]; then
    USD_ROOT="$1"
else
    # Try to find OpenUSD in aousd directory
    USD_ROOT="$(cd "$(dirname "$0")/../../../aousd/dist_nopython_monolithic" && pwd 2>/dev/null)" || USD_ROOT=""
fi

if [ -z "$USD_ROOT" ] || [ ! -d "$USD_ROOT" ]; then
    echo -e "${YELLOW}WARNING: OpenUSD not found. Please provide path as argument:${NC}"
    echo "  $0 /path/to/openusd"
    echo ""
    echo "Looking for OpenUSD in: $USD_ROOT"
    exit 1
fi

# Set up USD environment
export PATH="$USD_ROOT/bin:$PATH"
export LD_LIBRARY_PATH="$USD_ROOT/lib:$LD_LIBRARY_PATH"
export PYTHONPATH="$USD_ROOT/lib/python:$PYTHONPATH"

# Check if usdcat is available
if ! command -v usdcat &> /dev/null; then
    echo -e "${RED}ERROR: usdcat not found in PATH${NC}"
    echo "OpenUSD tools directory: $USD_ROOT/bin"
    exit 1
fi

echo "============================================"
echo "  OpenUSD Compatibility Tests"
echo "============================================"
echo "OpenUSD path: $USD_ROOT"
echo "usdcat: $(which usdcat)"
echo "Test directory: $TEST_DIR"
echo ""

# Create test directory
mkdir -p "$TEST_DIR"
cd "$TEST_DIR"

# Test counters
TOTAL_TESTS=0
PASSED_TESTS=0

# Function to run a test
run_test() {
    local test_name=$1
    TOTAL_TESTS=$((TOTAL_TESTS + 1))

    echo "----------------------------------------"
    echo "Test: $test_name"
    echo "----------------------------------------"

    # Generate test file using simple_write
    local usdc_file="test_${test_name}.usdc"

    if [ ! -f "$usdc_file" ]; then
        echo -e "${YELLOW}Skipping: Test file not found${NC}"
        return
    fi

    # Test 1: usdcat convert to USDA
    echo -n "  [1/2] usdcat USDC -> USDA... "
    if usdcat "$usdc_file" -o "${usdc_file%.usdc}.usda" 2>&1 | grep -q "Error"; then
        echo -e "${RED}FAILED${NC}"
        return
    else
        echo -e "${GREEN}OK${NC}"
    fi

    # Test 2: usddumpcrate inspect
    if command -v usddumpcrate &> /dev/null; then
        echo -n "  [2/2] usddumpcrate inspect... "
        if usddumpcrate "$usdc_file" > "${usdc_file%.usdc}.dump" 2>&1; then
            echo -e "${GREEN}OK${NC}"
        else
            echo -e "${RED}FAILED${NC}"
            return
        fi
    else
        echo "  [2/2] usddumpcrate: not available (skipped)"
    fi

    echo -e "${GREEN}✓ Test PASSED: $test_name${NC}"
    PASSED_TESTS=$((PASSED_TESTS + 1))
}

# Run round-trip tests to generate test files
echo "Generating test files..."
if [ -f "$BUILD_DIR/test_roundtrip" ]; then
    cd "$TEST_DIR"
    "$BUILD_DIR/test_roundtrip" > /dev/null 2>&1 || true
    echo "Test files generated in $TEST_DIR"
    echo ""
else
    echo -e "${RED}ERROR: test_roundtrip executable not found${NC}"
    echo "Please build the project first: cd build && make test_roundtrip"
    exit 1
fi

# Run compatibility tests on each generated file
for usdc_file in test_*.usdc; do
    if [ -f "$usdc_file" ]; then
        test_name="${usdc_file#test_}"
        test_name="${test_name%.usdc}"
        run_test "$test_name"
    fi
done

# Summary
echo ""
echo "============================================"
echo "  Test Summary"
echo "============================================"
echo "Passed: $PASSED_TESTS / $TOTAL_TESTS"
echo ""

if [ $PASSED_TESTS -eq $TOTAL_TESTS ]; then
    echo -e "${GREEN}✓ All OpenUSD compatibility tests PASSED${NC}"
    exit 0
else
    echo -e "${RED}✗ Some OpenUSD compatibility tests FAILED${NC}"
    exit 1
fi
