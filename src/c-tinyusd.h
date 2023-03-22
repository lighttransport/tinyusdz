// SPDX-License-Identifier: Apache 2.0
//
// C API(C11) for TinyUSDZ
// This C API is primarily for bindings for other languages.
// Various features/manipulations are missing and not intended to use C API
// solely(at the moment).
//
// NOTE: Use `c_tinyusd` or `CTinyUSD` prefix(`z` is missing) in C API.
//
#ifndef C_TINYUSD_H
#define C_TINYUSD_H

#include <stdint.h>
#include <assert.h>
#include <uchar.h>  // size_t

#ifdef __cplusplus
extern "C" {
#endif

// NOTE: Current(2023.03) USD spec does not support 2D or multi-dim array,
// so set max_dim to 1.
#define C_TINYUSD_MAX_DIM (1)

// USD format
typedef enum {
  C_TINYUSD_FORMAT_UNKNOWN,
  C_TINYUSD_FORMAT_AUTO,  // auto detect based on file extension.
  C_TINYUSD_FORMAT_USDA,
  C_TINYUSD_FORMAT_USDC,
  C_TINYUSD_FORMAT_USDZ
} CTinyUSDFormat;

typedef enum {
  C_TINYUSD_AXIS_UNKNOWN,
  C_TINYUSD_AXIS_X,
  C_TINYUSD_AXIS_Y,
  C_TINYUSD_AXIS_Z,
} CTinyUSDAxis;

typedef enum {
  C_TINYUSD_VALUE_TOKEN,
  C_TINYUSD_VALUE_STRING,
  C_TINYUSD_VALUE_BOOL,
  // C_TINYUSD_VALUE_SHORT,
  // C_TINYUSD_VALUE_USHORT,
  C_TINYUSD_VALUE_HALF,
  C_TINYUSD_VALUE_HALF2,
  C_TINYUSD_VALUE_HALF3,
  C_TINYUSD_VALUE_HALF4,
  C_TINYUSD_VALUE_INT,
  C_TINYUSD_VALUE_INT2,
  C_TINYUSD_VALUE_INT3,
  C_TINYUSD_VALUE_INT4,
  C_TINYUSD_VALUE_UINT,
  C_TINYUSD_VALUE_UINT2,
  C_TINYUSD_VALUE_UINT3,
  C_TINYUSD_VALUE_UINT4,
  C_TINYUSD_VALUE_INT64,
  C_TINYUSD_VALUE_UINT64,
  C_TINYUSD_VALUE_FLOAT,
  C_TINYUSD_VALUE_FLOAT2,
  C_TINYUSD_VALUE_FLOAT3,
  C_TINYUSD_VALUE_FLOAT4,
  C_TINYUSD_VALUE_DOUBLE,
  C_TINYUSD_VALUE_DOUBLE2,
  C_TINYUSD_VALUE_DOUBLE3,
  C_TINYUSD_VALUE_DOUBLE4,
  C_TINYUSD_VALUE_QUATH,
  C_TINYUSD_VALUE_QUATF,
  C_TINYUSD_VALUE_QUATD,
  C_TINYUSD_VALUE_COLOR3H,
  C_TINYUSD_VALUE_COLOR3F,
  C_TINYUSD_VALUE_COLOR3D,
  C_TINYUSD_VALUE_COLOR4H,
  C_TINYUSD_VALUE_COLOR4F,
  C_TINYUSD_VALUE_COLOR4D,
  C_TINYUSD_VALUE_TEXCOORD2H,
  C_TINYUSD_VALUE_TEXCOORD2F,
  C_TINYUSD_VALUE_TEXCOORD2D,
  C_TINYUSD_VALUE_TEXCOORD3H,
  C_TINYUSD_VALUE_TEXCOORD3F,
  C_TINYUSD_VALUE_TEXCOORD3D,
  C_TINYUSD_VALUE_NORMAL3H,
  C_TINYUSD_VALUE_NORMAL3F,
  C_TINYUSD_VALUE_NORMAL3D,
  C_TINYUSD_VALUE_VECTOR3H,
  C_TINYUSD_VALUE_VECTOR3F,
  C_TINYUSD_VALUE_VECTOR3D,
  C_TINYUSD_VALUE_POINT3H,
  C_TINYUSD_VALUE_POINT3F,
  C_TINYUSD_VALUE_POINT3D,
  C_TINYUSD_VALUE_MATRIX2D,
  C_TINYUSD_VALUE_MATRIX3D,
  C_TINYUSD_VALUE_MATRIX4D,
  C_TINYUSD_VALUE_FRAME4D,
  C_TINYUSD_VALUE_END,  // terminator
} CTinyUSDValueType;

// assume the number of value types is less than 1024.
#define C_TINYUSD_VALUE_1D_BIT (1 << 10)

// NOTE: No `Geom` prefix to usdGeom prims in C API.
typedef enum {
  C_TINYUSD_PRIM_UNKNOWN,
  C_TINYUSD_PRIM_MODEL,
  C_TINYUSD_PRIM_XFORM,
  C_TINYUSD_PRIM_MESH,
  C_TINYUSD_PRIM_GEOMSUBSET,
  C_TINYUSD_PRIM_MATERIAL,
  C_TINYUSD_PRIM_SHADER,
  C_TINYUSD_PRIM_CAMERA,
  C_TINYUSD_PRIM_SPHERE_LIGHT,
  C_TINYUSD_PRIM_DISTANT_LIGHT,
  C_TINYUSD_PRIM_RECT_LIGHT,
  // TODO: Add more prim types...
  C_TINYUSD_PRIM_END,
} CTinyUSDPrimType;

//
// Use lower snake case for frequently used base type.
// 

typedef uint16_t c_tinyusd_half;

// Assume struct elements will be tightly packed in C11.
// TODO: Ensure struct elements are tightly packed.
typedef struct {
  int x;
  int y;
} c_tinyusd_int2;
static_assert(sizeof(c_tinyusd_int2) == sizeof(float) * 2, "");

typedef struct {
  int x;
  int y;
  int z;
} c_tinyusd_int3;
static_assert(sizeof(c_tinyusd_int3) == sizeof(float) * 3, "");

typedef struct {
  int x;
  int y;
  int z;
  int w;
} c_tinyusd_int4;
static_assert(sizeof(c_tinyusd_int4) == sizeof(float) * 4, "");

typedef struct {
  uint32_t x;
  uint32_t y;
} c_tinyusd_uint2;

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t z;
} c_tinyusd_uint3;

