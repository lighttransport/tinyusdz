# TypedArray Architecture and Factory Functions

## Overview

TypedArray has a two-layer architecture:

```
┌─────────────────────────────────────────────────────────────┐
│                    TypedArray<T>                             │
│              (Smart Pointer Wrapper)                         │
│                                                              │
│  • 64-bit packed pointer                                     │
│  • Ownership flag (bit 63): 0=owned, 1=dedup/mmap          │
│  • Manages lifetime of TypedArrayImpl                        │
└──────────────────────┬──────────────────────────────────────┘
                       │ owns or references
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                 TypedArrayImpl<T>                            │
│                (Array Implementation)                        │
│                                                              │
│  • Actual data storage                                       │
│  • Two modes:                                                │
│    - Owned: std::vector<uint8_t> storage                    │
│    - View: Non-owning pointer to external memory            │
└─────────────────────────────────────────────────────────────┘
```

## Factory Functions Map

### Layer 1: TypedArray (Wrapper)

```
Purpose                                Factory Function
──────────────────────────────────────────────────────────────
Own & delete impl         ──────────►  MakeOwnedTypedArray(ptr)
Shared (dedup cache)      ──────────►  MakeDedupTypedArray(ptr)
Shared (general)          ──────────►  MakeSharedTypedArray(ptr)
Memory-mapped             ──────────►  MakeMmapTypedArray(ptr)
```

### Layer 2: TypedArrayImpl (Implementation)

```
Purpose                                Factory Function
──────────────────────────────────────────────────────────────
Copy data (owned)         ──────────►  MakeTypedArrayCopy(data, size)
Non-owning view           ──────────►  MakeTypedArrayView(data, size)
Memory-mapped view        ──────────►  MakeTypedArrayMmap(data, size)
Empty with capacity       ──────────►  MakeTypedArrayReserved<T>(cap)
```

### Combined (Both Layers)

```
Purpose                                Factory Function
──────────────────────────────────────────────────────────────
Create owned from data    ──────────►  CreateOwnedTypedArray(data, size)
Create owned (size only)  ──────────►  CreateOwnedTypedArray<T>(size)
Create owned with value   ──────────►  CreateOwnedTypedArray<T>(size, val)
Wrap dedup pointer        ──────────►  CreateDedupTypedArray(ptr)
Create mmap wrapper       ──────────►  CreateMmapTypedArray(data, size)
Deep copy array           ──────────►  DuplicateTypedArray(source)
Deep copy impl            ──────────►  DuplicateTypedArrayImpl(source)
```

## Data Flow Examples

### Example 1: Deduplication Cache

```
┌────────────────────────┐
│   Read array from      │
│   USDC file            │
└───────────┬────────────┘
            │
            ▼
    ┌───────────────┐
    │ Check cache:  │
    │ value_rep?    │
    └───┬───────┬───┘
        │       │
   Found│       │Not found
        │       │
        ▼       ▼
    ┌────┐  ┌────────────────────────────────────┐
    │    │  │ 1. Create TypedArrayImpl           │
    │    │  │    (with copied data)              │
    │    │  │                                    │
    │    │  │ 2. Wrap as owned TypedArray        │
    │    │  │    MakeOwnedTypedArray(impl)      │
    │    │  │                                    │
    │    │  │ 3. Store in cache                  │
    │    │  │    cache[value_rep] = owned_array  │
    │    │  └────────────────────────────────────┘
    │    │              │
    │    │◄─────────────┘
    │    │
    └────┤
         │
         ▼
    ┌─────────────────────────────────────┐
    │ Return deduplicated reference:      │
    │ MakeDedupTypedArray(impl)          │
    │ (won't delete - cache owns it)     │
    └─────────────────────────────────────┘
```

### Example 2: Memory-Mapped File

```
┌─────────────────────────────────────────────┐
│  Open file and mmap()                       │
│  void* mmap_ptr = mmap(...)                 │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Calculate array offset and size            │
│  float* data = (float*)(mmap_ptr + offset)  │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Create mmap view:                          │
│  auto arr = CreateMmapTypedArray(data, sz) │
│                                             │
│  Under the hood:                            │
│  1. TypedArrayImpl(data, sz, true) [view]  │
│  2. MakeMmapTypedArray(impl) [dedup=true]  │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Use array (zero-copy access)               │
│  Process(arr)                               │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Cleanup (TypedArray destroyed)             │
│  - TypedArrayImpl not deleted (view mode)   │
│  - mmap_ptr still valid                     │
│  - munmap(mmap_ptr) called separately       │
└─────────────────────────────────────────────┘
```

