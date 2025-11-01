# Makefile for MaterialX export test (standalone build)
#
# Usage:
#   make              - Build the test executable
#   make run          - Build and run the test
#   make clean        - Remove built artifacts
#

# Directories
ROOT_DIR = ../../..
SRC_DIR = $(ROOT_DIR)/src
INCLUDE_DIR = $(ROOT_DIR)/include
EXTERNAL_DIR = $(ROOT_DIR)/external
BUILD_DIR = $(ROOT_DIR)/build

# Compiler settings
CXX ?= g++
CXXFLAGS = -std=c++14 -Wall -O2
INCLUDES = -I$(SRC_DIR) \
           -I$(INCLUDE_DIR) \
           -I$(EXTERNAL_DIR)/jsonhpp

# Library settings
LDFLAGS = -L$(BUILD_DIR)
LIBS = -ltinyusdz_static -pthread

# Detect platform-specific libraries
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LIBS += -ldl
endif
ifeq ($(UNAME_S),Darwin)
    # macOS specific libraries if needed
endif

# Targets
TARGET_EXPORT = test_materialx_export
SOURCE_EXPORT = threejs_mtlx_export_example.cc

TARGET_IMPORT = test_mtlx_import
SOURCE_IMPORT = test_mtlx_import.cc

TARGETS = $(TARGET_EXPORT) $(TARGET_IMPORT)

# Build rules
.PHONY: all clean run run-import run-export check-library

all: check-library $(TARGETS)

$(TARGET_EXPORT): $(SOURCE_EXPORT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $< $(LDFLAGS) $(LIBS)
	@echo "Built $(TARGET_EXPORT)"

$(TARGET_IMPORT): $(SOURCE_IMPORT)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTINYUSDZ_USE_USDMTLX -o $@ $< $(LDFLAGS) $(LIBS)
	@echo "Built $(TARGET_IMPORT)"

run-export: $(TARGET_EXPORT)
	@echo "Running MaterialX export test..."
	@./$(TARGET_EXPORT)

run-import: $(TARGET_IMPORT)
	@echo "Running MaterialX import test..."
	@./$(TARGET_IMPORT)

run: run-export run-import

check-library:
	@if [ ! -f "$(BUILD_DIR)/libtinyusdz_static.a" ]; then \
		echo "ERROR: libtinyusdz_static.a not found in $(BUILD_DIR)"; \
		echo "Please build the main library first:"; \
		echo "  cd $(ROOT_DIR) && mkdir -p build && cd build && cmake .. && make"; \
		exit 1; \
	fi

clean:
	rm -f $(TARGETS) *.o *.mtlx

help:
	@echo "MaterialX Export Test Makefile"
	@echo ""
	@echo "Available targets:"
	@echo "  make          - Build the test executable"
	@echo "  make run      - Build and run the test"
	@echo "  make clean    - Remove built artifacts"
	@echo "  make help     - Show this help message"
	@echo ""
	@echo "Requirements:"
	@echo "  - TinyUSDZ library must be built first ($(BUILD_DIR)/libtinyusdz_static.a)"
	@echo "  - C++14 compatible compiler"