typedef struct {
  uint32_t x;
  uint32_t y;
  uint32_t z;
  uint32_t w;
} c_tinyusd_uint4;

typedef struct {
  c_tinyusd_half x;
  c_tinyusd_half y;
} c_tinyusd_half2;
static_assert(sizeof(c_tinyusd_half2) == sizeof(uint16_t) * 2, "");

typedef struct {
  c_tinyusd_half x;
  c_tinyusd_half y;
  c_tinyusd_half z;
} c_tinyusd_half3;
static_assert(sizeof(c_tinyusd_half3) == sizeof(uint16_t) * 3, "");

typedef struct {
  c_tinyusd_half x;
  c_tinyusd_half y;
  c_tinyusd_half z;
  c_tinyusd_half w;
} c_tinyusd_half4;
static_assert(sizeof(c_tinyusd_half4) == sizeof(uint16_t) * 4, "");

typedef struct {
  float x;
  float y;
} c_tinyusd_float2;

typedef struct {
  float x;
  float y;
  float z;
} c_tinyusd_float3;

typedef struct {
  float x;
  float y;
  float z;
  float w;
} c_tinyusd_float4;

typedef struct {
  double x;
  double y;
} c_tinyusd_double2;

typedef struct {
  double x;
  double y;
  double z;
} c_tinyusd_double3;

typedef struct {
  double x;
  double y;
  double z;
  double w;
} c_tinyusd_double4;

typedef struct {
  double m[4];
} c_tinyusd_matrix2d;

typedef struct {
  double m[9];
} c_tinyusd_matrix3d;

typedef struct {
  double m[16];
} c_tinyusd_matrix4d;

typedef struct {
  c_tinyusd_half imag[3];
  c_tinyusd_half real;
} c_tinyusd_quath; 
static_assert(sizeof(c_tinyusd_quath) == sizeof(uint16_t) * 4, "");

