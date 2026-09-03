# LightUSD C99 API Reference

Complete reference documentation for the LightUSD C API.

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

### lightusd_init()

Initialize the LightUSD library. Must be called before using any other functions.

```c
lightusd_result lightusd_init(void);
```

**Returns:** `LIGHTUSD_SUCCESS` on success

**Example:**
```c
if (lightusd_init() != LIGHTUSD_SUCCESS) {
    fprintf(stderr, "Failed to initialize\n");
    return 1;
}
```

### lightusd_shutdown()

Shutdown the library and free global resources.

```c
void lightusd_shutdown(void);
```

**Example:**
```c
lightusd_shutdown();
```

### lightusd_get_version()

Get library version string.

```c
const char* lightusd_get_version(void);
```

**Returns:** Version string like "1.0.0"

**Example:**
```c
printf("LightUSD version: %s\n", lightusd_get_version());
```

## Loading

### lightusd_load_from_file()

Load USD file from disk.

```c
lightusd_result lightusd_load_from_file(
    const char* filepath,
    const lightusd_load_options* options,
    lightusd_stage* out_stage,
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
lightusd_stage stage = NULL;
char error[1024];
lightusd_result result = lightusd_load_from_file(
    "model.usd",
    NULL,  // Use default options
    &stage,
    error,
    sizeof(error)
);

if (result != LIGHTUSD_SUCCESS) {
    fprintf(stderr, "Error: %s\n", error);
} else {
    // Use stage...
    lightusd_stage_free(stage);
}
```

### lightusd_load_from_memory()

Load USD from memory buffer.

```c
lightusd_result lightusd_load_from_memory(
    const void* data,
    size_t size,
    lightusd_format format,
    const lightusd_load_options* options,
    lightusd_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);
```

**Parameters:**
- `data`: Memory buffer containing USD data
- `size`: Size of buffer in bytes
- `format`: Format of the data (`LIGHTUSD_FORMAT_USDA`, `LIGHTUSD_FORMAT_USDC`, `LIGHTUSD_FORMAT_USDZ`, or `LIGHTUSD_FORMAT_AUTO`)
- `options`: Load options (NULL for defaults)
- `out_stage`: Output stage handle
- `error_buf`: Buffer for error message
- `error_buf_size`: Size of error buffer

**Returns:** Result code

**Example:**
```c
const uint8_t* data = /* ... */;
size_t size = /* ... */;

lightusd_stage stage = NULL;
lightusd_result result = lightusd_load_from_memory(
    data, size, LIGHTUSD_FORMAT_AUTO, NULL, &stage, NULL, 0
);

if (result == LIGHTUSD_SUCCESS && stage) {
    // Use stage...
    lightusd_stage_free(stage);
}
```

## Stage Operations

### lightusd_stage_free()

Free a stage and all associated resources.

```c
void lightusd_stage_free(lightusd_stage stage);
```

**Example:**
```c
lightusd_stage_free(stage);
```

### lightusd_stage_get_root_prim()

Get the root prim of the stage.

```c
lightusd_prim lightusd_stage_get_root_prim(lightusd_stage stage);
```

**Returns:** Root prim (borrowed reference, do not free)

**Example:**
```c
lightusd_prim root = lightusd_stage_get_root_prim(stage);
if (root) {
    printf("Root prim: %s\n", lightusd_prim_get_name(root));
}
```

### lightusd_stage_get_prim_at_path()

Get prim at specific path.

```c
lightusd_prim lightusd_stage_get_prim_at_path(lightusd_stage stage, const char* path);
```

**Parameters:**
- `stage`: Stage handle
- `path`: Prim path (e.g., "/World/Geo/Mesh")

**Returns:** Prim handle or NULL if not found

**Example:**
```c
lightusd_prim prim = lightusd_stage_get_prim_at_path(stage, "/World/Cube");
if (prim) {
    printf("Found: %s\n", lightusd_prim_get_name(prim));
}
```

### lightusd_stage_has_animation()

Check if stage has animation.

```c
int lightusd_stage_has_animation(lightusd_stage stage);
```

**Returns:** 1 if animated, 0 otherwise

### lightusd_stage_get_time_range()

Get animation time range.

```c
lightusd_result lightusd_stage_get_time_range(
    lightusd_stage stage,
    double* out_start_time,
    double* out_end_time,
    double* out_fps
);
```

**Example:**
```c
double start, end, fps;
if (lightusd_stage_get_time_range(stage, &start, &end, &fps) == LIGHTUSD_SUCCESS) {
    printf("Animation: %.1f to %.1f @ %.1f fps\n", start, end, fps);
}
```

## Prim Operations

### lightusd_prim_get_name()

Get prim name.

```c
const char* lightusd_prim_get_name(lightusd_prim prim);
```

**Returns:** Name string (borrowed, do not free)

### lightusd_prim_get_path()

Get full path of prim.

