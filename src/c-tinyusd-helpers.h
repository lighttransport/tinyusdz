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