typedef struct {
  float imag[3];
  float real;
} c_tinyusd_quatf; 
static_assert(sizeof(c_tinyusd_quatf) == sizeof(float) * 4, "");

typedef struct {
  double imag[3];
  double real;
} c_tinyusd_quatd; 
static_assert(sizeof(c_tinyusd_quatd) == sizeof(double) * 4, "");

typedef c_tinyusd_half3 c_tinyusd_color3h;
typedef c_tinyusd_float3 c_tinyusd_color3f;
typedef c_tinyusd_double3 c_tinyusd_color3d;

typedef c_tinyusd_half4 c_tinyusd_color4h;
typedef c_tinyusd_float4 c_tinyusd_color4f;
typedef c_tinyusd_double4 c_tinyusd_color4d;

typedef c_tinyusd_half3 c_tinyusd_point3h;
typedef c_tinyusd_float3 c_tinyusd_point3f;
typedef c_tinyusd_double3 c_tinyusd_point3d;

typedef c_tinyusd_half3 c_tinyusd_normal3h;
typedef c_tinyusd_float3 c_tinyusd_normal3f;
typedef c_tinyusd_double3 c_tinyusd_normal3d;

typedef c_tinyusd_half3 c_tinyusd_vector3h;
typedef c_tinyusd_float3 c_tinyusd_vector3f;
typedef c_tinyusd_double3 c_tinyusd_vector3d;

typedef c_tinyusd_matrix4d c_tinyusd_frame4d;

typedef c_tinyusd_half2 c_tinyusd_texcoord2h;
typedef c_tinyusd_float2 c_tinyusd_texcoord2f;
typedef c_tinyusd_double2 c_tinyusd_texcoord2d;

typedef c_tinyusd_half3 c_tinyusd_texcoord3h;
typedef c_tinyusd_float3 c_tinyusd_texcoord3f;
typedef c_tinyusd_double3 c_tinyusd_texcoord3d;

typedef struct {
  void *data;  // opaque pointer to `tinyusdz::value::token`.
} c_tinyusd_token;

// Create token and set a string to it.
// returns 0 when failed to allocate memory.
int c_tinyusd_token_new(c_tinyusd_token *tok, const char *str);

// Length of token string. equivalent to std::string::size.
size_t c_tinyusd_token_size(const c_tinyusd_token *tok);

// Get C char from a token.
// Returned char pointer is valid until `c_tinyusd_token` instance is free'ed.
const char *c_tinyusd_token_str(const c_tinyusd_token *tok);

// Free token
// Return 0 when failed to free.
int c_tinyusd_token_free(c_tinyusd_token *tok);

typedef struct {
  void *data;  // opaque pointer to `std::string`.
} c_tinyusd_string;

// Create empty string.
// Return 0 when failed to new
int c_tinyusd_string_new_empty(c_tinyusd_string *s);

// Create string.
// Pass NULL is identical to `c_tinyusd_string_new_empty`.
// Return 0 when failed to new
int c_tinyusd_string_new(c_tinyusd_string *s, const char *str);

// Length of string. equivalent to std::string::size.
size_t c_tinyusd_string_size(const c_tinyusd_string *s);

// Replace existing string with given `str`.
// `c_tinyusd_string` object must be created beforehand.
// Return 0 when failed to set.
int c_tinyusd_string_replace(c_tinyusd_string *s, const char *str);

// Get C char(`std::string::c_str()`)
// Returned char pointer is valid until `c_tinyusd_string` instance is free'ed.
const char *c_tinyusd_string_str(const c_tinyusd_string *s);

int c_tinyusd_string_free(c_tinyusd_string *s);

// Return the name of Prim type.
// Return NULL for unsupported/unknown Prim type.
const char *c_tinyusd_prim_type_name(CTinyUSDPrimType prim_type);

// Return Builtin PrimType from a string.
// Returns C_TINYUSD_PRIM_UNKNOWN for invalid or unknown/unsupported Prim type
CTinyUSDPrimType c_tinyusd_prim_type_from_string(const char *prim_type);

