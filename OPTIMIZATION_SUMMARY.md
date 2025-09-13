# Float Array Parser Optimization

## Overview
Optimized the float and double array parsing in TinyUSDZ's ASCII parser to minimize memory allocations and small C++ object creation.

## Changes Made

### Files Modified
- `src/ascii-parser-basetype.cc`: Implemented `ParseFloatArrayOptimized()` and `ParseDoubleArrayOptimized()`
- `src/ascii-parser.hh`: Updated function declarations and documentation

### Key Optimizations

1. **Fixed-Size Buffer for Number Parsing**
   - Uses a stack-allocated `char buffer[128]` instead of `std::string`
   - Avoids dynamic memory allocation for each number
   - No string concatenation overhead

2. **Direct Character-by-Character Parsing**
   - Processes input stream directly without building intermediate strings
   - Minimizes object creation and destruction

3. **Pre-allocated Vector Capacity**
   - Reserves initial capacity (64 elements) to reduce vector reallocations
   - Particularly beneficial for typical USD array sizes

4. **Efficient State Machine**
   - Simple boolean flags track parsing state
   - Handles whitespace, comments, and special values (inf, nan) efficiently

## Implementation Details

```cpp
// Key optimization: Fixed-size buffer instead of dynamic string
constexpr size_t BUFFER_SIZE = 128;
char buffer[BUFFER_SIZE];
size_t buffer_pos = 0;

// Direct parsing without intermediate allocations
while (!Eof()) {
    char c;
    if (!Char1(&c)) return false;
    
    // Process character directly into buffer
    if (is_number_char(c)) {
        buffer[buffer_pos++] = c;
    } else if (c == ',' || c == ']') {
        // Parse accumulated buffer
        buffer[buffer_pos] = '\0';
        float value = std::strtof(buffer, &end_ptr);
        result->push_back(value);
        buffer_pos = 0;
    }
}
```

## Benefits

1. **Reduced Memory Allocations**
   - No `std::string` allocations per number
   - No repeated string concatenations
   - Fixed-size buffer on stack

2. **Better Cache Locality**
   - Stack buffer stays in cache
   - Sequential access pattern

3. **Lower Overhead**
   - Fewer constructor/destructor calls
   - Less memory fragmentation
   - Reduced GC pressure (if applicable)

## Compatibility

- Maintains full compatibility with existing USD files
- Handles all valid float formats:
  - Regular decimals: `1.0`, `-3.14`
  - Scientific notation: `1.0e-5`, `2.3e4`
  - Special values: `inf`, `-inf`, `nan`
- Properly handles comments and whitespace

## Testing

Created test programs to verify:
1. Correctness with various input formats
2. Empty arrays, single elements, large arrays
3. Special values and edge cases
4. Performance comparison with allocation-heavy approach

## Integer Array Optimizations

### Enhanced ParseIntArrayOptimized() and ParseInt64ArrayOptimized()

Building on the float/double optimizations, the integer array parsers now implement:

1. **Two-Phase Parsing**
   - Phase 1: Lexing to count commas and estimate array size
   - Phase 2: Actual parsing with pre-allocated memory

2. **Accurate Pre-allocation**
   - Counts commas during lexing phase
   - Reserves exact capacity (comma_count + 1) upfront
   - Eliminates most vector reallocations

3. **Chunked Memory Growth**
   - Configurable chunk size (default: 16384 elements)
   - Reduces allocation overhead for very large arrays
   - Better memory locality
   - Optimized for production workloads with large datasets
   - Can be adjusted via `AsciiParser::SetArrayParseChunkSize()`

4. **Implementation Details**
```cpp
// Phase 1: Count commas for size estimation
size_t comma_count = 0;
while (!Eof() && !found_end) {
    if (c == ',') comma_count++;
    // ... scan through array
}

// Pre-allocate based on estimation
size_t estimated_size = comma_count + 1;
result->reserve(estimated_size);

// Phase 2: Parse with chunked allocation
const size_t CHUNK_SIZE = _array_parse_chunk_size;  // Configurable, default 16384
if (items_parsed % CHUNK_SIZE == 0 && result->capacity() < estimated_size) {
    result->reserve(result->capacity() + CHUNK_SIZE);
}

// Usage: Configure chunk size for specific needs
AsciiParser parser;
parser.SetArrayParseChunkSize(32768);  // Use 32K chunks for very large arrays
```

### Performance Benefits

1. **Reduced Allocations**
   - Single upfront allocation for most arrays
   - Chunked growth for very large arrays
   - No per-element allocation overhead

2. **Better Cache Performance**
   - Sequential memory access patterns
   - Reduced memory fragmentation
   - Predictable allocation behavior

3. **Optimized for Common Cases**
   - Small arrays: Single allocation
   - Medium arrays: Exact pre-allocation
   - Large arrays: Efficient chunked growth

## Future Work

Similar optimizations could be applied to:
- Other numeric array types (half, uint32, float2, float3, etc.)
- String array parsing (with string interning)
- Matrix array parsing

## Notes

The optimization is particularly effective for:
- Large arrays (1000+ elements)
- Frequently parsed files
- Memory-constrained environments
- Real-time applications requiring predictable performance

The template specialization mechanism ensures the optimized path is used automatically when parsing `std::vector<float>` and `std::vector<double>` arrays.