# Value Class Redesign Proposal

## Executive Summary

The current new Value implementation (32 bytes) uses a raw byte array with manual flag tracking, causing memory corruption crashes. After studying both `linb::any` and `std::any` implementations, the root cause is clear: **union storage is mandatory** for type-safe dual-purpose (inline/heap) storage.

## The Fundamental Flaw

### Current Broken Design
```cpp
class Value {
    uint8_t data_[24];          // Ambiguous: pointer OR inline data?
    uint32_t type_id_;
    uint8_t flags_;             // Manual tracking with kHeapAllocatedFlag
    // If flags_ corrupts → disaster (delete inline data as pointer!)
};
```

### Why It Fails
1. **Type ambiguity**: `data_` could contain anything - compiler can't help
2. **Single point of failure**: One corrupted bit in `flags_` → crash
3. **Wrong construction**: Uses `memcpy()` instead of placement new
4. **No dispatch mechanism**: switch statement on type_id is brittle

## Proven Patterns from std::any and linb::any

### Pattern 1: Union Storage (Both)
```cpp
union Storage {
    void* ptr;                  // Heap allocation
    aligned_storage_t buf;      // Inline storage
};
```
**Benefit**: Compiler enforces mutual exclusion - impossible to misinterpret

### Pattern 2a: VTable Dispatch (linb::any)
```cpp
struct vtable_type {
    void(*destroy)(storage_union&) noexcept;
    void(*copy)(const storage_union&, storage_union&);
    void(*move)(storage_union&, storage_union&) noexcept;
    void(*swap)(storage_union&, storage_union&) noexcept;
};
```
**Benefit**: Type-specific operations encoded in function pointers

### Pattern 2b: Handler Dispatch (std::any)
```cpp
enum class Action { Destroy, Copy, Move, Get, TypeInfo };
void* (*handler)(Action, const any*, any*, ...);
```
**Benefit**: More compact (1 function pointer vs 4), slight overhead from switch

### Pattern 3: Placement New (Both)
```cpp
// WRONG (current Value):
std::memcpy(data_, &value, sizeof(T));

// CORRECT (std::any/linb::any):
std::construct_at(reinterpret_cast<T*>(&storage.buf), value);
// or: new (&storage.buf) T(value);
```
**Benefit**: Proper constructor/destructor calls for non-trivial types

### Pattern 4: No-RTTI Type Checking (std::any)
```cpp
template <typename T>
const void* get_type_id() {
    static char dummy;
    return &dummy;  // Unique address per type!
}
```
**Benefit**: Type safety without `typeid()` or RTTI

## Recommended Design Options

### Option A: 40-byte Design (Maximum Safety + Capacity)
```cpp
class Value {
    enum class Action { Destroy, Copy, Move, Get, TypeId };
    
    union Storage {
        void* ptr;
        std::aligned_storage_t<24, 8> buf;  // 3 * sizeof(void*)
    };
    
    using HandlerFunc = void* (*)(Action, const Value*, Value*, 
                                   uint32_t, const void*);
    
    Storage storage_;       // 24 bytes
    HandlerFunc handler_;   // 8 bytes
    uint32_t type_id_;      // 4 bytes (keep for compatibility)
    uint32_t padding_;      // 4 bytes
    // Total: 40 bytes
};
```

**Pros**:
- Largest inline capacity (24 bytes)
- Single handler function (compact)
- Compatible with existing type_id system
- Safest design

**Cons**:
- 8 bytes larger than current broken implementation
- 16 bytes larger than old linb::any-based Value

### Option B: 32-byte Design (Size-Constrained)
```cpp
class Value {
    union Storage {
        void* ptr;
        std::aligned_storage_t<16, 8> buf;  // Like linb::any
    };
    
    HandlerFunc handler_;   // 8 bytes
    uint32_t type_id_;      // 4 bytes
    uint32_t flags_;        // 4 bytes
    // Total: 32 bytes
};
```

**Pros**:
- Same size as current broken implementation
- Proven inline capacity (16 bytes works for linb::any)
- Still uses union (safe)

**Cons**:
- Less inline capacity than Option A
- Some larger types will heap-allocate

### Option C: Stick with Old Value (linb::any-based)
```cpp
// Already implemented, already works
class Value {
    linb::any data_;  // 24 bytes
    // ...
};
```

**Pros**:
- Battle-tested, zero crashes
- No implementation work needed
- Proven performance

**Cons**:
- Larger memory footprint
- Less optimization potential

## Implementation Roadmap

### Phase 1: Proof of Concept
- [x] Study linb::any implementation → LINB_ANY_ANALYSIS.md
- [x] Study std::any implementation → STD_ANY_ANALYSIS.md
- [x] Create practical demonstration → /tmp/test_memcpy.cc
- [ ] Implement minimal working Value with union storage
- [ ] Verify it handles non-POD types correctly

### Phase 2: Full Implementation (if POC succeeds)
- [ ] Implement handler functions for all Value types
- [ ] Add proper placement new construction
- [ ] Add proper in-place destruction
- [ ] Implement copy/move semantics
- [ ] Port all type_id-based operations

### Phase 3: Testing & Validation
- [ ] Run existing test suite
- [ ] Add tests for non-trivial types
- [ ] Memory leak testing (Valgrind)
- [ ] Performance comparison with old Value
- [ ] ASan/UBSan validation

### Phase 4: Migration
- [ ] Feature flag for gradual rollout
- [ ] Benchmark memory usage impact
- [ ] Update documentation
- [ ] Commit to main branch

## Decision Matrix

| Criterion | Option A (40B) | Option B (32B) | Option C (Old) |
|-----------|----------------|----------------|----------------|
| **Safety** | ✅ Excellent | ✅ Good | ✅ Proven |
| **Memory** | ⚠️ +8 bytes | ✅ Same size | ❌ +larger |
| **Inline capacity** | ✅ 24 bytes | ⚠️ 16 bytes | N/A |
| **Implementation work** | ⚠️ Medium | ⚠️ Medium | ✅ None |
| **Risk** | ⚠️ New code | ⚠️ New code | ✅ Zero |
| **Performance** | ✅ Optimized | ✅ Optimized | ✅ Proven |

## Recommendation

**Short term**: Revert to Option C (old linb::any-based Value) for stability

**Long term**: Implement Option A (40-byte design) if memory budget allows, otherwise Option B

**Rationale**:
1. Union storage is non-negotiable for safety
2. 8-byte size increase (Option A) is acceptable given safety benefits
3. Current 32-byte implementation is fundamentally broken and unfixable without union
4. Handler pattern from std::any is more modern and maintainable than vtable

## Key Learnings

1. **Never use raw byte arrays for dual-purpose storage** - use union
2. **Never use memcpy for non-trivial types** - use placement new
3. **Manual flag tracking is inherently fragile** - use function pointers to encode state
4. **Size optimization must not compromise safety** - 8 bytes is cheap compared to debugging crashes
5. **Study production implementations before reinventing** - std::any and linb::any have solved these problems

## References

- `LINB_ANY_ANALYSIS.md` - Detailed linb::any study
- `STD_ANY_ANALYSIS.md` - Detailed std::any study  
- `NEW_VALUE_DEBUG_NOTES.md` - Complete debugging history
- `/tmp/test_memcpy.cc` - Practical demonstration
- `src/tiny-any.inc` - linb::any source code
- libc++ `<any>` header - std::any reference implementation