// Generic Buffer data with type info
typedef struct {
  CTinyUSDValueType value_type;
  uint32_t ndim; // 0 = scalar value
  uint64_t shape[C_TINYUSD_MAX_DIM];
  void *data;  // opaque pointer

  // TODO: stride
} CTinyUSDBuffer;

// Returns name of ValueType.
// The pointer points to static Thread-local storage(so thread-safe), thus no
// need to free it.
const char *c_tinyusd_value_type_name(CTinyUSDValueType value_type);

// Returns sizeof(value_type);
// For non-numeric value type(e.g. STRING, TOKEN) and invalid enum value, it
// returns 0. NOTE: Returns 1 for bool type.
uint32_t c_tinyusd_value_type_sizeof(CTinyUSDValueType value_type);

// Returns the number of components of given value_type;
// For example, 3 for C_TINYUSD_VALUE_POINT3F.
// For non-numeric value type(e.g. STRING, TOKEN), it returns 0.
// For scalar type, it returns 1.
uint32_t c_tinyusd_value_type_components(CTinyUSDValueType value_type);

#if 0
// Initialize buffer, but do not allocate memory.
void c_tinyusd_buffer_init(CTinyUSDBuffer *buf, CTinyUSDValueType value_type,
                           int ndim);
#endif

// New buffer with scalar value.
// Returns 1 upon success.
int c_tinyusd_buffer_new(CTinyUSDBuffer *buf, CTinyUSDValueType value_type);

// New buffer with token value.
// token string is copied into buffer.
// Buffer size = strlen(token)
int c_tinyusd_buffer_new_and_copy_token(CTinyUSDBuffer *buf, const c_tinyusd_token *tok);

// New buffer with string value.
// string is copied into buffer.
// Buffer size = strlen(str)
int c_tinyusd_buffer_new_and_copy_string(CTinyUSDBuffer *buf, const c_tinyusd_string *str);

// New buffer with 1D array of `n` elements.
// Returns 1 upon success.
int c_tinyusd_buffer_new_array(CTinyUSDBuffer *buf, CTinyUSDValueType value_type, uint64_t n);

// Free Buffer's memory. Returns 1 upon success.
int c_tinyusd_buffer_free(CTinyUSDBuffer *buf);

typedef struct {
  CTinyUSDBuffer buffer;
} CTinyUSDAttributeValue;

//
// New AttributeValue with buffer.
// Type of AttributeValue is obtained from buffer->value_type;
//
int c_tinyusd_attribute_value_new(CTinyUSDAttributeValue *val,
                                   const CTinyUSDBuffer *buffer);

// Print AttributeValue.
// Return 0 upon error.
int c_tinyusd_attribute_value_to_string(const CTinyUSDAttributeValue *val, c_tinyusd_string *str);

// Free AttributeValue.
// Internally calls `c_tinyusd_buffer_free` to free buffer associated with this AttributeValue. 
int c_tinyusd_attribute_value_free(CTinyUSDAttributeValue *val);

//
// New AttributeValue with token type.
// NOTE: token data are copied. So it is safe to free token after calling this function.
//
int c_tinyusd_attribute_value_new_token(CTinyUSDAttributeValue *aval, const c_tinyusd_token *val);

//
// New AttributeValue with string type.
// NOTE: string data are copied. So it is safe to free string after calling this function.
//
int c_tinyusd_attribute_value_new_string(CTinyUSDAttributeValue *aval, const c_tinyusd_string *val);

//
// New AttributeValue with specific type.
// NOTE: Datas are copied.
// Returns 1 upon success, 0 failed.
//
int c_tinyusd_attribute_value_new_int(CTinyUSDAttributeValue *aval, int val);
int c_tinyusd_attribute_value_new_int2(CTinyUSDAttributeValue *aval, c_tinyusd_int2 val);
int c_tinyusd_attribute_value_new_int3(CTinyUSDAttributeValue *aval, c_tinyusd_int3 val);
int c_tinyusd_attribute_value_new_int4(CTinyUSDAttributeValue *aval, c_tinyusd_int4 val);
int c_tinyusd_attribute_value_new_float(CTinyUSDAttributeValue *aval, float val);
int c_tinyusd_attribute_value_new_float2(CTinyUSDAttributeValue *aval, c_tinyusd_float2 val);
int c_tinyusd_attribute_value_new_float3(CTinyUSDAttributeValue *aval, c_tinyusd_float3 val);
int c_tinyusd_attribute_value_new_float4(CTinyUSDAttributeValue *aval, c_tinyusd_float4 val);
// TODO: List up other types...

