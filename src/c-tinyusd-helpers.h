/* SPDX-License-Identifier: Apache 2.0
 *
 * Small C-callable helpers that fill gaps in c-tinyusd.h for the Python
 * extension. Everything here is pure C11; C++ interop lives in the .cc file.
 *
 * Ownership model (changed from the first draft):
 *
 *   - Functions named `*_get_*` may return a borrowed pointer OR allocate an
 *     owned handle, following the convention stated per-function.
 *   - `*_copy_*` / `*_new_*` always allocate; caller owns the result and must
 *     pair it with the matching `*_free` / `c_tinyusd_*_free`.
 *   - Borrowed pointers are stable only for the lifetime of the source object
 *     given — the "owner" argument establishes that lifetime contract.
 *
 * In particular, `c_tinyusd_prim_get_attribute` returns an OWNED
 * CTinyUSDAttribute*. This removes a correctness bug in the earlier version
 * where two concurrent handles aliased a single thread-local cache.
 */
#ifndef C_TINYUSD_HELPERS_H
#define C_TINYUSD_HELPERS_H

#include "c-tinyusd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ---- Stage helpers ---- */

/* Number of root prims in the stage. Returns 0 if stage is null. */
C_TINYUSD_EXPORT uint64_t
c_tinyusd_stage_num_root_prims(const CTinyUSDStage *stage);

/* Borrowed pointer to i-th root prim. *out is valid until the Stage is
 * modified or freed. Returns 1 on success, 0 otherwise.
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_get_root_prim(const CTinyUSDStage *stage, uint64_t index,
                              const CTinyUSDPrim **out);

/* Borrowed pointer to the prim at `abs_path` ("/World/Mesh", etc).
 * Returns 1 on success; 0 if not found or on error (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_get_prim_at_path(const CTinyUSDStage *stage,
                                 const char *abs_path,
                                 const CTinyUSDPrim **out,
                                 c_tinyusd_string_t *err);

/* Save stage to file. `format` is one of C_TINYUSD_FORMAT_USDA / USDC / USDZ
 * (AUTO = infer from extension). Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_save_to_file(const CTinyUSDStage *stage, const char *filename,
                             CTinyUSDFormat format,
                             c_tinyusd_string_t *warn,
                             c_tinyusd_string_t *err);

/* Parse USDA/USDC/USDZ bytes from memory into the supplied Stage. Unlike the
 * existing c_tinyusd_load_*_from_memory entry points (which lack a Stage *
 * output and are effectively useless), these thread the stage through
 * properly.
 *
 * `format` may be C_TINYUSD_FORMAT_AUTO to sniff from the first bytes.
 * Returns 1 on success, 0 on failure (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_load_from_memory(CTinyUSDStage *stage,
                                 const uint8_t *data, size_t nbytes,
                                 CTinyUSDFormat format,
                                 c_tinyusd_string_t *warn,
                                 c_tinyusd_string_t *err);

/* ---- Prim helpers ---- */

/* Return an OWNED attribute copy for `name` on `prim`, or NULL if not found
 * (or if the property is a relationship rather than an attribute). Free with
 * c_tinyusd_attribute_free.
 *
 * The returned attribute contains its own copy of the underlying value; the
 * value buffer is stable as long as the attribute is alive, which is what
 * `c_tinyusd_value_array_data` relies on for zero-copy access.
 */
C_TINYUSD_EXPORT CTinyUSDAttribute *
c_tinyusd_prim_get_attribute(const CTinyUSDPrim *prim, const char *name);

/* Return an OWNED relationship copy for `name` on `prim`. Free with
 * c_tinyusd_relationsip_free (sic: existing C API typo preserved for ABI).
 */
C_TINYUSD_EXPORT CTinyUSDRelationship *
c_tinyusd_prim_get_relationship(const CTinyUSDPrim *prim, const char *name);

/* Number of children on `prim`. (Already exists as
 * c_tinyusd_prim_num_children — kept here as a documented entry point.) */

/* ---- Attribute helpers ---- */

