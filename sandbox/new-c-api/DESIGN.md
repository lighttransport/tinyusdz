# TinyUSDZ C99 API Design Document

## Overview

This document describes the design of a minimal C99 API for TinyUSDZ, providing a clean, dependency-free interface to USD functionality without requiring C++ knowledge or toolchains.

## Design Principles

1. **C99 Standard Compliance**: Pure C99, no C++ dependencies in headers
2. **Minimal Surface Area**: Focus on core USD operations only
3. **Opaque Handles**: Hide implementation details, allow ABI stability
4. **Direct C Types**: Define enums and structs in C to avoid binding overhead
5. **Simple Error Handling**: Return codes + optional error strings
6. **Zero-Copy Where Possible**: Minimize memory allocation and copying
7. **Thread-Safe Design**: Immutable data access, explicit mutability

## Core Concepts

### Handle System

All C++ objects are wrapped in opaque handles:

```c
typedef struct tusdz_stage_t* tusdz_stage;
typedef struct tusdz_prim_t* tusdz_prim;
typedef struct tusdz_value_t* tusdz_value;
typedef struct tusdz_layer_t* tusdz_layer;
```

### Memory Management

- **Create/Destroy Pattern**: Every allocated object has explicit destroy function
- **Borrowed References**: Most getters return borrowed references (no ownership transfer)
- **Explicit Ownership**: Functions that transfer ownership are clearly named (_take, _copy)

### Error Handling

```c
typedef enum {
    TUSDZ_SUCCESS = 0,
    TUSDZ_ERROR_FILE_NOT_FOUND = -1,
    TUSDZ_ERROR_PARSE_FAILED = -2,
    TUSDZ_ERROR_OUT_OF_MEMORY = -3,
    TUSDZ_ERROR_INVALID_ARGUMENT = -4,
    TUSDZ_ERROR_NOT_SUPPORTED = -5,
    TUSDZ_ERROR_INTERNAL = -99
} tusdz_result;
```

## API Structure

### 1. Core Types (defined in C)

```c
// USD format types
typedef enum {
    TUSDZ_FORMAT_AUTO = 0,
    TUSDZ_FORMAT_USDA,  // ASCII
    TUSDZ_FORMAT_USDC,  // Binary/Crate
    TUSDZ_FORMAT_USDZ   // Zip archive
} tusdz_format;

// Prim types
typedef enum {
    TUSDZ_PRIM_UNKNOWN = 0,
    TUSDZ_PRIM_XFORM,
    TUSDZ_PRIM_MESH,
    TUSDZ_PRIM_MATERIAL,
    TUSDZ_PRIM_SHADER,
    TUSDZ_PRIM_CAMERA,
    TUSDZ_PRIM_LIGHT,
    TUSDZ_PRIM_SKELETON,
    TUSDZ_PRIM_SKELROOT,
    TUSDZ_PRIM_SKELANIMATION,
    TUSDZ_PRIM_SCOPE,
    TUSDZ_PRIM_GEOMSUBSET
} tusdz_prim_type;

// Value types
typedef enum {
    TUSDZ_VALUE_NONE = 0,
    TUSDZ_VALUE_BOOL,
    TUSDZ_VALUE_INT,
    TUSDZ_VALUE_UINT,
    TUSDZ_VALUE_FLOAT,
    TUSDZ_VALUE_DOUBLE,
    TUSDZ_VALUE_STRING,
    TUSDZ_VALUE_TOKEN,
    TUSDZ_VALUE_ASSET_PATH,
    TUSDZ_VALUE_FLOAT2,
    TUSDZ_VALUE_FLOAT3,
    TUSDZ_VALUE_FLOAT4,
    TUSDZ_VALUE_DOUBLE2,
    TUSDZ_VALUE_DOUBLE3,
    TUSDZ_VALUE_DOUBLE4,
    TUSDZ_VALUE_MATRIX3F,
    TUSDZ_VALUE_MATRIX4F,
    TUSDZ_VALUE_MATRIX3D,
    TUSDZ_VALUE_MATRIX4D,
    TUSDZ_VALUE_QUATF,
    TUSDZ_VALUE_QUATD,
    TUSDZ_VALUE_ARRAY  // Arrays are typed arrays
} tusdz_value_type;

// Load options
typedef struct {
    size_t max_memory_limit_mb;  // 0 = no limit
    int max_depth;                // Composition depth limit, 0 = default
    int enable_composition;       // 1 = resolve references/payloads
    int strict_mode;              // 1 = fail on any warning
} tusdz_load_options;
```

