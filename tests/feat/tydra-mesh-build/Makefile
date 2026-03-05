# Makefile for mesh build benchmark (standalone, no library dependency)
#
# Usage:
#   make              - Build the benchmark executable
#   make run          - Build and run the benchmark
#   make clean        - Remove built artifacts
#

# Compiler settings
CXX ?= g++
CXXFLAGS = -std=c++17 -Wall -O2

# Targets
TARGET = bench_mesh_build
SOURCE = bench_mesh_build.cc

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CXX) $(CXXFLAGS) -o $@ $< -pthread
	@echo "Built $(TARGET)"

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)
