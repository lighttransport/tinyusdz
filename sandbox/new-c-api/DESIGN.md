# LightUSD C99 API Design Document

## Overview

This document describes the design of a minimal C99 API for LightUSD, providing a clean, dependency-free interface to USD functionality without requiring C++ knowledge or toolchains.

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
typedef struct lightusd_stage_t* lightusd_stage;
typedef struct lightusd_prim_t* lightusd_prim;
typedef struct lightusd_value_t* lightusd_value;
typedef struct lightusd_layer_t* lightusd_layer;
```

### Memory Management

- **Create/Destroy Pattern**: Every allocated object has explicit destroy function
- **Borrowed References**: Most getters return borrowed references (no ownership transfer)
- **Explicit Ownership**: Functions that transfer ownership are clearly named (_take, _copy)

### Error Handling

```c
typedef enum {
    LIGHTUSD_SUCCESS = 0,
    LIGHTUSD_ERROR_FILE_NOT_FOUND = -1,
    LIGHTUSD_ERROR_PARSE_FAILED = -2,
    LIGHTUSD_ERROR_OUT_OF_MEMORY = -3,
    LIGHTUSD_ERROR_INVALID_ARGUMENT = -4,
    LIGHTUSD_ERROR_NOT_SUPPORTED = -5,
    LIGHTUSD_ERROR_INTERNAL = -99
} lightusd_result;
```

## API Structure

### 1. Core Types (defined in C)

```c
// USD format types
typedef enum {
    LIGHTUSD_FORMAT_AUTO = 0,
    LIGHTUSD_FORMAT_USDA,  // ASCII
    LIGHTUSD_FORMAT_USDC,  // Binary/Crate
    LIGHTUSD_FORMAT_USDZ   // Zip archive
} lightusd_format;

// Prim types
typedef enum {
    LIGHTUSD_PRIM_UNKNOWN = 0,
    LIGHTUSD_PRIM_XFORM,
    LIGHTUSD_PRIM_MESH,
    LIGHTUSD_PRIM_MATERIAL,
    LIGHTUSD_PRIM_SHADER,
    LIGHTUSD_PRIM_CAMERA,
    LIGHTUSD_PRIM_LIGHT,
    LIGHTUSD_PRIM_SKELETON,
    LIGHTUSD_PRIM_SKELROOT,
    LIGHTUSD_PRIM_SKELANIMATION,
    LIGHTUSD_PRIM_SCOPE,
    LIGHTUSD_PRIM_GEOMSUBSET
} lightusd_prim_type;

// Value types
typedef enum {
    LIGHTUSD_VALUE_NONE = 0,
    LIGHTUSD_VALUE_BOOL,
    LIGHTUSD_VALUE_INT,
    LIGHTUSD_VALUE_UINT,
    LIGHTUSD_VALUE_FLOAT,
    LIGHTUSD_VALUE_DOUBLE,
    LIGHTUSD_VALUE_STRING,
    LIGHTUSD_VALUE_TOKEN,
    LIGHTUSD_VALUE_ASSET_PATH,
    LIGHTUSD_VALUE_FLOAT2,
    LIGHTUSD_VALUE_FLOAT3,
    LIGHTUSD_VALUE_FLOAT4,
    LIGHTUSD_VALUE_DOUBLE2,
    LIGHTUSD_VALUE_DOUBLE3,
    LIGHTUSD_VALUE_DOUBLE4,
    LIGHTUSD_VALUE_MATRIX3F,
    LIGHTUSD_VALUE_MATRIX4F,
    LIGHTUSD_VALUE_MATRIX3D,
    LIGHTUSD_VALUE_MATRIX4D,
    LIGHTUSD_VALUE_QUATF,
    LIGHTUSD_VALUE_QUATD,
    LIGHTUSD_VALUE_ARRAY  // Arrays are typed arrays
} lightusd_value_type;

// Load options
typedef struct {
    size_t max_memory_limit_mb;  // 0 = no limit
    int max_depth;                // Composition depth limit, 0 = default
    int enable_composition;       // 1 = resolve references/payloads
    int strict_mode;              // 1 = fail on any warning
} lightusd_load_options;
```

### 2. Tier 1: Minimal Viable API (10 functions)

```c
// Initialization and cleanup
lightusd_result lightusd_init(void);
void lightusd_shutdown(void);

