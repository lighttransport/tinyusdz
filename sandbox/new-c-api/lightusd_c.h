/**
 * @file lightusd_c.h
 * @brief Minimal C99 API for LightUSD
 *
 * A pure C99 interface to LightUSD functionality, providing USD file loading,
 * scene traversal, and data access without requiring C++ compilation.
 *
 * @copyright 2024 LightUSD Contributors
 * @license MIT
 */

#ifndef LIGHTUSD_C_H
#define LIGHTUSD_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
#define LIGHTUSD_C_VERSION_MAJOR 1
#define LIGHTUSD_C_VERSION_MINOR 0
#define LIGHTUSD_C_VERSION_PATCH 0

/* Platform-specific export macros */
#ifdef _WIN32
    #ifdef LIGHTUSD_C_EXPORTS
        #define LIGHTUSD_API __declspec(dllexport)
    #else
        #define LIGHTUSD_API __declspec(dllimport)
    #endif
#else
    #define LIGHTUSD_API __attribute__((visibility("default")))
#endif

/* ============================================================================
 * Core Types and Enums
 * ============================================================================ */

/**
 * @brief Opaque handle types
 */
typedef struct lightusd_stage_impl* lightusd_stage;
typedef struct lightusd_prim_impl* lightusd_prim;
typedef struct lightusd_value_impl* lightusd_value;
typedef struct lightusd_layer_impl* lightusd_layer;

/**
 * @brief Result codes for API functions
 */
typedef enum {
    LIGHTUSD_SUCCESS = 0,
    LIGHTUSD_ERROR_FILE_NOT_FOUND = -1,
    LIGHTUSD_ERROR_PARSE_FAILED = -2,
    LIGHTUSD_ERROR_OUT_OF_MEMORY = -3,
    LIGHTUSD_ERROR_INVALID_ARGUMENT = -4,
    LIGHTUSD_ERROR_NOT_SUPPORTED = -5,
    LIGHTUSD_ERROR_COMPOSITION_FAILED = -6,
    LIGHTUSD_ERROR_INVALID_FORMAT = -7,
    LIGHTUSD_ERROR_IO_ERROR = -8,
    LIGHTUSD_ERROR_INTERNAL = -99
} lightusd_result;

/**
 * @brief USD file formats
 */
typedef enum {
    LIGHTUSD_FORMAT_AUTO = 0,  /**< Auto-detect format from file extension or content */
    LIGHTUSD_FORMAT_USDA,      /**< ASCII text format (.usda) */
    LIGHTUSD_FORMAT_USDC,      /**< Binary Crate format (.usdc) */
    LIGHTUSD_FORMAT_USDZ       /**< Zip archive format (.usdz) */
} lightusd_format;

/**
 * @brief USD prim types
 */
typedef enum {
    LIGHTUSD_PRIM_UNKNOWN = 0,
    LIGHTUSD_PRIM_XFORM,
    LIGHTUSD_PRIM_MESH,
    LIGHTUSD_PRIM_MATERIAL,
    LIGHTUSD_PRIM_SHADER,
    LIGHTUSD_PRIM_CAMERA,
    LIGHTUSD_PRIM_DISTANT_LIGHT,
    LIGHTUSD_PRIM_SPHERE_LIGHT,
    LIGHTUSD_PRIM_RECT_LIGHT,
    LIGHTUSD_PRIM_DISK_LIGHT,
    LIGHTUSD_PRIM_CYLINDER_LIGHT,
    LIGHTUSD_PRIM_DOME_LIGHT,
    LIGHTUSD_PRIM_SKELETON,
    LIGHTUSD_PRIM_SKELROOT,
    LIGHTUSD_PRIM_SKELANIMATION,
    LIGHTUSD_PRIM_SCOPE,
    LIGHTUSD_PRIM_GEOMSUBSET,
    LIGHTUSD_PRIM_SPHERE,
    LIGHTUSD_PRIM_CUBE,
    LIGHTUSD_PRIM_CYLINDER,
    LIGHTUSD_PRIM_CAPSULE,
    LIGHTUSD_PRIM_CONE,
    LIGHTUSD_PRIM_NURBS_PATCH,
    LIGHTUSD_PRIM_NURBS_CURVE,
    LIGHTUSD_PRIM_BASIS_CURVES,
    LIGHTUSD_PRIM_POINT_INSTANCER,
    LIGHTUSD_PRIM_VOLUME
} lightusd_prim_type;

