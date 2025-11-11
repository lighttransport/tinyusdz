# TinyUSDZ Python ABI3 Binding - Technical Design

## Overview

This document describes the technical design and architecture of the TinyUSDZ Python ABI3 binding experiment.

## Goals

1. **Stable ABI Compatibility**: Build once, run on Python 3.10+
2. **No Python Dev Dependencies**: No need for Python development headers at build time
3. **NumPy-Friendly**: Zero-copy array access via buffer protocol
4. **Efficient Memory Management**: RAII on C++ side, ref counting on Python side
5. **Minimal Footprint**: Small binary size, minimal runtime overhead

## Architecture

### Layer Overview

```
┌─────────────────────────────────────────┐
│         Python Application              │
│  (user code, NumPy, pandas, etc.)      │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│     Python ABI3 Binding Layer           │
│  (tinyusdz_abi3.c + py_limited_api.h)  │
│  • Stage, Prim, Value wrapper objects   │
│  • Buffer protocol implementation       │
│  • Reference counting management        │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│         TinyUSDZ C API                  │
│         (c-tinyusd.h/cc)                │
│  • C-friendly wrapper for C++ API       │
│  • Opaque pointer types                 │
│  • Manual memory management             │
└─────────────────┬───────────────────────┘
                  │
                  ▼
┌─────────────────────────────────────────┐
│      TinyUSDZ C++ Core Library          │
│  • USD parsing (USDA, USDC, USDZ)      │
│  • Stage, Prim, Value classes           │
│  • RAII memory management               │
└─────────────────────────────────────────┘
```

## Memory Management Strategy

### C++ Side (RAII)

The TinyUSDZ core library uses C++ RAII:

```cpp
// C++ side - automatic cleanup
{
    Stage stage;
    Prim prim("Mesh");
    // Automatically cleaned up when scope exits
}
```

The C API wraps this with manual management:

```c
// C API - manual management
CTinyUSDStage *stage = c_tinyusd_stage_new();
// ... use stage ...
c_tinyusd_stage_free(stage);  // Explicitly free
```

### Python Side (Reference Counting)

Python objects wrap C API pointers and use reference counting:

```python
# Python side - automatic via ref counting
stage = tusd.Stage()  # Creates C++ object, refcount = 1
# ... use stage ...
# When refcount reaches 0, __del__ is called
# which calls c_tinyusd_stage_free()
```

The binding layer manages the lifetime:

```c
typedef struct {
    PyObject_HEAD
    CTinyUSDStage *stage;  // Pointer to C++ object
} TinyUSDStageObject;

static void
TinyUSDStage_dealloc(TinyUSDStageObject *self)
{
    if (self->stage) {
        c_tinyusd_stage_free(self->stage);  // Free C++ object
        self->stage = NULL;
    }
    Py_TYPE(self)->tp_free((PyObject *)self);  // Free Python object
}
```

### Reference Cycle Handling

For objects with parent-child relationships:

```python
# Parent holds strong reference to children
stage = tusd.Stage()
prim = tusd.Prim("Mesh")
stage.add_prim(prim)  # stage holds reference

# prim can still be used independently
print(prim.type)

# When stage is deleted, it releases children
del stage  # prim may or may not be deleted, depending on other refs
```

## Buffer Protocol Implementation

The buffer protocol enables zero-copy array access for NumPy and other libraries.

### ValueArray Object

```c
typedef struct {
    PyObject_HEAD
    void *data;           // Pointer to array data (owned by C++)
    Py_ssize_t length;    // Number of elements
    Py_ssize_t itemsize;  // Size per element
    int readonly;         // Read-only flag
    char *format;         // Format string (e.g., "f", "fff")
    CTinyUSDValueType value_type;  // TinyUSDZ type
    PyObject *owner;      // Owner object (keeps C++ data alive)
} TinyUSDValueArrayObject;
```

### Buffer Protocol Methods

