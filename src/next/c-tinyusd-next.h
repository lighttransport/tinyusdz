// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - C API (C11)
// Clean, focused FFI surface for the "next" architecture.
// Suitable for Python bindings, Swift/Java/Kotlin FFI, etc.

#ifndef TINYUSDZ_NEXT_C_API_H
#define TINYUSDZ_NEXT_C_API_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// Version
// ============================================================

#define TINYUSDZ_NEXT_C_API_VERSION_MAJOR 0
#define TINYUSDZ_NEXT_C_API_VERSION_MINOR 2
#define TINYUSDZ_NEXT_C_API_VERSION_MICRO 0

// ============================================================
// Error / Result type
// ============================================================

/// Result type for C API operations.
/// 1 = success, 0 = failure.
typedef int tinyusdz_next_result_t;

// ============================================================
// Value type enum
// ============================================================

typedef enum {
  TINYUSDZ_NEXT_VALUE_UNKNOWN = 0,
  TINYUSDZ_NEXT_VALUE_BOOL,
  TINYUSDZ_NEXT_VALUE_INT32,
  TINYUSDZ_NEXT_VALUE_UINT32,
  TINYUSDZ_NEXT_VALUE_INT64,
  TINYUSDZ_NEXT_VALUE_UINT64,
  TINYUSDZ_NEXT_VALUE_FLOAT,
  TINYUSDZ_NEXT_VALUE_DOUBLE,
  TINYUSDZ_NEXT_VALUE_STRING,
  TINYUSDZ_NEXT_VALUE_TOKEN,
  TINYUSDZ_NEXT_VALUE_ASSET_PATH,
  TINYUSDZ_NEXT_VALUE_FLOAT2,
  TINYUSDZ_NEXT_VALUE_FLOAT3,
  TINYUSDZ_NEXT_VALUE_FLOAT4,
  TINYUSDZ_NEXT_VALUE_DOUBLE2,
  TINYUSDZ_NEXT_VALUE_DOUBLE3,
  TINYUSDZ_NEXT_VALUE_DOUBLE4,
  TINYUSDZ_NEXT_VALUE_INT32_2,
  TINYUSDZ_NEXT_VALUE_INT32_3,
  TINYUSDZ_NEXT_VALUE_INT32_4,
  TINYUSDZ_NEXT_VALUE_MATRIX4D,
  TINYUSDZ_NEXT_VALUE_FLOAT_ARRAY,
  TINYUSDZ_NEXT_VALUE_INT32_ARRAY,
  TINYUSDZ_NEXT_VALUE_DOUBLE_ARRAY,
  TINYUSDZ_NEXT_VALUE_INT64_ARRAY,
  TINYUSDZ_NEXT_VALUE_UINT_ARRAY,
  TINYUSDZ_NEXT_VALUE_UINT64_ARRAY,
  TINYUSDZ_NEXT_VALUE_BOOL_ARRAY,
  TINYUSDZ_NEXT_VALUE_TOKEN_ARRAY,
} tinyusdz_next_value_type_t;

// ============================================================
// Error string
// ============================================================

/// Get last error message (thread-local buffer).
/// Returns NULL if no error. Valid until next C API call.
const char* tinyusdz_next_error_string(void);

/// Set custom error message (for internal use).
void tinyusdz_next_set_error(const char* msg);

// ============================================================
// Opaque types
// ============================================================

typedef struct TinyUSDZNextStage TinyUSDZNextStage;
typedef struct TinyUSDZNextPrim TinyUSDZNextPrim;

// ============================================================
// Stage API
// ============================================================

/// Create an empty stage.
TinyUSDZNextStage* tinyusdz_next_stage_new(void);

/// Free a stage.
void tinyusdz_next_stage_free(TinyUSDZNextStage* stage);

/// Load a USD file (auto-detects USDA/USDC).
/// Returns stage on success, NULL on failure (check error_string).
TinyUSDZNextStage* tinyusdz_next_load_usd(const char* filename);

/// Load a USDA (ASCII) file.
TinyUSDZNextStage* tinyusdz_next_load_usda(const char* filename);