/**
 * @brief Value types for USD properties
 */
typedef enum {
    LIGHTUSD_VALUE_NONE = 0,
    /* Scalar types */
    LIGHTUSD_VALUE_BOOL,
    LIGHTUSD_VALUE_INT,
    LIGHTUSD_VALUE_UINT,
    LIGHTUSD_VALUE_INT64,
    LIGHTUSD_VALUE_UINT64,
    LIGHTUSD_VALUE_HALF,
    LIGHTUSD_VALUE_FLOAT,
    LIGHTUSD_VALUE_DOUBLE,
    /* String types */
    LIGHTUSD_VALUE_STRING,
    LIGHTUSD_VALUE_TOKEN,
    LIGHTUSD_VALUE_ASSET_PATH,
    /* Vector types */
    LIGHTUSD_VALUE_INT2,
    LIGHTUSD_VALUE_INT3,
    LIGHTUSD_VALUE_INT4,
    LIGHTUSD_VALUE_HALF2,
    LIGHTUSD_VALUE_HALF3,
    LIGHTUSD_VALUE_HALF4,
    LIGHTUSD_VALUE_FLOAT2,
    LIGHTUSD_VALUE_FLOAT3,
    LIGHTUSD_VALUE_FLOAT4,
    LIGHTUSD_VALUE_DOUBLE2,
    LIGHTUSD_VALUE_DOUBLE3,
    LIGHTUSD_VALUE_DOUBLE4,
    /* Matrix types */
    LIGHTUSD_VALUE_MATRIX2D,
    LIGHTUSD_VALUE_MATRIX3D,
    LIGHTUSD_VALUE_MATRIX4D,
    /* Quaternion types */
    LIGHTUSD_VALUE_QUATH,
    LIGHTUSD_VALUE_QUATF,
    LIGHTUSD_VALUE_QUATD,
    /* Color types */
    LIGHTUSD_VALUE_COLOR3F,
    LIGHTUSD_VALUE_COLOR3D,
    LIGHTUSD_VALUE_COLOR4F,
    LIGHTUSD_VALUE_COLOR4D,
    /* Other types */
    LIGHTUSD_VALUE_NORMAL3F,
    LIGHTUSD_VALUE_NORMAL3D,
    LIGHTUSD_VALUE_POINT3F,
    LIGHTUSD_VALUE_POINT3D,
    LIGHTUSD_VALUE_TEXCOORD2F,
    LIGHTUSD_VALUE_TEXCOORD2D,
    LIGHTUSD_VALUE_TEXCOORD3F,
    LIGHTUSD_VALUE_TEXCOORD3D,
    /* Complex types */
    LIGHTUSD_VALUE_ARRAY,
    LIGHTUSD_VALUE_DICTIONARY,
    LIGHTUSD_VALUE_TIME_SAMPLES,
    LIGHTUSD_VALUE_RELATIONSHIP
} lightusd_value_type;

/**
 * @brief Interpolation types for animated values
 */
typedef enum {
    LIGHTUSD_INTERPOLATION_HELD = 0,
    LIGHTUSD_INTERPOLATION_LINEAR,
    LIGHTUSD_INTERPOLATION_BEZIER
} lightusd_interpolation;

/**
 * @brief Load options for USD files
 */
