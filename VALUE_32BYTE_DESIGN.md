# 32-Byte Value Design: Simplified & Safe

## Key Insight

Current broken design wastes space with redundant type tracking:
- `data_[24]` - storage
- `type_id_` (4 bytes) - manual type tracking
- `flags_` (1 byte) - manual heap/inline tracking
- **Problem**: Two separate type tracking mechanisms (type_id AND flags)

## Proposed 32-Byte Design

### Memory Layout
```cpp
class Value {
    // 24 bytes: Union storage (inline OR heap)
    union Storage {
        void* ptr;                               // Heap pointer
        std::aligned_storage_t<24, 8> buf;       // 24-byte inline (3 pointers)
    };
    
    // 8 bytes: Handler function (encodes BOTH type AND storage location)
    using HandlerFunc = void* (*)(Action, const Value*, Value*);
    
    Storage storage_;     // 24 bytes
    HandlerFunc handler_; // 8 bytes
    // Total: 32 bytes!
};
```

### How It Works

**1. Handler pointer encodes everything:**
```cpp
enum class Action {
    Destroy,
    Copy,
    Move,
    Get,
    TypeId,
    TypeName
};

// Each type T gets TWO handler functions:
template <typename T>
void* handler_inline(Action act, const Value* src, Value* dst);  // For inline storage

template <typename T>
void* handler_heap(Action act, const Value* src, Value* dst);    // For heap storage
```

**2. Type information via TypeTraits (like linb::any):**
```cpp
template <typename T>
struct TypeTraits {
    static constexpr uint32_t type_id() {
        // Same hash/enum as current system for compatibility
        return GetTypeId<T>();
    }
    
    static constexpr const char* type_name() {
        return GetTypeName<T>();
    }
    
    static constexpr bool use_inline() {
        return sizeof(T) <= 24 && 
               alignof(T) <= 8 &&
               std::is_nothrow_move_constructible<T>::value;
    }
};
```

**3. Construction logic:**
```cpp
template <typename T>
void set(const T& value) {
    if (TypeTraits<T>::use_inline()) {
        // Placement new into inline buffer
        new (&storage_.buf) T(value);
        handler_ = &handler_inline<T>;
    } else {
        // Heap allocation
        storage_.ptr = new T(value);
        handler_ = &handler_heap<T>;
    }
}
```

**4. Getting type_id (when needed for compatibility):**
```cpp
uint32_t type_id() const {
    if (!handler_) return 0;
    // Ask handler for its type_id
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(
            handler_(Action::TypeId, this, nullptr)
        )
    );
}
```

## Handler Implementation Examples

### Inline Handler
```cpp
template <typename T>
void* handler_inline(Action act, const Value* src, Value* dst) {
    switch (act) {
    case Action::Destroy:
        // In-place destruction
        reinterpret_cast<const T*>(&src->storage_.buf)->~T();
        return nullptr;
        
    case Action::Copy:
        // Placement new copy
        new (&dst->storage_.buf) T(
            *reinterpret_cast<const T*>(&src->storage_.buf)
        );
        dst->handler_ = src->handler_;
        return nullptr;
        
    case Action::Move:
        // Move construct + destroy source
        new (&dst->storage_.buf) T(
            std::move(*reinterpret_cast<T*>(
                const_cast<Storage*>(&src->storage_)
            ).buf)
        );
        dst->handler_ = src->handler_;
        reinterpret_cast<const T*>(&src->storage_.buf)->~T();
        return nullptr;
        
    case Action::Get:
        // Return pointer to value
        return const_cast<void*>(
            static_cast<const void*>(&src->storage_.buf)
        );
        
    case Action::TypeId:
        return reinterpret_cast<void*>(
            static_cast<uintptr_t>(TypeTraits<T>::type_id())
        );
        
    case Action::TypeName:
        return const_cast<void*>(
            static_cast<const void*>(TypeTraits<T>::type_name())
        );
    }
    return nullptr;
}
```