/// Load a USDC (binary/Crate) file.
TinyUSDZNextStage* tinyusdz_next_load_usdc(const char* filename);

/// Get the root layer's defaultPrim name.
/// Returns NULL if not set.
const char* tinyusdz_next_stage_default_prim(const TinyUSDZNextStage* stage);

// ============================================================
// Prim API
// ============================================================

/// Get a prim at the given path (e.g. "/root/child/grandchild").
/// Returns NULL if not found.
const TinyUSDZNextPrim* tinyusdz_next_stage_get_prim_at_path(
    const TinyUSDZNextStage* stage, const char* path);

/// Get the type name of a prim (e.g. "Xform", "Mesh", "Material").
/// Returns NULL on invalid prim. Lifetime: until stage is freed.
const char* tinyusdz_next_prim_get_type_name(const TinyUSDZNextPrim* prim);

/// Get the element name of a prim (e.g. "root", "pbr").
const char* tinyusdz_next_prim_get_name(const TinyUSDZNextPrim* prim);

/// Get the full path of a prim.
const char* tinyusdz_next_prim_get_path(const TinyUSDZNextPrim* prim);

/// Check if a prim has a specific property (attribute or relationship).
tinyusdz_next_result_t tinyusdz_next_prim_has_property(
    const TinyUSDZNextPrim* prim, const char* prop_name);

/// Check if a prim has a specific relationship.
tinyusdz_next_result_t tinyusdz_next_prim_has_relationship(
    const TinyUSDZNextPrim* prim, const char* rel_name);

/// Get the number of child prims.
size_t tinyusdz_next_prim_get_child_count(const TinyUSDZNextPrim* prim);

/// Get a child prim by index. Returns NULL if index out of range.
const TinyUSDZNextPrim* tinyusdz_next_prim_get_child(
    const TinyUSDZNextPrim* prim, size_t index);

/// Traverse all root prims in a stage.
/// Callback receives (prim, depth). Return non-zero to continue.
/// Returns number of prims traversed.
typedef int (*tinyusdz_next_traverse_callback_t)(
    const TinyUSDZNextPrim* prim, int depth, void* user_data);

// ============================================================
// Composition arc authoring
// ============================================================

/// Add a reference to a prim: @asset@</path>
tinyusdz_next_result_t tinyusdz_next_prim_add_reference(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* asset_path, const char* ref_prim_path);

/// Add a payload to a prim: @asset@</path>
tinyusdz_next_result_t tinyusdz_next_prim_add_payload(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* asset_path, const char* payload_prim_path);

/// Add an inherit arc: </path>
tinyusdz_next_result_t tinyusdz_next_prim_add_inherit(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* inherited_prim_path);

/// Add a specialize arc: </path>
tinyusdz_next_result_t tinyusdz_next_prim_add_specialize(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* specialized_prim_path);

/// Set a variant selection: "variantSet=variantName"
tinyusdz_next_result_t tinyusdz_next_prim_set_variant_selection(
    TinyUSDZNextStage* stage, const char* prim_path,
    const char* variant_set, const char* variant_name);

size_t tinyusdz_next_stage_traverse(
    const TinyUSDZNextStage* stage,
    tinyusdz_next_traverse_callback_t callback,
    void* user_data);

// ============================================================
// Property value access
// ============================================================

/// Get the type of a property value.
tinyusdz_next_value_type_t tinyusdz_next_prim_get_value_type(
    const TinyUSDZNextPrim* prim, const char* prop_name);

/// Get a boolean property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_bool(
    const TinyUSDZNextPrim* prim, const char* prop_name, int* out);

/// Get a float property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_float(
    const TinyUSDZNextPrim* prim, const char* prop_name, float* out);

/// Get a double property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_double(
    const TinyUSDZNextPrim* prim, const char* prop_name, double* out);

/// Get an int32 property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_int32(
    const TinyUSDZNextPrim* prim, const char* prop_name, int32_t* out);

/// Get a string/token property value.
/// Returns NULL if not found or wrong type.
const char* tinyusdz_next_prim_get_string(
    const TinyUSDZNextPrim* prim, const char* prop_name);