typedef struct {
    /** Maximum memory limit in MB (0 = no limit) */
    size_t max_memory_limit_mb;

    /** Maximum composition depth (0 = use default) */
    int max_depth;

    /** Enable composition (resolve references, payloads) */
    int enable_composition;

    /** Strict mode - fail on any warnings */
    int strict_mode;

    /** Load only structure, skip heavy data */
    int structure_only;

    /** Custom asset resolver callback (can be NULL) */
    const char* (*asset_resolver)(const char* asset_path, void* user_data);
    void* asset_resolver_data;
} lightusd_load_options;

/* ============================================================================
 * Tier 1: Core API Functions (Essential)
 * ============================================================================ */

/**
 * @brief Initialize the LightUSD library
 * @return LIGHTUSD_SUCCESS on success
 */
LIGHTUSD_API lightusd_result lightusd_init(void);

/**
 * @brief Shutdown the LightUSD library and free global resources
 */
LIGHTUSD_API void lightusd_shutdown(void);

/**
 * @brief Get version string
 * @return Version string like "1.0.0"
 */
LIGHTUSD_API const char* lightusd_get_version(void);

/**
 * @brief Load USD from file
 * @param filepath Path to USD file
 * @param options Load options (can be NULL for defaults)
 * @param out_stage Output stage handle
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_buf_size Size of error buffer
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_load_from_file(
    const char* filepath,
    const lightusd_load_options* options,
    lightusd_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

/**
 * @brief Load USD from memory buffer
 * @param data Memory buffer containing USD data
 * @param size Size of buffer in bytes
 * @param format Format of the data (use LIGHTUSD_FORMAT_AUTO to detect)
 * @param options Load options (can be NULL for defaults)
 * @param out_stage Output stage handle
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_buf_size Size of error buffer
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_load_from_memory(
    const void* data,
    size_t size,
    lightusd_format format,
    const lightusd_load_options* options,
    lightusd_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

/**
 * @brief Free a stage and all associated resources
 * @param stage Stage to free
 */
LIGHTUSD_API void lightusd_stage_free(lightusd_stage stage);

/**
 * @brief Get the root prim of the stage
 * @param stage Stage handle
 * @return Root prim (borrowed reference, do not free)
 */
LIGHTUSD_API lightusd_prim lightusd_stage_get_root_prim(lightusd_stage stage);

/**
 * @brief Get number of child prims
 * @param prim Parent prim
 * @return Number of children
 */
LIGHTUSD_API size_t lightusd_prim_get_child_count(lightusd_prim prim);

/**
 * @brief Get child prim at index
 * @param prim Parent prim
 * @param index Child index
 * @return Child prim (borrowed reference, do not free)
 */
LIGHTUSD_API lightusd_prim lightusd_prim_get_child_at(lightusd_prim prim, size_t index);

/**
 * @brief Get prim name
 * @param prim Prim handle
 * @return Name string (borrowed, do not free)
 */
LIGHTUSD_API const char* lightusd_prim_get_name(lightusd_prim prim);

/**
 * @brief Get prim type
 * @param prim Prim handle
 * @return Prim type enum
 */
LIGHTUSD_API lightusd_prim_type lightusd_prim_get_type(lightusd_prim prim);

/* ============================================================================
 * Tier 2: Extended Core API
 * ============================================================================ */

/**
 * @brief Get full path of prim
 * @param prim Prim handle
 * @return Path string (borrowed, do not free)
 */
LIGHTUSD_API const char* lightusd_prim_get_path(lightusd_prim prim);

/**
 * @brief Get prim at specific path
 * @param stage Stage handle
 * @param path Prim path (e.g., "/World/Geo/Mesh")
 * @return Prim handle or NULL if not found
 */
LIGHTUSD_API lightusd_prim lightusd_stage_get_prim_at_path(lightusd_stage stage, const char* path);

/**
 * @brief Check if prim is specific type
 * @param prim Prim handle
 * @param type Type to check
 * @return 1 if matches, 0 otherwise
 */
LIGHTUSD_API int lightusd_prim_is_type(lightusd_prim prim, lightusd_prim_type type);

/**
 * @brief Get type name as string
 * @param prim Prim handle
 * @return Type name (e.g., "Mesh", "Xform")
 */