### Heap Handler
```cpp
template <typename T>
void* handler_heap(Action act, const Value* src, Value* dst) {
    switch (act) {
    case Action::Destroy:
        // Heap deallocation
        delete static_cast<T*>(src->storage_.ptr);
        return nullptr;
        
    case Action::Copy:
        // Heap allocate + copy
        dst->storage_.ptr = new T(*static_cast<T*>(src->storage_.ptr));
        dst->handler_ = src->handler_;
        return nullptr;
        
    case Action::Move:
        // Just transfer pointer ownership!
        dst->storage_.ptr = src->storage_.ptr;
        dst->handler_ = src->handler_;
        // Source will be left empty (handler set to nullptr by caller)
        return nullptr;
        
    case Action::Get:
        return src->storage_.ptr;
        
    case Action::TypeId:
        return reinterpret_cast<void*>(
            static_cast<uintptr_t>(TypeTraits<T>::type_id())
        );
        
    case Action::TypeName:
        return const_cast<void*>(
            static_cast<const void*>(TypeTraits<T>::type_name())
        );
    }
    return nullptr;
}
```

## Comparison with Alternatives

| Aspect | New 32B Design | std::any | linb::any | Current Broken |
|--------|---------------|----------|-----------|----------------|
| **Size** | 32 bytes | 32 bytes | 24 bytes | 32 bytes |
| **Inline capacity** | 24 bytes | 24 bytes | 16 bytes | 24 bytes |
| **Storage safety** | ✅ Union | ✅ Union | ✅ Union | ❌ Byte array |
| **Type tracking** | ✅ Handler | ✅ Handler | ✅ VTable | ❌ Manual flag |
| **Construction** | ✅ Placement new | ✅ Placement new | ✅ Placement new | ❌ memcpy |
| **Function pointers** | 1 (8B) | 1 (8B) | 1 (8B) | 0 (uses flags) |
| **Type ID field** | ❌ None | ❌ None | ❌ None | ✅ 4 bytes |
| **Redundancy** | ✅ None | ✅ None | ✅ None | ❌ type_id + flags |

## Benefits of This Design

### 1. **Same size as broken implementation** (32 bytes)
- No memory footprint increase
- Drop-in replacement

### 2. **Eliminates redundancy**
- Handler pointer encodes BOTH type AND storage location
- No separate `type_id_` field needed
- No separate `flags_` field needed

### 3. **Type-safe by construction**
- Handler function signature determines available operations
- Compiler enforces correct usage
- Impossible to call wrong handler for wrong type

### 4. **Simplified logic**
```cpp
// OLD (broken):
if (flags_ & kHeapAllocatedFlag) {
    switch (type_id_) {
        case TYPE_INT: /* ... */ break;
        case TYPE_FLOAT: /* ... */ break;
        // 50+ cases...
    }
} else {
    switch (type_id_) {
        // Another 50+ cases...
    }
}

// NEW (simple):
handler_(Action::Destroy, this, nullptr);  // Just call it!
```

### 4. **Backward compatibility**
```cpp
// Can still provide type_id() for existing code:
uint32_t type_id() const {
    return static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(
            handler_(Action::TypeId, this, nullptr)
        )
    );
}

// Can still provide type_name() for debugging:
const char* type_name() const {
    return static_cast<const char*>(
        handler_(Action::TypeName, this, nullptr)
    );
}
```

## Implementation Strategy

