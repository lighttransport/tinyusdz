/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * TinyUSDZ C API over the "next" core (tinyusdz::next).
 *
 * Design:
 * - C11, no exceptions cross the boundary (the core is built -fno-exceptions).
 * - Every fallible function returns tusd_status; details via the thread-local
 *   tusd_last_error(). Results go through out-params.
 * - Owning opaque handles (tusd_stage, tusd_value, tusd_string, tusd_strlist)
 *   are destroyed exactly once with their tusd_*_destroy function.
 * - tusd_prim is a small BY-VALUE handle (no allocation, nothing to destroy).
 *   It stays valid until its stage is destroyed or structurally mutated
 *   (define/remove prim); tusd_stage_generation() lets bindings detect that.
 * - Borrowed views (tusd_sv, tusd_value_view) point into stage-owned storage:
 *   valid until the stage is destroyed or the owning prim/property is mutated.
 *
 * Threading:
 * - Distinct handles are fully independent.
 * - Concurrent READS of one stage are safe (internal lazy-decode is serialized
 *   by a private mutex).
 * - A WRITE (any set / define / remove / add function taking a mutable
 *   tusd_stage pointer) must not run concurrently with reads of that stage.
 * - tusd_last_error() is thread-local.
 */

#ifndef TINYUSDZ_C_H_
#define TINYUSDZ_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef TUSD_API
#define TUSD_API
#endif

#define TUSD_API_VERSION_MAJOR 1
#define TUSD_API_VERSION_MINOR 0
#define TUSD_API_VERSION_PATCH 0

/* ============================================================
 * Status / error handling
 * ============================================================ */

typedef enum tusd_status {
  TUSD_OK = 0,
  TUSD_ERR_INVALID_ARG = -1,
  TUSD_ERR_IO = -2,
  TUSD_ERR_PARSE = -3,
  TUSD_ERR_NOT_FOUND = -4,
  TUSD_ERR_TYPE_MISMATCH = -5,
  TUSD_ERR_OUT_OF_MEMORY = -6,
  TUSD_ERR_UNSUPPORTED = -7,
  TUSD_ERR_COMPOSITION = -8,
  TUSD_ERR_INTERNAL = -99
} tusd_status;

/* (major << 16) | (minor << 8) | patch */
TUSD_API uint32_t tusd_api_version(void);
TUSD_API const char* tusd_version_string(void);

/* Thread-local message for the most recent failing call on this thread.
 * Never NULL (empty string when no error). Valid until the next failing
 * call on the same thread. */
TUSD_API const char* tusd_last_error(void);

/* ============================================================
 * Strings
 * ============================================================ */

/* Borrowed string view. `data` is NUL-terminated for stage-owned strings but
 * always carry `len` (export_usdc reuses tusd_string as a byte buffer). */
typedef struct tusd_sv {
  const char* data;
  size_t len;
} tusd_sv;

/* Owned string / byte buffer. */
typedef struct tusd_string tusd_string;
TUSD_API tusd_sv tusd_string_view(const tusd_string* s);
TUSD_API void tusd_string_destroy(tusd_string* s);

/* Owned list of strings. */
typedef struct tusd_strlist tusd_strlist;
TUSD_API size_t tusd_strlist_size(const tusd_strlist* l);
TUSD_API tusd_sv tusd_strlist_get(const tusd_strlist* l, size_t index);
TUSD_API void tusd_strlist_destroy(tusd_strlist* l);

/* ============================================================
 * Types (mirrors tinyusdz::next::TypeId numerically)
 * ============================================================ */

typedef uint16_t tusd_type;