//
// New AttributeValue with 1D array ofspecific type.
// NOTE: Array data is copied.
//
int c_tinyusd_attribute_value_new_int_array(CTinyUSDAttributeValue *aval, uint64_t n, int *vals);
int c_tinyusd_attribute_value_new_int2_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_int2 *vals);
int c_tinyusd_attribute_value_new_int3_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_int3 *vals);
int c_tinyusd_attribute_value_new_int4_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_int4 *vals);
int c_tinyusd_attribute_value_new_float_array(CTinyUSDAttributeValue *aval, uint64_t n, float *vals);
int c_tinyusd_attribute_value_new_float2_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_float2 *vals);
int c_tinyusd_attribute_value_new_float3_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_float3 *vals);
int c_tinyusd_attribute_value_new_float4_array(CTinyUSDAttributeValue *aval, uint64_t n, c_tinyusd_float4 *vals);
// TODO: List up other types...

typedef struct {
  // c_tinyusd_string prim_part;
  // c_tinyusd_string prop_part;
  void *data;  // opaque pointer to tinyusdz::Path
} CTinyUSDPath;

typedef struct {
  void *data;  // opaque pointer to tinyusdz::Property
} CTinyUSDProperty;

//typedef struct {
//  void *data;  // opaque pointer to std::map<std::string, tinyusdz::Property>
//} CTinyUSDPropertyMap;

typedef struct {
  void *data;  // opaque pointer to tinyusdz::Relationship
} CTinyUSDRelationship;

CTinyUSDRelationship *c_tinyusd_relationsip_new(int n,
                                                const char **targetPaths);
void c_tinyusd_relationsip_delete(CTinyUSDRelationship *rel);

typedef struct {
  void *data;  // opaque pointer to tinyusdz::Attribute
} CTinyUSDAttribute;

typedef struct {
  // c_tinyusd_string prim_element_name;
  // CTinyUSDPrimType prim_type;

  // CTinyUSDPropertyMap props;
  void *data;  // opaque pointer to tinyusdz::Prim
} CTinyUSDPrim;

// Create Prim
int c_tinyusd_prim_new(const char *prim_type, CTinyUSDPrim *prim /* out */);

// Create Prim with builtin Prim type.
int c_tinyusd_prim_builtin_new(CTinyUSDPrimType prim_type, CTinyUSDPrim *prim/* out */);

int c_tinyusd_prim_to_string(const CTinyUSDPrim *prim, c_tinyusd_string *str);

int c_tinyusd_prim_free(CTinyUSDPrim *prim);

// Get Prim's property. Returns 0 when property `prop_name` does not exist in the Prim.
// `prop` just holds pointer to corresponding C++ Property instance, so no free operation required.
int c_tinyusd_prim_property_get(const CTinyUSDPrim *prim, const char *prop_name, CTinyUSDProperty *prop);

// Add property to the Prim.
// It copies the content of `prop`, so please free `prop` after this add operation.
// Returns 0 when the operation failed(`err` will be returned. Please free `err` after using it)
int c_tinyusd_prim_property_add(CTinyUSDPrim *prim, const char *prop_name, CTinyUSDProperty *prop, c_tinyusd_string *err);

// Delete a property in the Prim.
// Returns 0 when `prop_name` does not exist in the prim.
int c_tinyusd_prim_property_del(CTinyUSDPrim *prim, const char *prop_name);

//
// Append Prim to `prim`'s children.
//
int c_tinyusd_prim_append_child(CTinyUSDPrim *prim, CTinyUSDPrim *child);

// Delete child[child_index].
// Return 0 when `child_index` is out-of-range.
int c_tinyusd_prim_del_child(CTinyUSDPrim *prim, int child_index);

//
// Return the number of child Prims in this Prim.
//
// Return 0 when `prim` is invalid or nullptr.
//
uint64_t c_tinyusd_prim_num_children(const CTinyUSDPrim *prim);

