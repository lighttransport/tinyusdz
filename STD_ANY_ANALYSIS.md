# std::any Implementation Analysis & Comparison

## Overview

libc++ std::any (C++17) represents a more modern and sophisticated implementation compared to linb::any. Let's analyze the key differences and what we can learn.

## Storage Design

### std::any Storage (libc++)
```cpp
union _Storage {
    void* __ptr;                           // Heap pointer
    _Buffer __buf;                         // Inline: 3 * sizeof(void*) = 24 bytes
};

_Storage __s_;                             // 24 bytes
_HandleFuncPtr __h_;                       // 8 bytes
// Total: 32 bytes
```

### linb::any Storage
```cpp
union storage_union {
    void* dynamic;                         // Heap pointer
    stack_storage_t stack;                 // Inline: 2 * sizeof(void*) = 16 bytes
};

storage_union storage;                     // 16 bytes
vtable_type* vtable;                       // 8 bytes
// Total: 24 bytes
```

### New Value Storage (BROKEN)
```cpp
uint8_t data_[24];                         // Ambiguous: pointer OR inline
uint32_t type_id_;                         // 4 bytes
uint8_t flags_;                            // 1 byte
uint8_t padding_[3];                       // 3 bytes
// Total: 32 bytes
```

## Key Differences

| Feature | std::any | linb::any | New Value |
|---------|----------|-----------|-----------|
| **Size** | 32 bytes | 24 bytes | 32 bytes |
| **Inline capacity** | 24 bytes | 16 bytes | 24 bytes |
| **Storage type** | Union | Union | **Byte array** ❌ |
| **Dispatch** | Single handler func | Separate vtable funcs | switch on type_id |
| **Type safety** | Function pointer | vtable pointer | **Manual flag** ❌ |
| **RTTI support** | Optional (fallback) | Removed | Not needed |
| **Exception support** | Full | Removed | Not needed |

## Handler Function Dispatch (std::any Innovation)

### The Action Enum Pattern
```cpp
enum class _Action { 
    _Destroy, 
    _Copy, 
    _Move, 
    _Get, 
    _TypeInfo 
};

using _HandleFuncPtr = void* (*)(
    _Action,              // What operation to perform
    any const*,           // Source any
    any*,                 // Dest any (for copy/move)
    const type_info*,     // For type checking (can be null)
    const void*           // Fallback type ID (no-RTTI mode)
);
```

**Brilliant Design**: One function pointer handles ALL operations via switch on action!

### Comparison with linb::any vtable

**linb::any**: Separate function pointers
```cpp
struct vtable_type {
    void(*destroy)(storage_union&) noexcept;
    void(*copy)(const storage_union&, storage_union&);
    void(*move)(storage_union&, storage_union&) noexcept;
    void(*swap)(storage_union&, storage_union&) noexcept;
};
```

**std::any**: Single handler with action dispatch
```cpp
// One function handles everything!
void* __handle(_Action, any const*, any*, const type_info*, const void*);
```

**Trade-offs**:
- linb::any: 4 function pointers = 32 bytes vtable (indirect calls)
- std::any: 1 function pointer = 8 bytes (single indirect call + switch)
- std::any is **more compact** but may be slightly slower due to switch

## No-RTTI Type Checking

std::any has an elegant solution for type checking without RTTI:

```cpp
template <class _Tp>
inline _LIBCPP_HIDE_FROM_ABI 
const void* __get_fallback_typeid() {
    // Each type gets a unique static variable address
    return &__unique_typeinfo<decay_t<_Tp>>::__id;
}

template <class _Tp>
inline bool __compare_typeid(const type_info* __id, const void* __fallback) {
#if _LIBCPP_HAS_RTTI
    if (__id && *__id == typeid(_Tp))
        return true;
#endif
    // No-RTTI fallback: compare unique addresses
    return !__id && __fallback == __get_fallback_typeid<_Tp>();
}
```

**How it works**: Each type T gets a unique static variable. The address of that variable serves as a unique type identifier!

## Handler Implementation Examples

### SmallHandler (Inline Storage)
```cpp
template <class _Tp>
struct _SmallHandler {
    static void* __handle(_Action __act, any const* __this, 
                         any* __other, ...) {
        switch (__act) {
        case _Action::_Destroy:
            // In-place destruction
            std::__destroy_at(
                reinterpret_cast<_Tp*>(&__this->__s_.__buf));
            return nullptr;
            
        case _Action::_Copy:
            // Placement new copy
            std::__construct_at(
                reinterpret_cast<_Tp*>(&__other->__s_.__buf),
                *reinterpret_cast<const _Tp*>(&__this->__s_.__buf));
            return nullptr;
            
        case _Action::_Move:
            // Move construct + destroy source
            std::__construct_at(
                reinterpret_cast<_Tp*>(&__other->__s_.__buf),
                std::move(*reinterpret_cast<_Tp*>(&__this->__s_.__buf)));
            std::__destroy_at(
                reinterpret_cast<_Tp*>(&__this->__s_.__buf));
            return nullptr;
            
        case _Action::_Get:
            // Return pointer to value (after type check)
            return reinterpret_cast<void*>(&__this->__s_.__buf);
            
        case _Action::_TypeInfo:
            return __type_info<_Tp>();
        }
    }
};
```