enum {
  TUSD_TYPE_INVALID = 0,
  TUSD_TYPE_BOOL = 1,
  TUSD_TYPE_INT = 2,
  TUSD_TYPE_UINT = 3,
  TUSD_TYPE_INT64 = 4,
  TUSD_TYPE_UINT64 = 5,
  TUSD_TYPE_HALF = 6,
  TUSD_TYPE_FLOAT = 7,
  TUSD_TYPE_DOUBLE = 8,
  TUSD_TYPE_STRING = 9,
  TUSD_TYPE_TOKEN = 10,
  TUSD_TYPE_ASSET_PATH = 11,
  TUSD_TYPE_INT2 = 12,
  TUSD_TYPE_INT3 = 13,
  TUSD_TYPE_INT4 = 14,
  TUSD_TYPE_UINT2 = 15,
  TUSD_TYPE_UINT3 = 16,
  TUSD_TYPE_UINT4 = 17,
  TUSD_TYPE_HALF2 = 18,
  TUSD_TYPE_HALF3 = 19,
  TUSD_TYPE_HALF4 = 20,
  TUSD_TYPE_FLOAT2 = 21,
  TUSD_TYPE_FLOAT3 = 22,
  TUSD_TYPE_FLOAT4 = 23,
  TUSD_TYPE_DOUBLE2 = 24,
  TUSD_TYPE_DOUBLE3 = 25,
  TUSD_TYPE_DOUBLE4 = 26,
  TUSD_TYPE_QUATH = 27,
  TUSD_TYPE_QUATF = 28,
  TUSD_TYPE_QUATD = 29,
  TUSD_TYPE_POINT3H = 30,
  TUSD_TYPE_POINT3F = 31,
  TUSD_TYPE_POINT3D = 32,
  TUSD_TYPE_VECTOR3H = 33,
  TUSD_TYPE_VECTOR3F = 34,
  TUSD_TYPE_VECTOR3D = 35,
  TUSD_TYPE_NORMAL3H = 36,
  TUSD_TYPE_NORMAL3F = 37,
  TUSD_TYPE_NORMAL3D = 38,
  TUSD_TYPE_COLOR3H = 39,
  TUSD_TYPE_COLOR3F = 40,
  TUSD_TYPE_COLOR3D = 41,
  TUSD_TYPE_COLOR4H = 42,
  TUSD_TYPE_COLOR4F = 43,
  TUSD_TYPE_COLOR4D = 44,
  TUSD_TYPE_MATRIX2F = 45,
  TUSD_TYPE_MATRIX2D = 46,
  TUSD_TYPE_MATRIX3F = 47,
  TUSD_TYPE_MATRIX3D = 48,
  TUSD_TYPE_MATRIX4F = 49,
  TUSD_TYPE_MATRIX4D = 50,
  TUSD_TYPE_TEXCOORD2H = 51,
  TUSD_TYPE_TEXCOORD2F = 52,
  TUSD_TYPE_TEXCOORD2D = 53,
  TUSD_TYPE_TEXCOORD3H = 54,
  TUSD_TYPE_TEXCOORD3F = 55,
  TUSD_TYPE_TEXCOORD3D = 56,
  TUSD_TYPE_TIMECODE = 57,
  TUSD_TYPE_EXTENT = 58,
  TUSD_TYPE_DICTIONARY = 59
};

/* Storage component type of a tusd_value_view / buffer. Half-element data is
 * materialized as float32 by the core, so TUSD_COMP_FLOAT16 never appears in
 * views today (kept for ABI completeness). */
typedef enum tusd_component_type {
  TUSD_COMP_NONE = 0, /* no POD buffer (string-family / dictionary / block) */
  TUSD_COMP_UINT8 = 1,
  TUSD_COMP_INT32 = 2,
  TUSD_COMP_UINT32 = 3,
  TUSD_COMP_INT64 = 4,
  TUSD_COMP_UINT64 = 5,
  TUSD_COMP_FLOAT16 = 6,
  TUSD_COMP_FLOAT32 = 7,
  TUSD_COMP_FLOAT64 = 8,
  TUSD_COMP_UINT16 = 9,
  TUSD_COMP_INT16 = 10
} tusd_component_type;

TUSD_API const char* tusd_type_name(tusd_type t);
TUSD_API tusd_type tusd_type_from_name(const char* name);
TUSD_API size_t tusd_type_size(tusd_type t);          /* bytes per element */
TUSD_API size_t tusd_type_component_count(tusd_type t);

/* ============================================================
 * Value views
 * ============================================================ */

