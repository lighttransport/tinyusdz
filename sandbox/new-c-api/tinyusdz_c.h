/**
 * @file tinyusdz_c.h
 * @brief Minimal C99 API for TinyUSDZ
 *
 * A pure C99 interface to TinyUSDZ functionality, providing USD file loading,
 * scene traversal, and data access without requiring C++ compilation.
 *
 * @copyright 2024 TinyUSDZ Contributors
 * @license MIT
 */

#ifndef TINYUSDZ_C_H
#define TINYUSDZ_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Version information */
#define TINYUSDZ_C_VERSION_MAJOR 1
#define TINYUSDZ_C_VERSION_MINOR 0
#define TINYUSDZ_C_VERSION_PATCH 0

/* Platform-specific export macros */
#ifdef _WIN32
    #ifdef TINYUSDZ_C_EXPORTS
        #define TUSDZ_API __declspec(dllexport)
    #else
        #define TUSDZ_API __declspec(dllimport)
    #endif
#else
    #define TUSDZ_API __attribute__((visibility("default")))
#endif

/* ============================================================================
 * Core Types and Enums
 * ============================================================================ */

/**
 * @brief Opaque handle types
 */
typedef struct tusdz_stage_impl* tusdz_stage;
typedef struct tusdz_prim_impl* tusdz_prim;
typedef struct tusdz_value_impl* tusdz_value;
typedef struct tusdz_layer_impl* tusdz_layer;

/**
 * @brief Result codes for API functions
 */
typedef enum {
    TUSDZ_SUCCESS = 0,
    TUSDZ_ERROR_FILE_NOT_FOUND = -1,
    TUSDZ_ERROR_PARSE_FAILED = -2,
    TUSDZ_ERROR_OUT_OF_MEMORY = -3,
    TUSDZ_ERROR_INVALID_ARGUMENT = -4,
    TUSDZ_ERROR_NOT_SUPPORTED = -5,
    TUSDZ_ERROR_COMPOSITION_FAILED = -6,
    TUSDZ_ERROR_INVALID_FORMAT = -7,
    TUSDZ_ERROR_IO_ERROR = -8,
    TUSDZ_ERROR_INTERNAL = -99
} tusdz_result;

/**
 * @brief USD file formats
 */
typedef enum {
    TUSDZ_FORMAT_AUTO = 0,  /**< Auto-detect format from file extension or content */
    TUSDZ_FORMAT_USDA,      /**< ASCII text format (.usda) */
    TUSDZ_FORMAT_USDC,      /**< Binary Crate format (.usdc) */
    TUSDZ_FORMAT_USDZ       /**< Zip archive format (.usdz) */
} tusdz_format;

/**
 * @brief USD prim types
 */
typedef enum {
    TUSDZ_PRIM_UNKNOWN = 0,
    TUSDZ_PRIM_XFORM,
    TUSDZ_PRIM_MESH,
    TUSDZ_PRIM_MATERIAL,
    TUSDZ_PRIM_SHADER,
    TUSDZ_PRIM_CAMERA,
    TUSDZ_PRIM_DISTANT_LIGHT,
    TUSDZ_PRIM_SPHERE_LIGHT,
    TUSDZ_PRIM_RECT_LIGHT,
    TUSDZ_PRIM_DISK_LIGHT,
    TUSDZ_PRIM_CYLINDER_LIGHT,
    TUSDZ_PRIM_DOME_LIGHT,
    TUSDZ_PRIM_SKELETON,
    TUSDZ_PRIM_SKELROOT,
    TUSDZ_PRIM_SKELANIMATION,
    TUSDZ_PRIM_SCOPE,
    TUSDZ_PRIM_GEOMSUBSET,
    TUSDZ_PRIM_SPHERE,
    TUSDZ_PRIM_CUBE,
    TUSDZ_PRIM_CYLINDER,
    TUSDZ_PRIM_CAPSULE,
    TUSDZ_PRIM_CONE,
    TUSDZ_PRIM_NURBS_PATCH,
    TUSDZ_PRIM_NURBS_CURVE,
    TUSDZ_PRIM_BASIS_CURVES,
    TUSDZ_PRIM_POINT_INSTANCER,
    TUSDZ_PRIM_VOLUME
} tusdz_prim_type;

