# PackedTypedArrayPtr - Optimized 64-bit TypedArray Storage

## Overview

`PackedTypedArrayPtr<T>` is a memory-optimized smart pointer for `TypedArray<T>` that packs both the pointer and a deduplication/mmap flag into a single 64-bit value.

## Memory Layout

```
Bit Layout (64 bits total):
┌────────┬──────────────────┬────────────────────────────────────────┐
│ Bit 63 │   Bits 48-62     │            Bits 0-47                   │
│  (MSB) │   (Reserved)     │          (Pointer)                     │
├────────┼──────────────────┼────────────────────────────────────────┤
│ Dedup  │   15 bits        │      48-bit pointer to                 │
│  Flag  │   Available      │      TypedArray<T> object              │
└────────┴──────────────────┴────────────────────────────────────────┘
```

### Bit Allocation

- **Bit 63 (MSB)**: Dedup/mmap flag
  - `1` = Shared/memory-mapped pointer (won't be deleted on destruction)
  - `0` = Owned pointer (will be deleted on destruction)

- **Bits 48-62**: Reserved (15 bits available for future use)

- **Bits 0-47**: Pointer to TypedArray<T> object (48 bits)
  - Sufficient for x86-64 canonical addresses (48-bit virtual address space)
  - Sufficient for ARM64 (typically 48-52 bits, with 48 being most common)

## Key Features

### 1. Memory Efficiency
- Only **8 bytes** per pointer (same as a raw pointer)
- No additional storage overhead for flags
- Reduces memory footprint when storing many TypedArray references

### 2. Deduplication Support
- Shared pointers marked with dedup flag won't be deleted
- Enables safe sharing of TypedArray instances
- Prevents double-free errors

### 3. Smart Pointer Semantics
- Automatic deletion of owned pointers
- Move semantics for zero-cost ownership transfer
- Copy semantics with automatic dedup flag handling

## Usage Examples

### Creating Owned Pointers

```cpp
// Creates owned pointer (will delete on destruction)
auto* arr = new TypedArray<float>({1.0f, 2.0f, 3.0f});
PackedTypedArrayPtr<float> ptr(arr, false);

// Access array
std::cout << ptr->size() << "\n";      // 3
std::cout << (*ptr)[0] << "\n";        // 1.0
```

### Creating Shared/Dedup Pointers

```cpp
// Array on stack or managed elsewhere
TypedArray<int> arr({10, 20, 30});

// Create dedup pointer (won't delete on destruction)
PackedTypedArrayPtr<int> ptr(&arr, true);

assert(ptr.is_dedup() == true);
// ptr goes out of scope but arr is not deleted
```

### Helper Functions

```cpp
// Create owned pointer
auto owned = make_packed_array_ptr(new TypedArray<int>({1, 2, 3}));

// Create dedup/mmap pointer
TypedArray<float> arr({1.0f, 2.0f});
auto shared = make_packed_array_ptr_dedup(&arr);
```

### Ownership Transfer

```cpp
auto* arr = new TypedArray<double>({1.1, 2.2});
PackedTypedArrayPtr<double> ptr1(arr, false);

// Move ownership
PackedTypedArrayPtr<double> ptr2(std::move(ptr1));

assert(ptr1.is_null());      // ptr1 no longer owns the array
assert(ptr2->size() == 2);   // ptr2 now owns it
```

### Copy Behavior

```cpp
TypedArray<int> arr({1, 2, 3});
PackedTypedArrayPtr<int> ptr1(&arr, true);

// Shallow copy - both point to same array
PackedTypedArrayPtr<int> ptr2 = ptr1;

assert(ptr1.get() == ptr2.get());
(*ptr1)[0] = 100;
assert((*ptr2)[0] == 100);  // Both see the change
```

## API Reference

### Constructors

```cpp
PackedTypedArrayPtr();                                  // Null pointer
PackedTypedArrayPtr(TypedArray<T>* ptr, bool dedup);    // From pointer
PackedTypedArrayPtr(const PackedTypedArrayPtr& other);  // Copy (shallow)
PackedTypedArrayPtr(PackedTypedArrayPtr&& other);       // Move
```

### Destructor

```cpp
~PackedTypedArrayPtr();  // Deletes if owned (!is_dedup())
```

### Access Methods

```cpp
TypedArray<T>* get() const;              // Get raw pointer
TypedArray<T>* operator->() const;       // Pointer access
TypedArray<T>& operator*() const;        // Dereference
bool is_null() const;                    // Check if null
explicit operator bool() const;          // Bool conversion
```

### Flag Management

```cpp
bool is_dedup() const;                   // Check dedup flag
void set_dedup(bool dedup);              // Set dedup flag
```

### Ownership Management

```cpp
void reset(TypedArray<T>* ptr = nullptr, bool dedup = false);  // Reset pointer
TypedArray<T>* release();                                       // Release ownership
```

### Debug/Inspection

```cpp
uint64_t get_packed_value() const;       // Get raw 64-bit value
```

## Implementation Details

### Canonical Address Support

The implementation properly handles x86-64 canonical addresses:

- **User space**: `0x0000'0000'0000'0000` - `0x0000'7FFF'FFFF'FFFF` (bits 63-47 all 0)
- **Kernel space**: `0xFFFF'8000'0000'0000` - `0xFFFF'FFFF'FFFF'FFFF` (bits 63-47 all 1)

When unpacking, if bit 47 is set, the pointer is sign-extended to maintain canonical form.

### Copy Semantics

- If copying a **dedup pointer**: Safe to copy as-is
- If copying an **owned pointer**: Copy is automatically marked as dedup to prevent double-free

## Performance Characteristics

| Operation | Time Complexity | Notes |
|-----------|-----------------|-------|
| Construction | O(1) | Constant time |
| get() | O(1) | Simple bit masking |
| is_dedup() | O(1) | Single bit check |
| set_dedup() | O(1) | Single bit operation |
| Destruction | O(1)* | *Plus array deletion if owned |

## Safety Considerations

1. **Pointer must fit in 48 bits**: The implementation asserts that pointers are canonical x86-64/ARM64 addresses
2. **Dedup flag prevents deletion**: Shared pointers won't be deleted, preventing dangling pointer issues
3. **Copy creates dedup**: Copying an owned pointer creates a dedup copy to prevent double-free

## Testing

All functionality is verified in `test_packed_array.cc`:
- ✓ Basic pointer operations
- ✓ Dedup flag behavior
- ✓ Move semantics
- ✓ Copy behavior (shallow copy with dedup)
- ✓ Null pointer handling
- ✓ Memory layout verification
- ✓ Helper functions

Run tests:
```bash
g++ -std=c++14 -I. test_packed_array.cc -o test_packed_array
./test_packed_array
```

## Use Cases

1. **Memory-mapped arrays**: Store references to mmap'd data without ownership
2. **Deduplication**: Share identical arrays without copying
3. **Memory optimization**: Reduce overhead in data structures storing many array references
4. **Caching**: Store both cached and owned arrays with unified interface

## Future Enhancements

The 15 reserved bits (48-62) can be used for:
- Reference counting
- Type tags or discriminators
- Cache coherency flags
- Additional metadata