// Loading
lightusd_result lightusd_load_from_file(
    const char* filepath,
    const lightusd_load_options* options,  // can be NULL for defaults
    lightusd_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

lightusd_result lightusd_load_from_memory(
    const void* data,
    size_t size,
    lightusd_format format,
    const lightusd_load_options* options,
    lightusd_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

void lightusd_stage_free(lightusd_stage stage);

// Basic navigation
lightusd_prim lightusd_stage_get_root_prim(lightusd_stage stage);
size_t lightusd_prim_get_child_count(lightusd_prim prim);
lightusd_prim lightusd_prim_get_child_at(lightusd_prim prim, size_t index);
const char* lightusd_prim_get_name(lightusd_prim prim);
lightusd_prim_type lightusd_prim_get_type(lightusd_prim prim);
```

### 3. Tier 2: Core Functionality (11 functions)

```c
// Path operations
const char* lightusd_prim_get_path(lightusd_prim prim);
lightusd_prim lightusd_stage_get_prim_at_path(lightusd_stage stage, const char* path);

// Type checking
int lightusd_prim_is_type(lightusd_prim prim, lightusd_prim_type type);
const char* lightusd_prim_get_type_name(lightusd_prim prim);

// Properties
size_t lightusd_prim_get_property_count(lightusd_prim prim);
const char* lightusd_prim_get_property_name_at(lightusd_prim prim, size_t index);
lightusd_value lightusd_prim_get_property(lightusd_prim prim, const char* name);
void lightusd_value_free(lightusd_value value);

// Value access
lightusd_value_type lightusd_value_get_type(lightusd_value value);
lightusd_result lightusd_value_get_float3(lightusd_value value, float* out_xyz);
lightusd_result lightusd_value_get_string(lightusd_value value, const char** out_str);
```

### 4. Tier 3: Extended API (15+ functions)

```c
// Mesh specific
lightusd_result lightusd_mesh_get_points(lightusd_prim mesh, float** out_points, size_t* out_count);
lightusd_result lightusd_mesh_get_face_counts(lightusd_prim mesh, int** out_counts, size_t* out_count);
lightusd_result lightusd_mesh_get_indices(lightusd_prim mesh, int** out_indices, size_t* out_count);
lightusd_result lightusd_mesh_get_normals(lightusd_prim mesh, float** out_normals, size_t* out_count);
lightusd_result lightusd_mesh_get_uvs(lightusd_prim mesh, float** out_uvs, size_t* out_count, int primvar_index);

// Transform
lightusd_result lightusd_xform_get_matrix(lightusd_prim xform, double* out_matrix4x4);
lightusd_result lightusd_xform_get_transform_ops(lightusd_prim xform, /* ... */);

// Material & Shading
lightusd_prim lightusd_prim_get_material(lightusd_prim prim);
lightusd_prim lightusd_material_get_surface_shader(lightusd_prim material);
lightusd_value lightusd_shader_get_input(lightusd_prim shader, const char* name);

// Animation & Time samples
int lightusd_stage_has_animation(lightusd_stage stage);
lightusd_result lightusd_stage_get_time_range(lightusd_stage stage, double* start, double* end);
lightusd_result lightusd_value_get_time_samples(lightusd_value value, double** out_times, size_t* count);

// Writing (future)
lightusd_result lightusd_stage_export_to_file(lightusd_stage stage, const char* filepath, lightusd_format format);
```

## Implementation Strategy

### Phase 1: Core Implementation (lightusd_c.h/c)
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
const char* name = lightusd_prim_get_name(prim);  // Do NOT free
// name is valid as long as prim is valid
```

### Pattern 2: Allocated Data (for arrays)
```c
float* points = NULL;
size_t count = 0;
if (lightusd_mesh_get_points(mesh, &points, &count) == LIGHTUSD_SUCCESS) {
    // Use points...
    lightusd_free(points);  // Must free when done
}
```

### Pattern 3: Handle Lifetime
```c
lightusd_stage stage = NULL;
if (lightusd_load_from_file("model.usd", NULL, &stage, NULL, 0) == LIGHTUSD_SUCCESS) {
    lightusd_prim root = lightusd_stage_get_root_prim(stage);  // Borrowed from stage
    // Use root... (valid only while stage exists)
    lightusd_stage_free(stage);  // Invalidates all prims from this stage
}
```

## Thread Safety

- **Immutable Access**: Reading from stages/prims is thread-safe
- **No Implicit State**: No global state modified by API calls
- **Explicit Contexts**: Future: lightusd_context for thread-local state if needed

## Error Handling Examples

### Simple (ignore errors)
```c
lightusd_stage stage = NULL;
lightusd_load_from_file("model.usd", NULL, &stage, NULL, 0);
if (stage) {
    // Use stage...
    lightusd_stage_free(stage);
}
```

### Detailed (capture errors)
```c
char error[1024];
lightusd_stage stage = NULL;
lightusd_result result = lightusd_load_from_file("model.usd", NULL, &stage, error, sizeof(error));
if (result != LIGHTUSD_SUCCESS) {
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