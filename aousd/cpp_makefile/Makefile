# Makefile for building C++ application with OpenUSD and TinyUSDZ
# Requires: OpenUSD built in ../dist and TinyUSDZ built in ../../build

# Compiler settings
CXX = clang++-20
CC = clang-20

# Project name
TARGET = usd_comparison

# Directories
OPENUSD_ROOT = ../dist
TINYUSDZ_ROOT = ../..
TINYUSDZ_BUILD = $(TINYUSDZ_ROOT)/build

# Include paths
INCLUDES = \
	-I$(OPENUSD_ROOT)/include \
	-I$(OPENUSD_ROOT)/include/boost-1_82 \
	-I$(TINYUSDZ_ROOT)/src \
	-I$(TINYUSDZ_ROOT)/src/external

# Compiler flags
CXXFLAGS = -std=c++17 -O2 -g -Wall -Wno-deprecated -Wno-deprecated-declarations \
	-DPXR_PYTHON_ENABLED \
	$(INCLUDES)

# Linker flags and libraries
LDFLAGS = -L$(OPENUSD_ROOT)/lib -L$(TINYUSDZ_BUILD)

# OpenUSD libraries (order matters!)
USD_LIBS = \
	-lusd \
	-lusdGeom \
	-lusdShade \
	-lusdLux \
	-lusdSkel \
	-lusdVol \
	-lusdProc \
	-lusdRender \
	-lusdHydra \
	-lusdRi \
	-lsdf \
	-lpcp \
	-lkind \
	-lusdUtils \
	-lgf \
	-ltf \
	-ljs \
	-lts \
	-lwork \
	-lplug \
	-ltrace \
	-larch \
	-lvt \
	-lar

# TinyUSDZ library
TINYUSDZ_LIBS = -ltinyusdz_static

# System libraries
SYS_LIBS = -ltbb -lpthread -ldl -lm

# All libraries
LIBS = $(USD_LIBS) $(TINYUSDZ_LIBS) $(SYS_LIBS)

# Source files
SRCS = main.cpp
OBJS = $(SRCS:.cpp=.o)

# Build rules
.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(OBJS) -o $(TARGET) $(LDFLAGS) $(LIBS)
	@echo "Build complete: $(TARGET)"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

run: $(TARGET)
	LD_LIBRARY_PATH=$(OPENUSD_ROOT)/lib:$$LD_LIBRARY_PATH ./$(TARGET)

# Debug target to print variables
debug:
	@echo "CXX: $(CXX)"
	@echo "OPENUSD_ROOT: $(OPENUSD_ROOT)"
	@echo "TINYUSDZ_BUILD: $(TINYUSDZ_BUILD)"
	@echo "INCLUDES: $(INCLUDES)"
	@echo "LIBS: $(LIBS)"

# Help target
help:
	@echo "Usage:"
	@echo "  make        - Build the application"
	@echo "  make clean  - Remove build artifacts"
	@echo "  make run    - Build and run the application"
	@echo "  make debug  - Print build variables"
	@echo "  make help   - Show this help message"