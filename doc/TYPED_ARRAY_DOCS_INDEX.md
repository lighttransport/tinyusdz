# TypedArray Factory Functions - Documentation Index

This is the complete documentation for the proposed TypedArray factory functions that provide cleaner, more intuitive interfaces for creating TypedArray instances for deduplication, mmap, and owned use cases.

## 📚 Documentation Files

### 1. [TYPED_ARRAY_API_SUMMARY.md](TYPED_ARRAY_API_SUMMARY.md) (5.5K)
**Quick reference guide**

- Function quick reference tables
- Common patterns and usage
- When to use each function
- Design principles
- Comparison: old vs new API

**Start here** if you want a quick overview!

---

### 2. [TYPED_ARRAY_FACTORY_PROPOSAL.md](TYPED_ARRAY_FACTORY_PROPOSAL.md) (6.4K)
**Detailed proposal document**

- Problem statement
- Proposed solution with code examples
- Factory function specifications
- Benefits and rationale
- Migration path
- Naming conventions
- Discussion questions

**Read this** for the full rationale and design decisions.

---

### 3. [typed-array-factories.hh](typed-array-factories.hh) (8.2K)
**Reference implementation**

- Complete, ready-to-use factory functions
- Comprehensive documentation comments
- Usage examples in comments
- All proposed functions implemented
- Optional short-name aliases

**Copy from here** to integrate into `src/typed-array.hh`!

---

### 4. [TYPED_ARRAY_MIGRATION_EXAMPLES.md](TYPED_ARRAY_MIGRATION_EXAMPLES.md) (8.4K)
**Practical migration guide**

- 8 detailed before/after examples
- Real code from crate-reader.cc
- Memory-mapped file examples
- Deduplication cache patterns
- Summary comparison table
- Migration strategy

**Use this** when updating existing code!

---

### 5. [TYPED_ARRAY_ARCHITECTURE.md](TYPED_ARRAY_ARCHITECTURE.md) (15K)
**Architecture deep dive**

- Visual architecture diagrams
- Factory function mapping
- Data flow examples
- Memory ownership decision tree
- Bit layout explanation
- Comparison matrix

**Read this** for deep understanding of the architecture!

---

## 🚀 Quick Start

### For Quick Reference
1. Read: **TYPED_ARRAY_API_SUMMARY.md**
2. Copy functions from: **typed-array-factories.hh**
3. Start using in your code!

### For Complete Understanding
1. Read: **TYPED_ARRAY_FACTORY_PROPOSAL.md** (why?)
2. Read: **TYPED_ARRAY_ARCHITECTURE.md** (how?)
3. Study: **TYPED_ARRAY_MIGRATION_EXAMPLES.md** (examples)
4. Integrate: **typed-array-factories.hh** (code)
5. Reference: **TYPED_ARRAY_API_SUMMARY.md** (cheat sheet)

---

## 🎯 Use Cases Quick Lookup

### I want to... → Read this document

| Task | Document | Section |
|------|----------|---------|
| **Understand the proposal** | FACTORY_PROPOSAL.md | Problem & Solution |
| **See code examples** | MIGRATION_EXAMPLES.md | Examples 1-8 |
| **Copy implementation** | typed-array-factories.hh | Entire file |
| **Quick function lookup** | API_SUMMARY.md | Quick Reference |
| **Understand architecture** | ARCHITECTURE.md | Overview & Diagrams |
| **Decide which function to use** | ARCHITECTURE.md | Decision Tree |
| **Migrate existing code** | MIGRATION_EXAMPLES.md | All examples |
| **Learn best practices** | API_SUMMARY.md | Common Patterns |

---

## 📋 Function Categories

### Smart Pointer Wrappers (TypedArray)
- `MakeOwnedTypedArray()` - For owned arrays
- `MakeDedupTypedArray()` - For deduplication cache
- `MakeSharedTypedArray()` - For shared arrays
- `MakeMmapTypedArray()` - For memory-mapped arrays