```c
static int
TinyUSDValueArray_getbuffer(TinyUSDValueArrayObject *self,
                            Py_buffer *view, int flags)
{
    // Fill in buffer info
    view->buf = self->data;           // Direct pointer to C++ data
    view->len = self->length * self->itemsize;
    view->itemsize = self->itemsize;
    view->format = get_format_string(self->value_type);
    view->ndim = 1;
    view->shape = &self->length;
    view->strides = &self->itemsize;

    Py_INCREF(self);  // Keep object alive while buffer is used
    return 0;
}

static void
TinyUSDValueArray_releasebuffer(TinyUSDValueArrayObject *self,
                                Py_buffer *view)
{
    // Nothing to do - data is managed by owner
}
```

### NumPy Integration

```python
# Python usage
positions = prim.get_attribute("points").get()  # Returns ValueArray

# Zero-copy conversion to NumPy
positions_np = np.asarray(positions)

# Data is shared:
# positions_np.data -> same memory as positions.data
# No copying, immediate access
```

### Format Strings

Format strings follow Python's struct format:

| TinyUSDZ Type | Format | Description |
|---------------|--------|-------------|
| `bool` | `?` | Boolean |
| `int` | `i` | 32-bit signed int |
| `float` | `f` | 32-bit float |
| `double` | `d` | 64-bit float |
| `half` | `e` | 16-bit half float |
| `float3` | `fff` | 3× 32-bit float |
| `float4` | `ffff` | 4× 32-bit float |

## Python Limited API (ABI3)

### What is ABI3?

Python's stable ABI (Application Binary Interface) defines a subset of the Python C API that:

1. **Remains stable** across Python versions (3.10, 3.11, 3.12, ...)
2. **Binary compatible** - one compiled module works with all versions
3. **Forward compatible** - works with future Python versions

### Custom Headers

We provide our own `py_limited_api.h` instead of using `<Python.h>`:

**Advantages:**
- No Python development package needed at build time
- Explicit about which APIs we use
- Easier to audit and understand dependencies
- Portable across build environments

**Contents:**
- Type definitions (`PyObject`, `PyTypeObject`, etc.)
- Function declarations (marked with `PyAPI_FUNC`)
- Macros and constants

### Platform Considerations

#### Linux

```c
#define PyAPI_FUNC(RTYPE) __attribute__((visibility("default"))) RTYPE
```

- Functions resolved at runtime via `dlopen`
- No need to link against `libpython3.so` at build time
- Module dynamically links to Python runtime when imported

#### Windows

```c
#define PyAPI_FUNC(RTYPE) __declspec(dllimport) RTYPE
```

- Need to link against `python3.lib` or `python310.lib`
- DLL import directives for function resolution

#### macOS

Similar to Linux with dylib instead of .so

### Build Configuration

**setup.py:**
```python
ext_modules = [
    Extension(
        name='tinyusdz_abi3',
        sources=['src/tinyusdz_abi3.c'],
        define_macros=[('Py_LIMITED_API', '0x030a0000')],
        py_limited_api=True,  # Enable stable ABI
    )
]
```

**CMake:**
```cmake
target_compile_definitions(tinyusdz_abi3 PRIVATE
    Py_LIMITED_API=0x030a0000  # Python 3.10+ API version
)
```

## Type System Mapping

### USD to Python Type Mapping

| USD Type | C Type | Python Type | NumPy dtype |
|----------|--------|-------------|-------------|
| `bool` | `uint8_t` | `bool` | `bool` |
| `int` | `int32_t` | `int` | `int32` |
| `float` | `float` | `float` | `float32` |
| `double` | `double` | `float` | `float64` |
| `token` | `c_tinyusd_token_t*` | `str` | - |
| `string` | `c_tinyusd_string_t*` | `str` | - |
| `float3` | `c_tinyusd_float3_t` | `ValueArray` | `(3,) float32` |
| `float3[]` | `c_tinyusd_float3_t*` | `ValueArray` | `(N, 3) float32` |

### Scalar Values

```python
# Python -> C -> C++
val = tusd.Value.from_int(42)
# → PyLong_AsLong(42)
# → c_tinyusd_value_new_int(42)
# → new Value(42)

# C++ -> C -> Python
result = val.as_int()
# → c_tinyusd_value_as_int(value, &out)
# → PyLong_FromLong(out)
# → 42
```

