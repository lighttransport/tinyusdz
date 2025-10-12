# Makefile for ValueView test

CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I../../../ -I../../../src
LDFLAGS =

# Source files
TEST_SRC = test_value_view.cc

# Output binary
TARGET = test_value_view

.PHONY: all clean test run

all: $(TARGET)

$(TARGET): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

test: run

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET) *.o

help:
	@echo "Available targets:"
	@echo "  make       - Build the test program"
	@echo "  make test  - Build and run the test"
	@echo "  make run   - Run the test (same as 'make test')"
	@echo "  make clean - Remove built files"
	@echo "  make help  - Show this help message"