LIGHTUSD_API const char* lightusd_prim_get_type_name(lightusd_prim prim);

/**
 * @brief Get number of properties on prim
 * @param prim Prim handle
 * @return Property count
 */
LIGHTUSD_API size_t lightusd_prim_get_property_count(lightusd_prim prim);

/**
 * @brief Get property name at index
 * @param prim Prim handle
 * @param index Property index
 * @return Property name (borrowed, do not free)
 */
LIGHTUSD_API const char* lightusd_prim_get_property_name_at(lightusd_prim prim, size_t index);

/**
 * @brief Get property value by name
 * @param prim Prim handle
 * @param name Property name
 * @return Value handle (must be freed with lightusd_value_free)
 */
LIGHTUSD_API lightusd_value lightusd_prim_get_property(lightusd_prim prim, const char* name);

/**
 * @brief Free a value handle
 * @param value Value to free
 */
LIGHTUSD_API void lightusd_value_free(lightusd_value value);

/**
 * @brief Get value type
 * @param value Value handle
 * @return Value type enum
 */
LIGHTUSD_API lightusd_value_type lightusd_value_get_type(lightusd_value value);

/**
 * @brief Check if value is an array
 * @param value Value handle
 * @return 1 if array, 0 otherwise
 */
LIGHTUSD_API int lightusd_value_is_array(lightusd_value value);

/**
 * @brief Get array length for array values
 * @param value Value handle
 * @return Array length (0 if not an array)
 */
LIGHTUSD_API size_t lightusd_value_get_array_size(lightusd_value value);

/* ============================================================================
 * Value Extraction Functions
 * ============================================================================ */

/* Scalar extraction */
LIGHTUSD_API lightusd_result lightusd_value_get_bool(lightusd_value value, int* out);
LIGHTUSD_API lightusd_result lightusd_value_get_int(lightusd_value value, int* out);
LIGHTUSD_API lightusd_result lightusd_value_get_uint(lightusd_value value, unsigned int* out);
LIGHTUSD_API lightusd_result lightusd_value_get_int64(lightusd_value value, int64_t* out);
LIGHTUSD_API lightusd_result lightusd_value_get_uint64(lightusd_value value, uint64_t* out);
LIGHTUSD_API lightusd_result lightusd_value_get_float(lightusd_value value, float* out);
LIGHTUSD_API lightusd_result lightusd_value_get_double(lightusd_value value, double* out);

/* String extraction */
LIGHTUSD_API lightusd_result lightusd_value_get_string(lightusd_value value, const char** out);
LIGHTUSD_API lightusd_result lightusd_value_get_token(lightusd_value value, const char** out);
LIGHTUSD_API lightusd_result lightusd_value_get_asset_path(lightusd_value value, const char** out);

/* Vector extraction */
LIGHTUSD_API lightusd_result lightusd_value_get_float2(lightusd_value value, float* out_xy);
LIGHTUSD_API lightusd_result lightusd_value_get_float3(lightusd_value value, float* out_xyz);
LIGHTUSD_API lightusd_result lightusd_value_get_float4(lightusd_value value, float* out_xyzw);
LIGHTUSD_API lightusd_result lightusd_value_get_double2(lightusd_value value, double* out_xy);
LIGHTUSD_API lightusd_result lightusd_value_get_double3(lightusd_value value, double* out_xyz);
LIGHTUSD_API lightusd_result lightusd_value_get_double4(lightusd_value value, double* out_xyzw);

/* Matrix extraction (column-major) */
LIGHTUSD_API lightusd_result lightusd_value_get_matrix3d(lightusd_value value, double* out_mat3x3);
LIGHTUSD_API lightusd_result lightusd_value_get_matrix4d(lightusd_value value, double* out_mat4x4);

/* Array extraction - returns pointer to internal data, do not free */
LIGHTUSD_API lightusd_result lightusd_value_get_float_array(lightusd_value value, const float** out_data, size_t* out_count);
LIGHTUSD_API lightusd_result lightusd_value_get_int_array(lightusd_value value, const int** out_data, size_t* out_count);
LIGHTUSD_API lightusd_result lightusd_value_get_float3_array(lightusd_value value, const float** out_data, size_t* out_count);

