# New Value Implementation Debugging Notes

## Problem Summary
Tests crash with SEGV when using the new 32-byte Value implementation (`TUSDZ_NEW_VALUE_TYPE=ON`), but pass with the old linb::any-based implementation.

## Crash Pattern
- Crash occurs during Property destruction
- AddressSanitizer shows attempt to `delete` an invalid pointer
- The pointer being deleted appears to be inline data being misinterpreted as a heap pointer
- Crash trace: Property::~Property() → Relationship::~Relationship() → Path corruption

## Root Cause Hypothesis
The `kHeapAllocatedFlag` (bit 1 of flags_) is being set incorrectly for values that should be inlined (e.g., int32_t). When the Value destructor runs, it attempts to free this "pointer", causing a crash.

## Fixes Applied (Partial)
1. **get_value_ptr() logic** - Changed from size-based heuristic to checking actual `kHeapAllocatedFlag`
2. **construct_value() flag management** - Explicitly clear heap flag when inlining  
3. **Data buffer initialization** - Clear data_ buffer in all constructors and copy_data_from()
4. **Validation in destroy()** - Added pointer validation checks to catch invalid addresses
5. **Safety checks** - Added checks in array_size() and copy_data_from() fallback

## Remaining Issues
Despite all fixes, tests still crash. Possible causes:
1. **Flag corruption during copy/move** - The heap flag may be getting set during Value copy or move operations
2. **Memory layout issues** - The 32-byte Value is smaller than linb::any, potentially causing layout/padding issues
3. **Adjacent memory corruption** - Attribute (containing Value) comes before Relationship in Property memory layout; buffer overflow in Value could corrupt Relationship
4. **Type trait issues** - Possible edge case in is_trivially_copyable() or sizeof() checks

## Testing Results
- Old Value implementation: Tests PASS
- New Value implementation (with fixes): Tests FAIL with SEGV
- Simple types (int32_t) that should inline are somehow getting heap flag set

## Next Steps for Resolution
1. **Use GDB with breakpoints** - Set breakpoint in Value constructor for int32_t and trace flag values
2. **Add extensive logging** - Log every flag modification with stack traces
3. **Memory watchpoints** - Use hardware watchpoints to catch when flags_ byte is modified
4. **Valgrind/Dr. Memory** - Run additional memory checkers besides ASan
5. **Simplify test case** - Create minimal reproducer with just Value construction/destruction
6. **Review move semantics** - Carefully audit all copy/move constructors and assignments
7. **Check compiler optimization** - Test with -O0 to rule out optimization bugs

## Files Modified
- src/value-types.hh (lines 2577-2582, 2972-3017)
- src/value-types.cc (lines 1423-1558)
- src/value-types-new.hh (lines 410-485) - Note: This file may not be used in current build

## Build Commands
```bash
# Build with new Value + ASan
mkdir build_asan && cd build_asan
cmake -DTUSDZ_NEW_VALUE_TYPE=ON -DSANITIZE_ADDRESS=ON ..
make -j4

# Test
ctest --output-on-failure -R usda-parser
```

## Commit History
- 9aafece8: Fix build issues with macro names
- 6d64546a: Fix critical memory bugs (get_value_ptr, array_size, copy_data_from, destroy)
- dfed3f21: Additional memory safety fixes (data_ clearing, validation)

## Update: Buffer Overflow Hypothesis

Further testing reveals the crash now occurs in Relationship destructor (after Value destructor completes), suggesting **memory corruption from Value is affecting adjacent objects**.

Memory layout in Property:
```
Property {
    Attribute _attrib;     // Contains Value - if this overflows...
    Relationship _rel;     // ...this gets corrupted!
    ...
}
```

### Evidence
1. Value safety check prevents crash in Value::destroy()
2. Crash now occurs in Relationship::~Relationship() → Path corruption
3. Path's optional<PathType> has corrupted internal pointer
4. This pattern is consistent with buffer overflow from preceding object

### Likely Root Causes
1. **Buffer overflow in Value data_ buffer** - Writing more than 24 bytes
2. **Incorrect size calculation** - sizeof(T) check may have edge case
3. **Padding/alignment issues** - 32-byte Value may have different padding than expected

### Additional Fixes Applied
- Added type-based safety check in destroy() to prevent freeing scalar types
- This is a workaround, not a fix for the underlying corruption

### Recommendation
The new Value implementation needs to be completely audited with a memory debugger (Valgrind, Dr. Memory) or rewritten to use unique_ptr for heap allocations to prevent these types of bugs entirely.

## ROOT CAUSE IDENTIFIED: Union vs Byte Array

After studying linb::any implementation, the fundamental issue is clear:

### linb::any Design (Works)
```cpp
union storage_union {
    void* dynamic;       // Heap pointer
    stack_storage_t stack;  // Inline data (16 bytes)
};
```
- Storage is **EITHER** a pointer **OR** inline data (enforced by union)
- vtable pointer determines which: `vtable->destroy(storage)` knows what to do
- **Impossible to misinterpret** - union guarantees mutual exclusion

### New Value Design (Broken)
```cpp
uint8_t data_[24];     // Could be ANYTHING!
uint8_t flags_;        // Manual tracking of what data_ contains
```
- Storage is ambiguous byte array
- Manual flag tracks whether it's a pointer or inline data
- **If flag is corrupted** → disaster (delete inline data as pointer!)

### Key Differences

| Aspect | linb::any | New Value |
|--------|-----------|-----------|
| Storage type safety | Union (type-safe) | Byte array (unsafe) |
| Dispatch | vtable function pointers | switch on type_id + flags |
| Heap indicator | vtable pointer type | kHeapAllocatedFlag bit |
| Corruption resilience | **High** - union prevents misinterpretation | **Low** - single bit corruption = crash |

### The Fatal Flaw

When `kHeapAllocatedFlag` is set for inline data (due to any bug):
1. destroy() extracts first 8 bytes as a "pointer"
2. Attempts to `delete` what is actually inline int32_t data
3. SEGV in allocator trying to free invalid address

With linb::any's union, this is **impossible** because:
- If vtable is `vtable_stack`, storage.stack is used (never as pointer)
- If vtable is `vtable_dynamic`, storage.dynamic is used (always valid pointer or null)
- No manual flag can get corrupted

### Recommended Solution

**Option 1**: Add vtable to new Value (increases size to 40 bytes)
- 24 bytes data
- 8 bytes vtable pointer  
- 4 bytes type_id
- 4 bytes flags/padding

**Option 2**: Use union like linb::any (reduces inline to 16 bytes)
- More robust
- Proven design
- Slightly less inline capacity

**Option 3**: Stick with old linb::any-based Value
- Already works
- Well-tested
- Accept the size cost

The 32-byte new Value was an optimization attempt, but the manual flag management makes it too fragile.
