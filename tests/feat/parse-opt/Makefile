# Makefile for parse optimization benchmark

CXX = clang++
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra
INCLUDES = -I../../../src
LDFLAGS = -L../../../build -ltinyusdz_static -ldl -lpthread

TARGET = test-parse-opt
SOURCES = test-parse-opt.cc
OBJECTS = $(SOURCES:.cc=.o)

# Library dependency
LIBTINYUSDZ = ../../../build/libtinyusdz_static.a

all: $(TARGET)

$(TARGET): $(OBJECTS) $(LIBTINYUSDZ)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(LIBTINYUSDZ):
	@echo "Building libtinyusdz_static.a..."
	@cd ../../../build && $(MAKE)

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