### 2. Tier 1: Minimal Viable API (10 functions)

```c
// Initialization and cleanup
tusdz_result tusdz_init(void);
void tusdz_shutdown(void);

// Loading
tusdz_result tusdz_load_from_file(
    const char* filepath,
    const tusdz_load_options* options,  // can be NULL for defaults
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

tusdz_result tusdz_load_from_memory(
    const void* data,
    size_t size,
    tusdz_format format,
    const tusdz_load_options* options,
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

void tusdz_stage_free(tusdz_stage stage);

// Basic navigation
tusdz_prim tusdz_stage_get_root_prim(tusdz_stage stage);
size_t tusdz_prim_get_child_count(tusdz_prim prim);
tusdz_prim tusdz_prim_get_child_at(tusdz_prim prim, size_t index);
const char* tusdz_prim_get_name(tusdz_prim prim);
tusdz_prim_type tusdz_prim_get_type(tusdz_prim prim);
```

### 3. Tier 2: Core Functionality (11 functions)

```c
// Path operations
const char* tusdz_prim_get_path(tusdz_prim prim);
tusdz_prim tusdz_stage_get_prim_at_path(tusdz_stage stage, const char* path);

// Type checking
int tusdz_prim_is_type(tusdz_prim prim, tusdz_prim_type type);
const char* tusdz_prim_get_type_name(tusdz_prim prim);

// Properties
size_t tusdz_prim_get_property_count(tusdz_prim prim);
const char* tusdz_prim_get_property_name_at(tusdz_prim prim, size_t index);
tusdz_value tusdz_prim_get_property(tusdz_prim prim, const char* name);
void tusdz_value_free(tusdz_value value);

// Value access
tusdz_value_type tusdz_value_get_type(tusdz_value value);
tusdz_result tusdz_value_get_float3(tusdz_value value, float* out_xyz);
tusdz_result tusdz_value_get_string(tusdz_value value, const char** out_str);
```

### 4. Tier 3: Extended API (15+ functions)

```c
// Mesh specific
tusdz_result tusdz_mesh_get_points(tusdz_prim mesh, float** out_points, size_t* out_count);
tusdz_result tusdz_mesh_get_face_counts(tusdz_prim mesh, int** out_counts, size_t* out_count);
tusdz_result tusdz_mesh_get_indices(tusdz_prim mesh, int** out_indices, size_t* out_count);
tusdz_result tusdz_mesh_get_normals(tusdz_prim mesh, float** out_normals, size_t* out_count);
tusdz_result tusdz_mesh_get_uvs(tusdz_prim mesh, float** out_uvs, size_t* out_count, int primvar_index);

// Transform
tusdz_result tusdz_xform_get_matrix(tusdz_prim xform, double* out_matrix4x4);
tusdz_result tusdz_xform_get_transform_ops(tusdz_prim xform, /* ... */);

// Material & Shading
tusdz_prim tusdz_prim_get_material(tusdz_prim prim);
tusdz_prim tusdz_material_get_surface_shader(tusdz_prim material);
tusdz_value tusdz_shader_get_input(tusdz_prim shader, const char* name);

// Animation & Time samples
int tusdz_stage_has_animation(tusdz_stage stage);
tusdz_result tusdz_stage_get_time_range(tusdz_stage stage, double* start, double* end);
tusdz_result tusdz_value_get_time_samples(tusdz_value value, double** out_times, size_t* count);

// Writing (future)
tusdz_result tusdz_stage_export_to_file(tusdz_stage stage, const char* filepath, tusdz_format format);
```

