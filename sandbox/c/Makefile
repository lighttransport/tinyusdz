CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -g
LDFLAGS = 

SOURCES = usda_parser.c test_parser.c
HEADERS = usda_parser.h
OBJECTS = $(SOURCES:.c=.o)
TARGET = test_parser

.PHONY: all clean test

all: $(TARGET)

$(TARGET): usda_parser.o test_parser.o
	$(CC) $(LDFLAGS) -o $@ $^

usda_parser.o: usda_parser.c usda_parser.h
	$(CC) $(CFLAGS) -c -o $@ usda_parser.c

test_parser.o: test_parser.c usda_parser.h
	$(CC) $(CFLAGS) -c -o $@ test_parser.c

clean:
	rm -f $(OBJECTS) $(TARGET)

test: $(TARGET)
	@echo "Testing with embedded example:"
	./$(TARGET)
	@echo ""
	@echo "Testing with sample USDA files (if available):"
	@if [ -f ../../models/simple.usda ]; then \
		echo "Found ../../models/simple.usda"; \
		./$(TARGET) ../../models/simple.usda; \
	else \
		echo "No sample USDA files found in ../../models/"; \
	fi

# Debug build
debug: CFLAGS += -DDEBUG -O0
debug: $(TARGET)

# Minimal build with size optimization
minimal: CFLAGS = -std=c99 -Os -DNDEBUG
minimal: $(TARGET)

# Static analysis
analyze:
	@if command -v cppcheck >/dev/null 2>&1; then \
		cppcheck --std=c99 --enable=all --suppress=missingIncludeSystem $(SOURCES); \
	else \
		echo "cppcheck not found, skipping static analysis"; \
	fi

# Memory check (requires valgrind)
memcheck: $(TARGET)
	@if command -v valgrind >/dev/null 2>&1; then \
		valgrind --leak-check=full --show-leak-kinds=all ./$(TARGET); \
	else \
		echo "valgrind not found, skipping memory check"; \
	fi

help:
	@echo "Available targets:"
	@echo "  all      - Build the test parser (default)"
	@echo "  clean    - Remove built files"
	@echo "  test     - Run tests with embedded and file examples"
	@echo "  debug    - Build with debug symbols and no optimization"
	@echo "  minimal  - Build with size optimization"
	@echo "  analyze  - Run static analysis (requires cppcheck)"
	@echo "  memcheck - Run memory leak check (requires valgrind)"
	@echo "  help     - Show this help message"