### Array Implementation (TypedArrayImpl)
- `MakeTypedArrayCopy()` - Copy data
- `MakeTypedArrayView()` - Non-owning view
- `MakeTypedArrayMmap()` - Mmap view
- `MakeTypedArrayReserved()` - Empty with capacity

### Combined Convenience
- `CreateOwnedTypedArray()` - Create owned in one call
- `CreateDedupTypedArray()` - Wrap as dedup
- `CreateMmapTypedArray()` - Create mmap in one call
- `DuplicateTypedArray()` - Deep copy

---

## 🔍 Key Concepts

### The Problem
Current API uses boolean flags that are unclear:
```cpp
TypedArray<T>(ptr, true);   // ❌ What does 'true' mean?
```

### The Solution
Named factory functions that are self-documenting:
```cpp
MakeDedupTypedArray(ptr);   // ✅ Clear intent!
```

### Benefits
1. **Self-documenting** - Function names explain purpose
2. **Type-safe** - No boolean flag confusion
3. **Zero overhead** - All inline, same performance
4. **Backward compatible** - Existing code still works

---

## 💡 Most Common Use Case: Deduplication

```cpp
// Check dedup cache
auto it = _dedup_float_array.find(value_rep);
if (it != _dedup_float_array.end()) {
    // Found - return shared reference
    return MakeDedupTypedArray(it->second.get());
} else {
    // Not found - create, cache, and return
    auto* impl = new TypedArrayImpl<float>(data, size);
    _dedup_float_array[value_rep] = MakeOwnedTypedArray(impl);
    return MakeDedupTypedArray(impl);
}
```

---

## 📊 Impact Summary

| Metric | Result |
|--------|--------|
| **New lines of code** | ~300 (all inline) |
| **Runtime overhead** | Zero (inline functions) |
| **Breaking changes** | None (backward compatible) |
| **Code clarity** | ✅ Much improved |
| **Type safety** | ✅ Enhanced |
| **Migration effort** | Low (gradual, optional) |

---

## 🎨 Naming Convention

```
Make*        - Returns object by value
Create*      - Allocates and wraps
Duplicate*   - Deep copies

Suffixes:
  *Owned     - TypedArray owns and will delete
  *Dedup     - Deduplicated, won't delete
  *Shared    - Shared ownership, won't delete
  *Mmap      - Memory-mapped, won't delete
  *View      - Non-owning view
  *Copy      - Copies data
```

---

## 🏗️ Integration Steps

1. **Add functions** to `src/typed-array.hh`
   - Copy from `typed-array-factories.hh`
   - Add to end of file (around line 1200)

2. **Update crate-reader.cc**
   - Replace dedup cache usage
   - Use `MakeDedupTypedArray()`

3. **Update timesamples.hh**
   - Replace `TypedArray<T>(ptr, true)`
   - Use `MakeDedupTypedArray(ptr)`

4. **Optional: Update other code**
   - Gradually migrate over time
   - Use migration examples as guide

---

## ❓ Questions & Discussion

If you have questions or suggestions about:
- Function naming
- Additional use cases
- Migration strategy
- Implementation details

Please refer to the **Questions for Discussion** section in **TYPED_ARRAY_FACTORY_PROPOSAL.md**.

---

## 📝 Created By

This documentation set was created to address the need for clearer, more intuitive factory functions for TypedArray creation, especially for common patterns like deduplication caches and memory-mapped arrays.

**Date**: 2025-01-09
**Context**: TinyUSDZ crate-timesamples-opt branch
**Purpose**: Improve API clarity and developer experience

---

## 📖 Additional Reading

Related documentation in the same directory:
- `PACKED_ARRAY_OPTIMIZATION.md` - Original TypedArray design
- `typed-array.hh` - Current implementation (to be extended)
- `crate-reader.hh` - Deduplication cache usage

---

**Total Documentation Size**: ~43.5K of comprehensive documentation
**Total Functions Proposed**: 15+ factory functions
**Breaking Changes**: None - fully backward compatible!
