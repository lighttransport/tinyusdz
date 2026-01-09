# SPDX-License-Identifier: Apache 2.0
# Makefile for TypedArray TimeSamples feature test
#
# This test can be built standalone without CMake
#

# Compiler settings
CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
INCLUDES = -I../../../src -I../../../src/external

# TinyUSDZ root directory (3 levels up from this Makefile)
TINYUSDZ_ROOT = ../../..
BUILD_DIR = $(TINYUSDZ_ROOT)/build

# Use the pre-built static library
TINYUSDZ_LIB = $(BUILD_DIR)/libtinyusdz_static.a

# Test source
TEST_SOURCE = test-typed-array-timesamples.cc

# Output binary
TARGET = test-typed-array-timesamples

# Default target
all: check-lib $(TARGET)

# Check if library exists
check-lib:
	@if [ ! -f "$(TINYUSDZ_LIB)" ]; then \
		echo "Error: TinyUSDZ library not found at $(TINYUSDZ_LIB)"; \
		echo "Please build TinyUSDZ first:"; \
		echo "  cd $(TINYUSDZ_ROOT) && mkdir -p build && cd build && cmake .. && make"; \
		exit 1; \
	fi

# Link the executable
$(TARGET): $(TEST_SOURCE)
	@echo "Building $(TARGET)..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(TINYUSDZ_LIB) -pthread
	@echo "✓ Build successful! Run with: ./$(TARGET)"

# Clean build artifacts
clean:
	rm -f $(TARGET)
	@echo "✓ Cleaned build artifacts"

# Run the test
test: $(TARGET)
	@echo "Running $(TARGET)..."
	@./$(TARGET)

# Help target
help:
	@echo "TypedArray TimeSamples Feature Test"
	@echo ""
	@echo "Targets:"
	@echo "  make          - Build the test binary"
	@echo "  make test     - Build and run the test"
	@echo "  make clean    - Remove build artifacts"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - C++14 compatible compiler"
	@echo "  - TinyUSDZ source tree (../../.. from this directory)"
	@echo ""
	@echo "This test verifies that TypedArray<T> can be used with"
	@echo "TimeSamples for memory-efficient deduplication of array data."

.PHONY: all clean test help
