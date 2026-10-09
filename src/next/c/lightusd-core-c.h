/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2024-Present Light Transport Entertainment Inc.
 *
 * lightusd-core-c.h -- C11 core for the `next` product.
 *
 * This is the C11 boundary for algorithmic core modules that the `next` build
 * shares with C++ consumers. The first module ported here is the SdfPathExpression
 * parser / serializer / matcher; further leaf modules follow the same shape.
 *
 * Design (mirrors src/c-api/lightusd-c.h, but internal to the next build):
 * - C11 only: no C++ types, no exceptions, no RTTI cross this boundary.
 * - Counted views (lu_view) instead of NUL-terminated strings; every buffer is
 *   bounded by an explicit capacity and every length is size_t.
 * - Owning opaque handles are released with their *_destroy function; borrowed
 *   views point into owned storage and stay valid while the owner lives.
 * - Fallible functions return 0 / negative lu_status. Match results are
 *   reported as 1 (match) / 0 (no match); negative is an error, never a match.
 * - There are no global side effects and no static state: every handle is
 *   independent, so this is safe to call from any number of threads.
 */

#ifndef LIGHTUSD_CORE_C_H_
#define LIGHTUSD_CORE_C_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LU_CORE_API
#if defined(_WIN32) && defined(LU_CORE_BUILD_SHARED)
#define LU_CORE_API __declspec(dllexport)
#elif defined(_WIN32) && defined(LU_CORE_USE_SHARED)
#define LU_CORE_API __declspec(dllimport)
#elif defined(__GNUC__) || defined(__clang__)
#define LU_CORE_API __attribute__((visibility("default")))
#else
#define LU_CORE_API
#endif
#endif

#define LU_CORE_C_VERSION_MAJOR 1
#define LU_CORE_C_VERSION_MINOR 0
#define LU_CORE_C_VERSION_PATCH 0

/* ============================================================
 * Status codes
 * ============================================================ */

typedef enum lu_status {
  LU_OK = 0,
  LU_ERR_INVALID_ARG = -1,    /* NULL / out-of-range argument */
  LU_ERR_OUT_OF_MEMORY = -2,  /* allocation failure */
  LU_ERR_LIMIT = -3,          /* structural limit exceeded (see below) */
  LU_ERR_BUFFER = -4,        /* out buffer too small; *out_len = needed size */
  LU_ERR_NOT_FOUND = -6,     /* no such schema/property/shader/rule */
  LU_ERR_INTERNAL = -99
} lu_status;

/* ============================================================
 * Counted byte view
 * ============================================================ */

typedef struct lu_view {
  const char *data;
  size_t len;
} lu_view;

/* View over a NUL-terminated string (uses strlen; returns an empty view for
 * NULL). Not part of the hot path. */
LU_CORE_API lu_view lu_view_from_cstr(const char *s);

/* ============================================================
 * Structural limits
 *
 * These mirror the historical next-side limits. They are unconditional: an
 * expression that exceeds them is rejected rather than parsed further, so a
 * hostile input cannot drive unbounded work or memory use.
 * ============================================================ */

#define LU_PATH_EXPR_MAX_BYTES ((size_t)1 << 20) /* 1 MiB of expression text */
#define LU_PATH_EXPR_MAX_DEPTH ((size_t)256)     /* nesting `((( )))` */
#define LU_PATH_EXPR_MAX_NODES ((size_t)4096)    /* parse-tree nodes */

#define LU_PATH_EXPR_MAX_MATCH_ELEMS ((size_t)4096)
#define LU_PATH_EXPR_MAX_MATCH_SEGMENTS ((size_t)4096)
#define LU_PATH_EXPR_MAX_EVAL_OPS ((size_t)4096)
/* Defence in depth: the matcher memo table is (elems+1) * (segments+1) entries.
 * Bounding the product keeps a pathological (but individually legal) pair from
 * allocating an unbounded memo. */
#define LU_PATH_EXPR_MAX_MEMO_CELLS ((size_t)1 << 22)

/* Operator of a prefix-op stream (values are part of the ABI). */
typedef enum lu_path_expr_op {
  LU_PATH_EXPR_OP_COMPLEMENT = 0,    /* ~   (unary) */
  LU_PATH_EXPR_OP_IMPLIED_UNION = 1, /* whitespace */
  LU_PATH_EXPR_OP_UNION = 2,         /* + */
  LU_PATH_EXPR_OP_INTERSECTION = 3,  /* & */
  LU_PATH_EXPR_OP_DIFFERENCE = 4,    /* - */
  LU_PATH_EXPR_OP_EXPRESSION_REF = 5,/* operand: consumes next reference */
  LU_PATH_EXPR_OP_PATTERN = 6        /* operand: consumes next pattern */
} lu_path_expr_op;

/* ============================================================
 * Parsed SdfPathExpression
 * ============================================================ */

/* Opaque parsed expression. Owns all strings it exposes. */
typedef struct lu_path_expr lu_path_expr;