/* Borrowed, zero-copy view of a value.
 * - POD scalar / vector / matrix / array data: `data` points into stage-owned
 *   storage, tightly packed; `count` is 1 for scalars, the array length for
 *   arrays; `components` scalars per element of `storage` component type.
 * - String / token / asset-path scalars, token arrays, and dictionaries have
 *   data == NULL, storage == TUSD_COMP_NONE: fetch through
 *   tusd_attr_get_string / tusd_attr_get_token_array / dict cursor instead.
 */
typedef struct tusd_value_view {
  tusd_type type;
  uint8_t is_array;
  uint8_t is_block; /* authored `= None` */
  uint8_t storage;  /* tusd_component_type of `data` */
  uint8_t components;
  size_t count;
  const void* data;
  size_t nbytes;
} tusd_value_view;

/* Owned value (results of interpolation / eval / metadata queries). */
typedef struct tusd_value tusd_value;
TUSD_API void tusd_value_destroy(tusd_value* v);
/* View into the owned value; lifetime = the tusd_value's lifetime. */
TUSD_API tusd_status tusd_value_get_view(const tusd_value* v,
                                         tusd_value_view* out);
TUSD_API tusd_status tusd_value_get_string(const tusd_value* v, tusd_sv* out);
TUSD_API tusd_status tusd_value_get_token_array(const tusd_value* v,
                                                tusd_strlist** out);

/* Borrowed dictionary cursor (customData / assetInfo / customLayerData). */
typedef struct tusd_dict_ref {
  const void* _dict; /* const next::Dict* */
} tusd_dict_ref;

TUSD_API int tusd_dict_is_valid(tusd_dict_ref d);
TUSD_API size_t tusd_dict_size(tusd_dict_ref d);
/* Fetch entry i. Any out-param may be NULL. If the entry is itself a
 * dictionary, *subdict becomes valid and *val reports TUSD_TYPE_DICTIONARY.
 * String-family entry values are returned via *sval. */
TUSD_API tusd_status tusd_dict_entry(tusd_dict_ref d, size_t index,
                                     tusd_sv* key, tusd_value_view* val,
                                     tusd_sv* sval, tusd_dict_ref* subdict);
TUSD_API tusd_status tusd_dict_find(tusd_dict_ref d, const char* key,
                                    tusd_value_view* val, tusd_sv* sval,
                                    tusd_dict_ref* subdict);

/* ============================================================
 * Stage: load / create / save
 * ============================================================ */

typedef struct tusd_stage tusd_stage;

typedef enum tusd_format {
  TUSD_FORMAT_AUTO = 0,
  TUSD_FORMAT_USDA = 1,
  TUSD_FORMAT_USDC = 2,
  TUSD_FORMAT_USDZ = 3
} tusd_format;

typedef struct tusd_load_options {
  uint32_t struct_size; /* = sizeof(tusd_load_options); enables ABI growth */
  uint32_t format;      /* tusd_format; AUTO sniffs extension + content */
  uint64_t max_memory;  /* per-input memory cap in bytes; 0 = unlimited */
  uint8_t composed;     /* 1: resolve composition arcs (LoadUSDComposed) */
  uint8_t load_payloads;
  /* USDA array parsing policy */
  uint8_t enable_usda_lazy_arrays; /* 1: enable lazy USDA array materialization */
  uint8_t _pad_usda[5];
  uint64_t max_usda_lazy_array_elements; /* 0 = parser disables lazy cap */
  int32_t usda_num_threads;             /* 0 = auto; 1 = serial */
  uint8_t _pad_compose[6];
  uint32_t max_depth; /* composition recursion limit; 0 = default */
  uint32_t _pad2;
  /* Variant selection overrides (set name -> variant name), applied on every
   * prim defining that set; stronger than authored selections. Parallel
   * arrays of length variant_override_count. */
  const char* const* variant_sets;
  const char* const* variant_names;
  size_t variant_override_count;
} tusd_load_options;

TUSD_API void tusd_load_options_init(tusd_load_options* opts);

