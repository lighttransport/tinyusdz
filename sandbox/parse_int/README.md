# Efficient Integer Array Parser

Based on the efficient float parsing implementation in `../parse_fp`, this is an optimized integer array parser that can handle large arrays with multithreading support.

## Features

- **Fast lexing**: Efficient tokenization of integer arrays in `[1,2,3,...]` format
- **Multithreaded parsing**: Uses `std::from_chars` with thread pool for large arrays
- **Memory efficient**: Zero-copy lexing using spans pointing to original input
- **Robust error handling**: Comprehensive validation and error reporting
- **Configurable**: Support for trailing delimiters and custom separators

## Usage

```bash
make
./parse_int [num_elements] [delim_at_end] [num_threads]
```

### Parameters
- `num_elements`: Number of integers to generate and parse (default: 33554432)
- `delim_at_end`: Allow trailing comma (1=yes, 0=no, default: 1)  
- `num_threads`: Number of threads for parsing (default: 1)

### Examples
```bash
# Parse 1M integers with 4 threads
./parse_int 1000000 1 4

# Parse 10M integers, no trailing comma, single-threaded
./parse_int 10000000 0 1
```

## Architecture

### Two-Phase Parsing
1. **Lexing Phase**: Fast scan through input to identify integer boundaries
   - Returns `int_lex_span` objects with pointer + length
   - Handles whitespace, delimiters, and validation
   - O(n) single pass through input

2. **Parsing Phase**: Convert lexed spans to actual integers
   - Uses fast `std::from_chars` for conversion
   - Automatic multithreading for arrays > 128K elements
   - Thread-safe with atomic counters

### Key Data Structures
- `int_lex_span`: Zero-copy span representing an integer token
- `Lexer`: Stateful lexer with position tracking and error reporting
- Thread pool with work stealing for parsing phase

## Performance Notes

- Optimized for large integer arrays (millions of elements)
- Multithreading kicks in automatically for arrays > 131,072 elements
- Uses `std::from_chars` which is typically faster than `std::stoi` or `atoi`
- Memory usage scales linearly with input size

## TODO

- Add support for different integer types (int32, uint64, etc.)
- Implement vector parsing (e.g., `[(1,2), (3,4)]`)
- Add SIMD optimizations for lexing phase
- Support for hexadecimal and binary integer formats