/* One component of a path pattern. `text` may contain '*'/'?' wildcards; a
 * "stretch" component (`//`) has both views empty and is_stretch set. */
typedef struct lu_component_view {
  lu_view text;       /* literal/glob text; empty for a stretch */
  lu_view predicate;  /* raw text inside `{...}`; empty when absent */
  int is_stretch;     /* 1 for `//`, 0 otherwise */
} lu_component_view;

/* SdfPathPattern: a prefix path followed by components. */
typedef struct lu_pattern_view {
  lu_view prefix;         /* canonical literal prefix, e.g. "/World/Geom" */
  size_t component_count;
  int match_property;     /* 1 when the pattern targets `.property` */
  lu_view property_name;  /* property glob when match_property */
} lu_pattern_view;

/* Expression reference: `%/Owner:name`, `%:name` (same prim) or `%_`. */
typedef struct lu_ref_view {
  lu_view path;        /* owning-prim path; empty for same-prim / weaker */
  lu_view name;        /* collection name; "_" denotes the weaker reference */
  int is_weaker;
} lu_ref_view;

/* Caller-provided hooks. Both are optional: when unset the matcher evaluates
 * structurally (predicates pass, references match nothing). The `user` pointer
 * is passed through unchanged and is never dereferenced here. */
typedef struct lu_path_expr_callbacks {
  /* Evaluate a predicate (raw text inside `{...}`, e.g. "model") against the
   * prim at `prim_path`. Return non-zero when satisfied, 0 when not. */
  int (*eval_predicate)(void *user, lu_view predicate, lu_view prim_path);

  /* Resolve a reference to a parsed expression. Return NULL when unresolved;
   * unresolved references match nothing. The returned handle is only read for
   * the duration of the calling lu_path_expr_match_path() invocation. */
  const lu_path_expr *(*resolve_ref)(void *user, lu_view ref_path,
                                      lu_view ref_name, int is_weaker);

  /* Guard against reference cycles. */
  int max_ref_depth;
} lu_path_expr_callbacks;

/* Parse `text` into `*out`.
 *
 * Always returns LU_OK and creates an object on success (including a valid
 * empty expression, and an *invalid* expression carrying a diagnostic -- use
 * lu_path_expr_valid() to tell). `err_buf` (optional) receives the diagnostic
 * when the expression is invalid; nothing is written for a valid parse.
 *
 * Negative status means no object was created (`*out` is set to NULL):
 * LU_ERR_OUT_OF_MEMORY, or LU_ERR_LIMIT when `text` exceeds
 * LU_PATH_EXPR_MAX_BYTES.
 */
LU_CORE_API int lu_path_expr_parse(lu_view text, lu_path_expr **out,
                                   char *err_buf, size_t err_cap);

/* Release an expression created by lu_path_expr_parse() / clone(). NULL-safe. */
LU_CORE_API void lu_path_expr_destroy(lu_path_expr *expr);

/* Deep copy. NULL-safe; returns LU_ERR_INVALID_ARG for NULL inputs. */
LU_CORE_API int lu_path_expr_clone(const lu_path_expr *expr, lu_path_expr **out);

/* 0 when the expression is invalid (parse failed), non-zero otherwise. */
LU_CORE_API int lu_path_expr_valid(const lu_path_expr *expr);

/* Non-zero when the expression has no ops (the empty expression). */
LU_CORE_API int lu_path_expr_empty(const lu_path_expr *expr);

/* Number of entries in the prefix-op stream. */
LU_CORE_API int lu_path_expr_op_count(const lu_path_expr *expr, size_t *out);

/* Op at `index`; returns LU_ERR_INVALID_ARG when out of range. */
LU_CORE_API int lu_path_expr_op_at(const lu_path_expr *expr, size_t index,
                                   lu_path_expr_op *out);

/* Op counts of the operand streams. */
LU_CORE_API int lu_path_expr_pattern_count(const lu_path_expr *expr,
                                           size_t *out);
LU_CORE_API int lu_path_expr_ref_count(const lu_path_expr *expr, size_t *out);

/* Borrowed view of pattern `index`. Views point into the expression's own
 * storage and stay valid until the expression is destroyed or mutated. */
LU_CORE_API int lu_path_expr_pattern_at(const lu_path_expr *expr, size_t index,
                                        lu_pattern_view *out);
LU_CORE_API int lu_path_expr_component_at(const lu_path_expr *expr,
                                          size_t index, size_t component,
                                          lu_component_view *out);

/* Borrowed view of reference `index`. */
LU_CORE_API int lu_path_expr_ref_at(const lu_path_expr *expr, size_t index,
                                    lu_ref_view *out);

/* Canonical text of the expression.
 *
 * Counted-buffer contract:
 *   - buf == NULL && cap == 0  -> size query; *out_len = bytes needed (no NUL).
 *   - buf != NULL && cap > 0   -> writes text + NUL when cap >= len + 1 and
 *                                 returns LU_OK with *out_len = len.
 *   - otherwise                -> LU_ERR_BUFFER with *out_len = len.
 * `out_len` may be NULL when only the text is wanted.
 */
