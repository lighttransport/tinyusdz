#!/bin/bash

# Test runner script for float parsing tests
# This script runs various test scenarios and generates a report

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo "================================================"
echo "String to Float Parsing Test Suite Runner"
echo "================================================"

# Function to run a test and check result
run_test() {
    local test_name=$1
    local test_cmd=$2

    echo -e "\n${YELLOW}Running: ${test_name}${NC}"
    echo "Command: ${test_cmd}"
    echo "----------------------------------------"

    if eval "${test_cmd}"; then
        echo -e "${GREEN}✓ ${test_name} PASSED${NC}"
        return 0
    else
        echo -e "${RED}✗ ${test_name} FAILED${NC}"
        return 1
    fi
}

# Check if binaries exist
if [ ! -f "test_parse_fp_comprehensive" ] || [ ! -f "test_exhaustive_float32" ]; then
    echo -e "${YELLOW}Building test programs...${NC}"
    make -f Makefile.test all
fi

# Initialize test counters
TOTAL_TESTS=0
PASSED_TESTS=0
FAILED_TESTS=0

# Run tests
echo -e "\n${YELLOW}Starting test suite...${NC}\n"

# Test 1: Edge cases and special values
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Edge Cases Test" "./test_parse_fp_comprehensive --random 1000"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 2: Random floats (medium)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Random Floats Test (10K)" "./test_parse_fp_comprehensive --random 10000"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 3: Random floats (large)
TOTAL_TESTS=$((TOTAL_TESTS + 1))
if run_test "Random Floats Test (100K)" "./test_parse_fp_comprehensive --random 100000"; then
    PASSED_TESTS=$((PASSED_TESTS + 1))
else
    FAILED_TESTS=$((FAILED_TESTS + 1))
fi

# Test 4: Exhaustive quick test (optional, takes a few minutes)
read -p "Run exhaustive quick test (100M patterns, ~5 minutes)? [y/N] " -n 1 -r
echo
if [[ $REPLY =~ ^[Yy]$ ]]; then
    TOTAL_TESTS=$((TOTAL_TESTS + 1))
    if run_test "Exhaustive Quick Test (100M)" "./test_exhaustive_float32 --quick --threads 8"; then
        PASSED_TESTS=$((PASSED_TESTS + 1))
    else
        FAILED_TESTS=$((FAILED_TESTS + 1))
    fi
fi

# Final report
echo ""
echo "================================================"
echo "Test Suite Summary"
echo "================================================"
echo -e "Total tests run: ${TOTAL_TESTS}"
echo -e "Passed: ${GREEN}${PASSED_TESTS}${NC}"
echo -e "Failed: ${RED}${FAILED_TESTS}${NC}"

if [ $FAILED_TESTS -eq 0 ]; then
    echo -e "\n${GREEN}✓ All tests passed!${NC}"
    exit 0
else
    echo -e "\n${RED}✗ Some tests failed!${NC}"
    exit 1
fi