```c
const char* lightusd_prim_get_path(lightusd_prim prim);
```

**Returns:** Path string (borrowed, do not free)

### lightusd_prim_get_type()

Get prim type enum.

```c
lightusd_prim_type lightusd_prim_get_type(lightusd_prim prim);
```

**Returns:** Prim type enum value

**Example:**
```c
lightusd_prim_type type = lightusd_prim_get_type(prim);
printf("Type: %s\n", lightusd_prim_type_to_string(type));
```

### lightusd_prim_get_type_name()

Get prim type name as string.

```c
const char* lightusd_prim_get_type_name(lightusd_prim prim);
```

**Returns:** Type name (e.g., "Mesh", "Xform")

### lightusd_prim_is_type()

Check if prim is specific type.

```c
int lightusd_prim_is_type(lightusd_prim prim, lightusd_prim_type type);
```

**Returns:** 1 if matches, 0 otherwise

**Example:**
```c
if (lightusd_prim_is_type(prim, LIGHTUSD_PRIM_MESH)) {
    printf("This is a mesh!\n");
}
```

### lightusd_prim_get_child_count()

Get number of child prims.

```c
size_t lightusd_prim_get_child_count(lightusd_prim prim);
```

### lightusd_prim_get_child_at()

Get child prim at index.

```c
lightusd_prim lightusd_prim_get_child_at(lightusd_prim prim, size_t index);
```

**Example:**
```c
size_t count = lightusd_prim_get_child_count(prim);
for (size_t i = 0; i < count; i++) {
    lightusd_prim child = lightusd_prim_get_child_at(prim, i);
    printf("Child: %s\n", lightusd_prim_get_name(child));
}
```

### lightusd_prim_get_property_count()

Get number of properties on prim.

```c
size_t lightusd_prim_get_property_count(lightusd_prim prim);
```

### lightusd_prim_get_property_name_at()

Get property name at index.

```c
const char* lightusd_prim_get_property_name_at(lightusd_prim prim, size_t index);
```

### lightusd_prim_get_property()

Get property value by name.

```c
lightusd_value lightusd_prim_get_property(lightusd_prim prim, const char* name);
```

**Returns:** Value handle (must be freed with lightusd_value_free)

## Value Operations

### lightusd_value_free()

Free a value handle.

```c
void lightusd_value_free(lightusd_value value);
```

### lightusd_value_get_type()

Get value type.

```c
lightusd_value_type lightusd_value_get_type(lightusd_value value);
```

### lightusd_value_get_bool()

Extract boolean value.

```c
lightusd_result lightusd_value_get_bool(lightusd_value value, int* out);
```

### lightusd_value_get_int()

Extract integer value.

```c
lightusd_result lightusd_value_get_int(lightusd_value value, int* out);
```

### lightusd_value_get_float()

Extract float value.

```c
lightusd_result lightusd_value_get_float(lightusd_value value, float* out);
```

### lightusd_value_get_double()

Extract double value.

```c
lightusd_result lightusd_value_get_double(lightusd_value value, double* out);
```

### lightusd_value_get_string()

Extract string value.

```c
lightusd_result lightusd_value_get_string(lightusd_value value, const char** out);
```

**Returns:** `LIGHTUSD_SUCCESS` if successful

**Example:**
```c
const char* str;
if (lightusd_value_get_string(value, &str) == LIGHTUSD_SUCCESS) {
    printf("String value: %s\n", str);
}
```

### lightusd_value_get_float3()

Extract 3-component float vector.

```c
lightusd_result lightusd_value_get_float3(lightusd_value value, float* out_xyz);
```

**Example:**
```c
float xyz[3];
if (lightusd_value_get_float3(value, xyz) == LIGHTUSD_SUCCESS) {
    printf("Position: (%f, %f, %f)\n", xyz[0], xyz[1], xyz[2]);
}
```

## Mesh Operations

### lightusd_mesh_get_points()

Get mesh vertex positions.

```c
lightusd_result lightusd_mesh_get_points(
    lightusd_prim mesh,
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
if (lightusd_mesh_get_points(mesh, &points, &point_count) == LIGHTUSD_SUCCESS) {
    size_t num_vertices = point_count / 3;
    for (size_t i = 0; i < num_vertices; i++) {
        printf("Point %zu: (%f, %f, %f)\n",
               i, points[i*3], points[i*3+1], points[i*3+2]);
    }
}
```

### lightusd_mesh_get_face_counts()

Get face vertex counts.

```c
lightusd_result lightusd_mesh_get_face_counts(
    lightusd_prim mesh,
    const int** out_counts,
    size_t* out_count
);
```

### lightusd_mesh_get_indices()

Get face vertex indices.

```c
lightusd_result lightusd_mesh_get_indices(
    lightusd_prim mesh,
    const int** out_indices,
    size_t* out_count
);
```

### lightusd_mesh_get_normals()

Get mesh normals.

