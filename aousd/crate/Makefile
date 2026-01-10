# Makefile for USD Crate Examples
# Supports both standard and monolithic USD builds

# Detect build type from environment or use standard by default
# Set MONOLITHIC=1 to use monolithic build
MONOLITHIC ?= 0

# USD installation paths
ifeq ($(MONOLITHIC), 1)
    USD_ROOT ?= $(CURDIR)/../dist_nopython_monolithic
    BUILD_TYPE = monolithic
else
    USD_ROOT ?= $(CURDIR)/../dist_nopython
    BUILD_TYPE = standard
endif

# Compiler settings
CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -Wno-deprecated
INCLUDES = -I$(USD_ROOT)/include
LDFLAGS = -L$(USD_ROOT)/lib -Wl,-rpath,$(USD_ROOT)/lib

# USD libraries
ifeq ($(MONOLITHIC), 1)
    # Monolithic build - single library
    USD_LIBS = -lusd_ms -ltbb
else
    # Standard build - multiple libraries
    USD_LIBS = -lusd -lusdGeom -lsdf -ltf -lvt -lgf -lar -larch -lplug -ltrace -lwork -ltbb
endif

# System libraries
SYS_LIBS = -lpthread -ldl

# All libraries
LIBS = $(USD_LIBS) $(SYS_LIBS)

# Build directory
BUILD_DIR = build
SRC_DIR = src

# Source files
SOURCES = $(wildcard $(SRC_DIR)/*.cpp)
TARGETS = $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%,$(SOURCES))

# Default target
all: check-usd $(BUILD_DIR) $(TARGETS)
	@echo ""
	@echo "========================================="
	@echo "Build complete ($(BUILD_TYPE))!"
	@echo "========================================="
	@echo "Executables in: $(BUILD_DIR)/"
	@echo "  - crate_reader"
	@echo "  - crate_writer"
	@echo "  - crate_internal_api"
	@echo ""

# Check if USD installation exists
check-usd:
	@if [ ! -d "$(USD_ROOT)" ]; then \
		echo "Error: USD installation not found at: $(USD_ROOT)"; \
		echo "Please build OpenUSD first or set USD_ROOT"; \
		exit 1; \
	fi
	@echo "========================================="
	@echo "Building USD Crate Examples"
	@echo "========================================="
	@echo "USD_ROOT: $(USD_ROOT)"
	@echo "Build type: $(BUILD_TYPE)"
	@echo "Compiler: $(CXX)"
	@echo ""

# Create build directory
$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Pattern rule for building executables
$(BUILD_DIR)/%: $(SRC_DIR)/%.cpp
	@echo "Building $@ ..."
	$(CXX) $(CXXFLAGS) $(INCLUDES) $< -o $@ $(LDFLAGS) $(LIBS)

# Individual targets
crate_reader: $(BUILD_DIR)/crate_reader
crate_writer: $(BUILD_DIR)/crate_writer
crate_internal_api: $(BUILD_DIR)/crate_internal_api

# Clean
clean:
	@echo "Cleaning build artifacts..."
	rm -rf $(BUILD_DIR)
	@echo "Clean complete"

# Help
help:
	@echo "USD Crate Examples Makefile"
	@echo ""
	@echo "Usage:"
	@echo "  make              - Build all examples (standard USD build)"
	@echo "  make MONOLITHIC=1 - Build with monolithic USD library"
	@echo "  make clean        - Remove build artifacts"
	@echo "  make help         - Show this help"
	@echo ""
	@echo "Individual targets:"
	@echo "  make crate_reader"
	@echo "  make crate_writer"
	@echo "  make crate_internal_api"
	@echo ""
	@echo "Environment variables:"
	@echo "  USD_ROOT     - USD installation directory"
	@echo "  MONOLITHIC   - Set to 1 for monolithic build"
	@echo "  CXX          - C++ compiler (default: g++)"
	@echo ""
	@echo "Examples:"
	@echo "  make"
	@echo "  make MONOLITHIC=1"
	@echo "  make CXX=clang++"
	@echo "  make USD_ROOT=/custom/usd/path"

# Phony targets
.PHONY: all check-usd clean help crate_reader crate_writer crate_internal_api