TUSD_API tusd_status tusd_stage_load(const char* filename,
                                     const tusd_load_options* opts, /* nullable */
                                     tusd_stage** out);
/* Single-layer load from memory (no composition: no anchor for externals). */
TUSD_API tusd_status tusd_stage_load_from_memory(const uint8_t* data,
                                                 size_t size,
                                                 const tusd_load_options* opts,
                                                 tusd_stage** out);
/* Empty stage ready for authoring. */
TUSD_API tusd_status tusd_stage_create(tusd_stage** out);
TUSD_API void tusd_stage_destroy(tusd_stage* stage);

/* Drain warnings accumulated by load (empty string when none). */
TUSD_API tusd_status tusd_stage_take_warnings(tusd_stage* stage,
                                              tusd_string** out);

/* Bumped by every structural mutation (define/remove prim); lets bindings
 * detect stale tusd_prim handles cheaply. */
TUSD_API uint64_t tusd_stage_generation(const tusd_stage* stage);

typedef struct tusd_save_options {
  uint32_t struct_size;
  uint32_t format; /* tusd_format; AUTO derives from the file extension */
} tusd_save_options;

TUSD_API void tusd_save_options_init(tusd_save_options* opts);

TUSD_API tusd_status tusd_stage_save(const tusd_stage* stage,
                                     const char* filename,
                                     const tusd_save_options* opts /* nullable */);
TUSD_API tusd_status tusd_stage_export_usda(const tusd_stage* stage,
                                            tusd_string** out);
/* Crate bytes; use tusd_string_view() for (data, len). */
TUSD_API tusd_status tusd_stage_export_usdc(const tusd_stage* stage,
                                            tusd_string** out);
/* Flatten composition into a new single-layer stage. */
TUSD_API tusd_status tusd_stage_flatten(const tusd_stage* stage,
                                        tusd_stage** out);
/* Low-memory file->file flatten pipeline (lazy arrays passed through). */
TUSD_API tusd_status tusd_flatten_file_to_usdc(const char* in_filename,
                                               const char* out_filename,
                                               const tusd_load_options* opts);

/* ============================================================
 * Stage: metadata & stats
 * ============================================================ */

/* Key-based stage metadata. Supported keys:
 *   "defaultPrim" (token), "upAxis" (token), "metersPerUnit" (double),
 *   "timeCodesPerSecond" (double), "startTimeCode" (double),
 *   "endTimeCode" (double), "framesPerSecond" (double),
 *   "kilogramsPerUnit" (double), "doc" (string), "comment" (string),
 *   "colorConfiguration" (asset), "colorManagementSystem" (token)
 * Get returns TUSD_ERR_NOT_FOUND for unknown keys. */
TUSD_API tusd_status tusd_stage_get_metadata(const tusd_stage* stage,
                                             const char* key,
                                             tusd_value** out);
TUSD_API tusd_status tusd_stage_set_metadata(tusd_stage* stage, const char* key,
                                             tusd_type type, const void* data,
                                             size_t count);

TUSD_API tusd_sv tusd_stage_default_prim_path(const tusd_stage* stage);
TUSD_API tusd_status tusd_stage_set_default_prim(tusd_stage* stage,
                                                 const char* prim_name);
TUSD_API tusd_status tusd_stage_sublayers(const tusd_stage* stage,
                                          tusd_strlist** out);
TUSD_API tusd_status tusd_stage_add_sublayer_path(tusd_stage* stage,
                                                  const char* asset_path);
TUSD_API tusd_status tusd_stage_custom_layer_data(const tusd_stage* stage,
                                                  tusd_dict_ref* out);

typedef struct tusd_stage_stats {
  uint64_t prim_count;
  uint64_t layer_count;
  uint64_t total_properties;
  uint64_t memory_bytes;
} tusd_stage_stats;

TUSD_API tusd_status tusd_stage_get_stats(const tusd_stage* stage,
                                          tusd_stage_stats* out);
TUSD_API double tusd_stage_start_timecode(const tusd_stage* stage);
TUSD_API double tusd_stage_end_timecode(const tusd_stage* stage);

