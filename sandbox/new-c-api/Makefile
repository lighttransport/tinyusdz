# Simple Makefile for LightUSD C API
# For quick building without CMake

CC = gcc
CXX = g++
AR = ar

# Flags
CFLAGS = -std=c99 -Wall -Wextra -O2 -fPIC
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -fPIC
LDFLAGS = -shared

# Paths
LIGHTUSD_ROOT = ../..
LIGHTUSD_SRC = $(LIGHTUSD_ROOT)/src

# Include paths
INCLUDES = -I. -I$(LIGHTUSD_SRC) -I$(LIGHTUSD_SRC)/external

# Output files
LIB_SHARED = liblightusd_c.so
LIB_STATIC = liblightusd_c.a

# Source files
C_API_SRC = lightusd_c.cpp

# LightUSD sources (simplified list - add more as needed)
LIGHTUSD_SRCS = \
    $(LIGHTUSD_SRC)/lightusd.cc \
    $(LIGHTUSD_SRC)/stage.cc \
    $(LIGHTUSD_SRC)/prim-types.cc \
    $(LIGHTUSD_SRC)/value-types.cc \
    $(LIGHTUSD_SRC)/usdGeom.cc \
    $(LIGHTUSD_SRC)/usdShade.cc \
    $(LIGHTUSD_SRC)/usdSkel.cc \
    $(LIGHTUSD_SRC)/usda-reader.cc \
    $(LIGHTUSD_SRC)/usdc-reader.cc \
    $(LIGHTUSD_SRC)/crate-reader.cc \
    $(LIGHTUSD_SRC)/ascii-parser.cc \
    $(LIGHTUSD_SRC)/asset-resolution.cc \
    $(LIGHTUSD_SRC)/composition.cc \
    $(LIGHTUSD_SRC)/prim-reconstruct.cc \
    $(LIGHTUSD_SRC)/path.cc \
    $(LIGHTUSD_SRC)/str-util.cc \
    $(LIGHTUSD_SRC)/io-util.cc \
    $(LIGHTUSD_SRC)/math-util.cc \
    $(LIGHTUSD_SRC)/tiny-format.cc

# Object files
C_API_OBJ = $(C_API_SRC:.cpp=.o)
LIGHTUSD_OBJS = $(LIGHTUSD_SRCS:.cc=.o)

# Example programs
EXAMPLES = example_basic example_mesh

# Default target
all: $(LIB_SHARED) $(LIB_STATIC)

# Build shared library
$(LIB_SHARED): $(C_API_OBJ) $(LIGHTUSD_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $^

# Build static library
$(LIB_STATIC): $(C_API_OBJ) $(LIGHTUSD_OBJS)
	$(AR) rcs $@ $^

# Build C API implementation
%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build LightUSD sources
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
	rm -f $(C_API_OBJ) $(LIGHTUSD_OBJS)
	rm -f $(LIB_SHARED) $(LIB_STATIC)
	rm -f $(EXAMPLES)

# Install (requires root/sudo)
PREFIX ?= /usr/local
install: $(LIB_SHARED) $(LIB_STATIC)
	install -d $(PREFIX)/lib
	install -d $(PREFIX)/include/lightusd
	install -m 644 $(LIB_SHARED) $(PREFIX)/lib/
	install -m 644 $(LIB_STATIC) $(PREFIX)/lib/
	install -m 644 lightusd_c.h $(PREFIX)/include/lightusd/

# Uninstall
uninstall:
	rm -f $(PREFIX)/lib/$(LIB_SHARED)
	rm -f $(PREFIX)/lib/$(LIB_STATIC)
	rm -f $(PREFIX)/include/lightusd/lightusd_c.h
	rmdir $(PREFIX)/include/lightusd 2>/dev/null || true

# Test
test: examples
	@echo "Running basic example..."
	./example_basic ../../models/simple_mesh.usda || true
	@echo ""
	@echo "Running mesh example..."
	./example_mesh ../../models/simple_mesh.usda || true

# Help
help:
	@echo "LightUSD C API Makefile"
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