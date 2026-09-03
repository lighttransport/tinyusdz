# Makefile for parse optimization benchmark

CXX = clang++
CXXFLAGS = -std=c++14 -O2 -g -Wall -Wextra
INCLUDES = -I../../../src
LDFLAGS = -L../../../build -llightusd_static -ldl -lpthread

TARGET = test-parse-opt
SOURCES = test-parse-opt.cc
OBJECTS = $(SOURCES:.cc=.o)

# Library dependency
LIBLIGHTUSD = ../../../build/liblightusd_static.a

all: $(TARGET)

$(TARGET): $(OBJECTS) $(LIBLIGHTUSD)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.cc
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(LIBLIGHTUSD):
	@echo "Building liblightusd_static.a..."
	@cd ../../../build && $(MAKE)

clean:
	rm -f $(OBJECTS) $(TARGET)

run: $(TARGET)
	./$(TARGET)

.PHONY: all clean run