/// Get a float3 property value (x, y, z).
tinyusdz_next_result_t tinyusdz_next_prim_get_float3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[3]);

/// Get a float4 / quatf property value (x, y, z, w).
tinyusdz_next_result_t tinyusdz_next_prim_get_float4(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[4]);

/// Get a float2 / vec2f property value (x, y).
tinyusdz_next_result_t tinyusdz_next_prim_get_float2(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    float out[2]);

/// Get a double2 / vec2d property value (x, y).
tinyusdz_next_result_t tinyusdz_next_prim_get_double2(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[2]);

/// Get a double3 / vec3d property value (x, y, z).
tinyusdz_next_result_t tinyusdz_next_prim_get_double3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[3]);

/// Get a double4 / vec4d property value (x, y, z, w).
tinyusdz_next_result_t tinyusdz_next_prim_get_double4(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[4]);

/// Get an int64 property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_int64(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    int64_t* out);

/// Get a uint32 property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_uint32(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    uint32_t* out);

/// Get a uint64 property value.
tinyusdz_next_result_t tinyusdz_next_prim_get_uint64(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    uint64_t* out);

/// Get a double array property value.
/// Returns number of doubles. out_ptr points to internal data.
size_t tinyusdz_next_prim_get_double_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const double** out_ptr);

/// Get an int64 array property value.
size_t tinyusdz_next_prim_get_int64_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const int64_t** out_ptr);

/// Get a uint32 array property value.
size_t tinyusdz_next_prim_get_uint_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const uint32_t** out_ptr);

/// Get a uint64 array property value.
size_t tinyusdz_next_prim_get_uint64_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const uint64_t** out_ptr);

/// Get a bool array property value.
/// Returns number of bools. out_ptr points to internal data.
size_t tinyusdz_next_prim_get_bool_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const uint8_t** out_ptr);

/// Get a token array property value.
/// Returns number of tokens. out_ptr points to internal data.
size_t tinyusdz_next_prim_get_token_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const char*** out_ptr);

/// Get a float array property value.
/// Returns number of floats in the array. out_ptr points to internal data
/// (valid until stage is freed). Returns 0 on failure.
size_t tinyusdz_next_prim_get_float_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const float** out_ptr);

/// Get an int32 array property value.
size_t tinyusdz_next_prim_get_int32_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    const int32_t** out_ptr);

/// Get a matrix4d property value (16 doubles, row-major).
tinyusdz_next_result_t tinyusdz_next_prim_get_matrix4d(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double out[16]);

/// Get a relationship target paths.
/// Returns number of targets. out_ptr points to internal data.
size_t tinyusdz_next_prim_get_relationship_targets(
    const TinyUSDZNextPrim* prim, const char* rel_name,
    const char*** out_ptr);

/// Get all property names on a prim.
/// Returns number of property names. out_ptr points to internal data
/// (valid until next C API call).
size_t tinyusdz_next_prim_get_property_names(
    const TinyUSDZNextPrim* prim, const char*** out_ptr);

/// Get all relationship names on a prim.
/// Returns number of relationships. out_ptr points to internal data.
size_t tinyusdz_next_prim_get_relationship_names(
    const TinyUSDZNextPrim* prim, const char*** out_ptr);

// ============================================================
// Attribute evaluation (time-sampled access)
// ============================================================

/// Check if a property has time samples.
tinyusdz_next_result_t tinyusdz_next_prim_has_time_samples(
    const TinyUSDZNextPrim* prim, const char* prop_name);

/// Evaluate a float property at a given time.
tinyusdz_next_result_t tinyusdz_next_prim_eval_float(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, float* out);

/// Evaluate a float3 property at a given time.
tinyusdz_next_result_t tinyusdz_next_prim_eval_float3(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, float out[3]);

/// Evaluate a float array at a given time.
size_t tinyusdz_next_prim_eval_float_array(
    const TinyUSDZNextPrim* prim, const char* prop_name,
    double time, const float** out_ptr);

#ifdef __cplusplus
}
#endif

#endif // TINYUSDZ_NEXT_C_API_H
