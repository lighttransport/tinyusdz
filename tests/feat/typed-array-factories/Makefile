# Makefile for TypedArray Factory Functions Tests
# Standalone test that can be built without CMake

CXX = g++-13
CXXFLAGS = -std=c++14 -Wall -Wextra -O2 -g
INCLUDES = -I../../../src

# Source files
TEST_SRC = test-typed-array-factories.cc
TEST_BIN = test-typed-array-factories

# Build target
all: $(TEST_BIN)

$(TEST_BIN): $(TEST_SRC)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TEST_SRC) -o $(TEST_BIN)
	@echo "Built: $(TEST_BIN)"

# Run tests
test: $(TEST_BIN)
	@echo "Running factory function tests..."
	./$(TEST_BIN)

# Run with verbose output
test-verbose: $(TEST_BIN)
	@echo "Running factory function tests (verbose)..."
	./$(TEST_BIN) -v

# Clean build artifacts
clean:
	rm -f $(TEST_BIN) *.o

# Help target
help:
	@echo "TypedArray Factory Functions Test Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all           - Build test executable (default)"
	@echo "  test          - Build and run tests"
	@echo "  test-verbose  - Build and run tests with verbose output"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Example usage:"
	@echo "  make          # Build"
	@echo "  make test     # Build and run"
	@echo "  make clean    # Clean up"

.PHONY: all test test-verbose clean help