## Implementation Strategy

### Phase 1: Core Implementation (tinyusdz_c.h/c)
1. Define all enums and structs in header
2. Implement opaque handle wrappers
3. Core loading and traversal functions
4. Basic error handling

### Phase 2: Extended Types
1. Mesh data access
2. Transform operations
3. Material/shader access
4. Animation queries

### Phase 3: Advanced Features
1. Composition control
2. Layer access
3. Value arrays and complex types
4. Writing support

## Memory Management Patterns

### Pattern 1: Borrowed References (most common)
```c
const char* name = tusdz_prim_get_name(prim);  // Do NOT free
// name is valid as long as prim is valid
```

### Pattern 2: Allocated Data (for arrays)
```c
float* points = NULL;
size_t count = 0;
if (tusdz_mesh_get_points(mesh, &points, &count) == TUSDZ_SUCCESS) {
    // Use points...
    tusdz_free(points);  // Must free when done
}
```

### Pattern 3: Handle Lifetime
```c
tusdz_stage stage = NULL;
if (tusdz_load_from_file("model.usd", NULL, &stage, NULL, 0) == TUSDZ_SUCCESS) {
    tusdz_prim root = tusdz_stage_get_root_prim(stage);  // Borrowed from stage
    // Use root... (valid only while stage exists)
    tusdz_stage_free(stage);  // Invalidates all prims from this stage
}
```

## Thread Safety

- **Immutable Access**: Reading from stages/prims is thread-safe
- **No Implicit State**: No global state modified by API calls
- **Explicit Contexts**: Future: tusdz_context for thread-local state if needed

## Error Handling Examples

### Simple (ignore errors)
```c
tusdz_stage stage = NULL;
tusdz_load_from_file("model.usd", NULL, &stage, NULL, 0);
if (stage) {
    // Use stage...
    tusdz_stage_free(stage);
}
```

### Detailed (capture errors)
```c
char error[1024];
tusdz_stage stage = NULL;
tusdz_result result = tusdz_load_from_file("model.usd", NULL, &stage, error, sizeof(error));
if (result != TUSDZ_SUCCESS) {
    fprintf(stderr, "Failed to load USD: %s (code: %d)\n", error, result);
    return -1;
}
```

## Advantages of This Design

1. **No C++ Dependencies**: Users only need C99 compiler
2. **ABI Stable**: Opaque handles allow implementation changes
3. **Minimal Overhead**: Direct mapping to C++ internals
4. **Clear Ownership**: Explicit memory management
5. **Gradual Adoption**: Start with Tier 1, add features as needed
6. **Type Safe**: Enums prevent invalid values
7. **Future Proof**: Can extend without breaking existing code

## Implementation Notes

- Use `extern "C"` blocks in implementation (.c file can be .cpp internally)
- Keep internal C++ headers separate from C API header
- Validate all inputs to prevent C++ exceptions from escaping
- Use PIMPL pattern for opaque types
- Consider code generation for repetitive accessors

## Testing Strategy

1. **Unit Tests**: Test each function in isolation
2. **Integration Tests**: Load real USD files, traverse, extract data
3. **Memory Tests**: Valgrind/ASAN to verify no leaks
4. **Thread Tests**: Concurrent read access verification
5. **Error Tests**: Invalid inputs, corrupted files, edge cases
6. **Compatibility Tests**: Ensure C99 compliance (no C11/C++ features)

## Documentation Requirements

- Doxygen comments for all public APIs
- Simple examples for each tier
- Migration guide from C++ API
- Performance characteristics documented
- Memory ownership clearly stated

## Future Considerations

- Python bindings via ctypes (trivial with C API)
- WebAssembly compilation (already C, easier than C++)
- Dynamic loading support (clean ABI)
- Extension mechanism for custom prims/schemas
- Async/streaming loading for large files