/* ============================================================
 * Prim access & traversal
 * ============================================================ */

/* By-value prim handle: mirrors next::UsdPrim. No allocation, no destroy.
 * Never touch the members directly. */
typedef struct tusd_prim {
  const void* _spec;
  const void* _layer;
  uint32_t _index;
  uint32_t _pad;
} tusd_prim;

TUSD_API int tusd_prim_is_valid(tusd_prim p);

TUSD_API tusd_prim tusd_stage_pseudo_root(const tusd_stage* stage);
TUSD_API tusd_prim tusd_stage_prim_at_path(const tusd_stage* stage,
                                           const char* path);
TUSD_API tusd_prim tusd_stage_default_prim(const tusd_stage* stage);
TUSD_API size_t tusd_stage_root_prim_count(const tusd_stage* stage);
TUSD_API tusd_prim tusd_stage_root_prim(const tusd_stage* stage, size_t index);
TUSD_API size_t tusd_stage_prim_count(const tusd_stage* stage);

TUSD_API tusd_sv tusd_prim_name(tusd_prim p);
TUSD_API tusd_sv tusd_prim_type_name(tusd_prim p);
TUSD_API tusd_sv tusd_prim_path(tusd_prim p);
/* 0 = def, 1 = over, 2 = class */
TUSD_API uint8_t tusd_prim_specifier(tusd_prim p);
TUSD_API int tusd_prim_is_active(tusd_prim p);

TUSD_API tusd_prim tusd_prim_parent(tusd_prim p);
TUSD_API size_t tusd_prim_child_count(tusd_prim p);
TUSD_API tusd_prim tusd_prim_child(tusd_prim p, size_t index);
TUSD_API tusd_prim tusd_prim_child_by_name(tusd_prim p, const char* name);

/* ============================================================
 * Properties / attributes (read)
 * ============================================================ */

/* Property flags (mirror next::PropSlot flag bits). */
enum {
  TUSD_PROP_CUSTOM = 0x0001,
  TUSD_PROP_UNIFORM = 0x0002,
  TUSD_PROP_TIMESAMPLED = 0x0004,
  TUSD_PROP_CONNECTION = 0x0008,
  TUSD_PROP_RELATIONSHIP = 0x0010,
  TUSD_PROP_ARRAY = 0x0020
};

TUSD_API size_t tusd_prim_property_count(tusd_prim p);
TUSD_API tusd_sv tusd_prim_property_name(tusd_prim p, size_t index);
TUSD_API uint16_t tusd_prim_property_flags_at(tusd_prim p, size_t index);
TUSD_API int tusd_prim_has_property(tusd_prim p, const char* name);
TUSD_API uint16_t tusd_prim_property_flags(tusd_prim p, const char* name);
/* Declared USD type name (e.g. "color3f", "float[]"); empty if unrecorded. */
TUSD_API tusd_sv tusd_prim_property_type_name(tusd_prim p, const char* name);

/* Zero-copy default-value view (materializes lazy crate arrays in place,
 * thread-safely). TUSD_ERR_NOT_FOUND if the property or its default value is
 * absent. */
TUSD_API tusd_status tusd_attr_get(tusd_prim p, const char* name,
                                   tusd_value_view* out);
/* String / token / asset-path scalar defaults. */
TUSD_API tusd_status tusd_attr_get_string(tusd_prim p, const char* name,
                                          tusd_sv* out);
/* Token/string array defaults (owned list, one call). */
TUSD_API tusd_status tusd_attr_get_token_array(tusd_prim p, const char* name,
                                               tusd_strlist** out);

/* Property metadata by key. Supported keys: "interpolation", "elementSize",
 * "colorSpace", "displayName", "displayGroup", "doc", "hidden", "renderType",
 * "connectability", "bindMaterialAs", "weight", "customData" (dictionary).
 * TUSD_ERR_NOT_FOUND when the key was not authored. */
TUSD_API tusd_status tusd_attr_metadata(tusd_prim p, const char* name,
                                        const char* key, tusd_value** out);
