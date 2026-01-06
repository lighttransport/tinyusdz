# TinyUSDZ C99 API Reference

Complete reference documentation for the TinyUSDZ C API.

## Table of Contents

1. [Initialization](#initialization)
2. [Loading](#loading)
3. [Stage Operations](#stage-operations)
4. [Prim Operations](#prim-operations)
5. [Value Operations](#value-operations)
6. [Mesh Operations](#mesh-operations)
7. [Transform Operations](#transform-operations)
8. [Material and Shader Operations](#material-and-shader-operations)
9. [Animation Operations](#animation-operations)
10. [Utilities](#utilities)

## Initialization

### tusdz_init()

Initialize the TinyUSDZ library. Must be called before using any other functions.

```c
tusdz_result tusdz_init(void);
```

**Returns:** `TUSDZ_SUCCESS` on success

**Example:**
```c
if (tusdz_init() != TUSDZ_SUCCESS) {
    fprintf(stderr, "Failed to initialize\n");
    return 1;
}
```

### tusdz_shutdown()

Shutdown the library and free global resources.

```c
void tusdz_shutdown(void);
```

**Example:**
```c
tusdz_shutdown();
```

### tusdz_get_version()

Get library version string.

```c
const char* tusdz_get_version(void);
```

**Returns:** Version string like "1.0.0"

**Example:**
```c
printf("TinyUSDZ version: %s\n", tusdz_get_version());
```

## Loading

### tusdz_load_from_file()

Load USD file from disk.

```c
tusdz_result tusdz_load_from_file(
    const char* filepath,
    const tusdz_load_options* options,
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);
```

**Parameters:**
- `filepath`: Path to USD file
- `options`: Load options (NULL for defaults)
- `out_stage`: Output stage handle
- `error_buf`: Buffer for error message (NULL to ignore errors)
- `error_buf_size`: Size of error buffer

**Returns:** Result code

**Example:**
```c
tusdz_stage stage = NULL;
char error[1024];
tusdz_result result = tusdz_load_from_file(
    "model.usd",
    NULL,  // Use default options
    &stage,
    error,
    sizeof(error)
);

if (result != TUSDZ_SUCCESS) {
    fprintf(stderr, "Error: %s\n", error);
} else {
    // Use stage...
    tusdz_stage_free(stage);
}
```

### tusdz_load_from_memory()

Load USD from memory buffer.

```c
tusdz_result tusdz_load_from_memory(
    const void* data,
    size_t size,
    tusdz_format format,
    const tusdz_load_options* options,
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);
```

**Parameters:**
- `data`: Memory buffer containing USD data
- `size`: Size of buffer in bytes
- `format`: Format of the data (`TUSDZ_FORMAT_USDA`, `TUSDZ_FORMAT_USDC`, `TUSDZ_FORMAT_USDZ`, or `TUSDZ_FORMAT_AUTO`)
- `options`: Load options (NULL for defaults)
- `out_stage`: Output stage handle
- `error_buf`: Buffer for error message
- `error_buf_size`: Size of error buffer

**Returns:** Result code

**Example:**
```c
const uint8_t* data = /* ... */;
size_t size = /* ... */;

tusdz_stage stage = NULL;
tusdz_result result = tusdz_load_from_memory(
    data, size, TUSDZ_FORMAT_AUTO, NULL, &stage, NULL, 0
);

if (result == TUSDZ_SUCCESS && stage) {
    // Use stage...
    tusdz_stage_free(stage);
}
```

## Stage Operations

### tusdz_stage_free()

Free a stage and all associated resources.

```c
void tusdz_stage_free(tusdz_stage stage);
```

**Example:**
```c
tusdz_stage_free(stage);
```

### tusdz_stage_get_root_prim()

Get the root prim of the stage.

```c
tusdz_prim tusdz_stage_get_root_prim(tusdz_stage stage);
```

**Returns:** Root prim (borrowed reference, do not free)

**Example:**
```c
tusdz_prim root = tusdz_stage_get_root_prim(stage);
if (root) {
    printf("Root prim: %s\n", tusdz_prim_get_name(root));
}
```

### tusdz_stage_get_prim_at_path()

Get prim at specific path.

```c
tusdz_prim tusdz_stage_get_prim_at_path(tusdz_stage stage, const char* path);
```

**Parameters:**
- `stage`: Stage handle
- `path`: Prim path (e.g., "/World/Geo/Mesh")

**Returns:** Prim handle or NULL if not found

**Example:**
```c
tusdz_prim prim = tusdz_stage_get_prim_at_path(stage, "/World/Cube");
if (prim) {
    printf("Found: %s\n", tusdz_prim_get_name(prim));
}
```

### tusdz_stage_has_animation()

Check if stage has animation.

```c
int tusdz_stage_has_animation(tusdz_stage stage);
```

**Returns:** 1 if animated, 0 otherwise

### tusdz_stage_get_time_range()

Get animation time range.

```c
tusdz_result tusdz_stage_get_time_range(
    tusdz_stage stage,
    double* out_start_time,
    double* out_end_time,
    double* out_fps
);
```

**Example:**
```c
double start, end, fps;
if (tusdz_stage_get_time_range(stage, &start, &end, &fps) == TUSDZ_SUCCESS) {
    printf("Animation: %.1f to %.1f @ %.1f fps\n", start, end, fps);
}
```

## Prim Operations

### tusdz_prim_get_name()

Get prim name.

```c
const char* tusdz_prim_get_name(tusdz_prim prim);
```

**Returns:** Name string (borrowed, do not free)

### tusdz_prim_get_path()

Get full path of prim.

```c
const char* tusdz_prim_get_path(tusdz_prim prim);
```

**Returns:** Path string (borrowed, do not free)

### tusdz_prim_get_type()

Get prim type enum.

```c
tusdz_prim_type tusdz_prim_get_type(tusdz_prim prim);
```

**Returns:** Prim type enum value

**Example:**
```c
tusdz_prim_type type = tusdz_prim_get_type(prim);
printf("Type: %s\n", tusdz_prim_type_to_string(type));
```

### tusdz_prim_get_type_name()

Get prim type name as string.

```c
const char* tusdz_prim_get_type_name(tusdz_prim prim);
```

**Returns:** Type name (e.g., "Mesh", "Xform")

### tusdz_prim_is_type()

Check if prim is specific type.

```c
int tusdz_prim_is_type(tusdz_prim prim, tusdz_prim_type type);
```

**Returns:** 1 if matches, 0 otherwise

**Example:**
```c
if (tusdz_prim_is_type(prim, TUSDZ_PRIM_MESH)) {
    printf("This is a mesh!\n");
}
```

### tusdz_prim_get_child_count()

Get number of child prims.

```c
size_t tusdz_prim_get_child_count(tusdz_prim prim);
```

### tusdz_prim_get_child_at()

Get child prim at index.

```c
tusdz_prim tusdz_prim_get_child_at(tusdz_prim prim, size_t index);
```

**Example:**
```c
size_t count = tusdz_prim_get_child_count(prim);
for (size_t i = 0; i < count; i++) {
    tusdz_prim child = tusdz_prim_get_child_at(prim, i);
    printf("Child: %s\n", tusdz_prim_get_name(child));
}
```

### tusdz_prim_get_property_count()

Get number of properties on prim.

```c
size_t tusdz_prim_get_property_count(tusdz_prim prim);
```

### tusdz_prim_get_property_name_at()

Get property name at index.

```c
const char* tusdz_prim_get_property_name_at(tusdz_prim prim, size_t index);
```

### tusdz_prim_get_property()

Get property value by name.

```c
tusdz_value tusdz_prim_get_property(tusdz_prim prim, const char* name);
```

**Returns:** Value handle (must be freed with tusdz_value_free)

## Value Operations

### tusdz_value_free()

Free a value handle.

```c
void tusdz_value_free(tusdz_value value);
```

### tusdz_value_get_type()

Get value type.

```c
tusdz_value_type tusdz_value_get_type(tusdz_value value);
```

### tusdz_value_get_bool()

Extract boolean value.

```c
tusdz_result tusdz_value_get_bool(tusdz_value value, int* out);
```

### tusdz_value_get_int()

Extract integer value.

```c
tusdz_result tusdz_value_get_int(tusdz_value value, int* out);
```

### tusdz_value_get_float()

Extract float value.

```c
tusdz_result tusdz_value_get_float(tusdz_value value, float* out);
```

### tusdz_value_get_double()

Extract double value.

```c
tusdz_result tusdz_value_get_double(tusdz_value value, double* out);
```

### tusdz_value_get_string()

Extract string value.

```c
tusdz_result tusdz_value_get_string(tusdz_value value, const char** out);
```

**Returns:** `TUSDZ_SUCCESS` if successful

**Example:**
```c
const char* str;
if (tusdz_value_get_string(value, &str) == TUSDZ_SUCCESS) {
    printf("String value: %s\n", str);
}
```

### tusdz_value_get_float3()

Extract 3-component float vector.

```c
tusdz_result tusdz_value_get_float3(tusdz_value value, float* out_xyz);
```

**Example:**
```c
float xyz[3];
if (tusdz_value_get_float3(value, xyz) == TUSDZ_SUCCESS) {
    printf("Position: (%f, %f, %f)\n", xyz[0], xyz[1], xyz[2]);
}
```

## Mesh Operations

### tusdz_mesh_get_points()

Get mesh vertex positions.

```c
tusdz_result tusdz_mesh_get_points(
    tusdz_prim mesh,
    const float** out_points,
    size_t* out_count
);
```

**Parameters:**
- `mesh`: Mesh prim
- `out_points`: Pointer to points array (do not free)
- `out_count`: Number of float values (each point is 3 floats)

**Example:**
```c
const float* points;
size_t point_count;
if (tusdz_mesh_get_points(mesh, &points, &point_count) == TUSDZ_SUCCESS) {
    size_t num_vertices = point_count / 3;
    for (size_t i = 0; i < num_vertices; i++) {
        printf("Point %zu: (%f, %f, %f)\n",
               i, points[i*3], points[i*3+1], points[i*3+2]);
    }
}
```

### tusdz_mesh_get_face_counts()

Get face vertex counts.

```c
tusdz_result tusdz_mesh_get_face_counts(
    tusdz_prim mesh,
    const int** out_counts,
    size_t* out_count
);
```

### tusdz_mesh_get_indices()

Get face vertex indices.

```c
tusdz_result tusdz_mesh_get_indices(
    tusdz_prim mesh,
    const int** out_indices,
    size_t* out_count
);
```

### tusdz_mesh_get_normals()

Get mesh normals.

```c
tusdz_result tusdz_mesh_get_normals(
    tusdz_prim mesh,
    const float** out_normals,
    size_t* out_count
);
```

### tusdz_mesh_get_uvs()

Get mesh UV coordinates.

```c
tusdz_result tusdz_mesh_get_uvs(
    tusdz_prim mesh,
    const float** out_uvs,
    size_t* out_count,
    int primvar_index
);
```

## Transform Operations

### tusdz_xform_get_local_matrix()

Get local transformation matrix.

```c
tusdz_result tusdz_xform_get_local_matrix(
    tusdz_prim xform,
    double time,
    double* out_matrix
);
```

**Parameters:**
- `xform`: Transform prim
- `time`: Time for evaluation (0.0 for default)
- `out_matrix`: Output 4x4 matrix in column-major order

**Example:**
```c
double matrix[16];
if (tusdz_xform_get_local_matrix(xform, 0.0, matrix) == TUSDZ_SUCCESS) {
    // Use matrix for rendering
}
```

## Material and Shader Operations

### tusdz_prim_get_bound_material()

Get material bound to prim.

```c
tusdz_prim tusdz_prim_get_bound_material(tusdz_prim prim);
```

### tusdz_material_get_surface_shader()

Get surface shader from material.

```c
tusdz_prim tusdz_material_get_surface_shader(tusdz_prim material);
```

### tusdz_shader_get_input()

Get shader input value.

```c
tusdz_value tusdz_shader_get_input(tusdz_prim shader, const char* input_name);
```

### tusdz_shader_get_type_id()

Get shader type ID.

```c
const char* tusdz_shader_get_type_id(tusdz_prim shader);
```

## Animation Operations

### tusdz_value_is_animated()

Check if value has animation.

```c
int tusdz_value_is_animated(tusdz_value value);
```

### tusdz_value_eval_at_time()

Evaluate value at specific time.

```c
tusdz_value tusdz_value_eval_at_time(tusdz_value value, double time);
```

## Utilities

### tusdz_result_to_string()

Convert result code to string.

```c
const char* tusdz_result_to_string(tusdz_result result);
```

### tusdz_prim_type_to_string()

Convert prim type to string.

```c
const char* tusdz_prim_type_to_string(tusdz_prim_type type);
```

### tusdz_value_type_to_string()

Convert value type to string.

```c
const char* tusdz_value_type_to_string(tusdz_value_type type);
```

### tusdz_detect_format()

Detect USD format from file path.

```c
tusdz_format tusdz_detect_format(const char* filepath);
```

### tusdz_free()

Free memory allocated by TinyUSDZ.

```c
void tusdz_free(void* ptr);
```

### tusdz_stage_print_hierarchy()

Print stage hierarchy to stdout.

```c
void tusdz_stage_print_hierarchy(tusdz_stage stage, int max_depth);
```

### tusdz_get_memory_stats()

Get memory usage statistics.

```c
void tusdz_get_memory_stats(
    tusdz_stage stage,
    size_t* out_bytes_used,
    size_t* out_bytes_peak
);
```

### tusdz_set_debug_logging()

Enable/disable debug logging.

```c
void tusdz_set_debug_logging(int enable);
```

## Error Codes

```c
TUSDZ_SUCCESS = 0
TUSDZ_ERROR_FILE_NOT_FOUND = -1
TUSDZ_ERROR_PARSE_FAILED = -2
TUSDZ_ERROR_OUT_OF_MEMORY = -3
TUSDZ_ERROR_INVALID_ARGUMENT = -4
TUSDZ_ERROR_NOT_SUPPORTED = -5
TUSDZ_ERROR_COMPOSITION_FAILED = -6
TUSDZ_ERROR_INVALID_FORMAT = -7
TUSDZ_ERROR_IO_ERROR = -8
TUSDZ_ERROR_INTERNAL = -99
```

## Type Definitions

### tusdz_load_options

```c
typedef struct {
    size_t max_memory_limit_mb;
    int max_depth;
    int enable_composition;
    int strict_mode;
    int structure_only;
    const char* (*asset_resolver)(const char*, void*);
    void* asset_resolver_data;
} tusdz_load_options;
```

### Prim Types

- `TUSDZ_PRIM_XFORM` - Transform
- `TUSDZ_PRIM_MESH` - Polygon mesh
- `TUSDZ_PRIM_MATERIAL` - Material
- `TUSDZ_PRIM_SHADER` - Shader
- `TUSDZ_PRIM_CAMERA` - Camera
- `TUSDZ_PRIM_SKELETON` - Skeletal rig
- `TUSDZ_PRIM_LIGHT` - Light (various subtypes)
- `TUSDZ_PRIM_SCOPE` - Organizational scope
- And many more...

### Value Types

- Scalars: `BOOL`, `INT`, `UINT`, `FLOAT`, `DOUBLE`
- Strings: `STRING`, `TOKEN`, `ASSET_PATH`
- Vectors: `FLOAT2`, `FLOAT3`, `FLOAT4`, `DOUBLE2`, etc.
- Matrices: `MATRIX3D`, `MATRIX4D`
- Special: `ARRAY`, `TIME_SAMPLES`

## Best Practices

1. **Always check return codes:** Verify all API function results
2. **Handle NULL returns:** Many functions return NULL on error
3. **Don't free borrowed references:** Pointers from `get_*` functions are borrowed
4. **Use error buffers:** Provide error buffers to understand failures
5. **Cleanup properly:** Always call `tusdz_stage_free()` and `tusdz_shutdown()`
6. **Use appropriate paths:** Paths should start with "/" (e.g., "/World/Geo")
7. **Type check before extracting:** Verify value types before extraction
8. **Handle arrays properly:** Check `is_array()` before accessing array data