/**
 * @brief Value types for USD properties
 */
typedef enum {
    TUSDZ_VALUE_NONE = 0,
    /* Scalar types */
    TUSDZ_VALUE_BOOL,
    TUSDZ_VALUE_INT,
    TUSDZ_VALUE_UINT,
    TUSDZ_VALUE_INT64,
    TUSDZ_VALUE_UINT64,
    TUSDZ_VALUE_HALF,
    TUSDZ_VALUE_FLOAT,
    TUSDZ_VALUE_DOUBLE,
    /* String types */
    TUSDZ_VALUE_STRING,
    TUSDZ_VALUE_TOKEN,
    TUSDZ_VALUE_ASSET_PATH,
    /* Vector types */
    TUSDZ_VALUE_INT2,
    TUSDZ_VALUE_INT3,
    TUSDZ_VALUE_INT4,
    TUSDZ_VALUE_HALF2,
    TUSDZ_VALUE_HALF3,
    TUSDZ_VALUE_HALF4,
    TUSDZ_VALUE_FLOAT2,
    TUSDZ_VALUE_FLOAT3,
    TUSDZ_VALUE_FLOAT4,
    TUSDZ_VALUE_DOUBLE2,
    TUSDZ_VALUE_DOUBLE3,
    TUSDZ_VALUE_DOUBLE4,
    /* Matrix types */
    TUSDZ_VALUE_MATRIX2D,
    TUSDZ_VALUE_MATRIX3D,
    TUSDZ_VALUE_MATRIX4D,
    /* Quaternion types */
    TUSDZ_VALUE_QUATH,
    TUSDZ_VALUE_QUATF,
    TUSDZ_VALUE_QUATD,
    /* Color types */
    TUSDZ_VALUE_COLOR3F,
    TUSDZ_VALUE_COLOR3D,
    TUSDZ_VALUE_COLOR4F,
    TUSDZ_VALUE_COLOR4D,
    /* Other types */
    TUSDZ_VALUE_NORMAL3F,
    TUSDZ_VALUE_NORMAL3D,
    TUSDZ_VALUE_POINT3F,
    TUSDZ_VALUE_POINT3D,
    TUSDZ_VALUE_TEXCOORD2F,
    TUSDZ_VALUE_TEXCOORD2D,
    TUSDZ_VALUE_TEXCOORD3F,
    TUSDZ_VALUE_TEXCOORD3D,
    /* Complex types */
    TUSDZ_VALUE_ARRAY,
    TUSDZ_VALUE_DICTIONARY,
    TUSDZ_VALUE_TIME_SAMPLES,
    TUSDZ_VALUE_RELATIONSHIP
} tusdz_value_type;

/**
 * @brief Interpolation types for animated values
 */
typedef enum {
    TUSDZ_INTERPOLATION_HELD = 0,
    TUSDZ_INTERPOLATION_LINEAR,
    TUSDZ_INTERPOLATION_BEZIER
} tusdz_interpolation;

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
} tusdz_load_options;

/* ============================================================================
 * Tier 1: Core API Functions (Essential)
 * ============================================================================ */

/**
 * @brief Initialize the TinyUSDZ library
 * @return TUSDZ_SUCCESS on success
 */
TUSDZ_API tusdz_result tusdz_init(void);

/**
 * @brief Shutdown the TinyUSDZ library and free global resources
 */
TUSDZ_API void tusdz_shutdown(void);

/**
 * @brief Get version string
 * @return Version string like "1.0.0"
 */
TUSDZ_API const char* tusdz_get_version(void);

/**
 * @brief Load USD from file
 * @param filepath Path to USD file
 * @param options Load options (can be NULL for defaults)
 * @param out_stage Output stage handle
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_buf_size Size of error buffer
 * @return Result code
 */