TUSD_API tusd_status tusd_attr_custom_data(tusd_prim p, const char* name,
                                           tusd_dict_ref* out);

TUSD_API size_t tusd_attr_connection_count(tusd_prim p, const char* name);
TUSD_API tusd_sv tusd_attr_connection(tusd_prim p, const char* name,
                                      size_t index);

/* Composition-time evaluation (follows connections, samples time). */
TUSD_API tusd_status tusd_attr_eval(const tusd_stage* stage, tusd_prim p,
                                    const char* name, double time,
                                    tusd_value** out);

TUSD_API tusd_status tusd_prim_local_transform(tusd_prim p, double time,
                                               double out16[16]);
TUSD_API tusd_status tusd_prim_world_transform(const tusd_stage* stage,
                                               tusd_prim p, double time,
                                               double out16[16]);

/* ============================================================
 * Time samples
 * ============================================================ */

TUSD_API int tusd_attr_has_timesamples(tusd_prim p, const char* name);
TUSD_API size_t tusd_attr_timesample_count(tusd_prim p, const char* name);
/* Copy up to `cap` sample times into out. Returns total count via return
 * value regardless of cap (call with cap=0, out=NULL to size). */
TUSD_API size_t tusd_attr_timesample_times(tusd_prim p, const char* name,
                                           double* out, size_t cap);
/* Zero-copy view of sample `index` (sorted by time). */
TUSD_API tusd_status tusd_attr_timesample_at(tusd_prim p, const char* name,
                                             size_t index, double* time,
                                             tusd_value_view* out);
/* Interpolated value at `time` (owned). interp_mode: 0=held, 1=linear. */
TUSD_API tusd_status tusd_attr_interpolate(tusd_prim p, const char* name,
                                           double time, uint8_t interp_mode,
                                           tusd_value** out);

/* ============================================================
 * Relationships
 * ============================================================ */

TUSD_API size_t tusd_prim_relationship_count(tusd_prim p);
TUSD_API tusd_status tusd_prim_relationship_names(tusd_prim p,
                                                  tusd_strlist** out);
TUSD_API int tusd_prim_has_relationship(tusd_prim p, const char* name);
TUSD_API size_t tusd_rel_target_count(tusd_prim p, const char* name);
TUSD_API tusd_sv tusd_rel_target(tusd_prim p, const char* name, size_t index);

/* ============================================================
 * Variants
 * ============================================================ */

TUSD_API size_t tusd_prim_variant_set_count(tusd_prim p);
TUSD_API tusd_sv tusd_prim_variant_set_name(tusd_prim p, size_t set_index);
TUSD_API size_t tusd_variant_count(tusd_prim p, const char* set_name);
TUSD_API tusd_sv tusd_variant_name(tusd_prim p, const char* set_name,
                                   size_t index);
TUSD_API tusd_sv tusd_variant_selection(tusd_prim p, const char* set_name);

/* ============================================================
 * Prim metadata
 * ============================================================ */

/* Key-based prim metadata. Supported keys: "active" (bool), "hidden" (bool),
 * "instanceable" (bool), "kind" (token), "doc" (string), "comment" (string),
 * "displayName" (string), "apiSchemas" (token[]).
 * TUSD_ERR_NOT_FOUND when unauthored / unknown. */
TUSD_API tusd_status tusd_prim_get_metadata(tusd_prim p, const char* key,
                                            tusd_value** out);
TUSD_API tusd_status tusd_prim_custom_data(tusd_prim p, tusd_dict_ref* out);
TUSD_API tusd_status tusd_prim_asset_info(tusd_prim p, tusd_dict_ref* out);
TUSD_API tusd_sv tusd_prim_kind(tusd_prim p);

/* ============================================================
 * Authoring (path-addressed; all take the stage write lock and, when
 * structural, bump the stage generation)
 * ============================================================ */

/* specifier: 0=def, 1=over, 2=class */
TUSD_API tusd_status tusd_stage_define_prim(tusd_stage* stage, const char* path,
                                            const char* type_name, /* nullable */
                                            uint8_t specifier,
                                            tusd_prim* out /* nullable */);