LU_CORE_API int lu_path_expr_get_text(const lu_path_expr *expr, char *buf,
                                      size_t cap, size_t *out_len);

/* Match a parsed expression against `path`.
 * Returns 1 (match), 0 (no match) or a negative lu_status on error.
 * A short memo table makes this O(elems * segments) per expression. */
LU_CORE_API int lu_path_expr_match_path(const lu_path_expr *expr, lu_view path,
                                        const lu_path_expr_callbacks *cb,
                                        void *user);

/* Match a single pattern (by index into the expression) against `path`. */
LU_CORE_API int lu_path_expr_match_pattern(const lu_path_expr *expr,
                                           size_t index, lu_view path,
                                           const lu_path_expr_callbacks *cb,
                                           void *user);

/* ============================================================
 * Generated checker tables (src/next/c/generated-schema-tables.c)
 *
 * The AOUSD schema inventory: 134 schemas, 403 properties, 691 shaders and the
 * checker rule -> specification mapping. All of it is a NUL-separated string
 * arena plus index rows, so nothing is constructed at startup and nothing
 * allocates per lookup. Rows are name-sorted for binary search.
 * ============================================================ */

typedef struct lu_validation_schema_view {
  lu_view name;
  lu_view parent;            /* canonical parent schema name; empty for roots */
  lu_view kind;              /* abstract, concrete, singleApply, ... */
  lu_view property_namespace;/* namespaced prefix for multipleApply schemas */
  lu_view source;            /* defining schema.usda path */
  size_t property_count;
} lu_validation_schema_view;

typedef struct lu_validation_property_view {
  lu_view name;
  lu_view type;
  lu_view variability;
  int is_relationship;
  size_t token_count;
} lu_validation_property_view;

typedef struct lu_validation_shader_view {
  lu_view identifier;
  lu_view source_type;
  lu_view source;
  size_t input_count;
  size_t output_count;
} lu_validation_shader_view;

/* Number of schemas in the builtin inventory. */
LU_CORE_API int lu_validation_schema_count(size_t *out);

/* Index of the schema named `name`, or LU_ERR_NOT_FOUND-like -6 (see
 * lu_validation_lookup_miss). */
LU_CORE_API int lu_validation_schema_find(lu_view name, size_t *index);

/* Borrowed view of schema `index`; views point into the arena and stay valid
 * for the lifetime of the program. */
LU_CORE_API int lu_validation_schema_at(size_t index,
                                        lu_validation_schema_view *out);

/* Property `prop_index` within schema `schema_index`. */
LU_CORE_API int lu_validation_property_at(size_t schema_index, size_t prop_index,
                                          lu_validation_property_view *out);

/* Index of the property named `name` within schema `schema_index`. */
LU_CORE_API int lu_validation_property_find(size_t schema_index, lu_view name,
                                            size_t *prop_index);

/* `token_index` of the property's allowedTokens list. */
LU_CORE_API int lu_validation_property_token(size_t schema_index,
                                             size_t prop_index,
                                             size_t token_index, lu_view *out);

LU_CORE_API int lu_validation_shader_count(size_t *out);
LU_CORE_API int lu_validation_shader_find(lu_view identifier,
                                          lu_view source_type, size_t *index);
LU_CORE_API int lu_validation_shader_at(size_t index,
                                        lu_validation_shader_view *out);
LU_CORE_API int lu_validation_shader_port(size_t index, int is_output,
                                          size_t port_index, lu_view *name,
                                          lu_view *type);

LU_CORE_API int lu_validation_ref_error_count(size_t *out);
/* Row index and error count for a rule; the individual entries are then read
 * one by one with lu_validation_ref_error_item(). */
LU_CORE_API int lu_validation_ref_error_find(lu_view rule, size_t *index,
                                             size_t *count);
LU_CORE_API int lu_validation_ref_error_item(size_t rule_index, size_t item,
                                             lu_view *out);

/* ============================================================
 * Integer formatting (src/next/c/strfmt.c)
 *
 * Locale-independent, libc-free decimal formatting. Byte-identical to
 * `ss << v` under the classic locale. `buf` needs >= 20 bytes for lu_uint_to
 * and >= 21 for lu_int_to; the return value is the number of bytes written
 * (no NUL terminator).
 * ============================================================ */

LU_CORE_API size_t lu_uint_to(char *buf, uint64_t v);
LU_CORE_API size_t lu_int_to(char *buf, int64_t v);

/* ============================================================
 * IEEE 754 half <-> float (src/next/c/half.c)
 * ============================================================ */

LU_CORE_API float lu_half_to_float(uint16_t h);
LU_CORE_API uint16_t lu_float_to_half(float value);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* LIGHTUSD_CORE_C_H_ */