TUSDZ_API tusdz_result tusdz_load_from_file(
    const char* filepath,
    const tusdz_load_options* options,
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

/**
 * @brief Load USD from memory buffer
 * @param data Memory buffer containing USD data
 * @param size Size of buffer in bytes
 * @param format Format of the data (use TUSDZ_FORMAT_AUTO to detect)
 * @param options Load options (can be NULL for defaults)
 * @param out_stage Output stage handle
 * @param error_buf Buffer for error message (can be NULL)
 * @param error_buf_size Size of error buffer
 * @return Result code
 */
TUSDZ_API tusdz_result tusdz_load_from_memory(
    const void* data,
    size_t size,
    tusdz_format format,
    const tusdz_load_options* options,
    tusdz_stage* out_stage,
    char* error_buf,
    size_t error_buf_size
);

/**
 * @brief Free a stage and all associated resources
 * @param stage Stage to free
 */
TUSDZ_API void tusdz_stage_free(tusdz_stage stage);

/**
 * @brief Get the root prim of the stage
 * @param stage Stage handle
 * @return Root prim (borrowed reference, do not free)
 */
TUSDZ_API tusdz_prim tusdz_stage_get_root_prim(tusdz_stage stage);

/**
 * @brief Get number of child prims
 * @param prim Parent prim
 * @return Number of children
 */
TUSDZ_API size_t tusdz_prim_get_child_count(tusdz_prim prim);

/**
 * @brief Get child prim at index
 * @param prim Parent prim
 * @param index Child index
 * @return Child prim (borrowed reference, do not free)
 */
TUSDZ_API tusdz_prim tusdz_prim_get_child_at(tusdz_prim prim, size_t index);

/**
 * @brief Get prim name
 * @param prim Prim handle
 * @return Name string (borrowed, do not free)
 */
TUSDZ_API const char* tusdz_prim_get_name(tusdz_prim prim);

/**
 * @brief Get prim type
 * @param prim Prim handle
 * @return Prim type enum
 */
TUSDZ_API tusdz_prim_type tusdz_prim_get_type(tusdz_prim prim);

/* ============================================================================
 * Tier 2: Extended Core API
 * ============================================================================ */

/**
 * @brief Get full path of prim
 * @param prim Prim handle
 * @return Path string (borrowed, do not free)
 */
TUSDZ_API const char* tusdz_prim_get_path(tusdz_prim prim);

/**
 * @brief Get prim at specific path
 * @param stage Stage handle
 * @param path Prim path (e.g., "/World/Geo/Mesh")
 * @return Prim handle or NULL if not found
 */
TUSDZ_API tusdz_prim tusdz_stage_get_prim_at_path(tusdz_stage stage, const char* path);

/**
 * @brief Check if prim is specific type
 * @param prim Prim handle
 * @param type Type to check
 * @return 1 if matches, 0 otherwise
 */
TUSDZ_API int tusdz_prim_is_type(tusdz_prim prim, tusdz_prim_type type);

/**
 * @brief Get type name as string
 * @param prim Prim handle
 * @return Type name (e.g., "Mesh", "Xform")
 */
TUSDZ_API const char* tusdz_prim_get_type_name(tusdz_prim prim);

/**
 * @brief Get number of properties on prim
 * @param prim Prim handle
 * @return Property count
 */
TUSDZ_API size_t tusdz_prim_get_property_count(tusdz_prim prim);

/**
 * @brief Get property name at index
 * @param prim Prim handle
 * @param index Property index
 * @return Property name (borrowed, do not free)
 */
TUSDZ_API const char* tusdz_prim_get_property_name_at(tusdz_prim prim, size_t index);

/**
 * @brief Get property value by name
 * @param prim Prim handle
 * @param name Property name
 * @return Value handle (must be freed with tusdz_value_free)
 */
TUSDZ_API tusdz_value tusdz_prim_get_property(tusdz_prim prim, const char* name);

/**
 * @brief Free a value handle
 * @param value Value to free
 */
TUSDZ_API void tusdz_value_free(tusdz_value value);

/**
 * @brief Get value type
 * @param value Value handle
 * @return Value type enum
 */
TUSDZ_API tusdz_value_type tusdz_value_get_type(tusdz_value value);

/**
 * @brief Check if value is an array
 * @param value Value handle
 * @return 1 if array, 0 otherwise
 */
TUSDZ_API int tusdz_value_is_array(tusdz_value value);

/**
 * @brief Get array length for array values
 * @param value Value handle
 * @return Array length (0 if not an array)
 */
TUSDZ_API size_t tusdz_value_get_array_size(tusdz_value value);

/* ============================================================================
 * Value Extraction Functions
 * ============================================================================ */

/* Scalar extraction */
TUSDZ_API tusdz_result tusdz_value_get_bool(tusdz_value value, int* out);
TUSDZ_API tusdz_result tusdz_value_get_int(tusdz_value value, int* out);
TUSDZ_API tusdz_result tusdz_value_get_uint(tusdz_value value, unsigned int* out);
TUSDZ_API tusdz_result tusdz_value_get_int64(tusdz_value value, int64_t* out);
TUSDZ_API tusdz_result tusdz_value_get_uint64(tusdz_value value, uint64_t* out);
TUSDZ_API tusdz_result tusdz_value_get_float(tusdz_value value, float* out);
TUSDZ_API tusdz_result tusdz_value_get_double(tusdz_value value, double* out);

/* String extraction */
TUSDZ_API tusdz_result tusdz_value_get_string(tusdz_value value, const char** out);
TUSDZ_API tusdz_result tusdz_value_get_token(tusdz_value value, const char** out);
TUSDZ_API tusdz_result tusdz_value_get_asset_path(tusdz_value value, const char** out);

/* Vector extraction */
TUSDZ_API tusdz_result tusdz_value_get_float2(tusdz_value value, float* out_xy);
TUSDZ_API tusdz_result tusdz_value_get_float3(tusdz_value value, float* out_xyz);
TUSDZ_API tusdz_result tusdz_value_get_float4(tusdz_value value, float* out_xyzw);
TUSDZ_API tusdz_result tusdz_value_get_double2(tusdz_value value, double* out_xy);
TUSDZ_API tusdz_result tusdz_value_get_double3(tusdz_value value, double* out_xyz);
TUSDZ_API tusdz_result tusdz_value_get_double4(tusdz_value value, double* out_xyzw);

/* Matrix extraction (column-major) */
TUSDZ_API tusdz_result tusdz_value_get_matrix3d(tusdz_value value, double* out_mat3x3);
TUSDZ_API tusdz_result tusdz_value_get_matrix4d(tusdz_value value, double* out_mat4x4);

/* Array extraction - returns pointer to internal data, do not free */
TUSDZ_API tusdz_result tusdz_value_get_float_array(tusdz_value value, const float** out_data, size_t* out_count);
TUSDZ_API tusdz_result tusdz_value_get_int_array(tusdz_value value, const int** out_data, size_t* out_count);
TUSDZ_API tusdz_result tusdz_value_get_float3_array(tusdz_value value, const float** out_data, size_t* out_count);

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
TUSDZ_API tusdz_result tusdz_mesh_get_points(
    tusdz_prim mesh,
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
TUSDZ_API tusdz_result tusdz_mesh_get_face_counts(
    tusdz_prim mesh,
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
TUSDZ_API tusdz_result tusdz_mesh_get_indices(
    tusdz_prim mesh,
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
TUSDZ_API tusdz_result tusdz_mesh_get_normals(
    tusdz_prim mesh,
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
TUSDZ_API tusdz_result tusdz_mesh_get_uvs(
    tusdz_prim mesh,
    const float** out_uvs,
    size_t* out_count,
    int primvar_index
);

/**
 * @brief Get subdivision scheme
 * @param mesh Mesh prim
 * @return Subdivision scheme ("none", "catmullClark", "loop", "bilinear")
 */
TUSDZ_API const char* tusdz_mesh_get_subdivision_scheme(tusdz_prim mesh);

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
TUSDZ_API tusdz_result tusdz_xform_get_local_matrix(
    tusdz_prim xform,
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
TUSDZ_API tusdz_result tusdz_prim_get_world_matrix(
    tusdz_prim prim,
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
TUSDZ_API tusdz_prim tusdz_prim_get_bound_material(tusdz_prim prim);

/**
 * @brief Get surface shader from material
 * @param material Material prim
 * @return Shader prim or NULL
 */
TUSDZ_API tusdz_prim tusdz_material_get_surface_shader(tusdz_prim material);

/**
 * @brief Get shader input value
 * @param shader Shader prim
 * @param input_name Input name (e.g., "diffuseColor", "roughness")
 * @return Value handle (must be freed)
 */
TUSDZ_API tusdz_value tusdz_shader_get_input(tusdz_prim shader, const char* input_name);

/**
 * @brief Get shader type/ID
 * @param shader Shader prim
 * @return Shader type string (e.g., "UsdPreviewSurface")
 */
TUSDZ_API const char* tusdz_shader_get_type_id(tusdz_prim shader);

/* ============================================================================
 * Animation and Time Sampling API
 * ============================================================================ */

/**
 * @brief Check if stage has animation
 * @param stage Stage handle
 * @return 1 if animated, 0 otherwise
 */
TUSDZ_API int tusdz_stage_has_animation(tusdz_stage stage);

/**
 * @brief Get time code range for stage
 * @param stage Stage handle
 * @param out_start_time Start time
 * @param out_end_time End time
 * @param out_fps Frames per second
 * @return Result code
 */
TUSDZ_API tusdz_result tusdz_stage_get_time_range(
    tusdz_stage stage,
    double* out_start_time,
    double* out_end_time,
    double* out_fps
);

/**
 * @brief Check if value has time samples (is animated)
 * @param value Value handle
 * @return 1 if animated, 0 otherwise
 */
TUSDZ_API int tusdz_value_is_animated(tusdz_value value);

/**
 * @brief Get time sample times for animated value
 * @param value Value handle
 * @param out_times Output pointer to times array (do not free)
 * @param out_count Number of time samples
 * @return Result code
 */
TUSDZ_API tusdz_result tusdz_value_get_time_samples(
    tusdz_value value,
    const double** out_times,
    size_t* out_count
);

/**
 * @brief Evaluate value at specific time
 * @param value Value handle
 * @param time Time to evaluate at
 * @return New value handle at that time (must be freed)
 */
TUSDZ_API tusdz_value tusdz_value_eval_at_time(tusdz_value value, double time);

/* ============================================================================
 * Metadata API
 * ============================================================================ */

/**
 * @brief Get metadata value for prim
 * @param prim Prim handle
 * @param key Metadata key (e.g., "documentation", "hidden")
 * @return Value handle or NULL if not found (must be freed if not NULL)
 */
TUSDZ_API tusdz_value tusdz_prim_get_metadata(tusdz_prim prim, const char* key);

/**
 * @brief Get list of metadata keys
 * @param prim Prim handle
 * @param out_keys Output array of key strings (do not free)
 * @param out_count Number of keys
 * @return Result code
 */
TUSDZ_API tusdz_result tusdz_prim_get_metadata_keys(
    tusdz_prim prim,
    const char*** out_keys,
    size_t* out_count
);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/**
 * @brief Free memory allocated by TinyUSDZ
 * @param ptr Pointer to free
 */
TUSDZ_API void tusdz_free(void* ptr);

/**
 * @brief Convert result code to string
 * @param result Result code
 * @return String description
 */
TUSDZ_API const char* tusdz_result_to_string(tusdz_result result);

/**
 * @brief Convert prim type to string
 * @param type Prim type
 * @return Type name string
 */
TUSDZ_API const char* tusdz_prim_type_to_string(tusdz_prim_type type);

/**
 * @brief Convert value type to string
 * @param type Value type
 * @return Type name string
 */
TUSDZ_API const char* tusdz_value_type_to_string(tusdz_value_type type);

/**
 * @brief Detect USD format from file extension
 * @param filepath File path
 * @return Detected format
 */
TUSDZ_API tusdz_format tusdz_detect_format(const char* filepath);

/* ============================================================================
 * Debug and Diagnostic Functions
 * ============================================================================ */

/**
 * @brief Print stage hierarchy to stdout
 * @param stage Stage handle
 * @param max_depth Maximum depth to print (0 = all)
 */
TUSDZ_API void tusdz_stage_print_hierarchy(tusdz_stage stage, int max_depth);

/**
 * @brief Get memory usage statistics
 * @param stage Stage handle (can be NULL for global stats)
 * @param out_bytes_used Bytes currently used
 * @param out_bytes_peak Peak bytes used
 */
TUSDZ_API void tusdz_get_memory_stats(
    tusdz_stage stage,
    size_t* out_bytes_used,
    size_t* out_bytes_peak
);

/**
 * @brief Enable/disable debug logging
 * @param enable 1 to enable, 0 to disable
 */
TUSDZ_API void tusdz_set_debug_logging(int enable);

#ifdef __cplusplus
}
#endif

#endif /* TINYUSDZ_C_H */