### Phase 1: Core Implementation
```cpp
// value-types-handler.hh
namespace tinyusdz {

enum class ValueAction {
    Destroy,
    Copy,
    Move,
    Get,
    TypeId,
    TypeName
};

using ValueHandler = void* (*)(ValueAction, const Value*, Value*);

class Value {
public:
    Value() : handler_(nullptr) {}
    
    template <typename T>
    Value(const T& value) {
        set(value);
    }
    
    ~Value() {
        destroy();
    }
    
    // Copy constructor
    Value(const Value& other) : handler_(nullptr) {
        if (other.handler_) {
            other.handler_(ValueAction::Copy, &other, this);
        }
    }
    
    // Move constructor
    Value(Value&& other) noexcept : handler_(nullptr) {
        if (other.handler_) {
            other.handler_(ValueAction::Move, &other, this);
            other.handler_ = nullptr;
        }
    }
    
    template <typename T>
    void set(const T& value) {
        destroy();  // Clear old value first
        
        if (TypeTraits<T>::use_inline()) {
            new (&storage_.buf) T(value);
            handler_ = &handler_inline<T>;
        } else {
            storage_.ptr = new T(value);
            handler_ = &handler_heap<T>;
        }
    }
    
    template <typename T>
    const T* as() const {
        if (!handler_) return nullptr;
        
        void* ptr = handler_(ValueAction::Get, this, nullptr);
        // TODO: Add type checking via TypeId action
        return static_cast<const T*>(ptr);
    }
    
    uint32_t type_id() const {
        if (!handler_) return 0;
        return static_cast<uint32_t>(
            reinterpret_cast<uintptr_t>(
                handler_(ValueAction::TypeId, this, nullptr)
            )
        );
    }
    
    bool is_empty() const { return handler_ == nullptr; }
    
private:
    void destroy() {
        if (handler_) {
            handler_(ValueAction::Destroy, this, nullptr);
            handler_ = nullptr;
        }
    }
    
    union Storage {
        void* ptr;
        std::aligned_storage_t<24, 8> buf;
    };
    
    Storage storage_;      // 24 bytes
    ValueHandler handler_; // 8 bytes
    // Total: 32 bytes
};

} // namespace tinyusdz
```

### Phase 2: Type-Specific Handlers
```cpp
// Generate handlers for each USD type
template <> void* handler_inline<int32_t>(ValueAction, const Value*, Value*);
template <> void* handler_inline<float>(ValueAction, const Value*, Value*);
template <> void* handler_inline<double>(ValueAction, const Value*, Value*);
// ... etc for all USD value types

template <> void* handler_heap<std::string>(ValueAction, const Value*, Value*);
template <> void* handler_heap<std::vector<int>>(ValueAction, const Value*, Value*);
// ... etc for heap-allocated types
```

### Phase 3: Integration
- Keep existing Value API surface
- Implement all existing methods using handler
- Add compatibility shims for type_id() queries
- Test with existing test suite

## Migration Path

1. **Week 1**: Implement core 32-byte Value with union + handler
2. **Week 2**: Implement handlers for all primitive types (int, float, etc.)
3. **Week 3**: Implement handlers for complex types (string, vector, matrix, etc.)
4. **Week 4**: Integration testing with existing codebase
5. **Week 5**: Performance benchmarking and optimization
6. **Week 6**: Production rollout with feature flag

## Advantages Over Manual Flag Approach

| Issue | Manual Flags | Handler Approach |
|-------|--------------|------------------|
| **Corruption resilience** | ❌ Single bit → crash | ✅ Function pointer invalid → null check catches it |
| **Type safety** | ❌ Any switch case can be taken | ✅ Handler function signature enforces correctness |
| **Code complexity** | ❌ Giant switch statements everywhere | ✅ Isolated per-type implementations |
| **Debugging** | ❌ "Which case triggered?" | ✅ "Which handler was called?" (visible in debugger) |
| **Testing** | ❌ Need to test all combinations | ✅ Test each handler independently |
| **Maintenance** | ❌ Add type = modify multiple switches | ✅ Add type = add one handler template |

## Questions Answered

> Use 24byte sbo, deprecate manual flags, Use TypeTraits<T> for type_id as done in linb::any, use vtable(lib::any) or handler(std::any) 8byte. This would fit to 32byte version and get simplyfied Value implementation?

**YES!** By removing the redundant `type_id_` field (4 bytes) and `flags_` byte, we get:
- 24 bytes: Union storage (inline OR heap)
- 8 bytes: Handler function pointer
- **Total: 32 bytes exactly**

The handler pointer serves triple duty:
1. **Storage location** (inline vs heap via function address)
2. **Type operations** (destroy/copy/move via Action parameter)
3. **Type identity** (via TypeId action returning type_id)

This is the optimal 32-byte design: safe, simple, and same size as the broken implementation.