```c
lightusd_result lightusd_mesh_get_normals(
    lightusd_prim mesh,
    const float** out_normals,
    size_t* out_count
);
```

### lightusd_mesh_get_uvs()

Get mesh UV coordinates.

```c
lightusd_result lightusd_mesh_get_uvs(
    lightusd_prim mesh,
    const float** out_uvs,
    size_t* out_count,
    int primvar_index
);
```

## Transform Operations

### lightusd_xform_get_local_matrix()

Get local transformation matrix.

```c
lightusd_result lightusd_xform_get_local_matrix(
    lightusd_prim xform,
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
if (lightusd_xform_get_local_matrix(xform, 0.0, matrix) == LIGHTUSD_SUCCESS) {
    // Use matrix for rendering
}
```

## Material and Shader Operations

### lightusd_prim_get_bound_material()

Get material bound to prim.

```c
lightusd_prim lightusd_prim_get_bound_material(lightusd_prim prim);
```

### lightusd_material_get_surface_shader()

Get surface shader from material.

```c
lightusd_prim lightusd_material_get_surface_shader(lightusd_prim material);
```

### lightusd_shader_get_input()

Get shader input value.

```c
lightusd_value lightusd_shader_get_input(lightusd_prim shader, const char* input_name);
```

### lightusd_shader_get_type_id()

Get shader type ID.

```c
const char* lightusd_shader_get_type_id(lightusd_prim shader);
```

## Animation Operations

### lightusd_value_is_animated()

Check if value has animation.

```c
int lightusd_value_is_animated(lightusd_value value);
```

### lightusd_value_eval_at_time()

Evaluate value at specific time.

```c
lightusd_value lightusd_value_eval_at_time(lightusd_value value, double time);
```

## Utilities

### lightusd_result_to_string()

Convert result code to string.

```c
const char* lightusd_result_to_string(lightusd_result result);
```

### lightusd_prim_type_to_string()

Convert prim type to string.

```c
const char* lightusd_prim_type_to_string(lightusd_prim_type type);
```

### lightusd_value_type_to_string()

Convert value type to string.

```c
const char* lightusd_value_type_to_string(lightusd_value_type type);
```

### lightusd_detect_format()

Detect USD format from file path.

```c
lightusd_format lightusd_detect_format(const char* filepath);
```

### lightusd_free()

Free memory allocated by LightUSD.

```c
void lightusd_free(void* ptr);
```

### lightusd_stage_print_hierarchy()

Print stage hierarchy to stdout.

```c
void lightusd_stage_print_hierarchy(lightusd_stage stage, int max_depth);
```

### lightusd_get_memory_stats()

Get memory usage statistics.

```c
void lightusd_get_memory_stats(
    lightusd_stage stage,
    size_t* out_bytes_used,
    size_t* out_bytes_peak
);
```

### lightusd_set_debug_logging()

Enable/disable debug logging.

```c
void lightusd_set_debug_logging(int enable);
```

## Error Codes

```c
LIGHTUSD_SUCCESS = 0
LIGHTUSD_ERROR_FILE_NOT_FOUND = -1
LIGHTUSD_ERROR_PARSE_FAILED = -2
LIGHTUSD_ERROR_OUT_OF_MEMORY = -3
LIGHTUSD_ERROR_INVALID_ARGUMENT = -4
LIGHTUSD_ERROR_NOT_SUPPORTED = -5
LIGHTUSD_ERROR_COMPOSITION_FAILED = -6
LIGHTUSD_ERROR_INVALID_FORMAT = -7
LIGHTUSD_ERROR_IO_ERROR = -8
LIGHTUSD_ERROR_INTERNAL = -99
```

## Type Definitions

### lightusd_load_options

```c
typedef struct {
    size_t max_memory_limit_mb;
    int max_depth;
    int enable_composition;
    int strict_mode;
    int structure_only;
    const char* (*asset_resolver)(const char*, void*);
    void* asset_resolver_data;
} lightusd_load_options;
```

### Prim Types

- `LIGHTUSD_PRIM_XFORM` - Transform
- `LIGHTUSD_PRIM_MESH` - Polygon mesh
- `LIGHTUSD_PRIM_MATERIAL` - Material
- `LIGHTUSD_PRIM_SHADER` - Shader
- `LIGHTUSD_PRIM_CAMERA` - Camera
- `LIGHTUSD_PRIM_SKELETON` - Skeletal rig
- `LIGHTUSD_PRIM_LIGHT` - Light (various subtypes)
- `LIGHTUSD_PRIM_SCOPE` - Organizational scope
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
5. **Cleanup properly:** Always call `lightusd_stage_free()` and `lightusd_shutdown()`
6. **Use appropriate paths:** Paths should start with "/" (e.g., "/World/Geo")
7. **Type check before extracting:** Verify value types before extraction
8. **Handle arrays properly:** Check `is_array()` before accessing array data