### LargeHandler (Heap Storage)
```cpp
template <class _Tp>
struct _LargeHandler {
    static void* __handle(_Action __act, any const* __this, 
                         any* __other, ...) {
        switch (__act) {
        case _Action::_Destroy:
            // Heap deallocation
            _Tp* __p = static_cast<_Tp*>(__this->__s_.__ptr);
            std::__destroy_at(__p);
            std::__libcpp_deallocate<_Tp>(__p, 1);
            return nullptr;
            
        case _Action::_Copy:
            // Heap allocate + copy
            _Tp* __p = static_cast<_Tp*>(__this->__s_.__ptr);
            __other->__s_.__ptr = std::__allocate<_Tp>(1);
            std::__construct_at(
                static_cast<_Tp*>(__other->__s_.__ptr), *__p);
            return nullptr;
            
        case _Action::_Move:
            // Just transfer pointer!
            __other->__s_.__ptr = __this->__s_.__ptr;
            return nullptr;
            
        // ... other cases
        }
    }
};
```

## Small Object Optimization Criteria

```cpp
template <class _Tp>
using _IsSmallObject = integral_constant<bool,
    sizeof(_Tp) <= sizeof(_Buffer) &&
    alignment_of_v<_Buffer> % alignment_of_v<_Tp> == 0 &&
    is_nothrow_move_constructible_v<_Tp>
>;
```

**Three conditions**:
1. Fits in buffer (24 bytes)
2. Alignment compatible
3. **Nothrow move constructible** (critical for exception safety!)

## Application to TinyUSDZ Value

### What We Can Adopt

1. **Union storage** (not byte array!)
   ```cpp
   union Storage {
       void* ptr;                    // Heap
       aligned_storage_t<24> buf;    // Inline
   };
   ```

2. **Single handler function** instead of vtable
   ```cpp
   enum class Action { Destroy, Copy, Move, Get, TypeInfo };
   
   void* (*handler_)(Action, const Value*, Value*, 
                    uint32_t type_id, const void* fallback);
   ```

3. **No-RTTI type checking** using unique addresses
   ```cpp
   template <typename T>
   const void* get_type_id() {
       static char dummy;
       return &dummy;  // Unique address per type!
   }
   ```

4. **Proper placement new/destroy** for inline storage
   ```cpp
   // NOT: std::memcpy(data_, &value, sizeof(T))
   // YES: std::construct_at(&storage.buf, value)
   ```

### Recommended Value Design

```cpp
class Value {
    enum class Action { Destroy, Copy, Move, Get, TypeId };
    
    union Storage {
        void* ptr;
        std::aligned_storage_t<24, 8> buf;
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

**Trade-off**: 40 bytes (vs 32 for broken implementation) but **SAFE and ROBUST**.

Alternative if size is critical:
```cpp
class Value {
    union Storage {
        void* ptr;
        std::aligned_storage_t<16, 8> buf;  // Reduce to 16 like linb::any
    };
    
    HandlerFunc handler_;   // 8 bytes
    uint32_t type_id_;      // 4 bytes
    uint32_t flags_;        // 4 bytes
    // Total: 32 bytes with less inline capacity
};
```

## Key Takeaways

1. ✅ **Union is mandatory** - never use raw byte array for dual-purpose storage
2. ✅ **Handler/vtable pattern** - function pointers encode storage type
3. ✅ **No-RTTI via unique addresses** - simple and effective
4. ✅ **Placement new for inline** - proper construction/destruction
5. ✅ **Nothrow move check** - critical for exception safety
6. ❌ **Manual flags are dangerous** - single bit corruption = crash

The new Value's attempt to save space with manual flag management was fundamentally flawed. Both std::any and linb::any prove that union + handler/vtable is the correct approach.

## Practical Demonstration: Why Placement New Matters

Created test program `/tmp/test_memcpy.cc` showing three approaches:

### Method 1: memcpy (BROKEN - what new Value does)
```cpp
uint8_t buffer[24];
TestData source(42);
std::memcpy(buffer, &source, sizeof(TestData));  // Bypasses constructor!
```
**Output**: Only 1 constructor call, but 1 destructor call - objects not properly paired

### Method 2: Placement new (CORRECT - what std::any/linb::any do)
```cpp
uint8_t buffer[24];
TestData source(42);
TestData* p = new (buffer) TestData(source);     // Proper copy constructor
p->~TestData();                                   // Proper destructor
```
**Output**: 2 constructor calls (original + copy), 2 destructor calls - properly paired

### Method 3: Union storage (SAFE - prevents misinterpretation)
```cpp
union Storage {
    void* ptr;
    alignas(8) uint8_t buf[16];
};
Storage s;
TestData* p = new (&s.buf) TestData(source);     // Placement new in union
```
**Output**: Same as Method 2, but union prevents treating inline data as pointer

**Key Insight**: For simple POD types like int32_t, memcpy works by accident. For non-trivial types with constructors/destructors, memcpy is undefined behavior. The new Value implementation gets away with it for scalars but would fail catastrophically for std::string, std::vector, or any user-defined type.