/* ============================================================================
 * Tier 3: Geometry and Mesh API
 * ============================================================================ */

/**
 * @brief Get mesh point positions
 * @param mesh Mesh prim
 * @param out_points Output pointer to points array (do not free)
 * @param out_count Number of points (each point is 3 floats)
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_mesh_get_points(
    lightusd_prim mesh,
    const float** out_points,
    size_t* out_count
);

/**
 * @brief Get mesh face vertex counts
 * @param mesh Mesh prim
 * @param out_counts Output pointer to counts array (do not free)
 * @param out_count Number of faces
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_mesh_get_face_counts(
    lightusd_prim mesh,
    const int** out_counts,
    size_t* out_count
);

/**
 * @brief Get mesh face vertex indices
 * @param mesh Mesh prim
 * @param out_indices Output pointer to indices array (do not free)
 * @param out_count Number of indices
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_mesh_get_indices(
    lightusd_prim mesh,
    const int** out_indices,
    size_t* out_count
);

/**
 * @brief Get mesh normals
 * @param mesh Mesh prim
 * @param out_normals Output pointer to normals array (do not free)
 * @param out_count Number of normals (each normal is 3 floats)
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_mesh_get_normals(
    lightusd_prim mesh,
    const float** out_normals,
    size_t* out_count
);

/**
 * @brief Get mesh UV coordinates
 * @param mesh Mesh prim
 * @param out_uvs Output pointer to UVs array (do not free)
 * @param out_count Number of UV pairs (each UV is 2 floats)
 * @param primvar_index Which UV set to get (0 for primary)
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_mesh_get_uvs(
    lightusd_prim mesh,
    const float** out_uvs,
    size_t* out_count,
    int primvar_index
);

/**
 * @brief Get subdivision scheme
 * @param mesh Mesh prim
 * @return Subdivision scheme ("none", "catmullClark", "loop", "bilinear")
 */
LIGHTUSD_API const char* lightusd_mesh_get_subdivision_scheme(lightusd_prim mesh);

/* ============================================================================
 * Transform API
 * ============================================================================ */

/**
 * @brief Get local transformation matrix
 * @param xform Transform prim
 * @param time Time for evaluation (use 0.0 for default time)
 * @param out_matrix Output 4x4 matrix in column-major order
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_xform_get_local_matrix(
    lightusd_prim xform,
    double time,
    double* out_matrix
);

/**
 * @brief Get world transformation matrix (includes parent transforms)
 * @param prim Any prim
 * @param time Time for evaluation
 * @param out_matrix Output 4x4 matrix in column-major order
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_prim_get_world_matrix(
    lightusd_prim prim,
    double time,
    double* out_matrix
);

/* ============================================================================
 * Material and Shading API
 * ============================================================================ */

/**
 * @brief Get material bound to prim
 * @param prim Prim with material binding
 * @return Material prim or NULL
 */
LIGHTUSD_API lightusd_prim lightusd_prim_get_bound_material(lightusd_prim prim);

/**
 * @brief Get surface shader from material
 * @param material Material prim
 * @return Shader prim or NULL
 */
LIGHTUSD_API lightusd_prim lightusd_material_get_surface_shader(lightusd_prim material);

/**
 * @brief Get shader input value
 * @param shader Shader prim
 * @param input_name Input name (e.g., "diffuseColor", "roughness")
 * @return Value handle (must be freed)
 */
LIGHTUSD_API lightusd_value lightusd_shader_get_input(lightusd_prim shader, const char* input_name);

/**
 * @brief Get shader type/ID
 * @param shader Shader prim
 * @return Shader type string (e.g., "UsdPreviewSurface")
 */
LIGHTUSD_API const char* lightusd_shader_get_type_id(lightusd_prim shader);