//
// Get a child Prim of specified child_index.
//
// Child's conent is just a pointer, so please do not call Prim deleter(`c_tinyusd_prim_free`) to it.
// (Please use `c_tinyusd_prim_del_child` if you want to remove a child Prim)
//
// Also the content(pointer) is valid unless the `prim`'s children is preserved(i.e., child is not deleted/added)
//
// Return 0 when `child_index` is out-of-range.
int c_tinyusd_prim_get_child(const CTinyUSDPrim *prim, uint32_t child_index,
                             CTinyUSDPrim **child_prim);

typedef struct {
  void *data;  // opaque pointer to tinyusd::Stage
} CTinyUSDStage;

int c_tinyusd_stage_new(CTinyUSDStage *stage);
int c_tinyusd_stage_to_string(const CTinyUSDStage *stage,
                              c_tinyusd_string *str);
int c_tinyusd_stage_free(CTinyUSDStage *stage);

// Callback function for Stage's root Prim traversal.
// Return 1 for success, Return 0 to stop traversal futher.
typedef int (*CTinyUSDTraversalFunction)(const CTinyUSDPrim *prim,
                                         const CTinyUSDPath *path);

///
/// Traverse root Prims in the Stage and invoke callback function for each Prim.
///
/// @param[in] stage Stage.
/// @param[in] callbacl_fun Callback function.
/// @param[out] err Optional. error message.
///
/// @return 1 upon success. 0 when failed(and `err` will be set).
///
/// When providing `err`, it must be created with `c_tinyusd_string_new` before
/// calling this `c_tinyusd_stage_traverse` function, and an App must free it by
/// calling `c_tinyusd_string_free` after using it.
///
int c_tinyusd_stage_traverse(const CTinyUSDStage *stage,
                             CTinyUSDTraversalFunction callback_fun,
                             c_tinyusd_string *err);

///
/// Detect file format of input file.
///
///
CTinyUSDFormat c_tinyusd_detect_format(const char *filename);

int c_tinyusd_is_usd_file(const char *filename);
int c_tinyusd_is_usda_file(const char *filename);
int c_tinyusd_is_usdc_file(const char *filename);
int c_tinyusd_is_usdz_file(const char *filename);

int c_tinyusd_is_usd_memory(const uint8_t *addr, const size_t nbytes);
int c_tinyusd_is_usda_memory(const uint8_t *addr, const size_t nbytes);
int c_tinyusd_is_usdc_memory(const uint8_t *addr, const size_t nbytes);
int c_tinyusd_is_usdz_memory(const uint8_t *addr, const size_t nbytes);

int c_tinyusd_load_usd_from_file(const char *filename, CTinyUSDStage *stage,
                                 c_tinyusd_string *warn, c_tinyusd_string *err);
int c_tinyusd_load_usda_from_file(const char *filename, CTinyUSDStage *stage,
                                  c_tinyusd_string *warn,
                                  c_tinyusd_string *err);
int c_tinyusd_load_usdc_from_file(const char *filename, CTinyUSDStage *stage,
                                  c_tinyusd_string *warn,
                                  c_tinyusd_string *err);
int c_tinyusd_load_usdz_from_file(const char *filename, CTinyUSDStage *stage,
                                  c_tinyusd_string *warn,
                                  c_tinyusd_string *err);

int c_tinyusd_load_usd_from_memory(const uint8_t *addr, const size_t nbytes,
                                   c_tinyusd_string *warn,
                                   c_tinyusd_string *err);
int c_tinyusd_load_usda_from_memory(const uint8_t *addr, const size_t nbytes,
                                    c_tinyusd_string *warn,
                                    c_tinyusd_string *err);
int c_tinyusd_load_usdc_from_memory(const uint8_t *addr, const size_t nbytes,
                                    c_tinyusd_string *warn,
                                    c_tinyusd_string *err);
int c_tinyusd_load_usdz_from_memory(const uint8_t *addr, const size_t nbytes,
                                    c_tinyusd_string *warn,
                                    c_tinyusd_string *err);

#ifdef __cplusplus
}
#endif

#endif  // C_TINYUSD_H