### Array Values

```python
# C++ -> C -> Python (zero-copy)
positions = prim.get_attribute("points").get()
# → C++: const std::vector<GfVec3f>& data
# → C: Creates ValueArray pointing to data
# → Python: Wraps pointer, exposes via buffer protocol

# NumPy access (zero-copy)
np_positions = np.asarray(positions)
# → Calls __getbuffer__
# → Returns pointer to same data
# → NumPy wraps pointer as ndarray
```

## Error Handling

### C API Level

```c
int c_tinyusd_load_usd_from_file(
    const char *filename,
    CTinyUSDStage *stage,
    c_tinyusd_string_t *warn,
    c_tinyusd_string_t *err)
{
    // Returns 1 for success, 0 for failure
    // Populates err string on failure
}
```

### Python Binding Level

```c
static PyObject *
TinyUSDStage_load_from_file(PyTypeObject *type, PyObject *args)
{
    // ...
    int ret = c_tinyusd_load_usd_from_file(filename, stage, warn, err);

    if (!ret) {
        const char *err_str = c_tinyusd_string_str(err);
        PyErr_SetString(PyExc_RuntimeError, err_str);
        // Clean up
        return NULL;  // Python will raise exception
    }

    return (PyObject *)self;
}
```

### Python Level

```python
try:
    stage = tusd.Stage.load_from_file("invalid.usd")
except RuntimeError as e:
    print(f"Failed to load: {e}")
```

## Performance Considerations

### Zero-Copy Data Access

Traditional approach (copying):
```
C++ vector → C array copy → Python list copy → NumPy array
```

Our approach (zero-copy):
```
C++ vector → C pointer wrapper → Python buffer view → NumPy array
```

**Memory:** 1× vs 3×
**Time:** O(1) vs O(n)

### Reference Counting Overhead

Python reference counting has minimal overhead:
- Increment/decrement are atomic operations
- No GC pauses (Python uses ref counting + cycle detection)
- Predictable cleanup timing

### Type Safety

The binding provides:
1. **Compile-time type safety** (C type checking)
2. **Runtime type safety** (Python type checking)
3. **Buffer format validation** (NumPy dtype checking)

## Future Enhancements

### 1. Complete Attribute API

```python
# Get attributes with buffer protocol
positions = mesh.get_attribute("points").get()
normals = mesh.get_attribute("normals").get()

# Set attributes (if writable)
mesh.get_attribute("points").set(new_positions)
```

### 2. Stage Traversal

```python
# Iterate over prims
for prim in stage.traverse():
    print(prim.path, prim.type)
```

### 3. Relationship Support

```python
# Material binding
material_rel = mesh.get_relationship("material:binding")
material_path = material_rel.get_targets()[0]
```

### 4. Composition Arcs

```python
# References
prim.add_reference("asset.usd", "/Root/Mesh")

# Payloads
prim.add_payload("heavy_data.usd", "/BigMesh")
```

### 5. Type Stubs

```python
# .pyi files for IDE support
class Stage:
    def load_from_file(cls, filename: str) -> Stage: ...
    def to_string(self) -> str: ...
```

### 6. Async I/O

```python
# Async loading for large files
async def load_scene():
    stage = await tusd.Stage.load_from_file_async("huge.usd")
    return stage
```

## Testing Strategy

### Unit Tests
- C API correctness
- Memory leak detection (valgrind)
- Type conversion accuracy

### Integration Tests
- NumPy interoperability
- Large file handling
- Multi-threading safety

### Performance Tests
- Loading speed vs pxrUSD
- Memory usage profiling
- Buffer protocol overhead

## References

- [Python Stable ABI Documentation](https://docs.python.org/3/c-api/stable.html)
- [Python Buffer Protocol](https://docs.python.org/3/c-api/buffer.html)
- [NumPy Array Interface](https://numpy.org/doc/stable/reference/arrays.interface.html)
- [TinyUSDZ Documentation](https://github.com/syoyo/tinyusdz)