/* ============================================================================
 * Animation and Time Sampling API
 * ============================================================================ */

/**
 * @brief Check if stage has animation
 * @param stage Stage handle
 * @return 1 if animated, 0 otherwise
 */
LIGHTUSD_API int lightusd_stage_has_animation(lightusd_stage stage);

/**
 * @brief Get time code range for stage
 * @param stage Stage handle
 * @param out_start_time Start time
 * @param out_end_time End time
 * @param out_fps Frames per second
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_stage_get_time_range(
    lightusd_stage stage,
    double* out_start_time,
    double* out_end_time,
    double* out_fps
);

/**
 * @brief Check if value has time samples (is animated)
 * @param value Value handle
 * @return 1 if animated, 0 otherwise
 */
LIGHTUSD_API int lightusd_value_is_animated(lightusd_value value);

/**
 * @brief Get time sample times for animated value
 * @param value Value handle
 * @param out_times Output pointer to times array (do not free)
 * @param out_count Number of time samples
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_value_get_time_samples(
    lightusd_value value,
    const double** out_times,
    size_t* out_count
);

/**
 * @brief Evaluate value at specific time
 * @param value Value handle
 * @param time Time to evaluate at
 * @return New value handle at that time (must be freed)
 */
LIGHTUSD_API lightusd_value lightusd_value_eval_at_time(lightusd_value value, double time);

/* ============================================================================
 * Metadata API
 * ============================================================================ */

/**
 * @brief Get metadata value for prim
 * @param prim Prim handle
 * @param key Metadata key (e.g., "documentation", "hidden")
 * @return Value handle or NULL if not found (must be freed if not NULL)
 */
LIGHTUSD_API lightusd_value lightusd_prim_get_metadata(lightusd_prim prim, const char* key);

/**
 * @brief Get list of metadata keys
 * @param prim Prim handle
 * @param out_keys Output array of key strings (do not free)
 * @param out_count Number of keys
 * @return Result code
 */
LIGHTUSD_API lightusd_result lightusd_prim_get_metadata_keys(
    lightusd_prim prim,
    const char*** out_keys,
    size_t* out_count
);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Free memory allocated by LightUSD
 * @param ptr Pointer to free
 */
LIGHTUSD_API void lightusd_free(void* ptr);

/**
 * @brief Convert result code to string
 * @param result Result code
 * @return String description
 */
LIGHTUSD_API const char* lightusd_result_to_string(lightusd_result result);

/**
 * @brief Convert prim type to string
 * @param type Prim type
 * @return Type name string
 */
LIGHTUSD_API const char* lightusd_prim_type_to_string(lightusd_prim_type type);

/**
 * @brief Convert value type to string
 * @param type Value type
 * @return Type name string
 */
LIGHTUSD_API const char* lightusd_value_type_to_string(lightusd_value_type type);

/**
 * @brief Detect USD format from file extension
 * @param filepath File path
 * @return Detected format
 */
LIGHTUSD_API lightusd_format lightusd_detect_format(const char* filepath);

/* ============================================================================
 * Debug and Diagnostic Functions
 * ============================================================================ */

/**
 * @brief Print stage hierarchy to stdout
 * @param stage Stage handle
 * @param max_depth Maximum depth to print (0 = all)
 */
LIGHTUSD_API void lightusd_stage_print_hierarchy(lightusd_stage stage, int max_depth);

/**
 * @brief Get memory usage statistics
 * @param stage Stage handle (can be NULL for global stats)
 * @param out_bytes_used Bytes currently used
 * @param out_bytes_peak Peak bytes used
 */
LIGHTUSD_API void lightusd_get_memory_stats(
    lightusd_stage stage,
    size_t* out_bytes_used,
    size_t* out_bytes_peak
);

/**
 * @brief Enable/disable debug logging
 * @param enable 1 to enable, 0 to disable
 */
LIGHTUSD_API void lightusd_set_debug_logging(int enable);

#ifdef __cplusplus
}
#endif

#endif /* LIGHTUSD_C_H */