/* Allocate and initialise an empty Attribute. */
C_TINYUSD_EXPORT CTinyUSDAttribute *
c_tinyusd_attribute_new(void);

/* Free an owned Attribute allocated by c_tinyusd_prim_get_attribute /
 * c_tinyusd_attribute_new. Returns 0 if `attr` was null.
 */
C_TINYUSD_EXPORT int
c_tinyusd_attribute_free(CTinyUSDAttribute *attr);

/* Borrowed pointer to the default value of `attr`. Returns 1 if a default
 * value is authored, 0 otherwise. *out is valid as long as `attr` is alive.
 */
C_TINYUSD_EXPORT int
c_tinyusd_attribute_get_value(const CTinyUSDAttribute *attr,
                              const CTinyUSDValue **out);

/* Copy the attribute's name / USD type-name into caller-allocated string. */
C_TINYUSD_EXPORT int
c_tinyusd_attribute_get_name(const CTinyUSDAttribute *attr,
                             c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int
c_tinyusd_attribute_get_type_name(const CTinyUSDAttribute *attr,
                                  c_tinyusd_string_t *out);

/* ---- Value helpers ---- */

/* Return a zero-copy view of the stored array element storage.
 *
 *   *out_ptr         pointer to contiguous component storage
 *   *out_n_outer     number of outer-dimension elements (e.g. N points)
 *   *out_n_inner     number of inner-dimension components per outer item
 *                    (1 for plain scalars, 3 for point3f, 4 for quatf, 16
 *                    for matrix4d, etc.)
 *   *out_component_size  size of ONE component in bytes
 *                        (4 for float, 8 for double, 2 for half)
 *   *out_format      single-char Python struct format for the component
 *                    ('f', 'd', 'e', 'i', 'I', 'q', 'Q', 'B')
 *
 * Total buffer length in bytes = n_outer * n_inner * component_size.
 * Python can expose this as shape (n_outer,) or (n_outer, n_inner) based on
 * whether n_inner > 1.
 *
 * Returns 1 on success. 0 if the Value is not an array, is empty, or has an
 * unsupported element type.
 */
C_TINYUSD_EXPORT int
c_tinyusd_value_array_data(const CTinyUSDValue *value,
                           const void **out_ptr,
                           uint64_t *out_n_outer,
                           uint32_t *out_n_inner,
                           uint32_t *out_component_size,
                           const char **out_format);

/* Scalar-coercion accessors. */
C_TINYUSD_EXPORT int
c_tinyusd_value_as_double(const CTinyUSDValue *value, double *out);
C_TINYUSD_EXPORT int
c_tinyusd_value_as_int64(const CTinyUSDValue *value, int64_t *out);
C_TINYUSD_EXPORT int
c_tinyusd_value_as_bool(const CTinyUSDValue *value, int *out);

/* For token/string scalars, copy into `out`. Returns 1 on success. */
C_TINYUSD_EXPORT int
c_tinyusd_value_get_string(const CTinyUSDValue *value, c_tinyusd_string_t *out);

/* Is this value a 1D array? (e.g. float3[] not float3.) */
C_TINYUSD_EXPORT int
c_tinyusd_value_is_array(const CTinyUSDValue *value);

/* ---- Path helpers ---- */

/* Copy the string form of a Path into `out`. Returns 1 on success. */
C_TINYUSD_EXPORT int
c_tinyusd_path_to_string(const CTinyUSDPath *path, c_tinyusd_string_t *out);

/* ---- Authoring helpers (used by Python bindings) ---- */

/* Set the element name (leaf path component) of a Prim. Required after
 * c_tinyusd_prim_new()/_new_builtin() and before adding the prim under a
 * Stage / parent Prim. Returns 1 on success, 0 on bad input.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_set_element_name(CTinyUSDPrim *prim, const char *name);

/* Add `prim` as a root prim under `stage`. The prim is *copied* into the
 * stage; the caller still owns `prim` and must free it (or transfer it).
 * Returns 1 on success, 0 on failure (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_add_root_prim(CTinyUSDStage *stage, CTinyUSDPrim *prim,
                              c_tinyusd_string_t *err);

/* Set name / type-name / value on an Attribute that was allocated via
 * c_tinyusd_attribute_new(). The Value is copied; the caller still owns it.
 * Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_attribute_set_name(CTinyUSDAttribute *attr, const char *name);
C_TINYUSD_EXPORT int
c_tinyusd_attribute_set_type_name(CTinyUSDAttribute *attr, const char *type_name);
C_TINYUSD_EXPORT int
c_tinyusd_attribute_set_value(CTinyUSDAttribute *attr,
                              const CTinyUSDValue *value);

/* Add an Attribute (as a Property) to the Prim's properties map. The
 * attribute name comes from c_tinyusd_attribute_get_name (i.e. the attribute
 * must have been named beforehand). The attribute is copied; the caller
 * still owns `attr`. Returns 1 on success, 0 on failure (err is filled).
 *
 * Supported prim types: Xform, GeomMesh, GeomSphere, GeomCube, GeomCylinder,
 * GeomCone, GeomCapsule, GeomCamera, GeomPoints, Scope, Model. Other prim
 * types currently return 0 with an explanatory error message.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_add_attribute(CTinyUSDPrim *prim,
                             const CTinyUSDAttribute *attr,
                             c_tinyusd_string_t *err);

/* Construct a Value holding a token[] (array of tokens). Returns NULL on
 * allocation failure. The caller owns the returned Value and must free it
 * via c_tinyusd_value_free. The token strings are copied.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_token(uint64_t n, const char *const *toks);

/* Construct a Value holding a string[]. */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_string(uint64_t n, const char *const *strs);

/* Append an applied API schema to the prim's apiSchemas metadata.
 * `instance_name` is the optional instance suffix for multi-apply schemas
 * (e.g. for `CollectionAPI:material` pass instance_name="material"); pass
 * NULL or "" for single-apply schemas like MaterialXConfigAPI.
 *
 * Schema names not recognised by tinyusdz' built-in enum are stored under
 * `apiSchemas.unknownSchemas` so they survive write/read round-trips.
 * Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_apply_api_schema(CTinyUSDPrim *prim, const char *schema_name,
                                const char *instance_name);

/* Get the list of applied API schemas as a comma-separated string. The
 * returned form is the schema-name list, with multi-apply instances
 * formatted as "Schema:instance". Returns 1 if any schema is authored.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_get_api_schemas(const CTinyUSDPrim *prim,
                               c_tinyusd_string_t *out);

/* Set a string-valued Prim metadatum (kind, doc, displayName, comment, ...).
 * `meta_name` is one of "kind", "displayName", "doc", "documentation",
 * "comment", "sceneName"; other names are stored as a generic string entry.
 * Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_meta_set_string(CTinyUSDPrim *prim, const char *meta_name,
                               const char *value);

/* Get a string-valued Prim metadatum. Returns 1 if authored, 0 otherwise.
 * On success, `*out` is filled with the string value.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_meta_get_string(const CTinyUSDPrim *prim, const char *meta_name,
                               c_tinyusd_string_t *out);

/* Bool-valued prim metadata (active, hidden). */
C_TINYUSD_EXPORT int
c_tinyusd_prim_meta_set_bool(CTinyUSDPrim *prim, const char *meta_name,
                             int value);
C_TINYUSD_EXPORT int
c_tinyusd_prim_meta_get_bool(const CTinyUSDPrim *prim, const char *meta_name,
                             int *out);

/* ---- Double-precision value constructors ----
 *
 * Fill the gap left by c-tinyusd.h, which only exposes float* constructors.
 * USD spec types like `double3` (xformOp:translate), `matrix4d` (transform),
 * `metersPerUnit`, etc. require these.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_double(double v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_double2(c_tinyusd_double2_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_double3(c_tinyusd_double3_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_double4(c_tinyusd_double4_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_double(uint64_t n, const double *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_double3(uint64_t n, const c_tinyusd_double3_t *v);
/* matrix4d: 16 doubles in row-major order. */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_matrix4d(const double v[16]);

/* matrix2d / matrix3d row-major scalars. */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_matrix2d_t(c_tinyusd_matrix2d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_matrix3d_t(c_tinyusd_matrix3d_t v);
/* matrix4d via the typedef'd struct (alternative form to the [16] one above). */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_matrix4d_t(c_tinyusd_matrix4d_t v);

/* Array forms for matrices and double-vectors. */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_double2(uint64_t n, const c_tinyusd_double2_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_double4(uint64_t n, const c_tinyusd_double4_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_matrix2d(uint64_t n, const c_tinyusd_matrix2d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_matrix3d(uint64_t n, const c_tinyusd_matrix3d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_matrix4d(uint64_t n, const c_tinyusd_matrix4d_t *v);

/* ---- bool ----
 * USD has a `bool` value type distinct from `int` (mass enabled flags etc.).
 * c-tinyusd.h doesn't expose it; provide it here.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_bool(int v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_bool(uint64_t n, const int *v);

/* ---- Typed float3/double3 alias scalars ----
 * color3f / point3f / normal3f / vector3f (and their double counterparts)
 * share the float3/double3 memory layout but author as distinct types so
 * the USDC writer emits the spec-correct attribute role.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_color3f(c_tinyusd_color3f_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_point3f(c_tinyusd_point3f_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_normal3f(c_tinyusd_normal3f_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_vector3f(c_tinyusd_float3_t v);

C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_color3d(c_tinyusd_color3d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_point3d(c_tinyusd_point3d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_normal3d(c_tinyusd_normal3d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_vector3d(c_tinyusd_double3_t v);

/* Array forms for typed-vec aliases. */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_color3f(uint64_t n, const c_tinyusd_color3f_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_point3f(uint64_t n, const c_tinyusd_point3f_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_normal3f(uint64_t n, const c_tinyusd_normal3f_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_vector3f(uint64_t n, const c_tinyusd_float3_t *v);

C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_color3d(uint64_t n, const c_tinyusd_color3d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_point3d(uint64_t n, const c_tinyusd_point3d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_normal3d(uint64_t n, const c_tinyusd_normal3d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_vector3d(uint64_t n, const c_tinyusd_double3_t *v);

/* Convenience: convert IEEE-754 binary32 to binary16 (half) bit pattern. */
C_TINYUSD_EXPORT c_tinyusd_half_t
c_tinyusd_float_to_half(float f);

/* ---- Half precision ----
 * Half values use the IEEE-754 binary16 bit pattern packed into uint16_t.
 * The caller is responsible for the float->half conversion (matches the
 * existing c_tinyusd_half_t convention).
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_half(c_tinyusd_half_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_half2(c_tinyusd_half2_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_half3(c_tinyusd_half3_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_half4(c_tinyusd_half4_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_half(uint64_t n, const c_tinyusd_half_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_half2(uint64_t n, const c_tinyusd_half2_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_half3(uint64_t n, const c_tinyusd_half3_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_half4(uint64_t n, const c_tinyusd_half4_t *v);

/* ---- Wide integer scalars ---- */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_uint(uint32_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_uint64(uint64_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_int64(int64_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_uint(uint64_t n, const uint32_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_uint64(uint64_t n, const uint64_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_int64(uint64_t n, const int64_t *v);

/* ---- Quaternions ----
 * Memory layout is {imag[3], real} (matches both tinyusdz value::quat*
 * and c_tinyusd_quat*_t). USDA spelling is `(w, x, y, z)` so the caller
 * is responsible for filling .imag and .real correctly.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_quath(c_tinyusd_quath_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_quatf(c_tinyusd_quatf_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_quatd(c_tinyusd_quatd_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_quath(uint64_t n, const c_tinyusd_quath_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_quatf(uint64_t n, const c_tinyusd_quatf_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_quatd(uint64_t n, const c_tinyusd_quatd_t *v);

/* ---- TexCoord role types (float2/double2 aliases) ----
 * texCoord2f/2d are 2-element role types; texCoord3f/3d are 3-element.
 * Memory layout is identical to float2/double2 and float3/double3.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_texcoord2f(c_tinyusd_texcoord2f_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_texcoord2d(c_tinyusd_texcoord2d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_texcoord3f(c_tinyusd_float3_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_texcoord3d(c_tinyusd_double3_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_texcoord2f(uint64_t n, const c_tinyusd_texcoord2f_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_texcoord2d(uint64_t n, const c_tinyusd_texcoord2d_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_texcoord3f(uint64_t n, const c_tinyusd_float3_t *v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_texcoord3d(uint64_t n, const c_tinyusd_double3_t *v);

/* ---- frame4d (semantic alias for matrix4d) ----
 * frame4d has identical layout to matrix4d; authored as frame4d in USDA/USDC.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_frame4d(c_tinyusd_matrix4d_t v);
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_frame4d(uint64_t n, const c_tinyusd_matrix4d_t *v);

/* ---- Stage convenience ----
 * Set/get the stage's `defaultPrim` metadatum. The name is the prim's
 * element name (no leading slash). Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_set_default_prim(CTinyUSDStage *stage, const char *name);
C_TINYUSD_EXPORT int
c_tinyusd_stage_get_default_prim(const CTinyUSDStage *stage,
                                 c_tinyusd_string_t *out_name);

/* ---- Asset value constructor ----
 *
 * Construct a Value holding an `asset` (e.g. `asset inputs:file = @./tex.png@`).
 * The path string is copied. Returns NULL on allocation failure.
 */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_asset(const char *asset_path);

/* Array form: `asset[]` */
C_TINYUSD_EXPORT CTinyUSDValue *
c_tinyusd_value_new_array_asset(uint64_t n, const char *const *paths);

/* ---- Time-sampled attribute authoring ----
 *
 * Author a single (time, value) sample on the named attribute. If the
 * attribute does not yet exist, it is created with the deduced type.
 * `value` is borrowed; the helper copies. Repeated calls with different
 * `time` values build up the TimeSamples vector.
 *
 * The attribute's USD type-name is taken from the first sample's value
 * (or kept if already authored). `type_name` may be NULL to infer.
 *
 * Returns 1 on success, 0 on failure (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_set_attribute_timesample(CTinyUSDPrim *prim, const char *name,
                                        double time,
                                        const CTinyUSDValue *value,
                                        const char *type_name,
                                        c_tinyusd_string_t *err);

/* Read the number of authored time samples on `name`. Returns 0 if the
 * attribute is not found or has no time samples. */
C_TINYUSD_EXPORT uint64_t
c_tinyusd_prim_get_attribute_timesample_count(const CTinyUSDPrim *prim,
                                              const char *name);

/* Read the i-th authored time sample. *out_time is set to the sample time;
 * *out_value receives a NEW owned Value (caller must c_tinyusd_value_free).
 * Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_get_attribute_timesample(const CTinyUSDPrim *prim,
                                        const char *name, uint64_t index,
                                        double *out_time,
                                        CTinyUSDValue **out_value);

/* ---- Stage metadata helpers ----
 *
 * Set / get stage-level metadata fields (those that USD writers emit between
 * the `#usda 1.0` header and the first prim).
 *
 * `key` is one of:
 *   "defaultPrim"          (token; bare prim name, no leading slash)
 *   "upAxis"               ("Y", "Z", or "X")
 *   "metersPerUnit"        (double)
 *   "timeCodesPerSecond"   (double)
 *   "framesPerSecond"      (double)
 *   "startTimeCode"        (double)
 *   "endTimeCode"          (double)
 *   "doc" / "documentation" (string)
 *   "comment"              (string)
 *
 * For string-valued keys use the *_string variants; for numeric keys use
 * *_double. Returns 1 on success, 0 on bad key or unsupported type.
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_meta_set_string(CTinyUSDStage *stage, const char *key,
                                const char *value);
C_TINYUSD_EXPORT int
c_tinyusd_stage_meta_get_string(const CTinyUSDStage *stage, const char *key,
                                c_tinyusd_string_t *out);
C_TINYUSD_EXPORT int
c_tinyusd_stage_meta_set_double(CTinyUSDStage *stage, const char *key,
                                double value);
C_TINYUSD_EXPORT int
c_tinyusd_stage_meta_get_double(const CTinyUSDStage *stage, const char *key,
                                double *out);

/* ---- Attribute connection / metadata helpers ----
 *
 * Author an attribute connection (the `.connect = </path>` form):
 *
 *   inputs:diffuseColor.connect = </Mat/Tex.outputs:rgb>
 *
 * `type_name` is the USD attribute type (e.g. "color3f", "float", "token") —
 * required for typed attributes; pass NULL or "" to leave it inferred.
 * `target_paths` is an array of N target path strings.
 * Returns 1 on success, 0 on failure (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_add_attribute_connection(CTinyUSDPrim *prim, const char *name,
                                        const char *type_name,
                                        uint64_t n_targets,
                                        const char *const *target_paths,
                                        c_tinyusd_string_t *err);

/* Get the connection target paths of an attribute on a prim. Returns 1 if
 * the named attribute exists and has connections; 0 otherwise. *out_csv is
 * filled with comma-separated target paths.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_get_attribute_connections(const CTinyUSDPrim *prim,
                                         const char *name,
                                         c_tinyusd_string_t *out_csv);

/* Set an attribute-level metadatum (custom, hidden, displayName, doc, ...).
 * For string-valued metas use *_string; for boolean *_bool. The attribute
 * must already exist on the prim. Returns 1 on success.
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_attribute_meta_set_string(CTinyUSDPrim *prim,
                                         const char *attr_name,
                                         const char *meta_key,
                                         const char *value);
C_TINYUSD_EXPORT int
c_tinyusd_prim_attribute_meta_set_bool(CTinyUSDPrim *prim,
                                       const char *attr_name,
                                       const char *meta_key, int value);

/* Read an attribute-level string meta. Returns 1 if authored. */
C_TINYUSD_EXPORT int
c_tinyusd_prim_attribute_meta_get_string(const CTinyUSDPrim *prim,
                                         const char *attr_name,
                                         const char *meta_key,
                                         c_tinyusd_string_t *out);

/* ---- Relationship helpers ----
 *
 * Author a Relationship property on a prim with one or more target paths.
 * `target_paths` is an array of N absolute path strings (e.g. "/Mat/PBR").
 * Returns 1 on success, 0 on failure (err is filled).
 */
C_TINYUSD_EXPORT int
c_tinyusd_prim_add_relationship(CTinyUSDPrim *prim, const char *name,
                                uint64_t n_targets,
                                const char *const *target_paths,
                                c_tinyusd_string_t *err);

C_TINYUSD_EXPORT int
c_tinyusd_prim_get_relationship_targets(const CTinyUSDPrim *prim,
                                        const char *name,
                                        c_tinyusd_string_t *out_csv);

/* ---- Tydra: scene-access helpers ---- */

typedef int (*CTinyUSDVisitFunction)(const CTinyUSDPrim *prim,
                                     const CTinyUSDPath *abs_path,
                                     uint32_t depth,
                                     void *userdata);

C_TINYUSD_EXPORT int
c_tinyusd_stage_visit_prims(const CTinyUSDStage *stage,
                            CTinyUSDVisitFunction cb,
                            void *userdata,
                            c_tinyusd_string_t *err);

/* Convenience DFS filter. For each prim whose `prim_type` matches
 * `type_name`, invoke `cb`. Pass NULL `type_name` to match any prim.
 */
C_TINYUSD_EXPORT int
c_tinyusd_stage_list_prims_by_type(const CTinyUSDStage *stage,
                                   const char *type_name,
                                   CTinyUSDVisitFunction cb,
                                   void *userdata,
                                   c_tinyusd_string_t *err);

#ifdef __cplusplus
}
#endif

#endif /* C_TINYUSD_HELPERS_H */
