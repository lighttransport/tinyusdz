# Simple Makefile for TinyUSDZ C API
# For quick building without CMake

CC = gcc
CXX = g++
AR = ar

# Flags
CFLAGS = -std=c99 -Wall -Wextra -O2 -fPIC
CXXFLAGS = -std=c++14 -Wall -Wextra -O2 -fPIC
LDFLAGS = -shared

# Paths
TINYUSDZ_ROOT = ../..
TINYUSDZ_SRC = $(TINYUSDZ_ROOT)/src

# Include paths
INCLUDES = -I. -I$(TINYUSDZ_SRC) -I$(TINYUSDZ_SRC)/external

# Output files
LIB_SHARED = libtinyusdz_c.so
LIB_STATIC = libtinyusdz_c.a

# Source files
C_API_SRC = tinyusdz_c.cpp

# TinyUSDZ sources (simplified list - add more as needed)
TINYUSDZ_SRCS = \
    $(TINYUSDZ_SRC)/tinyusdz.cc \
    $(TINYUSDZ_SRC)/stage.cc \
    $(TINYUSDZ_SRC)/prim-types.cc \
    $(TINYUSDZ_SRC)/value-types.cc \
    $(TINYUSDZ_SRC)/usdGeom.cc \
    $(TINYUSDZ_SRC)/usdShade.cc \
    $(TINYUSDZ_SRC)/usdSkel.cc \
    $(TINYUSDZ_SRC)/usda-reader.cc \
    $(TINYUSDZ_SRC)/usdc-reader.cc \
    $(TINYUSDZ_SRC)/crate-reader.cc \
    $(TINYUSDZ_SRC)/ascii-parser.cc \
    $(TINYUSDZ_SRC)/asset-resolution.cc \
    $(TINYUSDZ_SRC)/composition.cc \
    $(TINYUSDZ_SRC)/prim-reconstruct.cc \
    $(TINYUSDZ_SRC)/path.cc \
    $(TINYUSDZ_SRC)/str-util.cc \
    $(TINYUSDZ_SRC)/io-util.cc \
    $(TINYUSDZ_SRC)/math-util.cc \
    $(TINYUSDZ_SRC)/tiny-format.cc

# Object files
C_API_OBJ = $(C_API_SRC:.cpp=.o)
TINYUSDZ_OBJS = $(TINYUSDZ_SRCS:.cc=.o)

# Example programs
EXAMPLES = example_basic example_mesh

# Default target
all: $(LIB_SHARED) $(LIB_STATIC)

# Build shared library
$(LIB_SHARED): $(C_API_OBJ) $(TINYUSDZ_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Build static library
$(LIB_STATIC): $(C_API_OBJ) $(TINYUSDZ_OBJS)
	$(AR) rcs $@ $^

# Build C API implementation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build TinyUSDZ sources
%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build examples
examples: $(LIB_STATIC) $(EXAMPLES)

example_basic: example_basic.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_STATIC) -lstdc++ -lm

example_mesh: example_mesh.c $(LIB_STATIC)
	$(CC) $(CFLAGS) -o $@ $< $(LIB_STATIC) -lstdc++ -lm

# Clean
clean:
	rm -f $(C_API_OBJ) $(TINYUSDZ_OBJS)
	rm -f $(LIB_SHARED) $(LIB_STATIC)
	rm -f $(EXAMPLES)

# Install (requires root/sudo)
PREFIX ?= /usr/local
install: $(LIB_SHARED) $(LIB_STATIC)
	install -d $(PREFIX)/lib
	install -d $(PREFIX)/include/tinyusdz
	install -m 644 $(LIB_SHARED) $(PREFIX)/lib/
	install -m 644 $(LIB_STATIC) $(PREFIX)/lib/
	install -m 644 tinyusdz_c.h $(PREFIX)/include/tinyusdz/

# Uninstall
uninstall:
	rm -f $(PREFIX)/lib/$(LIB_SHARED)
	rm -f $(PREFIX)/lib/$(LIB_STATIC)
	rm -f $(PREFIX)/include/tinyusdz/tinyusdz_c.h
	rmdir $(PREFIX)/include/tinyusdz 2>/dev/null || true

# Test
test: examples
	@echo "Running basic example..."
	./example_basic ../../models/simple_mesh.usda || true
	@echo ""
	@echo "Running mesh example..."
	./example_mesh ../../models/simple_mesh.usda || true

# Help
help:
	@echo "TinyUSDZ C API Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all         - Build shared and static libraries (default)"
	@echo "  examples    - Build example programs"
	@echo "  test        - Run example programs"
	@echo "  clean       - Remove built files"
	@echo "  install     - Install libraries and headers"
	@echo "  uninstall   - Remove installed files"
	@echo ""
	@echo "Variables:"
	@echo "  CC          - C compiler (default: gcc)"
	@echo "  CXX         - C++ compiler (default: g++)"
	@echo "  PREFIX      - Install prefix (default: /usr/local)"
	@echo ""
	@echo "Examples:"
	@echo "  make                    - Build libraries"
	@echo "  make examples           - Build libraries and examples"
	@echo "  make test               - Build and run examples"
	@echo "  sudo make install       - Install to system"

.PHONY: all examples clean install uninstall test help