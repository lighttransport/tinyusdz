# Crate Reader Refactoring Guide

## Overview
This document describes the refactoring of `crate-reader.cc` from a monolithic 6800-line file into a modular, maintainable architecture.

## New Architecture

The refactored design splits functionality into focused modules:

### 1. **crate-io.{hh,cc}** - Basic I/O Operations
- `ReadIndex()`, `ReadIndices()`
- `ReadString()`, `ReadValueRep()`
- `ReadReference()`, `ReadPayload()`, `ReadLayerOffset()`
- `ReadPathArray()`, `ReadVariantSelectionMap()`
- `ReadCustomData()`
- ListOp reading functions

### 2. **crate-array-reader.{hh,cc}** - Array Reading Operations
- `ReadIntArray()`, `ReadIntArrayTyped()`
- `ReadHalfArray()`, `ReadFloatArray()`, `ReadDoubleArray()`
- `ReadFloatArrayTyped()`, `ReadDoubleArrayTyped()`
- `ReadDoubleVector()`, `ReadTimeSamples()`
- `ReadStringArray()`
- `ReadCompressedInts()`
- Generic array reading templates

### 3. **crate-value-unpacker.{hh,cc}** - Value Unpacking
- `UnpackInlinedValueRep()`
- `UnpackValueRep()`
- `UnpackArrayValue()`
- Dictionary unpacking
- TimeSamples unpacking
- PathVector unpacking
- VariantSelectionMap unpacking
- CustomData unpacking

### 4. **crate-path-decoder.{hh,cc}** - Path Operations
- `ReadCompressedPaths()`
- `BuildDecompressedPathsImpl()` (recursive version)
- `BuildDecompressedPathsImplIterative()` (iterative version)
- `BuildNodeHierarchy()` (recursive version)
- `BuildNodeHierarchyIterative()` (iterative version)
- Node class for scene graph hierarchy

### 5. **crate-section-reader.{hh,cc}** - Section Reading
- `ReadBootStrap()`, `ReadTOC()`
- `ReadSection()`
- `ReadTokens()`, `ReadStrings()`
- `ReadFields()`, `ReadFieldSets()`
- `ReadSpecs()`, `ReadPaths()`
- `BuildLiveFieldSets()`
- Progress reporting

### 6. **crate-reader-refactored.{hh,cc}** - Main Coordinator
- Manages all modules
- Provides public API
- Coordinates data flow between modules
- Maintains backward compatibility

## Migration Steps

### Step 1: Extract Functions by Category
Move functions from `crate-reader.cc` to appropriate module files:

```cpp
// Original in crate-reader.cc
bool CrateReader::ReadString(std::string *s) { ... }

// Move to crate-io.cc
bool CrateIOHelper::ReadString(std::string *s) { ... }
```

### Step 2: Update Function Signatures
Adjust function signatures to work with module classes:

```cpp
// Original
bool CrateReader::ReadIndex(crate::Index *i) {
  // Direct access to _sr, _err, etc.
}

// Refactored
bool CrateIOHelper::ReadIndex(crate::Index *i) {
  // Access via passed StreamReader* and local error string
}
```

### Step 3: Handle Cross-Module Dependencies
Some functions need access to data from other modules:

```cpp
// In CrateValueUnpacker
bool UnpackValueRep(const crate::ValueRep& rep, ...) {
  // Needs access to tokens, strings, etc. from CrateReader
  // Solution: Pass CrateReader* to constructor
}
```

### Step 4: Update CrateReader Implementation
```cpp
// crate-reader-refactored.cc
CrateReader::CrateReader(StreamReader *sr, const CrateReaderConfig &config)
    : _sr(sr), memory_manager_(config.maxMemoryBudget) {
  InitializeModules();
}

void CrateReader::InitializeModules() {
  io_helper_ = std::make_unique<CrateIOHelper>(_sr, memory_manager_);
  array_reader_ = std::make_unique<CrateArrayReader>(_sr, memory_manager_);
  value_unpacker_ = std::make_unique<CrateValueUnpacker>(this, _sr, memory_manager_);
  path_decoder_ = std::make_unique<CratePathDecoder>(this, memory_manager_);
  section_reader_ = std::make_unique<CrateSectionReader>(this, _sr, memory_manager_);
}

bool CrateReader::ReadTokens() {
  return section_reader_->ReadTokens();
}
```

## Key Considerations

### Memory Management
- Each module receives a reference to `MemoryBudget& memory_manager_`
- Memory checks remain consistent: `CHECK_MEMORY_USAGE()` and `REDUCE_MEMORY_USAGE()`

### Error Handling
- Each module maintains its own error/warning strings
- Main CrateReader aggregates errors from all modules

### Data Access Patterns
- Modules that need parsed data (tokens, strings, paths) receive `CrateReader*`
- Use friend classes for controlled access to private members

### Thread Safety
- Keep threading logic in main CrateReader
- Modules should be stateless where possible

## Benefits of Refactoring

1. **Improved Maintainability**: Each module is focused on a specific concern
2. **Better Testability**: Individual modules can be unit tested
3. **Enhanced Readability**: ~1000 lines per file vs 6800 lines
4. **Easier Debugging**: Issues can be isolated to specific modules
5. **Parallel Development**: Multiple developers can work on different modules
6. **Code Reuse**: Modules can potentially be used in other contexts

## Implementation Checklist

- [x] Create header files for all modules
- [x] Create example implementation (crate-io.cc)
- [ ] Migrate all ReadXXX functions to appropriate modules
- [ ] Migrate UnpackXXX functions to crate-value-unpacker
- [ ] Migrate path-related functions to crate-path-decoder
- [ ] Migrate section reading to crate-section-reader
- [ ] Update CMakeLists.txt to include new files
- [ ] Run comprehensive tests
- [ ] Update documentation

## Testing Strategy

1. **Unit Tests**: Create tests for each module
2. **Integration Tests**: Ensure modules work together correctly
3. **Regression Tests**: Verify existing USD files parse correctly
4. **Performance Tests**: Ensure no performance degradation
5. **Memory Tests**: Verify memory budget enforcement

## Gradual Migration Path

If immediate full refactoring is not feasible:

1. Start by creating the module files alongside existing code
2. Gradually move functions one module at a time
3. Use preprocessor flags to switch between old and new implementations
4. Once all modules are complete, remove old code

```cpp
#ifdef USE_REFACTORED_CRATE_READER
  #include "crate-reader-refactored.hh"
  using CrateReaderImpl = CrateReader;
#else
  #include "crate-reader.hh"
  using CrateReaderImpl = crate::CrateReader;
#endif
```

## Next Steps

1. Review and approve the proposed architecture
2. Complete implementation of remaining modules
3. Migrate existing code to new modules
4. Update build system
5. Run comprehensive test suite
6. Performance profiling and optimization
7. Documentation update