# linb::any Design Analysis

## Key Design Patterns

### 1. VTable Pattern (Type Erasure)
linb::any uses function pointers (vtable) to handle type-specific operations:
```cpp
struct vtable_type {
    void(*destroy)(storage_union&) noexcept;
    void(*copy)(const storage_union& src, storage_union& dest);
    void(*move)(storage_union& src, storage_union& dest) noexcept;
    void(*swap)(storage_union& lhs, storage_union& rhs) noexcept;
};
```

**Benefit**: The vtable pointer tells you EXACTLY how to destroy/copy/move the data. No guessing based on flags!

### 2. Union Storage (Stack vs Heap)
```cpp
union storage_union {
    void* dynamic;              // For heap-allocated types
    stack_storage_t stack;      // For inline storage (2 * sizeof(void*) = 16 bytes)
};
```

**Key Insight**: The union ensures the storage is EITHER a pointer OR inline data, never ambiguous.

### 3. Separate VTables for Stack vs Heap

**vtable_stack** - For inline storage:
```cpp
static void destroy(storage_union& storage) noexcept {
    reinterpret_cast<T*>(&storage.stack)->~T();  // Call destructor in-place
}

static void copy(const storage_union& src, storage_union& dest) {
    new (&dest.stack) T(reinterpret_cast<const T&>(src.stack));  // Placement new
}
```

**vtable_dynamic** - For heap storage:
```cpp
static void destroy(storage_union& storage) noexcept {
    delete reinterpret_cast<T*>(storage.dynamic);  // Delete pointer
}

static void copy(const storage_union& src, storage_union& dest) {
    dest.dynamic = new T(*reinterpret_cast<const T*>(src.dynamic));  // Heap copy
}
```

### 4. Smart Selection Logic
```cpp
template<typename T>
struct requires_allocation :
    std::integral_constant<bool,
        !(std::is_nothrow_move_constructible<T>::value
          && sizeof(T) <= sizeof(storage_union::stack)
          && std::alignment_of<T>::value <= std::alignment_of<storage_union::stack_storage_t>::value)>
{};
```

**Conditions for inline storage**:
1. Type has nothrow move constructor
2. Size fits in stack storage (16 bytes)
3. Alignment requirements are met

### 5. Construction Pattern
```cpp
template<typename ValueType>
any(ValueType&& value) {
    this->construct(std::forward<ValueType>(value));
}

template<typename ValueType>
void construct(ValueType&& value) {
    using T = typename std::decay<ValueType>::type;
    
    if(requires_allocation<T>::value) {
        storage.dynamic = new T(std::forward<ValueType>(value));
    } else {
        new (&storage.stack) T(std::forward<ValueType>(value));  // Placement new
    }
    
    vtable = vtable_for_type<T>();  // Set appropriate vtable
}
```

### 6. Destruction Pattern
```cpp
void clear() noexcept {
    if(!empty()) {
        this->vtable->destroy(storage);  // Call type-specific destroy
        this->vtable = nullptr;          // Mark as empty
    }
}
```

**Safety**: vtable nullptr means empty, no need for separate flags!

### 7. Copy/Move via Swap Idiom
```cpp
any& operator=(const any& rhs) {
    any(rhs).swap(*this);  // Copy-and-swap
    return *this;
}

any& operator=(any&& rhs) noexcept {
    any(std::move(rhs)).swap(*this);  // Move-and-swap
    return *this;
}
```

**Benefit**: Exception-safe, no manual cleanup needed.

## Comparison with New Value Implementation

| Feature | linb::any | New Value | Issue |
|---------|-----------|-----------|-------|
| Type dispatch | vtable (function pointers) | switch on type_id | Must keep switch in sync |
| Storage indicator | vtable pointer | kHeapAllocatedFlag bit | **Flag can get corrupted!** |
| Inline storage | Union with stack_storage_t | Raw byte array | **No type safety** |
| Destroy | vtable->destroy() | Manual switch + delete | **Error-prone** |
| Copy | vtable->copy() | Manual type dispatch | **Must handle all types** |
| Move | vtable->move() | memcpy + flag clear | **Can corrupt** |
| Empty check | vtable == nullptr | type_id == TYPE_ID_NULL | Separate state |

## Root Cause of New Value Bugs

### Problem 1: Manual Flag Management
New Value uses a bit flag to track heap allocation:
```cpp
if (sizeof(T) <= kInlineDataSize && is_trivially_copyable<T>()) {
    flags_ &= ~kHeapAllocatedFlag;  // Clear heap bit
} else {
    flags_ |= kHeapAllocatedFlag;   // Set heap bit
}
```

**Issue**: Flags can be corrupted during copy/move, leading to mismatched storage interpretation.

### Problem 2: Raw Byte Array for Storage
```cpp
uint8_t data_[24];  // Could be inline data OR a pointer!
```

**Issue**: No type safety. If flag is wrong, we interpret inline data as a pointer (or vice versa).

### Problem 3: Manual Type Dispatch
```cpp
void destroy() {
    if (flags_ & kHeapAllocatedFlag) {
        void* ptr;
        std::memcpy(&ptr, data_, sizeof(void*));  // Extract "pointer"
        switch (type_id_) {
            case TYPE_ID_INT32: delete reinterpret_cast<int32_t*>(ptr); break;
            // ... hundreds of cases
        }
    }
}
```

**Issue**: If flag is corrupted, we extract garbage as a pointer and try to delete it!

## Recommended Fix

Adopt linb::any's vtable pattern:

1. **Add vtable pointer** to Value
2. **Use union for storage** (pointer XOR inline data)
3. **Create vtable_stack and vtable_dynamic** templates
4. **Select vtable at construction time** based on type requirements
5. **Remove manual flag management** - vtable pointer indicates storage type

This would make the code more robust and closer to std::any semantics.