TUSD_API tusd_status tusd_stage_remove_prim(tusd_stage* stage, const char* path);

/* Author a full typed value in ONE call.
 * - POD types: `data` points at `count` elements of `type` (count > 1 or
 *   is_array != 0 authors an array; count == 1 && !is_array a scalar).
 * - TUSD_TYPE_STRING/TOKEN/ASSET_PATH scalar: data = const char* (NUL-term).
 * `flags` = TUSD_PROP_* to OR onto the property (custom/uniform). */
TUSD_API tusd_status tusd_attr_set(tusd_stage* stage, const char* prim_path,
                                   const char* name, tusd_type type,
                                   uint8_t is_array, const void* data,
                                   size_t count, uint16_t flags);
TUSD_API tusd_status tusd_attr_set_token_array(tusd_stage* stage,
                                               const char* prim_path,
                                               const char* name,
                                               tusd_type type, /* TOKEN or STRING */
                                               const char* const* items,
                                               size_t count, uint16_t flags);
TUSD_API tusd_status tusd_attr_set_timesample(tusd_stage* stage,
                                              const char* prim_path,
                                              const char* name, double time,
                                              tusd_type type, uint8_t is_array,
                                              const void* data, size_t count);
/* Set property metadata by key (same keys as tusd_attr_metadata; value
 * encoding as in tusd_attr_set). */
TUSD_API tusd_status tusd_attr_set_metadata(tusd_stage* stage,
                                            const char* prim_path,
                                            const char* name, const char* key,
                                            tusd_type type, const void* data,
                                            size_t count);
TUSD_API tusd_status tusd_attr_add_connection(tusd_stage* stage,
                                              const char* prim_path,
                                              const char* name,
                                              const char* target);
/* Author a value block (`= None`). */
TUSD_API tusd_status tusd_attr_block(tusd_stage* stage, const char* prim_path,
                                     const char* name);
TUSD_API tusd_status tusd_attr_remove(tusd_stage* stage, const char* prim_path,
                                      const char* name);

TUSD_API tusd_status tusd_rel_add_target(tusd_stage* stage,
                                         const char* prim_path,
                                         const char* rel_name,
                                         const char* target);
TUSD_API tusd_status tusd_rel_set_targets(tusd_stage* stage,
                                          const char* prim_path,
                                          const char* rel_name,
                                          const char* const* targets,
                                          size_t count);
TUSD_API tusd_status tusd_rel_remove(tusd_stage* stage, const char* prim_path,
                                     const char* rel_name);

/* Composition arcs. arc_type: 0=reference, 1=payload, 2=inherit,
 * 3=specialize. `asset_path` may be NULL/empty for internal arcs;
 * `target_prim_path` may be NULL for default-prim targeting. */
enum {
  TUSD_ARC_REFERENCE = 0,
  TUSD_ARC_PAYLOAD = 1,
  TUSD_ARC_INHERIT = 2,
  TUSD_ARC_SPECIALIZE = 3
};

TUSD_API tusd_status tusd_prim_add_arc(tusd_stage* stage, const char* prim_path,
                                       uint8_t arc_type,
                                       const char* asset_path,      /* nullable */
                                       const char* target_prim_path /* nullable */);

/* Prim metadata setters (same keys as tusd_prim_get_metadata). */
TUSD_API tusd_status tusd_prim_set_metadata(tusd_stage* stage,
                                            const char* prim_path,
                                            const char* key, tusd_type type,
                                            const void* data, size_t count);

/* Variant authoring. */
TUSD_API tusd_status tusd_prim_add_variant_set(tusd_stage* stage,
                                               const char* prim_path,
                                               const char* set_name);
TUSD_API tusd_status tusd_prim_add_variant(tusd_stage* stage,
                                           const char* prim_path,
                                           const char* set_name,
                                           const char* variant_name);
TUSD_API tusd_status tusd_prim_set_variant_selection(tusd_stage* stage,
                                                     const char* prim_path,
                                                     const char* set_name,
                                                     const char* variant_name);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* TINYUSDZ_C_H_ */