### Example 3: Temporary Processing

```
┌─────────────────────────────────────────────┐
│  Stack buffer                               │
│  float buffer[10000];                       │
│  PopulateFromSensor(buffer);                │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Create non-owning view:                    │
│  auto view = MakeTypedArrayView(buffer, sz)│
│  (TypedArrayImpl with view flag)            │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Process data:                              │
│  float avg = ComputeAverage(view);          │
│  (zero-copy, no allocation)                 │
└───────────────────┬─────────────────────────┘
                    │
                    ▼
┌─────────────────────────────────────────────┐
│  Cleanup:                                   │
│  - view destroyed (no memory freed)         │
│  - buffer still valid (stack allocation)    │
└─────────────────────────────────────────────┘
```

## Memory Ownership Decision Tree

```
Do you need a TypedArray or just TypedArrayImpl?
│
├─ Just TypedArrayImpl (no smart pointer needed)
│  │
│  ├─ Need to copy data?
│  │  └─► Use: MakeTypedArrayCopy(data, size)
│  │
│  ├─ Need non-owning view?
│  │  └─► Use: MakeTypedArrayView(data, size)
│  │
│  ├─ Working with mmap?
│  │  └─► Use: MakeTypedArrayMmap(data, size)
│  │
│  └─ Need empty array with capacity?
│     └─► Use: MakeTypedArrayReserved<T>(capacity)
│
└─ Need TypedArray wrapper (smart pointer)
   │
   ├─ Creating from scratch with data?
   │  └─► Use: CreateOwnedTypedArray(data, size)
   │
   ├─ Have existing TypedArrayImpl pointer?
   │  │
   │  ├─ Should TypedArray own it?
   │  │  └─► Use: MakeOwnedTypedArray(impl)
   │  │
   │  ├─ Is it in a dedup cache?
   │  │  └─► Use: MakeDedupTypedArray(impl)
   │  │
   │  ├─ Is it shared among owners?
   │  │  └─► Use: MakeSharedTypedArray(impl)
   │  │
   │  └─ Is it memory-mapped?
   │     └─► Use: MakeMmapTypedArray(impl)
   │
   └─ Need to duplicate an existing TypedArray?
      └─► Use: DuplicateTypedArray(source)
```

## Bit Layout of TypedArray

```
TypedArray<T> internal representation (64 bits):
┌──┬─────────────────┬──────────────────────────────────────────────┐
│63│   62 ... 48     │           47 ... 0                           │
├──┼─────────────────┼──────────────────────────────────────────────┤
│ D│    Reserved     │         Pointer (48 bits)                    │
│ e│    (15 bits)    │         to TypedArrayImpl<T>                 │
│ d│                 │                                              │
│ u│                 │                                              │
│ p│                 │                                              │
└──┴─────────────────┴──────────────────────────────────────────────┘

Bit 63 (Dedup flag):
  • 0 = Owned: TypedArray deletes TypedArrayImpl on destruction
  • 1 = Dedup/Mmap/Shared: TypedArray does NOT delete TypedArrayImpl

Bits 0-47 (Pointer):
  • 48-bit pointer to TypedArrayImpl<T>
  • Sufficient for x86-64 canonical addresses
  • Sign-extended to 64 bits when dereferenced

Bits 48-62 (Reserved):
  • Available for future use
  • Could store metadata, flags, version info, etc.
```

## Comparison Matrix

| Aspect | `Make*` Functions | Old Constructors |
|--------|-------------------|------------------|
| **Readability** | ✅ Self-documenting names | ❌ Boolean flags unclear |
| **Type Safety** | ✅ Compiler enforced | ⚠️ Easy to swap flags |
| **Intent** | ✅ Clear from name | ❌ Requires comments |
| **Maintenance** | ✅ Easy to understand | ❌ Need to check docs |
| **Migration** | ✅ Non-breaking | N/A |
| **Performance** | ✅ Zero overhead (inline) | ✅ Zero overhead |

## Summary

The factory functions provide:

1. **Clarity**: Function names explain ownership and semantics
2. **Safety**: No boolean flags to confuse
3. **Convenience**: Combined functions for common patterns
4. **Compatibility**: Works alongside existing constructors
5. **Performance**: Zero runtime overhead (all inline)

Choose the right factory function based on:
- **Ownership**: Who manages the lifetime?
- **Sharing**: Is data deduplicated or shared?
- **Source**: Creating new or wrapping existing?
- **Memory**: Owned, view, or mmap?
